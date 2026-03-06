#include "LlamaCppBlueprintLibrary.h"
#include "LlamaCppInference.h"
#include "WhisperCppTranscription.h"
#include "SherpaOnnxTextToSpeech.h"
#include "LlamaCppLog.h"

ULlamaCppInference* ULlamaCppBlueprintLibrary::CreateLlamaCppInference(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogLlamaCpp, Error, TEXT("LlamaCpp: CreateLlamaCppInference called with null WorldContextObject"));
		return nullptr;
	}

	ULlamaCppInference* Inference = NewObject<ULlamaCppInference>(WorldContextObject);
	return Inference;
}

UWhisperCppTranscription* ULlamaCppBlueprintLibrary::CreateWhisperTranscription(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogWhisperCpp, Error, TEXT("Whisper: CreateWhisperTranscription called with null WorldContextObject"));
		return nullptr;
	}

	UWhisperCppTranscription* Transcription = NewObject<UWhisperCppTranscription>(WorldContextObject);
	return Transcription;
}

USherpaOnnxTextToSpeech* ULlamaCppBlueprintLibrary::CreateSherpaOnnxTTS(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		UE_LOG(LogSherpaOnnxTTS, Error, TEXT("SherpaOnnxTTS: CreateSherpaOnnxTTS called with null WorldContextObject"));
		return nullptr;
	}

	USherpaOnnxTextToSpeech* Tts = NewObject<USherpaOnnxTextToSpeech>(WorldContextObject);
	return Tts;
}
