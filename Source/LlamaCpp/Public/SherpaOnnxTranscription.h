#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AudioCaptureCore.h"
#include "SherpaOnnxTranscription.generated.h"

struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineStream;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSherpaAsrModelLoaded, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSherpaTranscriptionText, const FString&, Text);

UCLASS(BlueprintType, Blueprintable)
class LLAMACPP_API USherpaOnnxTranscription : public UObject
{
	GENERATED_BODY()

public:
	USherpaOnnxTranscription();
	virtual void BeginDestroy() override;

	/** Load a streaming transducer model (encoder, decoder, joiner, tokens). */
	UFUNCTION(BlueprintCallable, Category = "SherpaOnnxASR")
	void SherpaOnnxLoadModel(const FString& EncoderPath, const FString& DecoderPath, const FString& JoinerPath, const FString& TokensPath);

	UFUNCTION(BlueprintCallable, Category = "SherpaOnnxASR")
	void SherpaOnnxUnloadModel();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SherpaOnnxASR")
	bool SherpaOnnxIsModelLoaded() const;

	/** Start streaming microphone transcription. Results fire continuously via OnPartialResult and OnFinalResult. */
	UFUNCTION(BlueprintCallable, Category = "SherpaOnnxASR")
	void SherpaOnnxStartStreaming();

	/** Stop streaming transcription and microphone capture. */
	UFUNCTION(BlueprintCallable, Category = "SherpaOnnxASR")
	void SherpaOnnxStopStreaming();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SherpaOnnxASR")
	bool SherpaOnnxIsStreaming() const;

	/** Maximum threads for the recognizer. Default 2 (streaming models are lighter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SherpaOnnxASR")
	int32 MaxThreads = 2;

	/** Fired when the model finishes loading. */
	UPROPERTY(BlueprintAssignable, Category = "SherpaOnnxASR")
	FOnSherpaAsrModelLoaded OnModelLoaded;

	/** Fired with partial (in-progress) transcription text as speech is recognized. */
	UPROPERTY(BlueprintAssignable, Category = "SherpaOnnxASR")
	FOnSherpaTranscriptionText OnPartialResult;

	/** Fired when an endpoint is detected, with the final text for that utterance. */
	UPROPERTY(BlueprintAssignable, Category = "SherpaOnnxASR")
	FOnSherpaTranscriptionText OnFinalResult;

private:
	const SherpaOnnxOnlineRecognizer* Recognizer = nullptr;
	const SherpaOnnxOnlineStream* Stream = nullptr;

	TAtomic<bool> bIsStreaming{false};
	TAtomic<bool> bIsBeingDestroyed{false};
	TAtomic<bool> bIsCapturing{false};

	TArray<float> AudioBuffer;
	FCriticalSection AudioBufferLock;

	class Audio::FAudioCapture* AudioCapture = nullptr;

	void DecodeLoop();

	FEvent* StreamingDoneEvent = nullptr;
};
