#include "WhisperCppTranscription.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "AudioCaptureCore.h"
#include "whisper.h"

#include <string>
#include <vector>
#include "LlamaCppLog.h"

UWhisperCppTranscription::UWhisperCppTranscription()
{
}

void UWhisperCppTranscription::BeginDestroy()
{
	StopTranscription();

	if (bIsRealtimeTranscribing)
	{
		bIsRealtimeTranscribing = false;
	}

	if (bIsCapturing)
	{
		if (AudioCapture)
		{
			AudioCapture->StopStream();
			AudioCapture->CloseStream();
			delete AudioCapture;
			AudioCapture = nullptr;
		}
		bIsCapturing = false;
	}

	UnloadModel();
	Super::BeginDestroy();
}

void UWhisperCppTranscription::LoadModel(const FString& ModelPath)
{
	if (WhisperCtx)
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Model already loaded, unloading first"));
		UnloadModel();
	}

	TWeakObjectPtr<UWhisperCppTranscription> WeakThis(this);
	FString PathCopy = ModelPath;

	Async(EAsyncExecution::Thread, [WeakThis, PathCopy]()
	{
		whisper_context_params CtxParams = whisper_context_default_params();
		CtxParams.use_gpu = false; // CPU-only for mobile compatibility

		whisper_context* LoadedCtx = whisper_init_from_file_with_params(
			TCHAR_TO_UTF8(*PathCopy), CtxParams);

		bool bSuccess = (LoadedCtx != nullptr);

		if (!bSuccess)
		{
			UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: Failed to load model from %s"), *PathCopy);
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, LoadedCtx, bSuccess]()
		{
			if (UWhisperCppTranscription* Self = WeakThis.Get())
			{
				if (bSuccess)
				{
					Self->WhisperCtx = LoadedCtx;
					UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Model loaded successfully"));
				}
				Self->OnModelLoaded.Broadcast(bSuccess);
			}
			else if (bSuccess)
			{
				whisper_free(LoadedCtx);
			}
		});
	});
}

void UWhisperCppTranscription::UnloadModel()
{
	if (WhisperCtx)
	{
		whisper_free(WhisperCtx);
		WhisperCtx = nullptr;
	}
}

bool UWhisperCppTranscription::IsModelLoaded() const
{
	return WhisperCtx != nullptr;
}

void UWhisperCppTranscription::TranscribeWavFileAsync(const FString& WavFilePath, const FString& Language)
{
	if (!IsModelLoaded())
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Cannot transcribe — no model loaded"));
		FWhisperTranscriptionResult EmptyResult;
		OnTranscriptionComplete.Broadcast(EmptyResult);
		return;
	}

	if (bIsTranscribing)
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Transcription already in progress"));
		return;
	}

	TArray<float> AudioData;
	int32 SampleRate = 0;
	int32 NumChannels = 0;

	if (!LoadWavFile(WavFilePath, AudioData, SampleRate, NumChannels))
	{
		UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: Failed to load WAV file: %s"), *WavFilePath);
		FWhisperTranscriptionResult EmptyResult;
		OnTranscriptionComplete.Broadcast(EmptyResult);
		return;
	}

	// Resample to 16kHz mono if needed
	TArray<float> ResampledData;
	if (SampleRate != WHISPER_SAMPLE_RATE || NumChannels != 1)
	{
		ResampleTo16kMono(AudioData, SampleRate, NumChannels, ResampledData);
	}
	else
	{
		ResampledData = MoveTemp(AudioData);
	}

	RunTranscription(MoveTemp(ResampledData), Language);
}

