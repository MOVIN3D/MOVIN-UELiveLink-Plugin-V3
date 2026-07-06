// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "LiveLinkSourceFactory.h"

/**
 * Slate widget shown in the LiveLink panel when the user selects "MOVIN Live Source".
 * Provides a port number input and an Add button to create the source.
 */
class SMOVINLiveLinkSourceEditor : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMOVINLiveLinkSourceEditor) {}
		SLATE_EVENT(ULiveLinkSourceFactory::FOnLiveLinkSourceCreated, OnSourceCreated)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

private:

	/** Callback when the user clicks Add */
	FReply OnAddSourceClicked();

	/** The port number entered by the user */
	int32 PortNumber;

	/** Delegate to call when a source is created */
	ULiveLinkSourceFactory::FOnLiveLinkSourceCreated OnSourceCreated;
};
