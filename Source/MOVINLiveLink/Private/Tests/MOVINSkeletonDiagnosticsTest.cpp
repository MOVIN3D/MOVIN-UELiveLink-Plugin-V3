// Copyright 2025 MOVIN. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MOVINSkeletonDiagnostics.h"
#include "Misc/AutomationTest.h"

namespace MOVINSkeletonDiagnosticsTestHelpers
{
	/** Nothing carries world movement - every streamed translation is a genuine bone length. */
	static const TSet<FName>& NoExclusions()
	{
		static const TSet<FName> Empty;
		return Empty;
	}

	/**
	 * What FindWorldMotionBones() reports for a normal performance. Named for both rig layouts so
	 * one set covers either fixture; a bone that is not in the skeleton simply never matches.
	 */
	static const TSet<FName>& PelvisExclusion()
	{
		static const TSet<FName> Excluded = { FName(TEXT("RootBone")), FName(TEXT("Hips")) };
		return Excluded;
	}

	/**
	 * The layout MOVINman_V3_Puppet_UE actually imports with: no separate root bone, so the pelvis
	 * is bone 0 and Spine sits one level below it.
	 */
	static TArray<FMOVINRefPoseBone> MakePelvisRootedSkeleton()
	{
		auto Bone = [](const TCHAR* Name, int32 ParentIndex, double Length)
		{
			FMOVINRefPoseBone Out;
			Out.Name = FName(Name);
			Out.ParentIndex = ParentIndex;
			Out.LocalTranslation = FVector(0.0, 0.0, Length);
			return Out;
		};

		TArray<FMOVINRefPoseBone> RefBones;
		RefBones.Add(Bone(TEXT("Hips"), INDEX_NONE, 90.868));  // root, at hip height
		RefBones.Add(Bone(TEXT("LeftUpLeg"), 0, 40.0));
		RefBones.Add(Bone(TEXT("RightUpLeg"), 0, 40.0));
		RefBones.Add(Bone(TEXT("Spine"), 0, 10.0));
		RefBones.Add(Bone(TEXT("Neck"), 3, 20.0));
		return RefBones;
	}

	/**
	 * A minimal MOVINman-shaped chain: RootBone -> Hips -> Spine -> Neck -> Head, plus a finger
	 * that the stream never sends. Lengths are in centimetres, matching the wire format.
	 */
	static TArray<FMOVINRefPoseBone> MakeRefSkeleton()
	{
		auto Bone = [](const TCHAR* Name, int32 ParentIndex, double Length)
		{
			FMOVINRefPoseBone Out;
			Out.Name = FName(Name);
			Out.ParentIndex = ParentIndex;
			Out.LocalTranslation = FVector(0.0, 0.0, Length);
			return Out;
		};

		TArray<FMOVINRefPoseBone> RefBones;
		RefBones.Add(Bone(TEXT("RootBone"), INDEX_NONE, 0.0));  // depth 0
		RefBones.Add(Bone(TEXT("Hips"), 0, 95.0));              // depth 1
		RefBones.Add(Bone(TEXT("Spine"), 1, 10.0));             // depth 2
		RefBones.Add(Bone(TEXT("Neck"), 2, 20.0));              // depth 3
		RefBones.Add(Bone(TEXT("Head"), 3, 10.0));              // depth 4
		RefBones.Add(Bone(TEXT("Finger"), 4, 3.0));             // depth 5, never streamed
		return RefBones;
	}

	static FVector Along(double Length)
	{
		return FVector(0.0, 0.0, Length);
	}

