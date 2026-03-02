#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SherpaOnnxTextToSpeech.generated.h"

struct SherpaOnnxOfflineTts;

class USoundWaveProcedural;
class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTtsModelLoaded, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpeakComplete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSpeakError, const FString&, ErrorMessage);

UCLASS(BlueprintType, Blueprintable)
class LLAMACPP_API USherpaOnnxTextToSpeech : public UObject
{
	GENERATED_BODY()

public:
	USherpaOnnxTextToSpeech();
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "SherpaOnnxTTS")
	void LoadModel(const FString& ModelPath, const FString& TokensPath, const FString& DataDir);

	UFUNCTION(BlueprintCallable, Category = "SherpaOnnxTTS")
	void UnloadModel();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SherpaOnnxTTS")
	bool IsModelLoaded() const;

	UFUNCTION(BlueprintCallable, Category = "SherpaOnnxTTS")
	void Speak(const FString& Text, int32 SpeakerId = 0, float Speed = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "SherpaOnnxTTS")
	void Stop();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SherpaOnnxTTS")
	bool IsSpeaking() const;

	UPROPERTY(BlueprintAssignable, Category = "SherpaOnnxTTS")
	FOnTtsModelLoaded OnModelLoaded;

	UPROPERTY(BlueprintAssignable, Category = "SherpaOnnxTTS")
	FOnSpeakComplete OnSpeakComplete;

	UPROPERTY(BlueprintAssignable, Category = "SherpaOnnxTTS")
	FOnSpeakError OnSpeakError;

private:
	SherpaOnnxOfflineTts* TtsEngine = nullptr;

	TAtomic<bool> bIsSpeaking{false};
	TAtomic<bool> bStopRequested{false};

	UPROPERTY()
	USoundWaveProcedural* SoundWave = nullptr;

	UPROPERTY()
	UAudioComponent* AudioComp = nullptr;

	FTimerHandle PlaybackTimerHandle;

	void MonitorPlayback();
	void CleanupAudio();
};
