// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkSourceFactory.h"
#include "MOVINLiveLinkSourceFactory.generated.h"

/**
 * Factory that registers the "MOVIN Live Source" entry
 * in the LiveLink sources panel and handles source creation.
 */
UCLASS()
class MOVINLIVELINK_API UMOVINLiveLinkSourceFactory : public ULiveLinkSourceFactory
{
	GENERATED_BODY()

public:

	virtual FText GetSourceDisplayName() const override;
	virtual FText GetSourceTooltip() const override;
	virtual EMenuType GetMenuType() const override { return EMenuType::SubPanel; }
	virtual TSharedPtr<SWidget> BuildCreationPanel(FOnLiveLinkSourceCreated OnLiveLinkSourceCreated) const override;
	virtual TSharedPtr<ILiveLinkSource> CreateSource(const FString& ConnectionString) const override;
};
