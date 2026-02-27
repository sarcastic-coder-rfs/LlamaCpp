#include "LlamaCppModule.h"
#include "llama.h"

#define LOCTEXT_NAMESPACE "FLlamaCppModule"

void FLlamaCppModule::StartupModule()
{
	llama_backend_init();
	UE_LOG(LogTemp, Log, TEXT("LlamaCpp: backend initialized"));
}

void FLlamaCppModule::ShutdownModule()
{
	llama_backend_free();
	UE_LOG(LogTemp, Log, TEXT("LlamaCpp: backend freed"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLlamaCppModule, LlamaCpp)
