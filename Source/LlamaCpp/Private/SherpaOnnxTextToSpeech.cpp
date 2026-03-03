#include "SherpaOnnxTextToSpeech.h"
#include "Async/Async.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"

THIRD_PARTY_INCLUDES_START
#include "c-api.h"
THIRD_PARTY_INCLUDES_END

USherpaOnnxTextToSpeech::USherpaOnnxTextToSpeech()
{
}

void USherpaOnnxTextToSpeech::BeginDestroy()
{
	Stop();
	CleanupAudio();
	UnloadModel();
	Super::BeginDestroy();
}

void USherpaOnnxTextToSpeech::LoadModel(const FString& ModelPath, const FString& TokensPath, const FString& DataDir)
{
	if (TtsEngine)
	{
		UE_LOG(LogTemp, Warning, TEXT("SherpaOnnxTTS: Model already loaded, unloading first"));
		UnloadModel();
	}

	TWeakObjectPtr<USherpaOnnxTextToSpeech> WeakThis(this);
	FString ModelPathCopy = ModelPath;
	FString TokensPathCopy = TokensPath;
	FString DataDirCopy = DataDir;

	Async(EAsyncExecution::Thread, [WeakThis, ModelPathCopy, TokensPathCopy, DataDirCopy]()
	{
		SherpaOnnxOfflineTtsConfig Config;
		memset(&Config, 0, sizeof(Config));

		std::string ModelPathUtf8 = TCHAR_TO_UTF8(*ModelPathCopy);
		std::string TokensPathUtf8 = TCHAR_TO_UTF8(*TokensPathCopy);
		std::string DataDirUtf8 = TCHAR_TO_UTF8(*DataDirCopy);

		Config.model.vits.model = ModelPathUtf8.c_str();
		Config.model.vits.tokens = TokensPathUtf8.c_str();
		Config.model.vits.data_dir = DataDirUtf8.c_str();
		Config.model.num_threads = FMath::Min(FPlatformMisc::NumberOfCoresIncludingHyperthreads(), 4);
		Config.model.debug = 0;
		Config.model.provider = "cpu";

		const SherpaOnnxOfflineTts* LoadedTts = SherpaOnnxCreateOfflineTts(&Config);
		bool bSuccess = (LoadedTts != nullptr);

		if (!bSuccess)
		{
			UE_LOG(LogTemp, Error, TEXT("SherpaOnnxTTS: Failed to load model from %s"), *ModelPathCopy);
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, LoadedTts, bSuccess]()
		{
			if (USherpaOnnxTextToSpeech* Self = WeakThis.Get())
			{
				if (bSuccess)
				{
					Self->TtsEngine = const_cast<SherpaOnnxOfflineTts*>(LoadedTts);
					UE_LOG(LogTemp, Log, TEXT("SherpaOnnxTTS: Model loaded successfully"));
				}
				Self->OnModelLoaded.Broadcast(bSuccess);
			}
			else if (bSuccess)
			{
				SherpaOnnxDestroyOfflineTts(LoadedTts);
			}
		});
	});
}

void USherpaOnnxTextToSpeech::UnloadModel()
{
	if (TtsEngine)
	{
		SherpaOnnxDestroyOfflineTts(TtsEngine);
		TtsEngine = nullptr;
	}
}

bool USherpaOnnxTextToSpeech::IsModelLoaded() const
{
	return TtsEngine != nullptr;
}

