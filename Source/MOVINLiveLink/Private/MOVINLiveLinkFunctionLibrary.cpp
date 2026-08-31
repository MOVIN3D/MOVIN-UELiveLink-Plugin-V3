// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINLiveLinkFunctionLibrary.h"
#include "MOVINLiveLinkModule.h"
#include "MOVINLiveLinkSource.h"
#include "MOVINActorMesh.h"
#include "MOVINSkeletonDiagnostics.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"

bool UMOVINLiveLinkFunctionLibrary::AddMOVINLiveLinkSource(int32 Port)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		UE_LOG(LogMOVINLiveLink, Error, TEXT("AddMOVINLiveLinkSource: LiveLink client is not available"));
		return false;
	}

	if (FMOVINLiveLinkSource::IsPortInUse(Port))
	{
		UE_LOG(LogMOVINLiveLink, Warning, TEXT("AddMOVINLiveLinkSource: A MOVIN LiveLink source on port %d already exists"), Port);
		return false;
	}

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	const TSharedPtr<FMOVINLiveLinkSource> NewSource = MakeShared<FMOVINLiveLinkSource>(Port);
	const bool bSuccess = LiveLinkClient.AddSource(NewSource).IsValid();

	if (bSuccess)
	{
		UE_LOG(LogMOVINLiveLink, Log, TEXT("AddMOVINLiveLinkSource: Successfully created MOVIN LiveLink source on port %d"), Port);
	}
	else
	{
		UE_LOG(LogMOVINLiveLink, Error, TEXT("AddMOVINLiveLinkSource: Failed to create MOVIN LiveLink source on port %d"), Port);
	}

	return bSuccess;
}

bool UMOVINLiveLinkFunctionLibrary::FitMeshToMOVINActor(USkeletalMeshComponent* TargetComponent, FName SubjectName)
{
	FString Error;
	if (FMOVINActorMesh::ApplyToComponent(TargetComponent, SubjectName, Error))
	{
		return true;
	}

	UE_LOG(LogMOVINLiveLink, Warning, TEXT("FitMeshToMOVINActor: %s"), *Error);
	return false;
}

bool UMOVINLiveLinkFunctionLibrary::IsMOVINActorCalibrated(FName SubjectName)
{
	FMOVINStreamedSkeleton Skeleton;
	return FMOVINSkeletonDiagnostics::GetStreamedSkeleton(SubjectName, Skeleton);
}
