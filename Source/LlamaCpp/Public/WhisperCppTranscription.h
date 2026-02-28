#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
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

	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnWhisperModelLoaded OnModelLoaded;

	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnTranscriptionComplete OnTranscriptionComplete;

private:
	whisper_context* WhisperCtx = nullptr;

	TAtomic<bool> bCancelTranscription{false};
	TAtomic<bool> bIsTranscribing{false};
	TAtomic<bool> bIsCapturing{false};

	TArray<float> CapturedAudioData;
	FCriticalSection CapturedAudioLock;
	float CaptureSampleRate = 0.0f;
	int32 CaptureNumChannels = 0;

	class Audio::FAudioCapture* AudioCapture = nullptr;

	void RunTranscription(TArray<float> AudioData, const FString& Language);
	bool LoadWavFile(const FString& FilePath, TArray<float>& OutAudioData, int32& OutSampleRate, int32& OutNumChannels);
	void ResampleTo16kMono(const TArray<float>& InData, int32 InSampleRate, int32 InNumChannels, TArray<float>& OutData);
};
