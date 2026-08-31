// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class USkeletalMeshComponent;
struct FMOVINStreamedSkeleton;

/**
 * Fits the Skeletal Mesh that feeds an IK Retargeter to the actor currently streaming.
 *
 * The retargeter builds every reference it works from - limb lengths, pelvis height - out of
 * SkeletalMesh->GetRefSkeleton() on whichever mesh sits on the source component at runtime, and it
 * reinitialises by itself when that mesh changes. So the whole problem of an Actor stream carrying
 * the actor's bone lengths rather than the mesh's is solved by handing it a mesh whose
 * reference pose is the actor. Nothing about the IK Rig or the IK Retargeter asset has to
 * change: their chains resolve by bone name, and the hierarchy is untouched.
 *
 * The fitted mesh is a runtime duplicate of the original, so the source asset on disk is never
 * modified. Skinning on the duplicate is wrong by construction - the skin weights were authored
 * against the original bind pose - which does not matter for a mesh that exists only to be sampled
 * by the retargeter, but does mean it should not be rendered.
 *
 * See FMOVINActorSkeleton for the reference pose maths, which is where the unit tests live.
 */
class MOVINLIVELINK_API FMOVINActorMesh
{
public:

	/**
	 * Duplicate InTemplate and replace its reference pose with the actor's proportions.
	 * Game thread only. Returns nullptr and fills OutError on failure.
	 */
	static USkeletalMesh* BuildMesh(USkeletalMesh* InTemplate, const FMOVINStreamedSkeleton& InSkeleton, FString& OutError);

	/**
	 * Fit the component's Skeletal Mesh to the actor streaming on InSubjectName.
	 *
	 * Fitted meshes are cached against the template they were built from, so components sharing a
	 * template share one mesh and a component that has already been fitted is recognised rather than
	 * fitted on top of itself. Game thread only, and cheap to call repeatedly: a rebuild happens
	 * only when the actor's bone lengths actually change.
	 */
	static bool ApplyToComponent(USkeletalMeshComponent* InComponent, const FName& InSubjectName, FString& OutError);

	/**
	 * Sweep every mesh driven by a streaming subject and fit it. Game thread only, rate limited
	 * internally. Does nothing while auto fitting is disabled.
	 */
	static void Tick();

	/** Whether Tick() fits meshes on its own. Controlled by movin.ActorMesh.AutoFit. */
	static bool IsAutoFitEnabled();

	/** Drop every fitted mesh and put the components that got one back on their template. */
	static void Reset();
};
