#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LlamaCppBlueprintLibrary.generated.h"

class ULlamaCppInference;
class UWhisperCppTranscription;

UCLASS()
class LLAMACPP_API ULlamaCppBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LlamaCpp", meta = (WorldContext = "WorldContextObject"))
	static ULlamaCppInference* CreateLlamaCppInference(UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Whisper", meta = (WorldContext = "WorldContextObject"))
	static UWhisperCppTranscription* CreateWhisperTranscription(UObject* WorldContextObject);
};
