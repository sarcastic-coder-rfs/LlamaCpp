#include "LlamaCppInference.h"
#include "Async/Async.h"
#include "llama.h"
#include <string>

ULlamaCppInference::ULlamaCppInference()
{
}

void ULlamaCppInference::BeginDestroy()
{
	StopGeneration();
	UnloadModel();
	Super::BeginDestroy();
}

void ULlamaCppInference::LoadModel(const FString& ModelPath, int32 ContextSize)
{
	if (Model)
	{
		UE_LOG(LogTemp, Warning, TEXT("LlamaCpp: Model already loaded, unloading first"));
		UnloadModel();
	}

	CachedContextSize = ContextSize;

	// Capture a weak reference for the async callback
	TWeakObjectPtr<ULlamaCppInference> WeakThis(this);
	FString PathCopy = ModelPath;

	Async(EAsyncExecution::Thread, [WeakThis, PathCopy, ContextSize]()
	{
		llama_model_params ModelParams = llama_model_default_params();
		ModelParams.n_gpu_layers = 0; // CPU-only on Quest 3

		llama_model* LoadedModel = llama_model_load_from_file(
			TCHAR_TO_UTF8(*PathCopy), ModelParams);

		bool bSuccess = (LoadedModel != nullptr);
		const llama_vocab* LoadedVocab = nullptr;
		llama_context* LoadedCtx = nullptr;

		if (bSuccess)
		{
			LoadedVocab = llama_model_get_vocab(LoadedModel);

			llama_context_params CtxParams = llama_context_default_params();
			CtxParams.n_ctx = ContextSize;
			CtxParams.n_batch = 512;
			CtxParams.no_perf = true;

			LoadedCtx = llama_init_from_model(LoadedModel, CtxParams);
			if (!LoadedCtx)
			{
				UE_LOG(LogTemp, Error, TEXT("LlamaCpp: Failed to create context"));
				llama_model_free(LoadedModel);
				LoadedModel = nullptr;
				LoadedVocab = nullptr;
				bSuccess = false;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("LlamaCpp: Failed to load model from %s"), *PathCopy);
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, LoadedModel, LoadedCtx, LoadedVocab, bSuccess]()
		{
			if (ULlamaCppInference* Self = WeakThis.Get())
			{
				if (bSuccess)
				{
					Self->Model = LoadedModel;
					Self->Ctx = LoadedCtx;
					Self->Vocab = LoadedVocab;
					UE_LOG(LogTemp, Log, TEXT("LlamaCpp: Model loaded successfully"));
				}
				Self->OnModelLoaded.Broadcast(bSuccess);
			}
			else if (bSuccess)
			{
				// Object was destroyed while loading — clean up
				llama_free(LoadedCtx);
				llama_model_free(LoadedModel);
			}
		});
	});
}

void ULlamaCppInference::UnloadModel()
{
	if (Ctx)
	{
		llama_free(Ctx);
		Ctx = nullptr;
	}
	if (Model)
	{
		llama_model_free(Model);
		Model = nullptr;
	}
	Vocab = nullptr;
}

bool ULlamaCppInference::IsModelLoaded() const
{
	return Model != nullptr && Ctx != nullptr;
}

