#include "SherpaOnnxTranscription.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "LlamaCppLog.h"

#if WITH_SHERPA_ONNX

#include <string>

THIRD_PARTY_INCLUDES_START
#include "c-api.h"
THIRD_PARTY_INCLUDES_END

USherpaOnnxTranscription::USherpaOnnxTranscription()
{
	StreamingDoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

void USherpaOnnxTranscription::BeginDestroy()
{
	bIsBeingDestroyed = true;
	SherpaOnnxStopStreaming();

	if (bIsStreaming)
	{
		StreamingDoneEvent->Wait();
	}

	SherpaOnnxUnloadModel();

	FPlatformProcess::ReturnSynchEventToPool(StreamingDoneEvent);
	StreamingDoneEvent = nullptr;

	Super::BeginDestroy();
}

void USherpaOnnxTranscription::SherpaOnnxLoadModel(const FString& EncoderPath, const FString& DecoderPath, const FString& JoinerPath, const FString& TokensPath)
{
	if (Recognizer)
	{
		UE_LOG(LogSherpaOnnxASR, Warning, TEXT("SherpaOnnxASR: Model already loaded, unloading first"));
		SherpaOnnxUnloadModel();
	}

	TWeakObjectPtr<USherpaOnnxTranscription> WeakThis(this);
	FString EncoderCopy = EncoderPath;
	FString DecoderCopy = DecoderPath;
	FString JoinerCopy = JoinerPath;
	FString TokensCopy = TokensPath;
	int32 NumThreads = FMath::Clamp(MaxThreads, 1, FPlatformMisc::NumberOfCoresIncludingHyperthreads());

	Async(EAsyncExecution::Thread, [WeakThis, EncoderCopy, DecoderCopy, JoinerCopy, TokensCopy, NumThreads]()
	{
		std::string EncoderUtf8 = TCHAR_TO_UTF8(*EncoderCopy);
		std::string DecoderUtf8 = TCHAR_TO_UTF8(*DecoderCopy);
		std::string JoinerUtf8 = TCHAR_TO_UTF8(*JoinerCopy);
		std::string TokensUtf8 = TCHAR_TO_UTF8(*TokensCopy);

		UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Encoder: %s"), *EncoderCopy);
		UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Decoder: %s"), *DecoderCopy);
		UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Joiner: %s"), *JoinerCopy);
		UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Tokens: %s"), *TokensCopy);
		UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Encoder exists: %s"), FPaths::FileExists(EncoderCopy) ? TEXT("YES") : TEXT("NO"));
		UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Decoder exists: %s"), FPaths::FileExists(DecoderCopy) ? TEXT("YES") : TEXT("NO"));
		UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Joiner exists: %s"), FPaths::FileExists(JoinerCopy) ? TEXT("YES") : TEXT("NO"));
		UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Tokens exists: %s"), FPaths::FileExists(TokensCopy) ? TEXT("YES") : TEXT("NO"));

		// Initialize sub-structs separately with memset, then assign to parent
		// (matches the pattern from sherpa-onnx C API examples)
		SherpaOnnxOnlineTransducerModelConfig TransducerConfig;
		memset(&TransducerConfig, 0, sizeof(TransducerConfig));
		TransducerConfig.encoder = EncoderUtf8.c_str();
		TransducerConfig.decoder = DecoderUtf8.c_str();
		TransducerConfig.joiner = JoinerUtf8.c_str();

		SherpaOnnxOnlineModelConfig ModelConfig;
		memset(&ModelConfig, 0, sizeof(ModelConfig));
		ModelConfig.transducer = TransducerConfig;
		ModelConfig.tokens = TokensUtf8.c_str();
		ModelConfig.num_threads = NumThreads;
		ModelConfig.provider = "cpu";
		ModelConfig.debug = 1;

		SherpaOnnxOnlineRecognizerConfig Config;
		memset(&Config, 0, sizeof(Config));
		Config.model_config = ModelConfig;
		Config.decoding_method = "greedy_search";

		const SherpaOnnxOnlineRecognizer* LoadedRecognizer = SherpaOnnxCreateOnlineRecognizer(&Config);
		bool bSuccess = (LoadedRecognizer != nullptr);

		if (!bSuccess)
		{
			UE_LOG(LogSherpaOnnxASR, Error, TEXT("SherpaOnnxASR: Failed to create online recognizer"));
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, LoadedRecognizer, bSuccess]()
		{
			if (USherpaOnnxTranscription* Self = WeakThis.Get())
			{
				if (bSuccess)
				{
					Self->Recognizer = LoadedRecognizer;
					UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Model loaded successfully"));
				}
				Self->OnModelLoaded.Broadcast(bSuccess);
			}
			else if (bSuccess)
			{
				SherpaOnnxDestroyOnlineRecognizer(LoadedRecognizer);
			}
		});
	});
}

