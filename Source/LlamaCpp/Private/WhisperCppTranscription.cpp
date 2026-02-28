#include "WhisperCppTranscription.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "AudioCaptureCore.h"
#include "whisper.h"

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
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Model already loaded, unloading first"));
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
			UE_LOG(LogTemp, Error, TEXT("Whisper: Failed to load model from %s"), *PathCopy);
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, LoadedCtx, bSuccess]()
		{
			if (UWhisperCppTranscription* Self = WeakThis.Get())
			{
				if (bSuccess)
				{
					Self->WhisperCtx = LoadedCtx;
					UE_LOG(LogTemp, Log, TEXT("Whisper: Model loaded successfully"));
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
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Cannot transcribe — no model loaded"));
		FWhisperTranscriptionResult EmptyResult;
		OnTranscriptionComplete.Broadcast(EmptyResult);
		return;
	}

	if (bIsTranscribing)
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Transcription already in progress"));
		return;
	}

	TArray<float> AudioData;
	int32 SampleRate = 0;
	int32 NumChannels = 0;

	if (!LoadWavFile(WavFilePath, AudioData, SampleRate, NumChannels))
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: Failed to load WAV file: %s"), *WavFilePath);
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
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Already capturing"));
		return;
	}

	CapturedAudioData.Empty();

	if (!AudioCapture)
	{
		AudioCapture = new Audio::FAudioCapture();
	}

	// Check if a capture device is available
	Audio::FCaptureDeviceInfo DeviceInfo;
	if (!AudioCapture->GetCaptureDeviceInfo(DeviceInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: No audio capture device found"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Whisper: Using capture device: %s (SampleRate=%d, Channels=%d)"),
		*DeviceInfo.DeviceName,
		DeviceInfo.PreferredSampleRate,
		DeviceInfo.InputChannels);

	Audio::FAudioCaptureDeviceParams Params;
	Audio::FOnAudioCaptureFunction OnCapture = [this](const float* InAudio, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow)
	{
		if (NumFrames <= 0 || InNumChannels <= 0)
		{
			return;
		}

		CaptureSampleRate = static_cast<float>(InSampleRate);
		CaptureNumChannels = InNumChannels;

		FScopeLock Lock(&CapturedAudioLock);
		int32 NumSamples = NumFrames * InNumChannels;
		int32 OldNum = CapturedAudioData.Num();
		CapturedAudioData.SetNum(OldNum + NumSamples);
		FMemory::Memcpy(CapturedAudioData.GetData() + OldNum, InAudio, NumSamples * sizeof(float));
	};

	if (!AudioCapture->OpenCaptureStream(Params, MoveTemp(OnCapture), 1024))
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: Failed to open audio capture stream"));
		return;
	}

	if (!AudioCapture->StartStream())
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: Failed to start audio capture stream"));
		return;
	}

	bIsCapturing = true;
	UE_LOG(LogTemp, Log, TEXT("Whisper: Microphone capture started"));
}

