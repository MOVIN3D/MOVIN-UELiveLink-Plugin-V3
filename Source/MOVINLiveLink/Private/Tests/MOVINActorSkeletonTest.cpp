// Copyright 2025 MOVIN. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MOVINActorSkeleton.h"
#include "MOVINSkeletonDiagnostics.h"
#include "Misc/AutomationTest.h"

namespace MOVINActorSkeletonTestHelpers
{
	static constexpr double Tolerance = 0.001;

	/**
	 * The layout MOVINman_V3_Puppet_UE imports with: no separate root bone, so the pelvis is bone 0.
	 * Legs run down, spine and arm run up, which is what lets the ground contact search tell them
	 * apart. LeftLegTwist is a bone the mesh has and MOVIN Studio does not stream.
	 */
	static TArray<FMOVINTemplateBone> MakeTemplate()
	{
		auto Bone = [](const TCHAR* Name, int32 ParentIndex, double OffsetZ)
		{
			FMOVINTemplateBone Out;
			Out.Name = FName(Name);
			Out.ParentIndex = ParentIndex;
			Out.RefPose = FTransform(FVector(0.0, 0.0, OffsetZ));
			return Out;
		};

		TArray<FMOVINTemplateBone> Template;
		Template.Add(Bone(TEXT("Hips"), INDEX_NONE, 90.0));      // root, at hip height
		Template.Add(Bone(TEXT("LeftUpLeg"), 0, -45.0));         // global Z 45
		Template.Add(Bone(TEXT("LeftFoot"), 1, -45.0));          // global Z 0
		Template.Add(Bone(TEXT("Spine"), 0, 10.0));              // global Z 100
		Template.Add(Bone(TEXT("LeftHand"), 3, 30.0));           // global Z 130
		Template.Add(Bone(TEXT("LeftLegTwist"), 1, -20.0));      // global Z 25, not streamed
		return Template;
	}

	/** Bone indices in MakeTemplate(), for readability at the call sites. */
	enum EBone : int32
	{
		Hips = 0,
		LeftUpLeg = 1,
		LeftFoot = 2,
		Spine = 3,
		LeftHand = 4,
		LeftLegTwist = 5,
	};

