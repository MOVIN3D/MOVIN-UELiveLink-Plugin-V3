// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINActorMesh.h"
#include "MOVINLiveLinkModule.h"
#include "MOVINActorSkeleton.h"
#include "MOVINSkeletonDiagnostics.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace MOVINActorMeshPrivate
{
	static TAutoConsoleVariable<int32> CVarAutoFit(
		TEXT("movin.ActorMesh.AutoFit"),
		1,
		TEXT("Fit the Skeletal Mesh driven by a MOVIN Actor subject to the actor's calibrated bone lengths.\n")
		TEXT("The fitted mesh is a runtime duplicate; the asset on disk is never modified.\n")
		TEXT("0: off, the mesh keeps its authored reference pose."),
		ECVF_Default);

	/** How often the sweep walks Skeletal Mesh components looking for one to fit. */
	static constexpr double ScanIntervalSeconds = 2.0;

	/** Bone offsets closer than this are the same reference pose as far as retargeting is concerned. */
	static constexpr double RefPoseToleranceCm = 0.01;

	/** A mesh built for one template, and the calibration it was built for. */
	struct FFittedMesh
	{
		/** Held strongly: nothing else references a runtime duplicate, so it would be collected. */
		TStrongObjectPtr<USkeletalMesh> Mesh;

		int32 Revision = INDEX_NONE;
	};

	static FCriticalSection StateCriticalSection;

	/**
	 * Fitted meshes keyed by the template they were built from.
	 *
	 * Deliberately not keyed by component. What the fitted mesh contains depends on the template and
	 * the actor's calibration and on nothing else, so every component sharing a template shares
	 * one mesh. Keying by component instead made the editor recreating a Blueprint's components -
	 * which it does on its own schedule, and which invalidates any pointer held to them - look like
	 * a new component that had never been fitted, and the mesh was duplicated again every time.
	 */
	static TMap<TWeakObjectPtr<USkeletalMesh>, FFittedMesh> FittedMeshes;

	/**
	 * Every mesh this has generated, mapped back to what it was generated from.
	 *
	 * Without this a component already carrying a fitted mesh would take that mesh as its template
	 * and be fitted on top of it, compounding into MOVINman_V3_Puppet_UE_Actor_1_Actor_0 and
	 * a skeleton scaled twice. It is also what Reset() uses to put components back.
	 */
	static TMap<TWeakObjectPtr<USkeletalMesh>, TWeakObjectPtr<USkeletalMesh>> FittedMeshOrigins;

	static double LastScanTimeSeconds = 0.0;

	/** Calibration version the last sweep ran against; see why the sweep watches it in Tick(). */
	static int32 LastCalibrationVersion = INDEX_NONE;

	/** The template's reference pose in the form the maths wants. */
	static void ReadTemplateBones(const USkeletalMesh& InMesh, TArray<FMOVINTemplateBone>& OutBones)
	{
		const FReferenceSkeleton& RefSkeleton = InMesh.GetRefSkeleton();
		const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

		OutBones.Reset(RefSkeleton.GetNum());
		for (int32 Index = 0; Index < RefSkeleton.GetNum(); ++Index)
		{
			FMOVINTemplateBone& Bone = OutBones.AddDefaulted_GetRef();
			Bone.Name = RefSkeleton.GetBoneName(Index);
			Bone.ParentIndex = RefSkeleton.GetParentIndex(Index);
			Bone.RefPose = RefPose.IsValidIndex(Index) ? RefPose[Index] : FTransform::Identity;
		}
	}

	/** Is this a component worth fitting? */
	static bool IsFittableComponent(const USkeletalMeshComponent* InComponent)
	{
		return FMOVINSkeletonDiagnostics::IsComponentInLiveWorld(InComponent)
			&& InComponent->GetSkeletalMeshAsset() != nullptr;
	}

	/** Does this mesh already carry the reference pose we would build for it? */
	static bool HasReferencePose(const USkeletalMesh& InMesh, const TArray<FTransform>& InRefPose)
	{
		const TArray<FTransform>& Current = InMesh.GetRefSkeleton().GetRefBonePose();
		if (Current.Num() != InRefPose.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Current.Num(); ++Index)
		{
			if (!Current[Index].GetTranslation().Equals(InRefPose[Index].GetTranslation(), RefPoseToleranceCm))
			{
				return false;
			}
		}

		return true;
	}

	/** Duplicate the template and stamp the given reference pose onto the copy. */
	static USkeletalMesh* DuplicateWithReferencePose(
		USkeletalMesh* InTemplate,
		const TArray<FTransform>& InRefPose,
		FString& OutError)
	{
		// Duplication carries the render data across: USkeletalMesh::Serialize writes it when the
		// archive is duplicating as well as when it is cooking, so the copy is a usable mesh rather
		// than an empty shell.
		const FName FittedName = MakeUniqueObjectName(
			GetTransientPackage(), USkeletalMesh::StaticClass(), *(InTemplate->GetName() + TEXT("_Actor")));
		USkeletalMesh* Fitted = DuplicateObject<USkeletalMesh>(InTemplate, GetTransientPackage(), FittedName);
		if (Fitted == nullptr)
		{
			OutError = FString::Printf(TEXT("Failed to duplicate '%s'."), *InTemplate->GetName());
			return nullptr;
		}

		{
			// Scoped: the modifier rebuilds the reference skeleton's name map and virtual bones when
			// it goes out of scope, and the inverse bind matrices have to be recalculated after that.
			FReferenceSkeletonModifier Modifier(Fitted->GetRefSkeleton(), Fitted->GetSkeleton());
			for (int32 Index = 0; Index < InRefPose.Num(); ++Index)
			{
				Modifier.UpdateRefPoseTransform(Index, InRefPose[Index]);
			}
		}
		Fitted->CalculateInvRefMatrices();

		return Fitted;
	}

	/** The mesh a component should be fitted from - itself, unless it is already carrying one of ours. */
	static USkeletalMesh* ResolveTemplate(USkeletalMesh* InCurrentMesh)
	{
		const TWeakObjectPtr<USkeletalMesh>* Origin = FittedMeshOrigins.Find(InCurrentMesh);
		return (Origin != nullptr && Origin->IsValid()) ? Origin->Get() : InCurrentMesh;
	}
}