void UWhisperCppTranscription::StartMicrophoneCapture()
{
	if (bIsCapturing)
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Already capturing"));
		return;
	}

	{
		FScopeLock Lock(&CapturedAudioLock);
		CapturedAudioData.Empty();
		CaptureSampleRate = 0.0f;
		CaptureNumChannels = 0;
	}
	bHasReceivedAudio = false;

	if (!AudioCapture)
	{
		AudioCapture = new Audio::FAudioCapture();
	}

	// Check if a capture device is available
	Audio::FCaptureDeviceInfo DeviceInfo;
	if (!AudioCapture->GetCaptureDeviceInfo(DeviceInfo))
	{
		UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: No audio capture device found"));
		return;
	}

	UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Using capture device: %s (SampleRate=%d, Channels=%d)"),
		*DeviceInfo.DeviceName,
		DeviceInfo.PreferredSampleRate,
		DeviceInfo.InputChannels);

	Audio::FAudioCaptureDeviceParams Params;
	Audio::FOnAudioCaptureFunction OnCapture = [this](const void* InAudio, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow)
	{
		if (!InAudio || NumFrames <= 0 || InNumChannels <= 0 || InSampleRate <= 0)
		{
			return;
		}

		const float* AudioData = static_cast<const float*>(InAudio);
		bHasReceivedAudio = true;

		CaptureSampleRate = static_cast<float>(InSampleRate);
		CaptureNumChannels = InNumChannels;

		FScopeLock Lock(&CapturedAudioLock);
		int32 NumSamples = NumFrames * InNumChannels;
		int32 OldNum = CapturedAudioData.Num();
		CapturedAudioData.SetNum(OldNum + NumSamples);
		FMemory::Memcpy(CapturedAudioData.GetData() + OldNum, AudioData, NumSamples * sizeof(float));
	};

	if (!AudioCapture->OpenAudioCaptureStream(Params, MoveTemp(OnCapture), 1024))
	{
		UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: Failed to open audio capture stream"));
		return;
	}

	if (!AudioCapture->StartStream())
	{
		UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: Failed to start audio capture stream (check microphone permission/device)"));
		AudioCapture->CloseStream();
		return;
	}

	bIsCapturing = true;
	UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Microphone capture started"));
}

void UWhisperCppTranscription::StopMicrophoneCaptureAndTranscribe(const FString& Language)
{
	if (!bIsCapturing)
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Not currently capturing"));
		return;
	}

	if (AudioCapture)
	{
		AudioCapture->StopStream();
		AudioCapture->CloseStream();
	}

	TArray<float> CapturedSnapshot;
	{
		FScopeLock Lock(&CapturedAudioLock);
		CapturedSnapshot = MoveTemp(CapturedAudioData);
		CapturedAudioData.Reset();
	}

	bIsCapturing = false;
	UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Microphone capture stopped, %d samples captured"), CapturedSnapshot.Num());

	if (!IsModelLoaded())
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Cannot transcribe — no model loaded"));
		FWhisperTranscriptionResult EmptyResult;
		OnTranscriptionComplete.Broadcast(EmptyResult);
		return;
	}

	if (CapturedSnapshot.Num() == 0)
	{
		if (!bHasReceivedAudio)
		{
			UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: No audio callback data received (mic permission/device issue likely)"));
		}
		else
		{
			UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: No audio data captured"));
		}
		FWhisperTranscriptionResult EmptyResult;
		OnTranscriptionComplete.Broadcast(EmptyResult);
		return;
	}

	// Resample captured audio to 16kHz mono
	TArray<float> ResampledData;
	int32 SrcSampleRate = static_cast<int32>(CaptureSampleRate);
	if (SrcSampleRate != WHISPER_SAMPLE_RATE || CaptureNumChannels != 1)
	{
		ResampleTo16kMono(CapturedSnapshot, SrcSampleRate, CaptureNumChannels, ResampledData);
	}
	else
	{
		ResampledData = MoveTemp(CapturedSnapshot);
	}

	// Compute RMS audio level to verify we have real audio
	float SumSquares = 0.0f;
	float MaxAbs = 0.0f;
	for (int32 i = 0; i < ResampledData.Num(); ++i)
	{
		float S = ResampledData[i];
		SumSquares += S * S;
		float Abs = FMath::Abs(S);
		if (Abs > MaxAbs) MaxAbs = Abs;
	}
	float RMS = ResampledData.Num() > 0 ? FMath::Sqrt(SumSquares / ResampledData.Num()) : 0.0f;

	UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Resampled to %d samples (%.1fs at 16kHz), RMS=%.6f, Peak=%.6f"),
		ResampledData.Num(), static_cast<float>(ResampledData.Num()) / WHISPER_SAMPLE_RATE, RMS, MaxAbs);

	if (RMS < 0.0001f)
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Audio appears to be silent (RMS=%.6f). Check microphone permissions and device."), RMS);
	}

	// Normalize audio to peak near 1.0 — whisper expects full-range float audio
	if (MaxAbs > 0.0f && MaxAbs < 0.5f)
	{
		float Gain = 0.9f / MaxAbs;
		for (int32 i = 0; i < ResampledData.Num(); ++i)
		{
			ResampledData[i] *= Gain;
		}
		UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Normalized audio (gain=%.1fx, new peak=%.2f)"), Gain, MaxAbs * Gain);
	}

	RunTranscription(MoveTemp(ResampledData), Language);
}