	/**
	 * An actor whose legs are 10% shorter than the mesh, standing somewhere out in the volume.
	 * The arm is left at the mesh's length on purpose: only the pelvis-to-floor chain should decide
	 * how high the fitted pelvis sits.
	 */
	static FMOVINStreamedSkeleton MakeStreamedSkeleton()
	{
		FMOVINStreamedSkeleton Skeleton;
		Skeleton.BoneNames = {
			FName(TEXT("Hips")),
			FName(TEXT("LeftUpLeg")),
			FName(TEXT("LeftFoot")),
			FName(TEXT("Spine")),
			FName(TEXT("LeftHand")),
		};
		Skeleton.LocalTranslations = {
			FVector(120.0, -40.0, 81.0),   // a world position, not a length
			FVector(0.0, 0.0, -40.5),
			FVector(0.0, 0.0, -40.5),
			FVector(0.0, 0.0, 10.0),
			FVector(0.0, 0.0, 30.0),
		};
		Skeleton.WorldMotionBones = { FName(TEXT("Hips")) };
		Skeleton.bWorldMotionResolved = true;
		Skeleton.CalibrationRevision = 1;
		return Skeleton;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINActorGroundContactTest, "MOVIN.ActorSkeleton.GroundContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINActorGroundContactTest::RunTest(const FString& Parameters)
{
	using namespace MOVINActorSkeletonTestHelpers;

	const TArray<FMOVINTemplateBone> Template = MakeTemplate();

	TestEqual(TEXT("The lowest descendant is the foot, not the hand"),
		FMOVINActorSkeleton::FindGroundContactBone(Template, EBone::Hips), (int32)EBone::LeftFoot);

	TestEqual(TEXT("A leaf bone has no descendants to measure"),
		FMOVINActorSkeleton::FindGroundContactBone(Template, EBone::LeftHand), (int32)INDEX_NONE);

	TestEqual(TEXT("An out of range bone is rejected"),
		FMOVINActorSkeleton::FindGroundContactBone(Template, 99), (int32)INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINActorMeasureChainTest, "MOVIN.ActorSkeleton.MeasureChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINActorMeasureChainTest::RunTest(const FString& Parameters)
{
	using namespace MOVINActorSkeletonTestHelpers;

	const TArray<FMOVINTemplateBone> Template = MakeTemplate();
	const FMOVINStreamedSkeleton Skeleton = MakeStreamedSkeleton();

	double TemplateLength = 0.0;
	double StreamedLength = 0.0;

	TestTrue(TEXT("The pelvis is an ancestor of the foot"),
		FMOVINActorSkeleton::MeasureChain(Template, Skeleton, EBone::LeftFoot, EBone::Hips, TemplateLength, StreamedLength));
	TestEqual(TEXT("Template leg is thigh plus shin"), TemplateLength, 90.0, Tolerance);
	TestEqual(TEXT("Streamed leg is 10% shorter"), StreamedLength, 81.0, Tolerance);

	// The bones on the chain are the same on both sides, so the ratio between the two sums is the
	// actor's scale and nothing else.
	TestEqual(TEXT("Neither sum picked up the pelvis world position"), StreamedLength / TemplateLength, 0.9, Tolerance);

	TestFalse(TEXT("A bone that is not an ancestor fails rather than reporting a partial chain"),
		FMOVINActorSkeleton::MeasureChain(Template, Skeleton, EBone::LeftFoot, EBone::LeftHand, TemplateLength, StreamedLength));
	TestEqual(TEXT("A failed measurement reports nothing"), TemplateLength, 0.0, Tolerance);

	// A bone the mesh has and the stream does not has to drop out of both sums, or the ratio would
	// compare a two bone chain against a one bone one.
	FMOVINStreamedSkeleton Partial = Skeleton;
	const int32 UpLegIndex = Partial.BoneNames.IndexOfByKey(FName(TEXT("LeftUpLeg")));
	Partial.BoneNames.RemoveAt(UpLegIndex);
	Partial.LocalTranslations.RemoveAt(UpLegIndex);

	TestTrue(TEXT("A gap in the stream still measures"),
		FMOVINActorSkeleton::MeasureChain(Template, Partial, EBone::LeftFoot, EBone::Hips, TemplateLength, StreamedLength));
	TestEqual(TEXT("The missing bone is skipped on the template side"), TemplateLength, 45.0, Tolerance);
	TestEqual(TEXT("The missing bone is skipped on the streamed side"), StreamedLength, 40.5, Tolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINActorReferencePoseTest, "MOVIN.ActorSkeleton.ReferencePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINActorReferencePoseTest::RunTest(const FString& Parameters)
{
	using namespace MOVINActorSkeletonTestHelpers;

	TArray<FMOVINTemplateBone> Template = MakeTemplate();

	// A bind rotation the fitted pose has to leave alone: replacing it would invalidate the rotation
	// deltas the retargeter stores in its retarget pose.
	const FQuat SpineBindRotation(FVector::RightVector, FMath::DegreesToRadians(15.0));
	Template[EBone::Spine].RefPose.SetRotation(SpineBindRotation);

	const FMOVINStreamedSkeleton Skeleton = MakeStreamedSkeleton();

	TArray<FTransform> RefPose;
	FString Error;
	if (!TestTrue(TEXT("Reference pose builds"),
		FMOVINActorSkeleton::BuildReferencePose(Template, Skeleton, RefPose, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("One transform per template bone"), RefPose.Num(), Template.Num());

	TestEqual(TEXT("Streamed bone lengths are taken verbatim"),
		RefPose[EBone::LeftUpLeg].GetTranslation().Z, -40.5, Tolerance);

	// Not the streamed 81cm world position: the pelvis offset is the mesh's own, scaled to the
	// actor, which is the height the retargeter compares the live pelvis against.
	TestEqual(TEXT("The pelvis is scaled, not copied from the stream"),
		RefPose[EBone::Hips].GetTranslation().Z, 81.0, Tolerance);
	TestEqual(TEXT("The pelvis keeps the mesh's horizontal offset"),
		RefPose[EBone::Hips].GetTranslation().X, 0.0, Tolerance);

	TestEqual(TEXT("A bone the stream does not carry scales with the body"),
		RefPose[EBone::LeftLegTwist].GetTranslation().Z, -18.0, Tolerance);

	TestTrue(TEXT("Bind rotations survive the fit"),
		RefPose[EBone::Spine].GetRotation().Equals(SpineBindRotation, Tolerance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINActorUncalibratedTest, "MOVIN.ActorSkeleton.Uncalibrated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINActorUncalibratedTest::RunTest(const FString& Parameters)
{
	using namespace MOVINActorSkeletonTestHelpers;

	const TArray<FMOVINTemplateBone> Template = MakeTemplate();

	TArray<FTransform> RefPose;
	FString Error;

	// Before world movement has been identified the pelvis is indistinguishable from a bone length,
	// and fitting would bake the actor's world position into the skeleton.
	FMOVINStreamedSkeleton Unresolved = MakeStreamedSkeleton();
	Unresolved.bWorldMotionResolved = false;
	TestFalse(TEXT("An uncalibrated stream is refused"),
		FMOVINActorSkeleton::BuildReferencePose(Template, Unresolved, RefPose, Error));
	TestTrue(TEXT("Refusal explains itself"), !Error.IsEmpty());

	// Resolved, but nothing the template recognises - a different rig on the wire.
	FMOVINStreamedSkeleton Foreign = MakeStreamedSkeleton();
	Foreign.WorldMotionBones = { FName(TEXT("SomeOtherRoot")) };
	TestFalse(TEXT("A stream with no recognisable pelvis is refused"),
		FMOVINActorSkeleton::BuildReferencePose(Template, Foreign, RefPose, Error));

	const TArray<FMOVINTemplateBone> EmptyTemplate;
	TestFalse(TEXT("An empty template is refused"),
		FMOVINActorSkeleton::BuildReferencePose(EmptyTemplate, MakeStreamedSkeleton(), RefPose, Error));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