void ULlamaCppInference::GenerateTextAsync(const FString& Prompt, int32 MaxTokens, FLlamaSamplingParams SamplingParams)
{
	if (!IsModelLoaded())
	{
		UE_LOG(LogTemp, Warning, TEXT("LlamaCpp: Cannot generate — no model loaded"));
		OnGenerationComplete.Broadcast(TEXT(""));
		return;
	}

	if (bIsGenerating)
	{
		UE_LOG(LogTemp, Warning, TEXT("LlamaCpp: Generation already in progress"));
		return;
	}

	bCancelGeneration = false;
	bIsGenerating = true;

	TWeakObjectPtr<ULlamaCppInference> WeakThis(this);
	FString PromptCopy = Prompt;

	// Capture raw pointers for the background thread (they remain valid
	// as long as the UObject is alive, and we check via WeakThis)
	llama_context* BgCtx = Ctx;
	const llama_vocab* BgVocab = Vocab;
	TAtomic<bool>* CancelFlag = &bCancelGeneration;
	TAtomic<bool>* GeneratingFlag = &bIsGenerating;

	Async(EAsyncExecution::Thread, [WeakThis, PromptCopy, MaxTokens, SamplingParams,
									BgCtx, BgVocab, CancelFlag, GeneratingFlag]()
	{
		FString FullResult;

		// --- Clear KV cache ---
		llama_memory_clear(llama_get_memory(BgCtx), true);

		// --- Tokenize ---
		std::string PromptUtf8 = TCHAR_TO_UTF8(*PromptCopy);
		int32_t NPromptTokens = -llama_tokenize(BgVocab, PromptUtf8.c_str(),
			PromptUtf8.size(), nullptr, 0, true, true);

		if (NPromptTokens <= 0)
		{
			UE_LOG(LogTemp, Error, TEXT("LlamaCpp: Failed to tokenize prompt"));
			*GeneratingFlag = false;
			AsyncTask(ENamedThreads::GameThread, [WeakThis]()
			{
				if (auto* Self = WeakThis.Get())
					Self->OnGenerationComplete.Broadcast(TEXT(""));
			});
			return;
		}

		TArray<int32_t> PromptTokens;
		PromptTokens.SetNum(NPromptTokens);
		llama_tokenize(BgVocab, PromptUtf8.c_str(), PromptUtf8.size(),
			PromptTokens.GetData(), PromptTokens.Num(), true, true);

		// --- Build sampler chain ---
		auto SChainParams = llama_sampler_chain_default_params();
		SChainParams.no_perf = true;
		llama_sampler* Sampler = llama_sampler_chain_init(SChainParams);

		llama_sampler_chain_add(Sampler, llama_sampler_init_penalties(
			64,                            // penalty_last_n
			SamplingParams.RepeatPenalty,   // penalty_repeat
			0.0f,                          // penalty_freq
			0.0f));                        // penalty_present
		llama_sampler_chain_add(Sampler, llama_sampler_init_top_k(SamplingParams.TopK));
		llama_sampler_chain_add(Sampler, llama_sampler_init_top_p(SamplingParams.TopP, 1));
		llama_sampler_chain_add(Sampler, llama_sampler_init_min_p(SamplingParams.MinP, 1));
		llama_sampler_chain_add(Sampler, llama_sampler_init_temp(SamplingParams.Temperature));
		llama_sampler_chain_add(Sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

		// --- Prompt eval ---
		llama_batch Batch = llama_batch_get_one(PromptTokens.GetData(), PromptTokens.Num());
		if (llama_decode(BgCtx, Batch) != 0)
		{
			UE_LOG(LogTemp, Error, TEXT("LlamaCpp: Failed to decode prompt"));
			llama_sampler_free(Sampler);
			*GeneratingFlag = false;
			AsyncTask(ENamedThreads::GameThread, [WeakThis]()
			{
				if (auto* Self = WeakThis.Get())
					Self->OnGenerationComplete.Broadcast(TEXT(""));
			});
			return;
		}

		// --- Token generation loop ---
		for (int32 i = 0; i < MaxTokens; ++i)
		{
			if (*CancelFlag)
			{
				break;
			}

			llama_token NewToken = llama_sampler_sample(Sampler, BgCtx, -1);

			if (llama_vocab_is_eog(BgVocab, NewToken))
			{
				break;
			}

			// Convert token to text
			char Buf[256];
			int32_t Len = llama_token_to_piece(BgVocab, NewToken, Buf, sizeof(Buf), 0, true);
			if (Len < 0)
			{
				break;
			}

			FString TokenStr = UTF8_TO_TCHAR(std::string(Buf, Len).c_str());
			FullResult += TokenStr;

			// Stream token to game thread
			AsyncTask(ENamedThreads::GameThread, [WeakThis, TokenStr]()
			{
				if (auto* Self = WeakThis.Get())
					Self->OnTokenGenerated.Broadcast(TokenStr);
			});

			// Prepare next batch
			Batch = llama_batch_get_one(&NewToken, 1);
			if (llama_decode(BgCtx, Batch) != 0)
			{
				UE_LOG(LogTemp, Error, TEXT("LlamaCpp: Decode failed at token %d"), i);
				break;
			}
		}

		llama_sampler_free(Sampler);
		*GeneratingFlag = false;

		// Broadcast final result
		AsyncTask(ENamedThreads::GameThread, [WeakThis, FullResult]()
		{
			if (auto* Self = WeakThis.Get())
				Self->OnGenerationComplete.Broadcast(FullResult);
		});
	});
}

void ULlamaCppInference::StopGeneration()
{
	bCancelGeneration = true;
}
