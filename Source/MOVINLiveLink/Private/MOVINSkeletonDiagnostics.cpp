// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINSkeletonDiagnostics.h"
#include "MOVINLiveLinkModule.h"

#include "AnimNode_LiveLinkPose.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "LiveLinkComponentController.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"
#include "ReferenceSkeleton.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "MOVINSkeletonDiagnostics"

const FName FMOVINSkeletonDiagnostics::ActorSubjectName(TEXT("MOVINMan"));

namespace MOVINSkeletonDiagnosticsPrivate
{
	/** How often the game thread sweeps for a mesh driven by a tracked subject. */
	static constexpr double ScanIntervalSeconds = 2.0;

	/** A subject with no frame this recently has stopped streaming and is not worth scanning for. */
	static constexpr double StaleFrameSeconds = 5.0;

	/** Bones listed in the message before it is truncated. */
	static constexpr int32 MaxReportedBones = 6;

	/** The latest skeleton streamed by a subject, and what the user was last told about it. */
	struct FTrackedSubject
	{
		TArray<FName> BoneNames;
		TArray<FVector> Translations;
		double LastFrameTimeSeconds = 0.0;

		/** Per bone, how many frames its length differed from the frame before. */
		TArray<int32> LengthChangeCounts;

		/** Per bone, the length seen on the previous frame. */
		TArray<double> PreviousLengths;

		int32 FramesObserved = 0;

		/**
		 * Signature of the report currently on screen, per Skeletal Mesh. Keyed by mesh rather than
		 * held as a single value because a subject can legitimately drive more than one mesh, and a
		 * single slot would flip between them and re-notify on every scan.
		 */
		TMap<FString, FString> NotifiedSignatures;
	};

	static FCriticalSection StateCriticalSection;
	static TMap<FName, FTrackedSubject> TrackedSubjects;
	static double LastScanTimeSeconds = 0.0;

	/** Identifies one subject driving one mesh - the unit a notification belongs to. */
	static FString MakeNotificationKey(const FName& SubjectName, const FString& MeshName)
	{
		return SubjectName.ToString() + TEXT("|") + MeshName;
	}

#if WITH_EDITOR
	/** Open notifications, keyed by subject and mesh. Game thread only. */
	static TMap<FString, TSharedPtr<SNotificationItem>> ActiveNotifications;
#endif

	/** How much a bone length has to move between frames to count as having changed. */
	static constexpr double LengthChangeToleranceCm = 0.05;

