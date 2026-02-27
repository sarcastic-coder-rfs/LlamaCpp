#include "LlamaCppModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "llama.h"

#define LOCTEXT_NAMESPACE "FLlamaCppModule"

void FLlamaCppModule::StartupModule()
{
	// Load shared libraries before calling into llama.cpp
	FString BaseDir = IPluginManager::Get().FindPlugin(TEXT("LlamaCpp"))->GetBaseDir();

#if PLATFORM_WINDOWS
	FString LibDir = FPaths::Combine(*BaseDir, TEXT("ThirdParty/llama/lib/Win64"));
	TArray<FString> LibNames = {
		TEXT("ggml-base.dll"),
		TEXT("ggml-cpu.dll"),
		TEXT("ggml.dll"),
		TEXT("llama.dll")
	};
#elif PLATFORM_MAC
	FString LibDir = FPaths::Combine(*BaseDir, TEXT("ThirdParty/llama/lib/Mac"));
	TArray<FString> LibNames = {
		TEXT("libggml-base.dylib"),
		TEXT("libggml-cpu.dylib"),
		TEXT("libggml.dylib"),
		TEXT("libllama.dylib")
	};
#endif

#if PLATFORM_WINDOWS || PLATFORM_MAC
	FPlatformProcess::PushDllDirectory(*LibDir);
	for (const FString& Lib : LibNames)
	{
		void* Handle = FPlatformProcess::GetDllHandle(*FPaths::Combine(*LibDir, Lib));
		if (Handle)
		{
			DllHandles.Add(Handle);
			UE_LOG(LogTemp, Log, TEXT("LlamaCpp: Loaded %s"), *Lib);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("LlamaCpp: Failed to load %s from %s"), *Lib, *LibDir);
		}
	}
	FPlatformProcess::PopDllDirectory(*LibDir);
#endif

	llama_backend_init();
	UE_LOG(LogTemp, Log, TEXT("LlamaCpp: backend initialized"));
}

void FLlamaCppModule::ShutdownModule()
{
	llama_backend_free();

	for (void* Handle : DllHandles)
	{
		FPlatformProcess::FreeDllHandle(Handle);
	}
	DllHandles.Empty();

	UE_LOG(LogTemp, Log, TEXT("LlamaCpp: backend freed"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLlamaCppModule, LlamaCpp)