void UWhisperCppTranscription::StopMicrophoneCaptureAndTranscribe(const FString& Language)
{
	if (!bIsCapturing)
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Not currently capturing"));
		return;
	}

	if (AudioCapture)
	{
		AudioCapture->StopStream();
		AudioCapture->CloseStream();
	}

	bIsCapturing = false;
	UE_LOG(LogTemp, Log, TEXT("Whisper: Microphone capture stopped, %d samples captured"), CapturedAudioData.Num());

	if (!IsModelLoaded())
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Cannot transcribe — no model loaded"));
		FWhisperTranscriptionResult EmptyResult;
		OnTranscriptionComplete.Broadcast(EmptyResult);
		return;
	}

	if (CapturedAudioData.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: No audio data captured"));
		FWhisperTranscriptionResult EmptyResult;
		OnTranscriptionComplete.Broadcast(EmptyResult);
		return;
	}

	// Resample captured audio to 16kHz mono
	TArray<float> ResampledData;
	int32 SrcSampleRate = static_cast<int32>(CaptureSampleRate);
	if (SrcSampleRate != WHISPER_SAMPLE_RATE || CaptureNumChannels != 1)
	{
		ResampleTo16kMono(CapturedAudioData, SrcSampleRate, CaptureNumChannels, ResampledData);
	}
	else
	{
		ResampledData = MoveTemp(CapturedAudioData);
	}

	CapturedAudioData.Empty();

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

	UE_LOG(LogTemp, Log, TEXT("Whisper: Resampled to %d samples (%.1fs at 16kHz), RMS=%.6f, Peak=%.6f"),
		ResampledData.Num(), static_cast<float>(ResampledData.Num()) / WHISPER_SAMPLE_RATE, RMS, MaxAbs);

	if (RMS < 0.0001f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Audio appears to be silent (RMS=%.6f). Check microphone permissions and device."), RMS);
	}

	// Normalize audio to peak near 1.0 — whisper expects full-range float audio
	if (MaxAbs > 0.0f && MaxAbs < 0.5f)
	{
		float Gain = 0.9f / MaxAbs;
		for (int32 i = 0; i < ResampledData.Num(); ++i)
		{
			ResampledData[i] *= Gain;
		}
		UE_LOG(LogTemp, Log, TEXT("Whisper: Normalized audio (gain=%.1fx, new peak=%.2f)"), Gain, MaxAbs * Gain);
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

	TWeakObjectPtr<UWhisperCppTranscription> WeakThis(this);
	whisper_context* BgCtx = WhisperCtx;
	TAtomic<bool>* CancelFlag = &bCancelTranscription;
	TAtomic<bool>* TranscribingFlag = &bIsTranscribing;

	Async(EAsyncExecution::Thread, [WeakThis, AudioData = MoveTemp(AudioData), Language, BgCtx, CancelFlag, TranscribingFlag]()
	{
		FWhisperTranscriptionResult Result;

		whisper_full_params WParams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
		WParams.n_threads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
		if (WParams.n_threads > 4)
		{
			WParams.n_threads = 4;
		}
		WParams.print_progress = true;
		WParams.print_special = true;
		WParams.print_realtime = true;
		WParams.print_timestamps = true;
		WParams.single_segment = false;
		WParams.no_timestamps = false;

		std::string LanguageUtf8 = TCHAR_TO_UTF8(*Language);
		WParams.language = LanguageUtf8.c_str();

		// Set up abort callback for cancellation
		WParams.abort_callback = [](void* UserData) -> bool
		{
			TAtomic<bool>* Cancel = static_cast<TAtomic<bool>*>(UserData);
			return *Cancel;
		};
		WParams.abort_callback_user_data = CancelFlag;

		UE_LOG(LogTemp, Log, TEXT("Whisper: Running whisper_full with %d samples (%.1fs of audio)"),
			AudioData.Num(), static_cast<float>(AudioData.Num()) / WHISPER_SAMPLE_RATE);

		int Ret = whisper_full(BgCtx, WParams, AudioData.GetData(), AudioData.Num());

		UE_LOG(LogTemp, Log, TEXT("Whisper: whisper_full returned %d"), Ret);

		if (Ret == 0)
		{
			int NSegments = whisper_full_n_segments(BgCtx);
			UE_LOG(LogTemp, Log, TEXT("Whisper: Got %d segments"), NSegments);
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
			UE_LOG(LogTemp, Log, TEXT("Whisper: Transcription complete — %d segments"), NSegments);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Whisper: whisper_full failed with code %d"), Ret);
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
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Already in realtime transcription mode"));
		return;
	}

	if (!IsModelLoaded())
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Cannot start realtime transcription — no model loaded"));
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
			UE_LOG(LogTemp, Error, TEXT("Whisper: Failed to start microphone for realtime transcription"));
			return;
		}
	}

	bIsRealtimeTranscribing = true;

	float ClampedInterval = FMath::Max(IntervalSeconds, 0.5f);

	RealtimeTranscriptionLoop(Language, ClampedInterval);

	UE_LOG(LogTemp, Log, TEXT("Whisper: Realtime transcription started (interval=%.1fs)"), ClampedInterval);
}

void UWhisperCppTranscription::StopRealtimeTranscription()
{
	if (!bIsRealtimeTranscribing)
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Not in realtime transcription mode"));
		return;
	}

	bIsRealtimeTranscribing = false;
	UE_LOG(LogTemp, Log, TEXT("Whisper: Stopping realtime transcription..."));

	// Stop microphone
	if (bIsCapturing && AudioCapture)
	{
		AudioCapture->StopStream();
		AudioCapture->CloseStream();
		bIsCapturing = false;
	}

	// Fire OnTranscriptionComplete with accumulated text
	FWhisperTranscriptionResult FinalResult;
	FinalResult.FullText = AccumulatedTranscription;
	FinalResult.bSuccess = true;
	OnTranscriptionComplete.Broadcast(FinalResult);

	UE_LOG(LogTemp, Log, TEXT("Whisper: Realtime transcription stopped. Final text: %s"), *AccumulatedTranscription);
}