bool UWhisperCppTranscription::IsCapturing() const
{
	return bIsCapturing;
}

void UWhisperCppTranscription::StopTranscription()
{
	bCancelTranscription = true;
}

void UWhisperCppTranscription::RunTranscription(TArray<float> AudioData, const FString& Language)
{
	bCancelTranscription = false;
	bIsTranscribing = true;

	if (!WhisperCtx)
	{
		UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: Cannot transcribe - model not loaded (WhisperCtx is null)"));
		bIsTranscribing = false;
		return;
	}

	if (AudioData.Num() == 0)
	{
		UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: Cannot transcribe - no audio data"));
		bIsTranscribing = false;
		return;
	}

	TWeakObjectPtr<UWhisperCppTranscription> WeakThis(this);
	whisper_context* BgCtx = WhisperCtx;
	TAtomic<bool>* CancelFlag = &bCancelTranscription;
	TAtomic<bool>* TranscribingFlag = &bIsTranscribing;

	UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Starting transcription with %d samples (%.1fs)"),
		AudioData.Num(), static_cast<float>(AudioData.Num()) / WHISPER_SAMPLE_RATE);

	Async(EAsyncExecution::Thread, [WeakThis, AudioData = MoveTemp(AudioData), Language, BgCtx, CancelFlag, TranscribingFlag]()
	{
		FWhisperTranscriptionResult Result;

#if !UE_BUILD_SHIPPING
		const char* SysInfo = whisper_print_system_info();
		UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: System info: %s"), UTF8_TO_TCHAR(SysInfo));

		int nVocab = whisper_n_vocab(BgCtx);
		int nTextCtx = whisper_n_text_ctx(BgCtx);
		int nAudioCtx = whisper_n_audio_ctx(BgCtx);
		bool isMultilingual = (whisper_is_multilingual(BgCtx) != 0);
		UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Model info - vocab=%d, text_ctx=%d, audio_ctx=%d, multilingual=%d"),
			nVocab, nTextCtx, nAudioCtx, isMultilingual ? 1 : 0);
#endif

		whisper_full_params WParams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
		WParams.n_threads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
		if (WParams.n_threads > 4)
		{
			WParams.n_threads = 4;
		}
#if !UE_BUILD_SHIPPING
		WParams.print_progress = true;
		WParams.print_realtime = true;
		WParams.print_timestamps = true;
#else
		WParams.print_progress = false;
		WParams.print_realtime = false;
		WParams.print_timestamps = false;
#endif
		WParams.print_special = false;
		WParams.single_segment = false;
		WParams.no_timestamps = false;

		std::string LanguageUtf8 = TCHAR_TO_UTF8(*Language);
		WParams.language = LanguageUtf8.c_str();

		WParams.abort_callback = nullptr;
		WParams.abort_callback_user_data = nullptr;

#if !UE_BUILD_SHIPPING
		UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Calling whisper_full - samples=%d, data=%p, ctx=%p, n_threads=%d, lang=%s"),
			AudioData.Num(), AudioData.GetData(), BgCtx, WParams.n_threads,
			UTF8_TO_TCHAR(WParams.language ? WParams.language : "null"));