void USherpaOnnxTextToSpeech::Speak(const FString& Text, int32 SpeakerId, float Speed)
{
	if (!IsModelLoaded())
	{
		UE_LOG(LogTemp, Warning, TEXT("SherpaOnnxTTS: Cannot speak — no model loaded"));
		OnSpeakError.Broadcast(TEXT("No model loaded"));
		return;
	}

	if (bIsSpeaking)
	{
		UE_LOG(LogTemp, Warning, TEXT("SherpaOnnxTTS: Already speaking"));
		OnSpeakError.Broadcast(TEXT("Already speaking"));
		return;
	}

	bIsSpeaking = true;
	bStopRequested = false;

	TWeakObjectPtr<USherpaOnnxTextToSpeech> WeakThis(this);
	SherpaOnnxOfflineTts* BgTts = TtsEngine;
	FString TextCopy = Text;

	Async(EAsyncExecution::Thread, [WeakThis, BgTts, TextCopy, SpeakerId, Speed]()
	{
		std::string TextUtf8 = TCHAR_TO_UTF8(*TextCopy);

		const SherpaOnnxGeneratedAudio* Audio = SherpaOnnxOfflineTtsGenerate(
			BgTts, TextUtf8.c_str(), SpeakerId, Speed);

		if (!Audio || Audio->n <= 0)
		{
			UE_LOG(LogTemp, Error, TEXT("SherpaOnnxTTS: Failed to generate speech"));

			AsyncTask(ENamedThreads::GameThread, [WeakThis]()
			{
				if (USherpaOnnxTextToSpeech* Self = WeakThis.Get())
				{
					Self->bIsSpeaking = false;
					Self->OnSpeakError.Broadcast(TEXT("Speech generation failed"));
				}
			});

			if (Audio)
			{
				SherpaOnnxDestroyOfflineTtsGeneratedAudio(Audio);
			}
			return;
		}

		int32 SampleRate = Audio->sample_rate;
		int32 NumSamples = Audio->n;

		// Copy PCM data (float samples)
		TArray<uint8> PcmBytes;
		PcmBytes.SetNum(NumSamples * sizeof(int16));
		int16* PcmData = reinterpret_cast<int16*>(PcmBytes.GetData());

		for (int32 i = 0; i < NumSamples; ++i)
		{
			float Sample = FMath::Clamp(Audio->samples[i], -1.0f, 1.0f);
			PcmData[i] = static_cast<int16>(Sample * 32767.0f);
		}

		SherpaOnnxDestroyOfflineTtsGeneratedAudio(Audio);

		UE_LOG(LogTemp, Log, TEXT("SherpaOnnxTTS: Generated %d samples at %d Hz (%.1fs)"),
			NumSamples, SampleRate, static_cast<float>(NumSamples) / SampleRate);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, PcmBytes = MoveTemp(PcmBytes), SampleRate, NumSamples]()
		{
			USherpaOnnxTextToSpeech* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (Self->bStopRequested)
			{
				Self->bIsSpeaking = false;
				return;
			}

			// Create procedural sound wave
			Self->SoundWave = NewObject<USoundWaveProcedural>(Self);
			Self->SoundWave->SetSampleRate(SampleRate);
			Self->SoundWave->NumChannels = 1;
			Self->SoundWave->Duration = static_cast<float>(NumSamples) / SampleRate;
			Self->SoundWave->SoundGroup = SOUNDGROUP_Default;
			Self->SoundWave->bLooping = false;

			// Queue the PCM data
			Self->SoundWave->QueueAudio(PcmBytes.GetData(), PcmBytes.Num());

			// Create audio component for playback
			UWorld* World = Self->GetWorld();
			if (!World)
			{
				UE_LOG(LogTemp, Error, TEXT("SherpaOnnxTTS: No world available for audio playback"));
				Self->bIsSpeaking = false;
				Self->OnSpeakError.Broadcast(TEXT("No world available for audio playback"));
				return;
			}

			Self->AudioComp = NewObject<UAudioComponent>(Self);
			Self->AudioComp->bAutoActivate = false;
			Self->AudioComp->bAutoDestroy = false;
			Self->AudioComp->SetSound(Self->SoundWave);
			Self->AudioComp->RegisterComponentWithWorld(World);
			Self->AudioComp->Play();

			UE_LOG(LogTemp, Log, TEXT("SherpaOnnxTTS: Playback started"));

			// Monitor playback completion
			World->GetTimerManager().SetTimer(
				Self->PlaybackTimerHandle,
				Self,
				&USherpaOnnxTextToSpeech::MonitorPlayback,
				0.1f,
				true
			);
		});
	});
}

void USherpaOnnxTextToSpeech::Stop()
{
	bStopRequested = true;

	if (AudioComp && AudioComp->IsPlaying())
	{
		AudioComp->Stop();
	}

	CleanupAudio();
	bIsSpeaking = false;
}

bool USherpaOnnxTextToSpeech::IsSpeaking() const
{
	return bIsSpeaking;
}

void USherpaOnnxTextToSpeech::MonitorPlayback()
{
	if (!AudioComp || !AudioComp->IsPlaying())
	{
		// Playback finished
		UWorld* World = GetWorld();
		if (World)
		{
			World->GetTimerManager().ClearTimer(PlaybackTimerHandle);
		}

		CleanupAudio();
		bIsSpeaking = false;

		UE_LOG(LogTemp, Log, TEXT("SherpaOnnxTTS: Playback complete"));
		OnSpeakComplete.Broadcast();
	}
}

void USherpaOnnxTextToSpeech::CleanupAudio()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(PlaybackTimerHandle);
	}

	if (AudioComp)
	{
		if (AudioComp->IsRegistered())
		{
			AudioComp->DestroyComponent();
		}
		AudioComp = nullptr;
	}

	SoundWave = nullptr;
}