bool UWhisperCppTranscription::IsRealtimeTranscribing() const
{
	return bIsRealtimeTranscribing;
}

void UWhisperCppTranscription::RealtimeTranscriptionLoop(FString Language, float IntervalSeconds)
{
	TWeakObjectPtr<UWhisperCppTranscription> WeakThis(this);
	whisper_context* BgCtx = WhisperCtx;
	TAtomic<bool>* RealtimeFlag = &bIsRealtimeTranscribing;

	Async(EAsyncExecution::Thread, [WeakThis, Language, IntervalSeconds, BgCtx, RealtimeFlag]()
	{
		const int32 MaxWindowSamples = 30 * WHISPER_SAMPLE_RATE; // 30 seconds at 16kHz = 480000

		while (*RealtimeFlag)
		{
			FPlatformProcess::Sleep(IntervalSeconds);

			if (!*RealtimeFlag)
			{
				break;
			}

			// Copy current audio buffer under lock
			TArray<float> AudioSnapshot;
			float SrcSampleRate = 0.0f;
			int32 SrcNumChannels = 0;
			bool bGotAudio = false;

			if (UWhisperCppTranscription* Self = WeakThis.Get())
			{
				{
					FScopeLock Lock(&Self->CapturedAudioLock);
					AudioSnapshot = Self->CapturedAudioData;
					SrcSampleRate = Self->CaptureSampleRate;
					SrcNumChannels = Self->CaptureNumChannels;
				}
				bGotAudio = AudioSnapshot.Num() > 0;
			}
			else
			{
				break;
			}

			if (!bGotAudio || SrcSampleRate <= 0.0f || SrcNumChannels <= 0)
			{
				UE_LOG(LogTemp, Verbose, TEXT("Whisper: Realtime loop — no audio yet (samples=%d, rate=%.0f, ch=%d)"),
					AudioSnapshot.Num(), SrcSampleRate, SrcNumChannels);
				continue;
			}

			UE_LOG(LogTemp, Log, TEXT("Whisper: Realtime loop — processing %d samples (rate=%.0f, ch=%d)"),
				AudioSnapshot.Num(), SrcSampleRate, SrcNumChannels);

			// Resample to 16kHz mono
			TArray<float> ResampledData;
			int32 IntSampleRate = static_cast<int32>(SrcSampleRate);

			if (IntSampleRate != WHISPER_SAMPLE_RATE || SrcNumChannels != 1)
			{
				// Inline resampling (can't call member function from background thread safely)
				// Downmix to mono
				TArray<float> MonoData;
				int32 NumFrames = AudioSnapshot.Num() / SrcNumChannels;

				if (SrcNumChannels > 1)
				{
					MonoData.SetNum(NumFrames);
					for (int32 i = 0; i < NumFrames; ++i)
					{
						float Sum = 0.0f;
						for (int32 c = 0; c < SrcNumChannels; ++c)
						{
							Sum += AudioSnapshot[i * SrcNumChannels + c];
						}
						MonoData[i] = Sum / static_cast<float>(SrcNumChannels);
					}
				}
				else
				{
					MonoData = MoveTemp(AudioSnapshot);
				}

				// Resample
				if (IntSampleRate == WHISPER_SAMPLE_RATE)
				{
					ResampledData = MoveTemp(MonoData);
				}
				else
				{
					double Ratio = static_cast<double>(WHISPER_SAMPLE_RATE) / static_cast<double>(IntSampleRate);
					int32 OutNumFrames = static_cast<int32>(NumFrames * Ratio);
					ResampledData.SetNum(OutNumFrames);

					for (int32 i = 0; i < OutNumFrames; ++i)
					{
						double SrcIndex = static_cast<double>(i) / Ratio;
						int32 Idx0 = static_cast<int32>(SrcIndex);
						int32 Idx1 = FMath::Min(Idx0 + 1, NumFrames - 1);
						double Frac = SrcIndex - static_cast<double>(Idx0);
						ResampledData[i] = static_cast<float>(MonoData[Idx0] * (1.0 - Frac) + MonoData[Idx1] * Frac);
					}
				}
			}
			else
			{
				ResampledData = MoveTemp(AudioSnapshot);
			}

			// Keep only last 30 seconds
			if (ResampledData.Num() > MaxWindowSamples)
			{
				int32 StartOffset = ResampledData.Num() - MaxWindowSamples;
				TArray<float> Trimmed;
				Trimmed.SetNum(MaxWindowSamples);
				FMemory::Memcpy(Trimmed.GetData(), ResampledData.GetData() + StartOffset, MaxWindowSamples * sizeof(float));
				ResampledData = MoveTemp(Trimmed);
			}

			// Normalize audio for whisper
			{
				float PeakAbs = 0.0f;
				for (int32 i = 0; i < ResampledData.Num(); ++i)
				{
					float Abs = FMath::Abs(ResampledData[i]);
					if (Abs > PeakAbs) PeakAbs = Abs;
				}
				if (PeakAbs > 0.0f && PeakAbs < 0.5f)
				{
					float Gain = 0.9f / PeakAbs;
					for (int32 i = 0; i < ResampledData.Num(); ++i)
					{
						ResampledData[i] *= Gain;
					}
				}
			}

			// Run whisper
			whisper_full_params WParams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
			WParams.n_threads = FMath::Min(FPlatformMisc::NumberOfCoresIncludingHyperthreads(), 4);
			WParams.print_progress = false;
			WParams.print_special = false;
			WParams.print_realtime = false;
			WParams.print_timestamps = false;
			WParams.single_segment = false;
			WParams.no_timestamps = true;

			std::string LanguageUtf8 = TCHAR_TO_UTF8(*Language);
			WParams.language = LanguageUtf8.c_str();

			// Use realtime flag as abort callback
			WParams.abort_callback = [](void* UserData) -> bool
			{
				TAtomic<bool>* Flag = static_cast<TAtomic<bool>*>(UserData);
				return !(*Flag); // Abort if no longer in realtime mode
			};
			WParams.abort_callback_user_data = RealtimeFlag;

			int Ret = whisper_full(BgCtx, WParams, ResampledData.GetData(), ResampledData.Num());

			if (Ret != 0 || !*RealtimeFlag)
			{
				continue;
			}

			// Extract full window text
			FString WindowText;
			int NSegments = whisper_full_n_segments(BgCtx);
			for (int i = 0; i < NSegments; ++i)
			{
				const char* SegText = whisper_full_get_segment_text(BgCtx, i);
				WindowText += UTF8_TO_TCHAR(SegText);
			}

			WindowText = WindowText.TrimStartAndEnd();

			// Compute delta: find new text by checking if window text starts with or contains last window text
			FString DeltaText;

			if (UWhisperCppTranscription* Self = WeakThis.Get())
			{
				if (Self->LastWindowText.IsEmpty())
				{
					DeltaText = WindowText;
				}
				else if (WindowText.Len() > Self->LastWindowText.Len() && WindowText.StartsWith(Self->LastWindowText))
				{
					DeltaText = WindowText.Mid(Self->LastWindowText.Len()).TrimStartAndEnd();
				}
				else if (!WindowText.Equals(Self->LastWindowText))
				{
					// Window shifted significantly, treat all as new
					DeltaText = WindowText;
				}

				Self->LastWindowText = WindowText;

				if (!DeltaText.IsEmpty())
				{
					if (!Self->AccumulatedTranscription.IsEmpty())
					{
						Self->AccumulatedTranscription += TEXT(" ");
					}
					Self->AccumulatedTranscription += DeltaText;

					// Broadcast on game thread
					FString PartialText = DeltaText;
					AsyncTask(ENamedThreads::GameThread, [WeakThis, PartialText]()
					{
						if (UWhisperCppTranscription* GameSelf = WeakThis.Get())
						{
							GameSelf->OnPartialTranscription.Broadcast(PartialText);
						}
					});
				}
			}
			else
			{
				break;
			}
		}
	});
}

