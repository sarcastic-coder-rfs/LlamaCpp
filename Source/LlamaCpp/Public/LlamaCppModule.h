#pragma once

#include "Modules/ModuleManager.h"

class FLlamaCppModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TArray<void*> LoadedLibHandles;

	bool LoadSharedLibrary(const FString& LibName);
	void FreeLoadedLibraries();
};
