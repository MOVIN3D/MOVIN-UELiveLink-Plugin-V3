// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkSourceSettings.h"
#include "MOVINLiveLinkSourceSettings.generated.h"

UCLASS()
class MOVINLIVELINK_API UMOVINLiveLinkSourceSettings : public ULiveLinkSourceSettings {
	GENERATED_BODY()

public:
	/** Rotates the entire motion, including its travel path, about the stream origin in Unreal coordinates. */
	UPROPERTY(EditAnywhere, Category = "MOVIN", meta = (DisplayName = "Global Rotation Offset", Units = "deg", ToolTip = "Unreal axes, in degrees: X (Roll), Y (Pitch), Z (Yaw). Rotates facing and the travel path about the stream origin, before the Skeletal Mesh component transform."))
	FRotator global_rotation_offset = FRotator::ZeroRotator;
};