void USherpaOnnxTranscription::SherpaOnnxUnloadModel()
{
	if (Stream)
	{
		SherpaOnnxDestroyOnlineStream(Stream);
		Stream = nullptr;
	}

	if (Recognizer)
	{
		SherpaOnnxDestroyOnlineRecognizer(Recognizer);
		Recognizer = nullptr;
	}
}

bool USherpaOnnxTranscription::SherpaOnnxIsModelLoaded() const
{
	return Recognizer != nullptr;
}

void USherpaOnnxTranscription::SherpaOnnxStartStreaming()
{
	if (!SherpaOnnxIsModelLoaded())
	{
		UE_LOG(LogSherpaOnnxASR, Warning, TEXT("SherpaOnnxASR: Cannot start streaming — no model loaded"));
		return;
	}

	if (bIsStreaming)
	{
		UE_LOG(LogSherpaOnnxASR, Warning, TEXT("SherpaOnnxASR: Already streaming"));
		return;
	}

	// Create a fresh stream
	if (Stream)
	{
		SherpaOnnxDestroyOnlineStream(Stream);
		Stream = nullptr;
	}
	Stream = SherpaOnnxCreateOnlineStream(Recognizer);
	if (!Stream)
	{
		UE_LOG(LogSherpaOnnxASR, Error, TEXT("SherpaOnnxASR: Failed to create online stream"));
		return;
	}

	// Start mic capture
	if (!AudioCapture)
	{
		AudioCapture = new Audio::FAudioCapture();
	}

	Audio::FAudioCaptureDeviceParams Params;
	if (!AudioCapture->OpenCaptureStream(Params, [this](const float* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate, double StreamTime, bool bOverflow)
	{
		if (bIsBeingDestroyed || !bIsStreaming)
		{
			return;
		}

		// Downmix to mono if needed and store
		FScopeLock Lock(&AudioBufferLock);
		if (NumChannels == 1)
		{
			AudioBuffer.Append(InAudio, NumFrames);
		}
		else
		{
			for (int32 i = 0; i < NumFrames; ++i)
			{
				float Mono = 0.0f;
				for (int32 Ch = 0; Ch < NumChannels; ++Ch)
				{
					Mono += InAudio[i * NumChannels + Ch];
				}
				AudioBuffer.Add(Mono / NumChannels);
			}
		}
	}, 1024))
	{
		UE_LOG(LogSherpaOnnxASR, Error, TEXT("SherpaOnnxASR: Failed to open audio capture stream"));
		return;
	}

	AudioCapture->StartStream();
	bIsCapturing = true;
	bIsStreaming = true;

	UE_LOG(LogSherpaOnnxASR, Log, TEXT("SherpaOnnxASR: Streaming started"));

	// Launch decode loop on background thread
	TWeakObjectPtr<USherpaOnnxTranscription> WeakThis(this);
	FEvent* DoneEvent = StreamingDoneEvent;

	Async(EAsyncExecution::Thread, [WeakThis, DoneEvent]()
	{
		// Run the decode loop — this accesses the object through WeakThis
		USherpaOnnxTranscription* Self = WeakThis.Get();
		if (Self)
		{
			Self->DecodeLoop();
		}
		DoneEvent->Trigger();
	});
}

void USherpaOnnxTranscription::SherpaOnnxStopStreaming()
{
	if (!bIsStreaming)
	{
		return;
	}

	bIsStreaming = false;

	if (AudioCapture)
	{
		AudioCapture->StopStream();
		AudioCapture->CloseStream();
		delete AudioCapture;
		AudioCapture = nullptr;
	}
	bIsCapturing = false;
}