USkeletalMesh* FMOVINActorMesh::BuildMesh(
	USkeletalMesh* InTemplate,
	const FMOVINStreamedSkeleton& InSkeleton,
	FString& OutError)
{
	using namespace MOVINActorMeshPrivate;

	check(IsInGameThread());

	OutError.Reset();

	if (InTemplate == nullptr)
	{
		OutError = TEXT("No template Skeletal Mesh to fit.");
		return nullptr;
	}

	TArray<FMOVINTemplateBone> TemplateBones;
	ReadTemplateBones(*InTemplate, TemplateBones);

	TArray<FTransform> FittedRefPose;
	if (!FMOVINActorSkeleton::BuildReferencePose(TemplateBones, InSkeleton, FittedRefPose, OutError))
	{
		return nullptr;
	}

	return DuplicateWithReferencePose(InTemplate, FittedRefPose, OutError);
}

bool FMOVINActorMesh::ApplyToComponent(
	USkeletalMeshComponent* InComponent,
	const FName& InSubjectName,
	FString& OutError)
{
	using namespace MOVINActorMeshPrivate;

	check(IsInGameThread());

	OutError.Reset();

	if (InComponent == nullptr || InComponent->GetSkeletalMeshAsset() == nullptr)
	{
		OutError = TEXT("No Skeletal Mesh Component to fit.");
		return false;
	}

	FMOVINStreamedSkeleton Skeleton;
	if (!FMOVINSkeletonDiagnostics::GetStreamedSkeleton(InSubjectName, Skeleton))
	{
		OutError = FString::Printf(
			TEXT("Subject '%s' has not streamed enough frames to identify its calibration yet."),
			*InSubjectName.ToString());
		return false;
	}

	FScopeLock Lock(&StateCriticalSection);

	USkeletalMesh* Template = ResolveTemplate(InComponent->GetSkeletalMeshAsset());
	FFittedMesh& Entry = FittedMeshes.FindOrAdd(Template);

	if (!Entry.Mesh.IsValid() || Entry.Revision != Skeleton.CalibrationRevision)
	{
		TArray<FMOVINTemplateBone> TemplateBones;
		ReadTemplateBones(*Template, TemplateBones);

		TArray<FTransform> FittedRefPose;
		if (!FMOVINActorSkeleton::BuildReferencePose(TemplateBones, Skeleton, FittedRefPose, OutError))
		{
			return false;
		}

		// The calibration revision moves every time the tracker reclassifies a bone, which happens a
		// number of times while world movement is still being learned. Most of those settle on the
		// same lengths, so comparing the pose we would build against the one already built avoids
		// duplicating the mesh again for a difference nothing can see.
		if (Entry.Mesh.IsValid() && HasReferencePose(*Entry.Mesh.Get(), FittedRefPose))
		{
			Entry.Revision = Skeleton.CalibrationRevision;
		}
		else
		{
			USkeletalMesh* NewMesh = DuplicateWithReferencePose(Template, FittedRefPose, OutError);
			if (NewMesh == nullptr)
			{
				return false;
			}

			FittedMeshOrigins.Add(NewMesh, Template);
			Entry.Mesh.Reset(NewMesh);
			Entry.Revision = Skeleton.CalibrationRevision;

			UE_LOG(LogMOVINLiveLink, Log,
				TEXT("Subject '%s': fitted '%s' to the streamed actor (calibration revision %d)"),
				*InSubjectName.ToString(), *Template->GetName(), Skeleton.CalibrationRevision);
		}
	}

	if (InComponent->GetSkeletalMeshAsset() != Entry.Mesh.Get())
	{
		// Swapping the mesh is what makes the retargeter pick this up: the anim node compares the
		// component's mesh against the one its processor was initialised with and reinitialises when
		// they differ.
		InComponent->SetSkeletalMeshAsset(Entry.Mesh.Get());

		// Verbose because the editor recreates Blueprint components on its own schedule, and each
		// new one has to be pointed at the fitted mesh again. That costs nothing and says nothing.
		UE_LOG(LogMOVINLiveLink, Verbose, TEXT("Subject '%s': applied fitted mesh to '%s'"),
			*InSubjectName.ToString(), *InComponent->GetName());
	}

	return true;
}

