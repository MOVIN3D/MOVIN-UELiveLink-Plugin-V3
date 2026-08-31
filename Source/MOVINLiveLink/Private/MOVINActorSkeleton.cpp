// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINActorSkeleton.h"
#include "MOVINSkeletonDiagnostics.h"

namespace MOVINActorSkeletonPrivate
{
	/** A chain shorter than this is a measurement failure, not a very small actor. */
	static constexpr double MinChainLengthCm = 1.0;

	/** Streamed translations by bone name, for the lookups the build does per bone. */
	static TMap<FName, FVector> MakeTranslationMap(const FMOVINStreamedSkeleton& InSkeleton)
	{
		TMap<FName, FVector> Translations;
		Translations.Reserve(InSkeleton.BoneNames.Num());
		for (int32 Index = 0; Index < InSkeleton.BoneNames.Num(); ++Index)
		{
			if (InSkeleton.LocalTranslations.IsValidIndex(Index))
			{
				Translations.Add(InSkeleton.BoneNames[Index], InSkeleton.LocalTranslations[Index]);
			}
		}
		return Translations;
	}

	/**
	 * Component space reference pose. A reference skeleton always lists parents before children, so
	 * one forward pass is enough.
	 */
	static void BuildGlobalRefPose(TArrayView<const FMOVINTemplateBone> InTemplate, TArray<FTransform>& OutGlobal)
	{
		OutGlobal.Reset(InTemplate.Num());
		for (const FMOVINTemplateBone& Bone : InTemplate)
		{
			OutGlobal.Add(OutGlobal.IsValidIndex(Bone.ParentIndex)
				? Bone.RefPose * OutGlobal[Bone.ParentIndex]
				: Bone.RefPose);
		}
	}

	static bool IsDescendantOf(TArrayView<const FMOVINTemplateBone> InTemplate, int32 InBone, int32 InAncestor)
	{
		for (int32 Index = InTemplate[InBone].ParentIndex; Index != INDEX_NONE; Index = InTemplate[Index].ParentIndex)
		{
			if (Index == InAncestor)
			{
				return true;
			}
		}
		return false;
	}
}

