#include "LlamaCppBlueprintLibrary.h"
#include "LlamaCppInference.h"
#include "WhisperCppTranscription.h"

ULlamaCppInference* ULlamaCppBlueprintLibrary::CreateLlamaCppInference(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogTemp, Error, TEXT("LlamaCpp: CreateLlamaCppInference called with null WorldContextObject"));
		return nullptr;
	}

	ULlamaCppInference* Inference = NewObject<ULlamaCppInference>(WorldContextObject);
	return Inference;
}

UWhisperCppTranscription* ULlamaCppBlueprintLibrary::CreateWhisperTranscription(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: CreateWhisperTranscription called with null WorldContextObject"));
		return nullptr;
	}

	UWhisperCppTranscription* Transcription = NewObject<UWhisperCppTranscription>(WorldContextObject);
	return Transcription;
}
