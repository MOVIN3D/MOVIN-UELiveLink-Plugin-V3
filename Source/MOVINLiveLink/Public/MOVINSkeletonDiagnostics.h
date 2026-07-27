// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MOVINDatagram.h"

/**
 * One bone whose streamed length differs from the target Skeletal Mesh's reference pose.
 * Ratio is StreamedLength / RefLength, so 1.63 means the streamed bone is 63% longer.
 */
struct FMOVINBoneLengthDeviation
{
	FName BoneName;
	float RefLength = 0.0f;
	float StreamedLength = 0.0f;
	float Ratio = 1.0f;

	/** How far this bone is from matching, used for ranking. */
	float Magnitude() const { return FMath::Abs(Ratio - 1.0f); }
};

/** Result of comparing a streamed MOVIN skeleton against a Skeletal Mesh reference pose. */
struct FMOVINSkeletonDeviationReport
{
	/** Bones that were present in both skeletons and eligible for comparison. */
	int32 ComparedBoneCount = 0;

	/** Deviating bones, largest deviation first. */
	TArray<FMOVINBoneLengthDeviation> Deviations;

	bool HasDeviation() const { return Deviations.Num() > 0; }
};

/** A single reference pose bone, as read from a Skeletal Mesh's reference skeleton. */
struct FMOVINRefPoseBone
{
	FName Name;
	FVector LocalTranslation = FVector::ZeroVector;
	int32 ParentIndex = INDEX_NONE;
};

/**
 * Detects and reports the "Skeleton Calibration Offset" - the expected difference between
 * the bone lengths MOVIN Studio streams and the bone lengths baked into the Unreal Skeletal Mesh.
 *
 * This only applies to Actor streaming. MOVIN Studio calibrates the skeleton to each performer's
 * body, so an Actor stream carries bone lengths that belong to the performer rather than to the
 * mesh. Those lengths are applied verbatim, which is what keeps the motion data intact - but it
 * means joints whose calibrated length differs from the mesh visibly change shape. That is correct
 * behaviour, and it reads as a plugin defect to anyone seeing it for the first time. This class
 * exists to say so, with the actual numbers, at the moment the user is looking at it.
 *
 * Character streaming needs none of this: MOVIN Studio has already retargeted onto the same .fbx,
 * so the lengths match and there is nothing to explain.
 */
class MOVINLIVELINK_API FMOVINSkeletonDiagnostics
{
public:

	/** Bones must differ by more than this fraction before they are worth mentioning. */
	static constexpr float DefaultToleranceRatio = 0.02f;

	/** The LiveLink subject name MOVIN Studio uses when streaming an Actor. */
	static const FName ActorSubjectName;

	/** Actor streams get diagnostics; Character streams are named after the character and do not. */
	static bool IsActorSubject(const FName& SubjectName);

	/**
	 * Compare streamed bone lengths against a reference pose. Pure data in, data out - no engine
	 * world required, so this is the part that carries the unit tests.
	 *
	 * Bones missing from either side are ignored, as are bones whose reference length is zero.
	 *
	 * Bones carrying world movement have to be excluded, or their translation - which is a position,
	 * not a length - registers as an enormous deviation that changes on every frame. The caller
	 * supplies them in ExcludedBoneNames; see FindWorldMotionBones().
	 *
	 * @param RefBones            Reference pose bones from the Skeletal Mesh, in mesh bone order
	 * @param StreamedBoneNames   Bone names as streamed by MOVIN Studio
	 * @param StreamedTranslations Local translations matching StreamedBoneNames by index
	 * @param ExcludedBoneNames   Bones whose translation is world movement rather than a length
	 * @param ToleranceRatio      Deviation below this fraction is not reported
	 */
	static FMOVINSkeletonDeviationReport CompareBoneLengths(
		TArrayView<const FMOVINRefPoseBone> RefBones,
		TArrayView<const FName> StreamedBoneNames,
		TArrayView<const FVector> StreamedTranslations,
		const TSet<FName>& ExcludedBoneNames,
		float ToleranceRatio = DefaultToleranceRatio);

	/**
	 * Which streamed bones carry world movement rather than a bone length.
	 *
	 * Decided by observation rather than by hierarchy shape. A bone length is constant for the whole
	 * session - the calibrated figures are stable to five decimal places - while the pelvis
	 * translation is a world position that moves on essentially every frame. Counting how often each
	 * bone's length changes separates the two without knowing anything about the rig, which matters
	 * because the answer differs between MOVIN Studio's streamed skeleton and however the target
	 * .fbx happened to import. Inferring it from the hierarchy instead is what previously let the
	 * pelvis into the report and made the notification re-fire every couple of seconds.
	 *
	 * A recalibration changes a length once and then holds it, so it does not look like movement and
	 * correctly re-raises the notification instead of silencing the bone.
	 *
	 * @param LengthChangeCounts  Per bone, how many frames its length differed from the previous one
	 * @param FramesObserved      Frames counted so far; too few and nothing can be concluded yet
	 */
	static TSet<FName> FindWorldMotionBones(
		TArrayView<const FName> BoneNames,
		TArrayView<const int32> LengthChangeCounts,
		int32 FramesObserved);

	/** Frames needed before world movement can be told apart from a static bone length. */
	static constexpr int32 MinFramesForMotionCheck = 30;

	/** A bone whose length changes on more than this fraction of frames is carrying world movement. */
	static constexpr float WorldMotionChangeFraction = 0.2f;

	/** Build the user-facing detail line for a report. Assumes Report.HasDeviation(). */
	static FString FormatReport(const FName& SubjectName, const FString& MeshName, const FMOVINSkeletonDeviationReport& Report);

	/**
	 * Identity of what a report would put on screen, used to decide whether the notification needs
	 * raising again.
	 *
	 * Deliberately built from the displayed figures rather than the raw stream: the pelvis
	 * translation is world movement and changes every frame, so anything derived from raw
	 * translations would read as a fresh recalibration on every packet. Ratios are rounded to the
	 * precision the message shows, so jitter below what the user can see does not re-notify either.
	 */
	static FString BuildReportSignature(const FString& MeshName, const FMOVINSkeletonDeviationReport& Report);

	/**
	 * Record the skeleton carried by a frame. Called for every frame; keeps the latest snapshot for
	 * the game thread to evaluate. Safe to call from the receiver thread.
	 */
	static void NoteSkeleton(const FName& SubjectName, const TArray<FMOVINJointData>& Bones);

	/**
	 * Look for a Skeletal Mesh driven by a tracked subject and raise the notification if what it
	 * would say has changed. Must be called on the game thread. Cheap to call every frame;
	 * internally rate limited.
	 */
	static void Tick();

	/** Dismiss any open notification and forget every tracked subject. */
	static void Reset();
};