void FMOVINActorMesh::Tick()
{
	using namespace MOVINActorMeshPrivate;

	check(IsInGameThread());

	if (!IsAutoFitEnabled())
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const int32 CalibrationVersion = FMOVINSkeletonDiagnostics::GetCalibrationVersion();
	{
		FScopeLock Lock(&StateCriticalSection);

		// The interval exists to find components that have appeared, which can wait. A recalibration
		// cannot: until the sweep runs, the mesh is still fitted to the previous body, and on a
		// restarting stream that is a second or two of the character visibly bending. So a changed
		// calibration version runs the sweep on the next frame instead of waiting the interval out.
		const bool bCalibrationChanged = CalibrationVersion != LastCalibrationVersion;
		if (!bCalibrationChanged && (Now - LastScanTimeSeconds) < ScanIntervalSeconds)
		{
			return;
		}
		LastCalibrationVersion = CalibrationVersion;
		LastScanTimeSeconds = Now;

		// Templates can be unloaded; their fitted meshes are then held alive by nothing but this.
		for (auto It = FittedMeshes.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}

		for (auto It = FittedMeshOrigins.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || !It.Value().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	TArray<FName> Subjects;
	FMOVINSkeletonDiagnostics::GetTrackedSubjects(Subjects);
	if (Subjects.Num() == 0)
	{
		return;
	}

	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Component = *It;
		if (!IsFittableComponent(Component))
		{
			continue;
		}

		for (const FName& SubjectName : Subjects)
		{
			if (!FMOVINSkeletonDiagnostics::IsComponentDrivenBySubject(Component, SubjectName))
			{
				continue;
			}

			FString Error;
			if (!ApplyToComponent(Component, SubjectName, Error))
			{
				UE_LOG(LogMOVINLiveLink, Verbose, TEXT("Subject '%s': not fitting '%s' - %s"),
					*SubjectName.ToString(), *Component->GetName(), *Error);
			}

			// One subject per component: a mesh driven by two subjects has a bigger problem than
			// which one it gets fitted to.
			break;
		}
	}
}

bool FMOVINActorMesh::IsAutoFitEnabled()
{
	using namespace MOVINActorMeshPrivate;

	return CVarAutoFit.GetValueOnAnyThread() != 0;
}

void FMOVINActorMesh::Reset()
{
	using namespace MOVINActorMeshPrivate;

	FScopeLock Lock(&StateCriticalSection);

	// Put every component carrying a generated mesh back on the asset it came from, so nothing is
	// left pointing at a mesh that is about to be collected. Skipped during engine exit, where the
	// object graph is already being torn down and the restore would be pointless anyway.
	if (!IsEngineExitRequested())
	{
		for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			USkeletalMeshComponent* Component = *It;
			if (!IsValid(Component))
			{
				continue;
			}

			const TWeakObjectPtr<USkeletalMesh>* Origin = FittedMeshOrigins.Find(Component->GetSkeletalMeshAsset());
			if (Origin != nullptr && Origin->IsValid())
			{
				Component->SetSkeletalMeshAsset(Origin->Get());
			}
		}
	}

	FittedMeshes.Empty();
	FittedMeshOrigins.Empty();
	LastScanTimeSeconds = 0.0;
}
