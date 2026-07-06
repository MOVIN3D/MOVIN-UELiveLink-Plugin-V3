// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINLiveLinkSourceEditor.h"
#include "MOVINLiveLinkSource.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MOVINLiveLinkSourceEditor"

void SMOVINLiveLinkSourceEditor::Construct(const FArguments& Args)
{
	OnSourceCreated = Args._OnSourceCreated;
	PortNumber = 11236; // Default MOVIN port

	ChildSlot
	[
		SNew(SVerticalBox)

		// Port number row
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PortLabel", "UDP Port:"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(1)
				.MaxValue(65535)
				.Value_Lambda([this]() { return PortNumber; })
				.OnValueChanged_Lambda([this](int32 NewValue) { PortNumber = NewValue; })
			]
		]

		// Add button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		.HAlign(HAlign_Right)
		[
			SNew(SButton)
			.Text(LOCTEXT("AddSourceButton", "Add"))
			.OnClicked(this, &SMOVINLiveLinkSourceEditor::OnAddSourceClicked)
		]
	];
}

FReply SMOVINLiveLinkSourceEditor::OnAddSourceClicked()
{
	if (FMOVINLiveLinkSource::IsPortInUse(PortNumber))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(
				LOCTEXT("DuplicatePortMessage", "A MOVIN LiveLink Source using UDP port {0} already exists."),
				FText::AsNumber(PortNumber)
			)
		);
		return FReply::Handled();
	}

	TSharedPtr<FMOVINLiveLinkSource> NewSource = MakeShared<FMOVINLiveLinkSource>(PortNumber);

	FString ConnectionString = FString::Printf(TEXT("Port=%d"), PortNumber);
	OnSourceCreated.ExecuteIfBound(NewSource, MoveTemp(ConnectionString));

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
