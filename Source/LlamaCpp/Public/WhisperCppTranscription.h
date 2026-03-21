#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AudioCaptureCore.h"
#include "WhisperCppTranscription.generated.h"

struct whisper_context;

USTRUCT(BlueprintType)
struct FWhisperTranscriptionSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	float StartTimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	float EndTimeSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct FWhisperTranscriptionResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	FString FullText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	TArray<FWhisperTranscriptionSegment> Segments;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	bool bSuccess = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWhisperModelLoaded, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranscriptionComplete, const FWhisperTranscriptionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPartialTranscription, const FString&, PartialText);

UCLASS(BlueprintType, Blueprintable)
class LLAMACPP_API UWhisperCppTranscription : public UObject
{
	GENERATED_BODY()

public:
	UWhisperCppTranscription();
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void LoadModel(const FString& ModelPath);

	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void UnloadModel();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Whisper")
	bool IsModelLoaded() const;

	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void TranscribeWavFileAsync(const FString& WavFilePath, const FString& Language = TEXT("en"));

	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void StartMicrophoneCapture();

	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void StopMicrophoneCaptureAndTranscribe(const FString& Language = TEXT("en"));

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Whisper")
	bool IsCapturing() const;

	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void StopTranscription();

	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void StartRealtimeTranscription(const FString& Language = TEXT("en"), float IntervalSeconds = 3.0f);

	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void StopRealtimeTranscription();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Whisper")
	bool IsRealtimeTranscribing() const;

	/** Maximum number of threads whisper uses for transcription. Default 4 (good for mobile). Increase on PC for faster results. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	int32 MaxThreads = 4;

	/** Force whisper to output a single segment. Faster for realtime use since it skips segmentation. Default false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper")
	bool bSingleSegment = false;

	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnWhisperModelLoaded OnModelLoaded;

	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnTranscriptionComplete OnTranscriptionComplete;

	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnPartialTranscription OnPartialTranscription;

private:
	whisper_context* WhisperCtx = nullptr;

	TAtomic<bool> bCancelTranscription{false};
	TAtomic<bool> bIsTranscribing{false};
	TAtomic<bool> bIsCapturing{false};
	TAtomic<bool> bHasReceivedAudio{false};

	TArray<float> CapturedAudioData;
	FCriticalSection CapturedAudioLock;
	float CaptureSampleRate = 0.0f;
	int32 CaptureNumChannels = 0;

	class Audio::FAudioCapture* AudioCapture = nullptr;

	void RunTranscription(TArray<float> AudioData, const FString& Language, int32 NumThreads, bool bUseSingleSegment);
	bool LoadWavFile(const FString& FilePath, TArray<float>& OutAudioData, int32& OutSampleRate, int32& OutNumChannels);
	void ResampleTo16kMono(const TArray<float>& InData, int32 InSampleRate, int32 InNumChannels, TArray<float>& OutData);

	void RealtimeTranscriptionLoop(FString Language, float IntervalSeconds);

	TAtomic<bool> bIsRealtimeTranscribing{false};
	TAtomic<bool> bIsBeingDestroyed{false};
	FString AccumulatedTranscription;
	FString LastWindowText;

	FEvent* TranscriptionDoneEvent = nullptr;
	FEvent* RealtimeDoneEvent = nullptr;
};
