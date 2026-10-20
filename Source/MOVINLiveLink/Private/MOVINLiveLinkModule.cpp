// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINLiveLinkModule.h"
#include "MOVINActorMesh.h"
#include "MOVINSkeletonDiagnostics.h"

#if WITH_EDITOR
#include "MOVINLiveLinkSourceSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#endif

DEFINE_LOG_CATEGORY(LogMOVINLiveLink);

#define LOCTEXT_NAMESPACE "FMOVINLiveLinkModule"

#if WITH_EDITOR
class source_settings_details : public IDetailCustomization {
public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& detail) override {
		auto rotation = detail.GetProperty(GET_MEMBER_NAME_CHECKED(UMOVINLiveLinkSourceSettings, global_rotation_offset));
		auto axis = [&](FName name, const FText& label) {
			auto field = rotation->GetChildHandle(name).ToSharedRef();
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[
					SNew(STextBlock).Text(label).ToolTipText(field->GetToolTipText())
				]
				+ SHorizontalBox::Slot().FillWidth(1)[field->CreatePropertyValueWidget()];
		};
		detail.EditCategory("MOVIN").AddProperty(rotation).CustomWidget()
			.NameContent()[rotation->CreatePropertyNameWidget()]
			.ValueContent().MinDesiredWidth(450).MaxDesiredWidth(600)[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)[axis(TEXT("Roll"), LOCTEXT("OffsetX", "X / Roll"))]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 8, 0)[axis(TEXT("Pitch"), LOCTEXT("OffsetY", "Y / Pitch"))]
				+ SHorizontalBox::Slot().FillWidth(1)[axis(TEXT("Yaw"), LOCTEXT("OffsetZ", "Z / Yaw"))]
			];
	}
};
#endif

void FMOVINLiveLinkModule::StartupModule()
{
#if WITH_EDITOR
	auto& editor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	editor.RegisterCustomClassLayout(UMOVINLiveLinkSourceSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateLambda([]() { return MakeShared<source_settings_details>(); }));
#endif
}

void FMOVINLiveLinkModule::ShutdownModule()
{
#if WITH_EDITOR
	if (auto* editor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor")) {
		editor->UnregisterCustomClassLayout(UMOVINLiveLinkSourceSettings::StaticClass()->GetFName());
	}
#endif
	FMOVINActorMesh::Reset();
	FMOVINSkeletonDiagnostics::Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMOVINLiveLinkModule, MOVINLiveLink)