	/**
	 * Does this Animation Blueprint contain a Live Link Pose node bound to the given subject?
	 * Container is the object the node properties live on - an anim instance, or its class default
	 * object when reading what the graph authored.
	 */
	static bool AnimClassBindsSubject(const UClass* AnimClass, const UObject* Container, const FName& SubjectName)
	{
		if (AnimClass == nullptr || Container == nullptr)
		{
			return false;
		}

		const IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(AnimClass);
		if (AnimClassInterface == nullptr)
		{
			return false;
		}

		for (const FStructProperty* Property : AnimClassInterface->GetAnimNodeProperties())
		{
			if (Property == nullptr || Property->Struct == nullptr)
			{
				continue;
			}
			if (!Property->Struct->IsChildOf(FAnimNode_LiveLinkPose::StaticStruct()))
			{
				continue;
			}

			const FAnimNode_LiveLinkPose* PoseNode = Property->ContainerPtrToValuePtr<FAnimNode_LiveLinkPose>(Container);
			if (PoseNode != nullptr && PoseNode->LiveLinkSubjectName.Name == SubjectName)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * Is this component actually being driven by the subject?
	 *
	 * Bone name matching is not enough on its own: Mixamo-derived skeletons all share MOVINman's
	 * bone names, so any such character in the level would match a subject that never touches it.
	 * The binding has to come from the Animation Blueprint or the Live Link controller.
	 */
	static bool IsDrivenBySubject(const USkeletalMeshComponent* Component, const FName& SubjectName)
	{
		if (const UAnimInstance* AnimInstance = Component->GetAnimInstance())
		{
			if (AnimClassBindsSubject(AnimInstance->GetClass(), AnimInstance, SubjectName))
			{
				return true;
			}
		}

		if (const UAnimInstance* PostProcessInstance = Component->GetPostProcessInstance())
		{
			if (AnimClassBindsSubject(PostProcessInstance->GetClass(), PostProcessInstance, SubjectName))
			{
				return true;
			}
		}

		// No live instance yet - outside PIE, or before the actor starts ticking its animation.
		// Fall back to the subject the graph was authored with.
		if (const UClass* AnimClass = const_cast<USkeletalMeshComponent*>(Component)->GetAnimClass())
		{
			if (AnimClassBindsSubject(AnimClass, AnimClass->GetDefaultObject(), SubjectName))
			{
				return true;
			}
		}

		// The Live Link Component Controller path, where the subject lives on the controller
		// rather than in a graph.
		if (const AActor* Owner = Component->GetOwner())
		{
			for (const UActorComponent* ActorComponent : Owner->GetComponents())
			{
				const ULiveLinkComponentController* Controller = Cast<ULiveLinkComponentController>(ActorComponent);
				if (Controller != nullptr && Controller->SubjectRepresentation.Subject.Name == SubjectName)
				{
					return true;
				}
			}
		}

		return false;
	}

	static void DismissNotification(FString NotificationKey)
	{
#if WITH_EDITOR
		TSharedPtr<SNotificationItem> Item;
		if (ActiveNotifications.RemoveAndCopyValue(NotificationKey, Item) && Item.IsValid())
		{
			Item->SetExpireDuration(0.0f);
			Item->ExpireAndFadeout();
		}
#endif
	}

	/**
	 * Raise the notification and leave it up. It stays until the user dismisses it or the skeleton
	 * changes: the offset is a standing property of the stream, not an event, so a toast that fades
	 * after a few seconds would be missed by anyone who looked at the viewport first.
	 */
	static void Notify(const FName& SubjectName, const FString& MeshName, const FString& Detail)
	{
		UE_LOG(LogMOVINLiveLink, Display, TEXT("[Skeleton Calibration Offset] %s"), *Detail);

#if WITH_EDITOR
		const FString NotificationKey = MakeNotificationKey(SubjectName, MeshName);

		// Replaces any notification already up for this subject and mesh, so a recalibration does
		// not leave stale figures on screen next to the new ones.
		DismissNotification(NotificationKey);

		FNotificationInfo Info(FText::Format(
			LOCTEXT("CalibrationOffsetTitle", "Skeleton Calibration Offset - '{0}'"),
			FText::FromName(SubjectName)));
		Info.SubText = FText::FromString(Detail);
		Info.bFireAndForget = false;
		Info.bUseThrobber = false;
		Info.bUseSuccessFailIcons = false;
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("CalibrationOffsetDismiss", "Dismiss"),
			LOCTEXT("CalibrationOffsetDismissTooltip", "Hide this message until the streamed skeleton changes."),
			FSimpleDelegate::CreateLambda([NotificationKey]() { DismissNotification(NotificationKey); }),
			SNotificationItem::CS_None));

		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(SNotificationItem::CS_None);
			ActiveNotifications.Add(NotificationKey, Item);
		}
#endif
	}
}

bool FMOVINSkeletonDiagnostics::IsActorSubject(const FName& SubjectName)
{
	// FName comparison is case insensitive, so "MOVINman" and "MOVINMan" both match.
	return SubjectName == ActorSubjectName;
}

