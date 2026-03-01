#include "LlamaCppModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "llama.h"

#define LOCTEXT_NAMESPACE "FLlamaCppModule"

bool FLlamaCppModule::LoadSharedLibrary(const FString& LibName)
{
	void* Handle = FPlatformProcess::GetDllHandle(*LibName);
	if (!Handle)
	{
		UE_LOG(LogTemp, Error, TEXT("LlamaCpp: Failed to load shared library: %s"), *LibName);
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("LlamaCpp: Loaded %s"), *LibName);
	LoadedLibHandles.Add(Handle);
	return true;
}

void FLlamaCppModule::FreeLoadedLibraries()
{
	// Free in reverse order (dependents first)
	for (int32 i = LoadedLibHandles.Num() - 1; i >= 0; --i)
	{
		if (LoadedLibHandles[i])
		{
			FPlatformProcess::FreeDllHandle(LoadedLibHandles[i]);
		}
	}
	LoadedLibHandles.Empty();
}

void FLlamaCppModule::StartupModule()
{
#if PLATFORM_WINDOWS
	FString BinPath = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("LlamaCpp"))->GetBaseDir(), TEXT("Binaries/Win64"));

	// llama.cpp libs in dependency order (llama uses separate ggml DLLs)
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("ggml-base.dll")));
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("ggml-cpu.dll")));
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("ggml.dll")));
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("llama.dll")));

	// whisper.dll is self-contained (ggml statically linked inside)
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("whisper.dll")));

#elif PLATFORM_MAC
	FString BinPath = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("LlamaCpp"))->GetBaseDir(), TEXT("Binaries/Mac"));

	// llama dylibs with separate ggml dependencies
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("libggml-base.dylib")));
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("libggml-cpu.dylib")));
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("libggml.dylib")));
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("libllama.dylib")));

	// libwhisper.dylib is self-contained (ggml statically linked inside)
	LoadSharedLibrary(FPaths::Combine(BinPath, TEXT("libwhisper.dylib")));
#endif

	// Android loads .so via System.loadLibrary in APL — no manual loading needed

	llama_backend_init();
	UE_LOG(LogTemp, Log, TEXT("LlamaCpp: backend initialized"));
}

void FLlamaCppModule::ShutdownModule()
{
	llama_backend_free();
	UE_LOG(LogTemp, Log, TEXT("LlamaCpp: backend freed"));

	FreeLoadedLibraries();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLlamaCppModule, LlamaCpp)
