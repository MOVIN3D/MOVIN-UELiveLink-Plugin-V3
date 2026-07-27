// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINLiveLinkModule.h"
#include "MOVINSkeletonDiagnostics.h"

DEFINE_LOG_CATEGORY(LogMOVINLiveLink);

#define LOCTEXT_NAMESPACE "FMOVINLiveLinkModule"

void FMOVINLiveLinkModule::StartupModule()
{
}

void FMOVINLiveLinkModule::ShutdownModule()
{
	FMOVINSkeletonDiagnostics::Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMOVINLiveLinkModule, MOVINLiveLink)