bool UWhisperCppTranscription::LoadWavFile(const FString& FilePath, TArray<float>& OutAudioData, int32& OutSampleRate, int32& OutNumChannels)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		return false;
	}

	if (FileData.Num() < 44)
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: WAV file too small"));
		return false;
	}

	const uint8* Data = FileData.GetData();

	// Verify RIFF header
	if (Data[0] != 'R' || Data[1] != 'I' || Data[2] != 'F' || Data[3] != 'F')
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: Not a valid RIFF file"));
		return false;
	}

	// Verify WAVE format
	if (Data[8] != 'W' || Data[9] != 'A' || Data[10] != 'V' || Data[11] != 'E')
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: Not a valid WAVE file"));
		return false;
	}

	// Parse fmt chunk — find it by scanning chunks
	int32 Offset = 12;
	int16 AudioFormat = 0;
	int16 NumChannels = 0;
	int32 SampleRate = 0;
	int16 BitsPerSample = 0;

	while (Offset + 8 <= FileData.Num())
	{
		const uint8* ChunkPtr = Data + Offset;
		uint32 ChunkSize = *reinterpret_cast<const uint32*>(ChunkPtr + 4);

		if (ChunkPtr[0] == 'f' && ChunkPtr[1] == 'm' && ChunkPtr[2] == 't' && ChunkPtr[3] == ' ')
		{
			if (Offset + 8 + 16 > FileData.Num())
			{
				return false;
			}
			AudioFormat = *reinterpret_cast<const int16*>(ChunkPtr + 8);
			NumChannels = *reinterpret_cast<const int16*>(ChunkPtr + 10);
			SampleRate = *reinterpret_cast<const int32*>(ChunkPtr + 12);
			BitsPerSample = *reinterpret_cast<const int16*>(ChunkPtr + 22);
		}
		else if (ChunkPtr[0] == 'd' && ChunkPtr[1] == 'a' && ChunkPtr[2] == 't' && ChunkPtr[3] == 'a')
		{
			if (AudioFormat == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("Whisper: data chunk found before fmt chunk"));
				return false;
			}

			const uint8* AudioDataPtr = ChunkPtr + 8;
			int32 AudioDataSize = FMath::Min(static_cast<int32>(ChunkSize), FileData.Num() - (Offset + 8));

			if (AudioFormat == 1 && BitsPerSample == 16) // PCM 16-bit
			{
				int32 NumSamples = AudioDataSize / 2;
				OutAudioData.SetNum(NumSamples);
				const int16* Samples16 = reinterpret_cast<const int16*>(AudioDataPtr);
				for (int32 i = 0; i < NumSamples; ++i)
				{
					OutAudioData[i] = static_cast<float>(Samples16[i]) / 32768.0f;
				}
			}
			else if (AudioFormat == 3 && BitsPerSample == 32) // IEEE float 32-bit
			{
				int32 NumSamples = AudioDataSize / 4;
				OutAudioData.SetNum(NumSamples);
				FMemory::Memcpy(OutAudioData.GetData(), AudioDataPtr, NumSamples * sizeof(float));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Whisper: Unsupported WAV format (format=%d, bits=%d)"), AudioFormat, BitsPerSample);
				return false;
			}

			OutSampleRate = SampleRate;
			OutNumChannels = NumChannels;
			return true;
		}

		Offset += 8 + ChunkSize;
		// Chunks are word-aligned
		if (ChunkSize % 2 != 0)
		{
			Offset++;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("Whisper: No data chunk found in WAV file"));
	return false;
}