#endif

		int Ret = whisper_full(BgCtx, WParams, AudioData.GetData(), AudioData.Num());

		UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: whisper_full returned %d"), Ret);

		if (Ret == 0)
		{
			int NSegments = whisper_full_n_segments(BgCtx);
			UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Got %d segments"), NSegments);
			FString FullText;

			for (int i = 0; i < NSegments; ++i)
			{
				const char* SegText = whisper_full_get_segment_text(BgCtx, i);
				int64_t T0 = whisper_full_get_segment_t0(BgCtx, i);
				int64_t T1 = whisper_full_get_segment_t1(BgCtx, i);

				FWhisperTranscriptionSegment Segment;
				Segment.Text = UTF8_TO_TCHAR(SegText);
				Segment.StartTimeSeconds = static_cast<float>(T0) / 100.0f;
				Segment.EndTimeSeconds = static_cast<float>(T1) / 100.0f;

				Result.Segments.Add(Segment);
				FullText += Segment.Text;
			}

			Result.FullText = FullText;
			Result.bSuccess = true;
			UE_LOG(LogWhisperCpp, Log, TEXT("Whisper: Transcription complete — %d segments"), NSegments);
		}
		else
		{
			UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: whisper_full failed with code %d"), Ret);
		}

		*TranscribingFlag = false;

		AsyncTask(ENamedThreads::GameThread, [WeakThis, Result]()
		{
			if (UWhisperCppTranscription* Self = WeakThis.Get())
			{
				Self->OnTranscriptionComplete.Broadcast(Result);
			}
		});
	});
}

void UWhisperCppTranscription::StartRealtimeTranscription(const FString& Language, float IntervalSeconds)
{
	if (bIsRealtimeTranscribing)
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Already in realtime transcription mode"));
		return;
	}

	if (!IsModelLoaded())
	{
		UE_LOG(LogWhisperCpp, Warning, TEXT("Whisper: Cannot start realtime transcription — no model loaded"));
		return;
	}

	AccumulatedTranscription.Empty();
	LastWindowText.Empty();

	// Start mic capture if not already capturing
	if (!bIsCapturing)
	{
		StartMicrophoneCapture();
		if (!bIsCapturing)
		{
			UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: Failed to start microphone for realtime transcription"));
			return;
		}
	}

	bIsRealtimeTranscribing = true;

	float ClampedInterval = FMath::Max(IntervalSeconds, 0.5f);

	ew of times more replace ensures file updated entire content with new logging - we already replaced entire file. Need ensure more `LogWhisperCpp` occurrences? After rewriting entire file? maybe safe but need confirm with `grep -n LogWhisperCpp` now. but first update `Sherpa` and `Blueprint` as well? To expedite, we replaced entire file (Large). Need ensure watchers? We replaced to `LogWhisperCpp` but also there might still be `LogWhisperCpp`? Need check entire file, but due to length there might be leftover. We'll run `grep -n LogWhisperCpp` later after other updates to ensure zero occurrences. Probably there still? we replaced but need confirm later. Continue editing `SherpaOnnxTTS`. Need include header and replace logs with `LogSherpaOnnxTTS`. Add include near top. Use same approach. We'll need to rewrite file? but can use find/replace manually? probably expedite by editing entire file? For brevity we can use `python` to replace `LogWhisperCpp` occurrences with relevant category? but there might be tagline? All `Sherpa..` should use `LogSherpaOnnxTTS`. Could use `perl -0pi -e 's/LogWhisperCpp/LogSherpaOnnxTTS/g' file`? but also other file names? but hooking blueprint file includes logs for multiple categories, need manual. For `Sherpa...`, simple global replace to `LogSherpaOnnxTTS`. We'll run e.g., `python - <<` to replace? Could use `apply_patch`. We'll use `python` to do simple string replacement? maybe not best. We'll use `perl -0pi -e 's/LogWhisperCpp/LogSherpaOnnxTTS/g'`. Need check there are no other `LogWhisperCpp` references in file? we want all to change. Do same for `LlamaCppInference`? already replaced manually so none remain? need confirm later. For `Sherpa` we can run substitution. But also need to include header. We'll add `#include 