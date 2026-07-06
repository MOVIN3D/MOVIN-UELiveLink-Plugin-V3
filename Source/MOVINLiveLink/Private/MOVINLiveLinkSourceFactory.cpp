// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINLiveLinkSourceFactory.h"
#include "MOVINLiveLinkSource.h"
#include "MOVINLiveLinkSourceEditor.h"

#define LOCTEXT_NAMESPACE "MOVINLiveLinkSourceFactory"

FText UMOVINLiveLinkSourceFactory::GetSourceDisplayName() const
{
	return LOCTEXT("SourceDisplayName", "MOVIN Live Source");
}

FText UMOVINLiveLinkSourceFactory::GetSourceTooltip() const
{
	return LOCTEXT("SourceTooltip", "Receives motion capture data from MOVIN Studio via UDP");
}

TSharedPtr<SWidget> UMOVINLiveLinkSourceFactory::BuildCreationPanel(FOnLiveLinkSourceCreated InOnLiveLinkSourceCreated) const
{
	return SNew(SMOVINLiveLinkSourceEditor)
		.OnSourceCreated(InOnLiveLinkSourceCreated);
}

TSharedPtr<ILiveLinkSource> UMOVINLiveLinkSourceFactory::CreateSource(const FString& ConnectionString) const
{
	// ConnectionString format: "Port=NNNNN"
	int32 PortValue = 11236; // default
	TArray<FString> Parsed;
	ConnectionString.ParseIntoArray(Parsed, TEXT("="));
	if (Parsed.Num() == 2)
	{
		PortValue = FCString::Atoi(*Parsed[1]);
	}

	if (FMOVINLiveLinkSource::IsPortInUse(PortValue))
	{
		return nullptr;
	}

	return MakeShared<FMOVINLiveLinkSource>(PortValue);
}

#undef LOCTEXT_NAMESPACE