void UWhisperCppTranscription::ResampleTo16kMono(const TArray<float>& InData, int32 InSampleRate, int32 InNumChannels, TArray<float>& OutData)
{
	if (InData.Num() == 0 || InSampleRate <= 0 || InNumChannels <= 0)
	{
		return;
	}

	// First: downmix to mono if needed
	TArray<float> MonoData;
	int32 NumFrames = InData.Num() / InNumChannels;

	if (InNumChannels > 1)
	{
		MonoData.SetNum(NumFrames);
		for (int32 i = 0; i < NumFrames; ++i)
		{
			float Sum = 0.0f;
			for (int32 c = 0; c < InNumChannels; ++c)
			{
				Sum += InData[i * InNumChannels + c];
			}
			MonoData[i] = Sum / static_cast<float>(InNumChannels);
		}
	}
	else
	{
		MonoData = InData;
	}

	// Then: resample to 16kHz if needed
	if (InSampleRate == WHISPER_SAMPLE_RATE)
	{
		OutData = MoveTemp(MonoData);
		return;
	}

	double Ratio = static_cast<double>(WHISPER_SAMPLE_RATE) / static_cast<double>(InSampleRate);
	int32 OutNumFrames = static_cast<int32>(NumFrames * Ratio);
	OutData.SetNum(OutNumFrames);

	for (int32 i = 0; i < OutNumFrames; ++i)
	{
		double SrcIndex = static_cast<double>(i) / Ratio;
		int32 Idx0 = static_cast<int32>(SrcIndex);
		int32 Idx1 = FMath::Min(Idx0 + 1, NumFrames - 1);
		double Frac = SrcIndex - static_cast<double>(Idx0);

		OutData[i] = static_cast<float>(MonoData[Idx0] * (1.0 - Frac) + MonoData[Idx1] * Frac);
	}
}