FMOVINSkeletonDeviationReport FMOVINSkeletonDiagnostics::CompareBoneLengths(
	TArrayView<const FMOVINRefPoseBone> RefBones,
	TArrayView<const FName> StreamedBoneNames,
	TArrayView<const FVector> StreamedTranslations,
	const TSet<FName>& ExcludedBoneNames,
	float ToleranceRatio)
{
	using namespace MOVINSkeletonDiagnosticsPrivate;

	FMOVINSkeletonDeviationReport Report;

	// Only trust indices that exist on both sides; a short translation array means a malformed frame.
	const int32 StreamedCount = FMath::Min(StreamedBoneNames.Num(), StreamedTranslations.Num());
	if (StreamedCount == 0 || RefBones.Num() == 0)
	{
		return Report;
	}

	TMap<FName, FVector> StreamedByName;
	StreamedByName.Reserve(StreamedCount);
	for (int32 Index = 0; Index < StreamedCount; ++Index)
	{
		StreamedByName.Add(StreamedBoneNames[Index], StreamedTranslations[Index]);
	}

	for (int32 BoneIndex = 0; BoneIndex < RefBones.Num(); ++BoneIndex)
	{
		const FMOVINRefPoseBone& RefBone = RefBones[BoneIndex];
		if (ExcludedBoneNames.Contains(RefBone.Name))
		{
			continue;
		}

		const FVector* StreamedTranslation = StreamedByName.Find(RefBone.Name);
		if (StreamedTranslation == nullptr)
		{
			continue;
		}

		const float RefLength = static_cast<float>(RefBone.LocalTranslation.Size());
		if (RefLength <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		++Report.ComparedBoneCount;

		FMOVINBoneLengthDeviation Deviation;
		Deviation.BoneName = RefBone.Name;
		Deviation.RefLength = RefLength;
		Deviation.StreamedLength = static_cast<float>(StreamedTranslation->Size());
		Deviation.Ratio = Deviation.StreamedLength / RefLength;

		if (Deviation.Magnitude() > ToleranceRatio)
		{
			Report.Deviations.Add(Deviation);
		}
	}

	// Rank by how far off each bone is, but compare at the precision the message prints and break
	// ties by name.
	//
	// A performer's left and right sides calibrate to figures that agree to two decimals and differ
	// only in the far ones, and TArray::Sort is not stable, so comparing raw floats let Left/Right
	// pairs swap places between scans. Nothing the user could see had changed, but the ordering had,
	// which was enough to look like a new report and re-raise the notification every two seconds.
	Report.Deviations.Sort([](const FMOVINBoneLengthDeviation& A, const FMOVINBoneLengthDeviation& B)
	{
		const int32 MagnitudeA = FMath::RoundToInt(A.Magnitude() * 100.0f);
		const int32 MagnitudeB = FMath::RoundToInt(B.Magnitude() * 100.0f);
		if (MagnitudeA != MagnitudeB)
		{
			return MagnitudeA > MagnitudeB;
		}
		return A.BoneName.LexicalLess(B.BoneName);
	});

	return Report;
}

FString FMOVINSkeletonDiagnostics::FormatReport(const FName& SubjectName, const FString& MeshName, const FMOVINSkeletonDeviationReport& Report)
{
	using namespace MOVINSkeletonDiagnosticsPrivate;

	TStringBuilder<1024> Builder;
	Builder.Appendf(TEXT("Subject '%s' is streaming bone lengths calibrated to the performer, "),
		*SubjectName.ToString());
	Builder.Appendf(TEXT("which differ from the reference pose of Skeletal Mesh '%s'"), *MeshName);

	const int32 ShownCount = FMath::Min(Report.Deviations.Num(), MaxReportedBones);
	if (ShownCount > 0)
	{
		Builder.Append(TEXT(" (largest first): "));
		for (int32 Index = 0; Index < ShownCount; ++Index)
		{
			const FMOVINBoneLengthDeviation& Deviation = Report.Deviations[Index];
			Builder.Appendf(TEXT("%s%s %.2fx"), (Index > 0 ? TEXT(", ") : TEXT("")), *Deviation.BoneName.ToString(), Deviation.Ratio);
		}
		if (Report.Deviations.Num() > ShownCount)
		{
			Builder.Appendf(TEXT(" (+%d more)"), Report.Deviations.Num() - ShownCount);
		}
	}

	Builder.Append(TEXT(".\nThe mesh will visibly deform at those joints. This is expected, not a plugin error - "
		"it means the motion data is being applied without loss.\n"
		"For a final character result, stream a Character from MOVIN Studio, or retarget onto your own "
		"character with an IK Retargeter."));

	return FString(Builder.ToString());
}

TSet<FName> FMOVINSkeletonDiagnostics::FindWorldMotionBones(
	TArrayView<const FName> BoneNames,
	TArrayView<const int32> LengthChangeCounts,
	int32 FramesObserved)
{
	TSet<FName> WorldMotionBones;

	if (FramesObserved < MinFramesForMotionCheck)
	{
		return WorldMotionBones;
	}

	const int32 Count = FMath::Min(BoneNames.Num(), LengthChangeCounts.Num());
	const int32 ChangeThreshold = FMath::CeilToInt(FramesObserved * WorldMotionChangeFraction);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (LengthChangeCounts[Index] >= ChangeThreshold)
		{
			WorldMotionBones.Add(BoneNames[Index]);
		}
	}

	return WorldMotionBones;
}

