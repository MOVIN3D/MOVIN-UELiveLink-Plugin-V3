// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMOVINStreamedSkeleton;

/**
 * One bone of the template mesh's reference pose - the rig the actor's proportions get fitted
 * onto. Local transform, matching how a Skeletal Mesh stores its reference skeleton.
 */
struct FMOVINTemplateBone
{
	FName Name;
	int32 ParentIndex = INDEX_NONE;
	FTransform RefPose = FTransform::Identity;
};

/**
 * Builds a reference pose carrying the actor's calibrated bone lengths.
 *
 * Why this exists: the IK Retargeter measures every ratio it works from - how far a limb is
 * extended, how high the pelvis sits - against the reference pose of whichever Skeletal Mesh is on
 * the source component at runtime. FRetargetSkeleton::Initialize reads
 * SkeletalMesh->GetRefSkeleton() directly, not the preview mesh stored in the retargeter asset.
 *
 * An Actor stream carries the actor's bone lengths, so those ratios come out wrong against the
 * .fbx reference pose. An actor shorter than the mesh streams a straight leg whose start-to-end
 * distance is less than the mesh's, the retargeter reads that as a partly extended limb, and the
 * retargeted character stands with bent knees. Hand the retargeter a reference pose built from the
 * actor instead and every ratio is 1 by construction - no scale factor to calibrate, and the
 * actor's real proportions survive into the source skeleton.
 *
 * Pure data in, data out - no engine world required, so this is the part that carries the unit
 * tests. FMOVINActorMesh turns the result into a USkeletalMesh.
 */
class MOVINLIVELINK_API FMOVINActorSkeleton
{
public:

	/**
	 * Build a reference pose for the actor currently streaming.
	 *
	 * Bone lengths come from the stream. Rotations are kept from the template: what MOVIN Studio
	 * streams per bone is animation, not a bind pose, and replacing the bind rotations would
	 * invalidate the rotation deltas the retargeter stores in its retarget pose.
	 *
	 * @param InTemplate   Reference pose of the mesh being fitted, in bone order (parents first)
	 * @param InSkeleton   The calibrated skeleton streamed by the subject
	 * @param OutRefPose   Local reference pose transforms, one per template bone
	 * @param OutError     Why the build failed, when it does
	 */
	static bool BuildReferencePose(
		TArrayView<const FMOVINTemplateBone> InTemplate,
		const FMOVINStreamedSkeleton& InSkeleton,
		TArray<FTransform>& OutRefPose,
		FString& OutError);

	/**
	 * Which bone carries the actor's world position rather than a length.
	 *
	 * The stream can report more than one bone as world movement; the one nearest the root is the
	 * pelvis, and a reference skeleton always lists parents before children.
	 */
	static int32 FindPelvisBone(
		TArrayView<const FMOVINTemplateBone> InTemplate,
		const FMOVINStreamedSkeleton& InSkeleton);

	/**
	 * The bone that reaches the ground: whichever descendant of InFromBone sits lowest in the
	 * template's reference pose.
	 *
	 * Found by geometry rather than by name because the streamed rig and the .fbx do not have to
	 * agree on a naming convention, and the chain this measures - pelvis to floor - is what decides
	 * how high the actor's pelvis sits when they stand.
	 */
	static int32 FindGroundContactBone(TArrayView<const FMOVINTemplateBone> InTemplate, int32 InFromBone);

	/**
	 * Measure the same chain twice, once on the template and once on the stream.
	 *
	 * Both sums are taken over exactly the same bones: a bone the mesh has and the stream does not
	 * is skipped on both sides, so the ratio between the two lengths stays meaningful instead of
	 * quietly comparing a five bone chain against a four bone one.
	 *
	 * @param InLeafBone   Bone to measure up from
	 * @param InStopBone   Ancestor to stop at, not included in either sum
	 * @return false if InStopBone is not an ancestor of InLeafBone
	 */
	static bool MeasureChain(
		TArrayView<const FMOVINTemplateBone> InTemplate,
		const FMOVINStreamedSkeleton& InSkeleton,
		int32 InLeafBone,
		int32 InStopBone,
		double& OutTemplateLength,
		double& OutStreamedLength);
};
