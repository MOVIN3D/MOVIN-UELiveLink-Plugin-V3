// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MOVINLiveLinkFunctionLibrary.generated.h"

class USkeletalMeshComponent;

/**
 * Blueprint entry points for the MOVIN LiveLink plugin.
 *
 * Creating sources at runtime, so motion capture data can be received without a LiveLink Preset,
 * and fitting a Skeletal Mesh to the actor a subject is streaming.
 */
UCLASS()
class MOVINLIVELINK_API UMOVINLiveLinkFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Create and register a MOVIN LiveLink source that listens for
	 * motion capture data on the specified UDP port.
	 *
	 * @param Port  The UDP port to listen on (default: 11236)
	 * @return True if the source was successfully created and registered
	 */
	UFUNCTION(BlueprintCallable, Category = "MOVIN LiveLink", meta = (DisplayName = "Add MOVIN LiveLink Source"))
	static bool AddMOVINLiveLinkSource(int32 Port = 11236);

	/**
	 * Fit a Skeletal Mesh Component to the actor currently streaming on a subject.
	 *
	 * The component is given a runtime copy of its mesh whose reference pose carries the streamed
	 * bone lengths, which is what lets an IK Retargeter reading that mesh measure limb extension
	 * and pelvis height against the actor rather than against the .fbx. The asset on disk is
	 * not modified, and the IK Rig and IK Retargeter assets need no changes.
	 *
	 * Only needed when movin.ActorMesh.AutoFit is off, or to fit a mesh the automatic sweep
	 * cannot recognise as being driven by the subject.
	 *
	 * @param TargetComponent  The Skeletal Mesh Component feeding the retargeter
	 * @param SubjectName      The MOVIN Actor subject whose calibration to fit to
	 * @return false if the subject has not streamed enough frames to be calibrated yet
	 */
	UFUNCTION(BlueprintCallable, Category = "MOVIN LiveLink", meta = (DisplayName = "Fit Mesh To MOVIN Actor"))
	static bool FitMeshToMOVINActor(USkeletalMeshComponent* TargetComponent, FName SubjectName);

	/**
	 * Has this subject streamed enough frames for its calibrated bone lengths to be readable?
	 *
	 * Takes a moment after a subject starts streaming: the actor's world movement has to be
	 * told apart from their bone lengths before either can be trusted.
	 */
	UFUNCTION(BlueprintPure, Category = "MOVIN LiveLink", meta = (DisplayName = "Is MOVIN Actor Calibrated"))
	static bool IsMOVINActorCalibrated(FName SubjectName);
};