FString FMOVINSkeletonDiagnostics::BuildReportSignature(const FString& MeshName, const FMOVINSkeletonDeviationReport& Report)
{
	if (!Report.HasDeviation())
	{
		return MeshName + TEXT("|match");
	}

	// Sorted by name, not by rank: the signature answers "is this the same set of figures", and the
	// order the message happens to list them in is not part of that. Deriving it from the ranked
	// order instead made a reshuffle of equally-deviating bones read as a fresh report.
	TArray<FString> Entries;
	Entries.Reserve(Report.Deviations.Num());
	for (const FMOVINBoneLengthDeviation& Deviation : Report.Deviations)
	{
		// Two decimals: the same precision the message prints, so a change the user cannot see is
		// not treated as a change.
		Entries.Add(FString::Printf(TEXT("%s=%.2f"), *Deviation.BoneName.ToString(), Deviation.Ratio));
	}
	Entries.Sort();

	return MeshName + TEXT("|") + FString::Join(Entries, TEXT("|"));
}

void FMOVINSkeletonDiagnostics::NoteSkeleton(const FName& SubjectName, const TArray<FMOVINJointData>& Bones)
{
	using namespace MOVINSkeletonDiagnosticsPrivate;

	// Character streams are already retargeted onto the target .fbx, so there is no offset to report.
	if (Bones.Num() == 0 || !IsActorSubject(SubjectName))
	{
		return;
	}

	FScopeLock Lock(&StateCriticalSection);

	FTrackedSubject& Tracked = TrackedSubjects.FindOrAdd(SubjectName);

	// A different bone list is a different skeleton; the movement counts collected for the old one
	// mean nothing for the new one.
	bool bBoneListChanged = Tracked.BoneNames.Num() != Bones.Num();
	if (!bBoneListChanged)
	{
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			if (Tracked.BoneNames[Index] != Bones[Index].BoneName)
			{
				bBoneListChanged = true;
				break;
			}
		}
	}

	if (bBoneListChanged)
	{
		Tracked.BoneNames.Reset(Bones.Num());
		for (const FMOVINJointData& Bone : Bones)
		{
			Tracked.BoneNames.Add(Bone.BoneName);
		}
		Tracked.LengthChangeCounts.Reset();
		Tracked.LengthChangeCounts.AddZeroed(Bones.Num());
		Tracked.PreviousLengths.Reset();
		Tracked.PreviousLengths.AddZeroed(Bones.Num());
		Tracked.FramesObserved = 0;
		Tracked.NotifiedSignatures.Empty();
	}

	// Reset() keeps the allocation, so this is a copy into existing storage after the first frame.
	Tracked.Translations.Reset(Bones.Num());
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector& Translation = Bones[Index].LocalPosition;
		Tracked.Translations.Add(Translation);

		const double Length = Translation.Size();
		if (Tracked.FramesObserved > 0 &&
			FMath::Abs(Length - Tracked.PreviousLengths[Index]) > LengthChangeToleranceCm)
		{
			++Tracked.LengthChangeCounts[Index];
		}
		Tracked.PreviousLengths[Index] = Length;
	}

	++Tracked.FramesObserved;
	Tracked.LastFrameTimeSeconds = FPlatformTime::Seconds();
}