int32 FMOVINActorSkeleton::FindPelvisBone(
	TArrayView<const FMOVINTemplateBone> InTemplate,
	const FMOVINStreamedSkeleton& InSkeleton)
{
	// Parents come before children, so the first match is the one nearest the root.
	for (int32 Index = 0; Index < InTemplate.Num(); ++Index)
	{
		if (InSkeleton.WorldMotionBones.Contains(InTemplate[Index].Name))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 FMOVINActorSkeleton::FindGroundContactBone(TArrayView<const FMOVINTemplateBone> InTemplate, int32 InFromBone)
{
	using namespace MOVINActorSkeletonPrivate;

	if (!InTemplate.IsValidIndex(InFromBone))
	{
		return INDEX_NONE;
	}

	TArray<FTransform> Global;
	BuildGlobalRefPose(InTemplate, Global);

	int32 LowestBone = INDEX_NONE;
	double LowestHeight = TNumericLimits<double>::Max();

	for (int32 Index = 0; Index < InTemplate.Num(); ++Index)
	{
		if (Index == InFromBone || !IsDescendantOf(InTemplate, Index, InFromBone))
		{
			continue;
		}

		const double Height = Global[Index].GetTranslation().Z;
		if (Height < LowestHeight)
		{
			LowestHeight = Height;
			LowestBone = Index;
		}
	}

	return LowestBone;
}

bool FMOVINActorSkeleton::MeasureChain(
	TArrayView<const FMOVINTemplateBone> InTemplate,
	const FMOVINStreamedSkeleton& InSkeleton,
	int32 InLeafBone,
	int32 InStopBone,
	double& OutTemplateLength,
	double& OutStreamedLength)
{
	using namespace MOVINActorSkeletonPrivate;

	OutTemplateLength = 0.0;
	OutStreamedLength = 0.0;

	if (!InTemplate.IsValidIndex(InLeafBone) || !InTemplate.IsValidIndex(InStopBone))
	{
		return false;
	}

	const TMap<FName, FVector> Translations = MakeTranslationMap(InSkeleton);

	for (int32 Index = InLeafBone; Index != INDEX_NONE; Index = InTemplate[Index].ParentIndex)
	{
		if (Index == InStopBone)
		{
			return true;
		}

		// Skipped on both sides rather than substituted, so the two sums always cover the same
		// bones and the ratio between them stays meaningful.
		const FVector* Streamed = Translations.Find(InTemplate[Index].Name);
		if (Streamed == nullptr)
		{
			continue;
		}

		OutTemplateLength += InTemplate[Index].RefPose.GetTranslation().Size();
		OutStreamedLength += Streamed->Size();
	}

	OutTemplateLength = 0.0;
	OutStreamedLength = 0.0;
	return false;
}

bool FMOVINActorSkeleton::BuildReferencePose(
	TArrayView<const FMOVINTemplateBone> InTemplate,
	const FMOVINStreamedSkeleton& InSkeleton,
	TArray<FTransform>& OutRefPose,
	FString& OutError)
{
	using namespace MOVINActorSkeletonPrivate;

	OutRefPose.Reset();
	OutError.Reset();

	if (InTemplate.Num() == 0)
	{
		OutError = TEXT("Template reference pose is empty.");
		return false;
	}

	if (!InSkeleton.IsUsable())
	{
		OutError = TEXT("Streamed skeleton is not calibrated yet - waiting for enough frames to identify world movement.");
		return false;
	}

	const int32 PelvisBone = FindPelvisBone(InTemplate, InSkeleton);
	if (PelvisBone == INDEX_NONE)
	{
		OutError = TEXT("No streamed world motion bone is present in the template skeleton.");
		return false;
	}

	const int32 GroundBone = FindGroundContactBone(InTemplate, PelvisBone);
	if (GroundBone == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Bone '%s' has no descendants to measure a pelvis-to-floor chain from."),
			*InTemplate[PelvisBone].Name.ToString());
		return false;
	}

	double TemplateChain = 0.0;
	double StreamedChain = 0.0;
	if (!MeasureChain(InTemplate, InSkeleton, GroundBone, PelvisBone, TemplateChain, StreamedChain)
		|| TemplateChain < MinChainLengthCm
		|| StreamedChain < MinChainLengthCm)
	{
		OutError = FString::Printf(TEXT("Could not measure the chain from '%s' to '%s' (template %.2fcm, streamed %.2fcm)."),
			*InTemplate[GroundBone].Name.ToString(),
			*InTemplate[PelvisBone].Name.ToString(),
			TemplateChain,
			StreamedChain);
		return false;
	}

	// How much smaller or larger this actor is than the mesh, measured along the chain that
	// decides how high the pelvis sits when they stand.
	const double ActorScale = StreamedChain / TemplateChain;

	const TMap<FName, FVector> Translations = MakeTranslationMap(InSkeleton);

	OutRefPose.Reserve(InTemplate.Num());
	for (const FMOVINTemplateBone& Bone : InTemplate)
	{
		FTransform RefPose = Bone.RefPose;

		if (InSkeleton.WorldMotionBones.Contains(Bone.Name))
		{
			// A world position, not a bone length - taking it from the stream would bake wherever
			// the actor happened to be standing into the reference pose. Scaling the template's
			// own offset instead puts the pelvis at the height this actor's legs would hold it,
			// which is exactly what the retargeter compares the live pelvis against.
			RefPose.SetTranslation(Bone.RefPose.GetTranslation() * ActorScale);
		}
		else if (const FVector* Streamed = Translations.Find(Bone.Name))
		{
			RefPose.SetTranslation(*Streamed);
		}
		else
		{
			// A twist or corrective bone the mesh has and MOVIN Studio does not stream. Scaling it
			// with the rest of the body keeps it in proportion with the bones around it.
			RefPose.SetTranslation(Bone.RefPose.GetTranslation() * ActorScale);
		}

		// Rotations stay as the mesh authored them. What the stream carries per bone is animation,
		// not a bind pose, and replacing the bind rotations would invalidate the rotation deltas
		// stored in the IK Retargeter's retarget pose.
		OutRefPose.Add(RefPose);
	}

	return true;
}