	static const FMOVINBoneLengthDeviation* Find(const FMOVINSkeletonDeviationReport& Report, const TCHAR* BoneName)
	{
		return Report.Deviations.FindByPredicate([BoneName](const FMOVINBoneLengthDeviation& Deviation)
		{
			return Deviation.BoneName == FName(BoneName);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonMatchingLengthsTest, "MOVIN.SkeletonDiagnostics.MatchingLengths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonMatchingLengthsTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	// Character streaming: MOVIN Studio has already retargeted onto this exact mesh, so every
	// streamed length matches the reference pose and there is nothing to tell the user about.
	const TArray<FMOVINRefPoseBone> RefBones = MakeRefSkeleton();
	const TArray<FName> StreamedNames = { TEXT("RootBone"), TEXT("Hips"), TEXT("Spine"), TEXT("Neck"), TEXT("Head") };
	const TArray<FVector> StreamedTranslations = { Along(0.0), Along(140.0), Along(10.0), Along(20.0), Along(10.0) };

	const FMOVINSkeletonDeviationReport Report =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, StreamedTranslations, PelvisExclusion());

	TestFalse(TEXT("Matching skeletons report no deviation"), Report.HasDeviation());
	TestEqual(TEXT("Spine, Neck and Head are compared"), Report.ComparedBoneCount, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonCalibrationOffsetTest, "MOVIN.SkeletonDiagnostics.CalibrationOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonCalibrationOffsetTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	// Actor streaming: calibrated lengths differ from the mesh. Head is stretched most, so it must
	// rank ahead of the milder Spine deviation; Neck is within tolerance and must not be listed.
	const TArray<FMOVINRefPoseBone> RefBones = MakeRefSkeleton();
	const TArray<FName> StreamedNames = { TEXT("RootBone"), TEXT("Hips"), TEXT("Spine"), TEXT("Neck"), TEXT("Head") };
	const TArray<FVector> StreamedTranslations = { Along(0.0), Along(140.0), Along(6.2), Along(20.0), Along(16.3) };

	const FMOVINSkeletonDeviationReport Report =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, StreamedTranslations, PelvisExclusion());

	TestTrue(TEXT("Calibration offset is detected"), Report.HasDeviation());
	TestEqual(TEXT("Only the two out-of-tolerance bones are listed"), Report.Deviations.Num(), 2);

	if (Report.Deviations.Num() == 2)
	{
		TestEqual(TEXT("Largest deviation is ranked first"), Report.Deviations[0].BoneName, FName(TEXT("Head")));
		TestNearlyEqual(TEXT("Head ratio"), Report.Deviations[0].Ratio, 1.63f, 0.001f);
		TestEqual(TEXT("Second deviation"), Report.Deviations[1].BoneName, FName(TEXT("Spine")));
		TestNearlyEqual(TEXT("Spine ratio"), Report.Deviations[1].Ratio, 0.62f, 0.001f);
	}

	TestNull(TEXT("In-tolerance bone is not reported"), Find(Report, TEXT("Neck")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonPelvisExcludedTest, "MOVIN.SkeletonDiagnostics.PelvisExcluded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonPelvisExcludedTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	// The pelvis carries world movement rather than a bone length, so its streamed translation
	// swings wildly frame to frame. Reporting it would bury the real findings under noise.
	const TArray<FMOVINRefPoseBone> RefBones = MakeRefSkeleton();
	const TArray<FName> StreamedNames = { TEXT("RootBone"), TEXT("Hips"), TEXT("Spine"), TEXT("Neck"), TEXT("Head") };
	const TArray<FVector> StreamedTranslations = { Along(0.0), Along(238.0), Along(10.0), Along(20.0), Along(10.0) };

	const FMOVINSkeletonDeviationReport Report =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, StreamedTranslations, PelvisExclusion());

	TestNull(TEXT("Pelvis is not treated as a bone length"), Find(Report, TEXT("Hips")));
	TestNull(TEXT("Root is not treated as a bone length"), Find(Report, TEXT("RootBone")));
	TestFalse(TEXT("A moving pelvis alone produces no report"), Report.HasDeviation());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonPartialBoneSetTest, "MOVIN.SkeletonDiagnostics.PartialBoneSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonPartialBoneSetTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	// The mesh has ~55 bones but the stream carries ~25, so bones that are never streamed
	// (fingers, twist bones) must be skipped rather than reported as infinitely wrong.
	const TArray<FMOVINRefPoseBone> RefBones = MakeRefSkeleton();
	const TArray<FName> StreamedNames = { TEXT("Spine"), TEXT("Head") };
	const TArray<FVector> StreamedTranslations = { Along(10.0), Along(16.3) };

	const FMOVINSkeletonDeviationReport Report =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, StreamedTranslations, PelvisExclusion());

	TestEqual(TEXT("Only streamed bones are compared"), Report.ComparedBoneCount, 2);
	TestNull(TEXT("Un-streamed bone is skipped"), Find(Report, TEXT("Finger")));
	TestEqual(TEXT("Only the deviating streamed bone is reported"), Report.Deviations.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonMalformedInputTest, "MOVIN.SkeletonDiagnostics.MalformedInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonMalformedInputTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	const TArray<FMOVINRefPoseBone> RefBones = MakeRefSkeleton();

	// A frame whose transform array is shorter than its name array must not read past the end.
	const TArray<FName> StreamedNames = { TEXT("Spine"), TEXT("Neck"), TEXT("Head") };
	const TArray<FVector> ShortTranslations = { Along(6.2) };

	const FMOVINSkeletonDeviationReport Report =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, ShortTranslations, NoExclusions());

	TestEqual(TEXT("Only the bones with a matching transform are compared"), Report.ComparedBoneCount, 1);

	// Empty input on either side is a no-op rather than a crash.
	const TArray<FName> NoNames;
	const TArray<FVector> NoTranslations;
	const TArray<FMOVINRefPoseBone> NoRefBones;

	const FMOVINSkeletonDeviationReport EmptyStream =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, NoNames, NoTranslations, NoExclusions());
	TestEqual(TEXT("Empty stream compares nothing"), EmptyStream.ComparedBoneCount, 0);

	const FMOVINSkeletonDeviationReport EmptyRef =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(NoRefBones, StreamedNames, ShortTranslations, NoExclusions());
	TestEqual(TEXT("Empty reference skeleton compares nothing"), EmptyRef.ComparedBoneCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonReportTextTest, "MOVIN.SkeletonDiagnostics.ReportText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonReportTextTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	// The message has one job: convince the reader this is expected. It must name the bones they
	// can see on screen, and it must say outright that it is not a plugin error.
	const TArray<FMOVINRefPoseBone> RefBones = MakeRefSkeleton();
	const TArray<FName> StreamedNames = { TEXT("Spine"), TEXT("Head") };
	const TArray<FVector> StreamedTranslations = { Along(6.2), Along(16.3) };

	const FMOVINSkeletonDeviationReport Report =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, StreamedTranslations, PelvisExclusion());
	const FString Message = FMOVINSkeletonDiagnostics::FormatReport(
		FName(TEXT("MOVINman")), TEXT("MOVINman_V3_Puppet_UE"), Report);

	TestTrue(TEXT("Names the subject"), Message.Contains(TEXT("MOVINman")));
	TestTrue(TEXT("Names the mesh"), Message.Contains(TEXT("MOVINman_V3_Puppet_UE")));
	TestTrue(TEXT("Quotes the worst bone with its ratio"), Message.Contains(TEXT("Head 1.63x")));
	TestTrue(TEXT("States that this is not a defect"), Message.Contains(TEXT("not a plugin error")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonSymmetricBonesTest, "MOVIN.SkeletonDiagnostics.SymmetricBones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonSymmetricBonesTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	// A real performer calibrates left and right to figures that agree to two decimals and differ
	// only in the far ones. TArray::Sort is not stable, so ranking on the raw floats let those pairs
	// swap places between scans - identical bones, identical printed ratios, different order. That
	// was enough to look like a new report and re-raise the notification every two seconds.
	auto Bone = [](const TCHAR* Name, int32 ParentIndex, double Length)
	{
		FMOVINRefPoseBone Out;
		Out.Name = FName(Name);
		Out.ParentIndex = ParentIndex;
		Out.LocalTranslation = FVector(0.0, 0.0, Length);
		return Out;
	};

	TArray<FMOVINRefPoseBone> RefBones;
	RefBones.Add(Bone(TEXT("Hips"), INDEX_NONE, 90.868));
	RefBones.Add(Bone(TEXT("LeftUpLeg"), 0, 40.0));
	RefBones.Add(Bone(TEXT("RightUpLeg"), 0, 40.0));
	RefBones.Add(Bone(TEXT("LeftArm"), 0, 30.0));
	RefBones.Add(Bone(TEXT("RightArm"), 0, 30.0));

	const TArray<FName> StreamedNames =
		{ TEXT("Hips"), TEXT("LeftUpLeg"), TEXT("RightUpLeg"), TEXT("LeftArm"), TEXT("RightArm") };

	// Both thighs read 1.20x and both upper arms 1.12x once printed, but no two are bit-identical.
	const TArray<FVector> FrameA =
		{ Along(95.0), Along(48.0001), Along(48.0002), Along(33.6002), Along(33.6001) };
	const TArray<FVector> FrameB =
		{ Along(238.0), Along(48.0002), Along(48.0001), Along(33.6001), Along(33.6002) };

	const FMOVINSkeletonDeviationReport ReportA =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, FrameA, PelvisExclusion());
	const FMOVINSkeletonDeviationReport ReportB =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, FrameB, PelvisExclusion());

	TestEqual(TEXT("Near-tied symmetric bones do not change the signature"),
		FMOVINSkeletonDiagnostics::BuildReportSignature(TEXT("MOVINman_V3_Puppet_UE"), ReportB),
		FMOVINSkeletonDiagnostics::BuildReportSignature(TEXT("MOVINman_V3_Puppet_UE"), ReportA));

	// The displayed order has to hold still too, or the message text churns even when nothing did.
	TestEqual(TEXT("Displayed order is stable"),
		FMOVINSkeletonDiagnostics::FormatReport(FName(TEXT("MOVINMan")), TEXT("MOVINman_V3_Puppet_UE"), ReportB),
		FMOVINSkeletonDiagnostics::FormatReport(FName(TEXT("MOVINMan")), TEXT("MOVINman_V3_Puppet_UE"), ReportA));

	// Ranking still has to work: the thighs deviate more than the arms and must come first.
	if (TestEqual(TEXT("All four bones are reported"), ReportA.Deviations.Num(), 4))
	{
		TestTrue(TEXT("Larger deviations rank first"),
			ReportA.Deviations[0].BoneName.ToString().Contains(TEXT("UpLeg")) &&
			ReportA.Deviations[1].BoneName.ToString().Contains(TEXT("UpLeg")));
		TestEqual(TEXT("Ties break by name, left before right"),
			ReportA.Deviations[0].BoneName, FName(TEXT("LeftUpLeg")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonWorldMotionDetectionTest, "MOVIN.SkeletonDiagnostics.WorldMotionDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonWorldMotionDetectionTest::RunTest(const FString& Parameters)
{
	// World movement is identified by watching the stream rather than by reading the hierarchy,
	// because the shape of the rig is not reliable: MOVIN Studio's skeleton and the imported .fbx
	// disagree about whether there is a root bone above the pelvis. Getting this wrong let the
	// pelvis into the report, and since its "length" is a world position it changed on every scan,
	// which is what made the notification fire again and again.
	const TArray<FName> BoneNames = { TEXT("Hips"), TEXT("Spine"), TEXT("Neck"), TEXT("Head") };
	const int32 FramesObserved = 600;

	// The pelvis moves nearly every frame; the rest hold a fixed calibrated length.
	const TArray<int32> Counts = { 590, 0, 0, 0 };
	const TSet<FName> Detected = FMOVINSkeletonDiagnostics::FindWorldMotionBones(BoneNames, Counts, FramesObserved);

	TestTrue(TEXT("A bone that moves every frame is world movement"), Detected.Contains(FName(TEXT("Hips"))));
	TestEqual(TEXT("Nothing else is excluded"), Detected.Num(), 1);

	// A recalibration changes a length once and then holds it. That must not silence the bone -
	// re-reporting the corrected figures is the whole point of watching for it.
	const TArray<int32> Recalibrated = { 590, 1, 0, 0 };
	const TSet<FName> AfterRecalibration =
		FMOVINSkeletonDiagnostics::FindWorldMotionBones(BoneNames, Recalibrated, FramesObserved);
	TestFalse(TEXT("A one-off recalibration is not world movement"), AfterRecalibration.Contains(FName(TEXT("Spine"))));

	// Too few frames to tell the two apart yet, so nothing is claimed.
	const TArray<int32> EarlyCounts = { 5, 0, 0, 0 };
	const TSet<FName> TooEarly = FMOVINSkeletonDiagnostics::FindWorldMotionBones(BoneNames, EarlyCounts, 5);
	TestEqual(TEXT("Nothing is concluded before enough frames"), TooEarly.Num(), 0);

	// A short translation array must not be read past the end.
	const TArray<int32> ShortCounts = { 590 };
	const TSet<FName> Partial = FMOVINSkeletonDiagnostics::FindWorldMotionBones(BoneNames, ShortCounts, FramesObserved);
	TestEqual(TEXT("Only the counts provided are considered"), Partial.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonPelvisRootedRigTest, "MOVIN.SkeletonDiagnostics.PelvisRootedRig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonPelvisRootedRigTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	// MOVINman has no separate root bone: Hips is bone 0, so Spine and both thighs sit one level
	// down. An earlier "skip the first two hierarchy levels" rule threw all three away on this rig -
	// most of what the user would want to see, including the worst offender.
	const TArray<FMOVINRefPoseBone> RefBones = MakePelvisRootedSkeleton();
	const TArray<FName> StreamedNames = { TEXT("Hips"), TEXT("LeftUpLeg"), TEXT("RightUpLeg"), TEXT("Spine"), TEXT("Neck") };
	const TArray<FVector> StreamedTranslations = { Along(238.0), Along(40.0), Along(40.0), Along(6.2), Along(20.0) };

	const FMOVINSkeletonDeviationReport Report =
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, StreamedTranslations, PelvisExclusion());

	TestNull(TEXT("Pelvis is excluded even when it is the root"), Find(Report, TEXT("Hips")));
	TestEqual(TEXT("Everything below the pelvis is compared"), Report.ComparedBoneCount, 4);
	TestEqual(TEXT("Only the deviating bone is reported"), Report.Deviations.Num(), 1);

	if (Report.Deviations.Num() == 1)
	{
		TestEqual(TEXT("Spine is reported on a pelvis-rooted rig"), Report.Deviations[0].BoneName, FName(TEXT("Spine")));
	}

	// A moving pelvis must not disturb the result, or the notification re-fires forever.
	const TArray<FVector> LaterFrame = { Along(43.0), Along(40.0), Along(40.0), Along(6.2), Along(20.0) };
	TestEqual(TEXT("Signature is stable while the pelvis moves"),
		FMOVINSkeletonDiagnostics::BuildReportSignature(TEXT("MOVINman_V3_Puppet_UE"),
			FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, LaterFrame, PelvisExclusion())),
		FMOVINSkeletonDiagnostics::BuildReportSignature(TEXT("MOVINman_V3_Puppet_UE"), Report));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonReportSignatureTest, "MOVIN.SkeletonDiagnostics.ReportSignature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonReportSignatureTest::RunTest(const FString& Parameters)
{
	using namespace MOVINSkeletonDiagnosticsTestHelpers;

	const TArray<FMOVINRefPoseBone> RefBones = MakeRefSkeleton();
	const TArray<FName> StreamedNames = { TEXT("RootBone"), TEXT("Hips"), TEXT("Spine"), TEXT("Neck"), TEXT("Head") };

	// Matches what the running plugin does: FindWorldMotionBones() has already spotted the pelvis
	// from the stream, and its name is passed in here.
	auto SignatureFor = [&RefBones, &StreamedNames](const TArray<FVector>& Translations)
	{
		return FMOVINSkeletonDiagnostics::BuildReportSignature(TEXT("MOVINman_V3_Puppet_UE"),
			FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, Translations, PelvisExclusion()));
	};

	// The pelvis translation is world movement, so it differs on every frame of a performance.
	// The notification must not treat that as a recalibration - this is what stopped the message
	// re-appearing every couple of seconds.
	const TArray<FVector> Frame1 = { Along(0.0), Along(95.0), Along(6.2), Along(20.0), Along(16.3) };
	const TArray<FVector> Frame2 = { Along(0.0), Along(238.0), Along(6.2), Along(20.0), Along(16.3) };
	TestEqual(TEXT("A moving pelvis does not change the signature"), SignatureFor(Frame2), SignatureFor(Frame1));

	// Neither does jitter finer than the two decimals the message prints.
	const TArray<FVector> Jittered = { Along(0.0), Along(95.0), Along(6.2001), Along(20.0), Along(16.3002) };
	TestEqual(TEXT("Sub-display jitter does not change the signature"), SignatureFor(Jittered), SignatureFor(Frame1));

	// A real recalibration does, so the user gets the corrected figures.
	const TArray<FVector> Recalibrated = { Along(0.0), Along(95.0), Along(6.2), Along(20.0), Along(14.1) };
	TestNotEqual(TEXT("A recalibrated bone length changes the signature"), SignatureFor(Recalibrated), SignatureFor(Frame1));

	// And so does swapping to a different mesh with a different reference pose.
	const FString OtherMesh = FMOVINSkeletonDiagnostics::BuildReportSignature(TEXT("Ch14_Body"),
		FMOVINSkeletonDiagnostics::CompareBoneLengths(RefBones, StreamedNames, Frame1, PelvisExclusion()));
	TestNotEqual(TEXT("A different mesh changes the signature"), OtherMesh, SignatureFor(Frame1));

	// Two meshes bound to the same subject must be tracked separately. Holding one signature per
	// subject made the two flip-flop and re-raise the notification on every scan, which is what
	// made the message keep coming back.
	TMap<FString, FString> NotifiedPerMesh;
	auto WouldNotify = [&NotifiedPerMesh](const FString& MeshName, const FString& Signature)
	{
		FString& Stored = NotifiedPerMesh.FindOrAdd(MeshName);
		if (Stored == Signature)
		{
			return false;
		}
		Stored = Signature;
		return true;
	};

	TestTrue(TEXT("First mesh notifies"), WouldNotify(TEXT("MOVINman_V3_Puppet_UE"), SignatureFor(Frame1)));
	TestTrue(TEXT("Second mesh notifies"), WouldNotify(TEXT("Ch14_Body"), OtherMesh));
	for (int32 Scan = 0; Scan < 5; ++Scan)
	{
		TestFalse(TEXT("First mesh stays quiet on rescan"), WouldNotify(TEXT("MOVINman_V3_Puppet_UE"), SignatureFor(Frame2)));
		TestFalse(TEXT("Second mesh stays quiet on rescan"), WouldNotify(TEXT("Ch14_Body"), OtherMesh));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSkeletonActorSubjectOnlyTest, "MOVIN.SkeletonDiagnostics.ActorSubjectOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINSkeletonActorSubjectOnlyTest::RunTest(const FString& Parameters)
{
	// Only Actor streams carry a calibration offset. Character streams are named after the loaded
	// character and are already retargeted, so they must never raise the notification.
	TestTrue(TEXT("Actor subject is diagnosed"), FMOVINSkeletonDiagnostics::IsActorSubject(FName(TEXT("MOVINMan"))));
	TestTrue(TEXT("Subject matching ignores case"), FMOVINSkeletonDiagnostics::IsActorSubject(FName(TEXT("MOVINman"))));
	TestFalse(TEXT("Character subject is ignored"), FMOVINSkeletonDiagnostics::IsActorSubject(FName(TEXT("Ch14_nonPBR"))));
	TestFalse(TEXT("Empty subject is ignored"), FMOVINSkeletonDiagnostics::IsActorSubject(NAME_None));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