void FMOVINSkeletonDiagnostics::Tick()
{
	using namespace MOVINSkeletonDiagnosticsPrivate;

	check(IsInGameThread());

	const double Now = FPlatformTime::Seconds();

	TMap<FName, FTrackedSubject> SubjectsToScan;
	{
		FScopeLock Lock(&StateCriticalSection);

		if (TrackedSubjects.Num() == 0 || (Now - LastScanTimeSeconds) < ScanIntervalSeconds)
		{
			return;
		}
		LastScanTimeSeconds = Now;

		for (const TPair<FName, FTrackedSubject>& Pair : TrackedSubjects)
		{
			// Only subjects that are still streaming, and only once enough frames have been seen to
			// tell a moving pelvis apart from a static bone length.
			if ((Now - Pair.Value.LastFrameTimeSeconds) <= StaleFrameSeconds &&
				Pair.Value.FramesObserved >= MinFramesForMotionCheck)
			{
				SubjectsToScan.Add(Pair.Key, Pair.Value);
			}
		}
	}

	if (SubjectsToScan.Num() == 0)
	{
		return;
	}

	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Component = *It;
		if (Component == nullptr || Component->IsTemplate() || !IsValid(Component))
		{
			continue;
		}

		const USkinnedAsset* SkinnedAsset = Component->GetSkinnedAsset();
		if (SkinnedAsset == nullptr)
		{
			continue;
		}

		const FReferenceSkeleton& RefSkeleton = SkinnedAsset->GetRefSkeleton();
		const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRefBoneInfo();
		const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
		if (BoneInfos.Num() == 0 || BoneInfos.Num() != RefPose.Num())
		{
			continue;
		}

		for (const TPair<FName, FTrackedSubject>& Pair : SubjectsToScan)
		{
			if (!IsDrivenBySubject(Component, Pair.Key))
			{
				continue;
			}

			TArray<FMOVINRefPoseBone> RefBones;
			RefBones.Reserve(BoneInfos.Num());
			for (int32 BoneIndex = 0; BoneIndex < BoneInfos.Num(); ++BoneIndex)
			{
				FMOVINRefPoseBone& RefBone = RefBones.AddDefaulted_GetRef();
				RefBone.Name = BoneInfos[BoneIndex].Name;
				RefBone.LocalTranslation = RefPose[BoneIndex].GetTranslation();
				RefBone.ParentIndex = BoneInfos[BoneIndex].ParentIndex;
			}

			const TSet<FName> WorldMotionBones = FindWorldMotionBones(
				Pair.Value.BoneNames, Pair.Value.LengthChangeCounts, Pair.Value.FramesObserved);

			const FMOVINSkeletonDeviationReport Report = CompareBoneLengths(
				RefBones, Pair.Value.BoneNames, Pair.Value.Translations, WorldMotionBones);

			const FString MeshName = SkinnedAsset->GetName();
			const FString Signature = BuildReportSignature(MeshName, Report);

			// Say it once per mesh. The scan repeats for as long as the subject streams, so this is
			// what keeps a standing offset from re-raising the notification every couple of
			// seconds - and what makes a genuine recalibration raise it again.
			{
				FScopeLock Lock(&StateCriticalSection);
				FTrackedSubject* Tracked = TrackedSubjects.Find(Pair.Key);
				if (Tracked == nullptr)
				{
					continue;
				}
				FString& NotifiedSignature = Tracked->NotifiedSignatures.FindOrAdd(MeshName);
				if (NotifiedSignature == Signature)
				{
					continue;
				}
				NotifiedSignature = Signature;
			}

			// If this message ever starts repeating again, this is the line that says why: a bone
			// carrying world movement that was not filtered out will show up in the signature and
			// change on every scan.
			{
				TArray<FString> ExcludedNames;
				for (const FName& BoneName : WorldMotionBones)
				{
					ExcludedNames.Add(BoneName.ToString());
				}
				ExcludedNames.Sort();
				UE_LOG(LogMOVINLiveLink, Log,
					TEXT("Subject '%s' / '%s': signature '%s' (world-motion bones excluded: %s; %d frames observed)"),
					*Pair.Key.ToString(), *MeshName, *Signature,
					ExcludedNames.Num() > 0 ? *FString::Join(ExcludedNames, TEXT(", ")) : TEXT("none"),
					Pair.Value.FramesObserved);
			}

			if (Report.HasDeviation())
			{
				Notify(Pair.Key, MeshName, FormatReport(Pair.Key, MeshName, Report));
			}
			else
			{
				UE_LOG(LogMOVINLiveLink, Log,
					TEXT("Subject '%s': streamed bone lengths match Skeletal Mesh '%s' (%d bones compared)."),
					*Pair.Key.ToString(), *MeshName, Report.ComparedBoneCount);
			}
		}
	}
}

void FMOVINSkeletonDiagnostics::Reset()
{
	using namespace MOVINSkeletonDiagnosticsPrivate;

#if WITH_EDITOR
	if (IsInGameThread())
	{
		TArray<FString> OpenNotifications;
		ActiveNotifications.GetKeys(OpenNotifications);
		for (const FString& NotificationKey : OpenNotifications)
		{
			DismissNotification(NotificationKey);
		}
	}
	ActiveNotifications.Empty();
#endif

	FScopeLock Lock(&StateCriticalSection);
	TrackedSubjects.Empty();
	LastScanTimeSeconds = 0.0;
}

#undef LOCTEXT_NAMESPACE
