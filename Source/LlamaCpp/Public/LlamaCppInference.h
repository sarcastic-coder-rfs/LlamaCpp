#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "LlamaCppInference.generated.h"

USTRUCT(BlueprintType)
struct FLlamaSamplingParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCpp")
	float Temperature = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCpp")
	int32 TopK = 40;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCpp")
	float TopP = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCpp")
	float MinP = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LlamaCpp")
	float RepeatPenalty = 1.1f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTokenGenerated, const FString&, Token);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGenerationComplete, const FString&, FullText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnModelLoaded, bool, bSuccess);

UCLASS(BlueprintType, Blueprintable)
class LLAMACPP_API ULlamaCppInference : public UObject
{
	GENERATED_BODY()

public:
	ULlamaCppInference();
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "LlamaCpp")
	void LoadModel(const FString& ModelPath, int32 ContextSize = 2048);

	UFUNCTION(BlueprintCallable, Category = "LlamaCpp")
	void UnloadModel();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "LlamaCpp")
	bool IsModelLoaded() const;

	UFUNCTION(BlueprintCallable, Category = "LlamaCpp")
	void GenerateTextAsync(const FString& Prompt, int32 MaxTokens = 256, FLlamaSamplingParams SamplingParams = FLlamaSamplingParams());

	UFUNCTION(BlueprintCallable, Category = "LlamaCpp")
	void StopGeneration();

	UPROPERTY(BlueprintAssignable, Category = "LlamaCpp")
	FOnTokenGenerated OnTokenGenerated;

	UPROPERTY(BlueprintAssignable, Category = "LlamaCpp")
	FOnGenerationComplete OnGenerationComplete;

	UPROPERTY(BlueprintAssignable, Category = "LlamaCpp")
	FOnModelLoaded OnModelLoaded;

private:
	struct llama_model* Model = nullptr;
	struct llama_context* Ctx = nullptr;
	const struct llama_vocab* Vocab = nullptr;
	int32 CachedContextSize = 2048;

	TAtomic<bool> bCancelGeneration{false};
	TAtomic<bool> bIsGenerating{false};
};
