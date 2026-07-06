// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMOVINLiveLink, Log, All);

class FMOVINLiveLinkModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FMOVINLiveLinkModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMOVINLiveLinkModule>("MOVINLiveLink");
	}
};
