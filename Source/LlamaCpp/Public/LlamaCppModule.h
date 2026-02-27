#pragma once

#include "Modules/ModuleManager.h"

class FLlamaCppModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