bool USherpaOnnxTranscription::SherpaOnnxIsStreaming() const
{
	return bIsStreaming;
}

void USherpaOnnxTranscription::DecodeLoop()
{
	while (bIsStreaming && !bIsBeingDestroyed)
	{
		// Grab buffered audio
		TArray<float> LocalBuffer;
		{
			FScopeLock Lock(&AudioBufferLock);
			if (AudioBuffer.Num() > 0)
			{
				LocalBuffer = MoveTemp(AudioBuffer);
				AudioBuffer.Reset();
			}
		}

		// Feed audio to the stream (sherpa handles resampling internally)
		if (LocalBuffer.Num() > 0 && Stream)
		{
			SherpaOnnxOnlineStreamAcceptWaveform(Stream, 16000, LocalBuffer.GetData(), LocalBuffer.Num());
		}

		// Decode as many frames as are ready
		if (Recognizer && Stream)
		{
			while (SherpaOnnxIsOnlineStreamReady(Recognizer, Stream))
			{
				SherpaOnnxDecodeOnlineStream(Recognizer, Stream);
			}

			// Get current result
			const SherpaOnnxOnlineRecognizerResult* Result = SherpaOnnxGetOnlineStreamResult(Recognizer, Stream);
			if (Result && Result->text && Result->text[0] != '\0')
			{
				FString Text = UTF8_TO_TCHAR(Result->text);
				Text.TrimStartAndEndInline();

				if (!Text.IsEmpty())
				{
					bool bIsEndpoint = SherpaOnnxOnlineStreamIsEndpoint(Recognizer, Stream) != 0;

					TWeakObjectPtr<USherpaOnnxTranscription> WeakThis(const_cast<USherpaOnnxTranscription*>(this));

					if (bIsEndpoint)
					{
						AsyncTask(ENamedThreads::GameThread, [WeakThis, Text]()
						{
							if (USherpaOnnxTranscription* Self = WeakThis.Get())
							{
								Self->OnFinalResult.Broadcast(Text);
							}
						});

						// Reset stream for next utterance
						SherpaOnnxOnlineStreamReset(Recognizer, Stream);
					}
					else
					{
						AsyncTask(ENamedThreads::GameThread, [WeakThis, Text]()
						{
							if (USherpaOnnxTranscription* Self = WeakThis.Get())
							{
								Self->OnPartialResult.Broadcast(Text);
							}
						});
					}
				}
			}

			if (Result)
			{
				SherpaOnnxDestroyOnlineRecognizerResult(Result);
			}
		}

		// Sleep briefly to avoid spinning — 30ms gives ~33 decode cycles/sec
		FPlatformProcess::Sleep(0.03f);
	}
}

#else // !WITH_SHERPA_ONNX

USherpaOnnxTranscription::USherpaOnnxTranscription()
{
	StreamingDoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

void USherpaOnnxTranscription::BeginDestroy()
{
	FPlatformProcess::ReturnSynchEventToPool(StreamingDoneEvent);
	StreamingDoneEvent = nullptr;
	Super::BeginDestroy();
}

void USherpaOnnxTranscription::SherpaOnnxLoadModel(const FString&, const FString&, const FString&, const FString&)
{
	UE_LOG(LogSherpaOnnxASR, Error, TEXT("SherpaOnnxASR: Not available — plugin was built without sherpa-onnx libraries"));
	OnModelLoaded.Broadcast(false);
}

void USherpaOnnxTranscription::SherpaOnnxUnloadModel() {}
bool USherpaOnnxTranscription::SherpaOnnxIsModelLoaded() const { return false; }

void USherpaOnnxTranscription::SherpaOnnxStartStreaming()
{
	UE_LOG(LogSherpaOnnxASR, Error, TEXT("SherpaOnnxASR: Not available — plugin was built without sherpa-onnx libraries"));
}

void USherpaOnnxTranscription::SherpaOnnxStopStreaming() {}
bool USherpaOnnxTranscription::SherpaOnnxIsStreaming() const { return false; }
void USherpaOnnxTranscription::DecodeLoop() {}

#endif // WITH_SHERPA_ONNX
