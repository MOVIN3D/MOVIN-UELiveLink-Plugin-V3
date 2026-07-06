// Copyright 2025 MOVIN. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MOVINDatagram.h"
#include "MOVINValidationProtocol.h"
#include "Misc/AutomationTest.h"
#include "Containers/StringConv.h"

namespace MOVINDatagramTestHelpers
{
	/** Encode an int as a C# BinaryWriter 7-bit length prefix. */
	static void Write7BitEncodedInt(TArray<uint8>& Out, int32 Value)
	{
		uint32 V = static_cast<uint32>(Value);
		while (V >= 0x80u)
		{
			Out.Add(static_cast<uint8>((V & 0x7Fu) | 0x80u));
			V >>= 7;
		}
		Out.Add(static_cast<uint8>(V));
	}

	/**
	 * Append the native byte representation so the bytes round-trip exactly through
	 * the parser's FMemory::Memcpy read on this platform (matches the wire format on
	 * the little-endian targets this plugin supports).
	 */
	static void WriteInt32(TArray<uint8>& Out, int32 Value)
	{
		Out.Append(reinterpret_cast<const uint8*>(&Value), sizeof(int32));
	}

	static void WriteFloat(TArray<uint8>& Out, float Value)
	{
		Out.Append(reinterpret_cast<const uint8*>(&Value), sizeof(float));
	}

	static void WriteString(TArray<uint8>& Out, const FString& Str)
	{
		FTCHARToUTF8 Utf8(*Str);
		Write7BitEncodedInt(Out, Utf8.Length());
		if (Utf8.Length() > 0)
		{
			Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
	}

	struct FTestBone
	{
		FString Name;
		float Pos[3];
		float Rot[4];
		float Scale[3];
	};

	static FTestBone MakeBone(const FString& Name,
		float PX, float PY, float PZ,
		float RX, float RY, float RZ, float RW,
		float SX, float SY, float SZ)
	{
		FTestBone Bone;
		Bone.Name = Name;
		Bone.Pos[0] = PX; Bone.Pos[1] = PY; Bone.Pos[2] = PZ;
		Bone.Rot[0] = RX; Bone.Rot[1] = RY; Bone.Rot[2] = RZ; Bone.Rot[3] = RW;
		Bone.Scale[0] = SX; Bone.Scale[1] = SY; Bone.Scale[2] = SZ;
		return Bone;
	}

	/** Wrap a datagram body with the 4-byte packet-size envelope the parser expects. */
	static TArray<uint8> WrapWithEnvelope(const TArray<uint8>& Body)
	{
		TArray<uint8> Packet;
		WriteInt32(Packet, Body.Num());
		Packet.Append(Body);
		return Packet;
	}

	static TArray<uint8> BuildMotionPacket(const FString& Subject, int32 FrameIdx, const TArray<FTestBone>& Bones)
	{
		TArray<uint8> Body;
		WriteString(Body, Subject);
		WriteInt32(Body, FrameIdx);
		WriteInt32(Body, Bones.Num());
		for (const FTestBone& Bone : Bones)
		{
			WriteString(Body, Bone.Name);
			WriteFloat(Body, Bone.Pos[0]);
			WriteFloat(Body, Bone.Pos[1]);
			WriteFloat(Body, Bone.Pos[2]);
			WriteFloat(Body, Bone.Rot[0]);
			WriteFloat(Body, Bone.Rot[1]);
			WriteFloat(Body, Bone.Rot[2]);
			WriteFloat(Body, Bone.Rot[3]);
			WriteFloat(Body, Bone.Scale[0]);
			WriteFloat(Body, Bone.Scale[1]);
			WriteFloat(Body, Bone.Scale[2]);
		}
		return WrapWithEnvelope(Body);
	}

	/** A motion packet that declares a bone count but writes no bone data. */
	static TArray<uint8> BuildHeaderOnlyPacket(const FString& Subject, int32 FrameIdx, int32 DeclaredBoneCount)
	{
		TArray<uint8> Body;
		WriteString(Body, Subject);
		WriteInt32(Body, FrameIdx);
		WriteInt32(Body, DeclaredBoneCount);
		return WrapWithEnvelope(Body);
	}

	static TArray<uint8> BuildControlPacket(int32 FrameIdx, const FString& SessionId,
		const FString& Target, int32 DurationSeconds, const FString& Directory)
	{
		TArray<uint8> Body;
		WriteString(Body, FString(MOVINValidationProtocol::ControlSubject));
		WriteInt32(Body, FrameIdx);
		WriteInt32(Body, 0); // boneCount must be 0 for control packets
		WriteString(Body, SessionId);
		WriteString(Body, Target);
		WriteInt32(Body, DurationSeconds);
		WriteString(Body, Directory);
		return WrapWithEnvelope(Body);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINDatagramValidMotionPacketTest, "MOVIN.Datagram.ValidMotionPacket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINDatagramValidMotionPacketTest::RunTest(const FString& Parameters)
{
	using namespace MOVINDatagramTestHelpers;

	TArray<FTestBone> Bones;
	Bones.Add(MakeBone(TEXT("Root"), 1.f, 2.f, 3.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f));
	Bones.Add(MakeBone(TEXT("Spine"), 10.f, 20.f, 30.f, 0.1f, 0.2f, 0.3f, 0.9f, 2.f, 3.f, 4.f));

	const TArray<uint8> Packet = BuildMotionPacket(TEXT("Actor01"), 42, Bones);

	FMOVINDatagram Datagram;
	TestTrue(TEXT("Parse should succeed"), FMOVINDatagramParser::Parse(Packet, Datagram));
	TestTrue(TEXT("Datagram is valid"), Datagram.bIsValid);
	TestFalse(TEXT("Not a control packet"), Datagram.bIsValidationControl);
	TestEqual(TEXT("Subject name"), Datagram.SubjectName, FString(TEXT("Actor01")));
	TestEqual(TEXT("Frame index"), Datagram.FrameIndex, 42);
	TestEqual(TEXT("Declared bone count"), Datagram.BoneCount, 2);
	TestEqual(TEXT("Parsed bone count"), Datagram.Bones.Num(), 2);

	if (Datagram.Bones.Num() == 2)
	{
		TestTrue(TEXT("Bone 0 name"), Datagram.Bones[0].BoneName == FName(TEXT("Root")));
		TestTrue(TEXT("Bone 1 name"), Datagram.Bones[1].BoneName == FName(TEXT("Spine")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINDatagramCoordinateConversionTest, "MOVIN.Datagram.CoordinateConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINDatagramCoordinateConversionTest::RunTest(const FString& Parameters)
{
	using namespace MOVINDatagramTestHelpers;

	// Unity (x, y, z) -> Unreal (z, x, y); the same swap applies to the quaternion
	// vector part with w unchanged.
	TArray<FTestBone> Bones;
	Bones.Add(MakeBone(TEXT("Joint"), 1.f, 2.f, 3.f, 0.1f, 0.2f, 0.3f, 0.4f, 5.f, 6.f, 7.f));

	const TArray<uint8> Packet = BuildMotionPacket(TEXT("Subj"), 1, Bones);

	FMOVINDatagram Datagram;
	TestTrue(TEXT("Parse should succeed"), FMOVINDatagramParser::Parse(Packet, Datagram));

	if (Datagram.Bones.Num() == 1)
	{
		const FMOVINJointData& Joint = Datagram.Bones[0];
		TestTrue(TEXT("Position axis swap (z,x,y)"), Joint.LocalPosition.Equals(FVector(3.f, 1.f, 2.f), 1e-4f));
		TestTrue(TEXT("Rotation axis swap (z,x,y,w)"), Joint.LocalRotation.Equals(FQuat(0.3f, 0.1f, 0.2f, 0.4f), 1e-4f));
		TestTrue(TEXT("Scale axis swap (z,x,y)"), Joint.LocalScale.Equals(FVector(7.f, 5.f, 6.f), 1e-4f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINDatagramTruncatedPacketTest, "MOVIN.Datagram.TruncatedPacket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINDatagramTruncatedPacketTest::RunTest(const FString& Parameters)
{
	using namespace MOVINDatagramTestHelpers;

	FMOVINDatagram Datagram;

	// Empty buffer
	{
		TArray<uint8> Empty;
		TestFalse(TEXT("Empty buffer rejected"), FMOVINDatagramParser::Parse(Empty, Datagram));
	}

	// Only a partial size field
	{
		TArray<uint8> Partial;
		Partial.Add(0x01);
		Partial.Add(0x02);
		TestFalse(TEXT("Partial size field rejected"), FMOVINDatagramParser::Parse(Partial, Datagram));
	}

	// Envelope claims more body than is actually present
	{
		TArray<uint8> Packet;
		WriteInt32(Packet, 1000); // PacketSize says 1000 body bytes...
		WriteString(Packet, TEXT("Subj")); // ...but only a few follow
		TestFalse(TEXT("Oversized declared packet size rejected"), FMOVINDatagramParser::Parse(Packet, Datagram));
	}

	// Valid header, but bone data truncated mid-stream
	{
		TArray<uint8> Body;
		WriteString(Body, TEXT("Subj"));
		WriteInt32(Body, 1);
		WriteInt32(Body, 2); // declares 2 bones
		WriteString(Body, TEXT("Bone0"));
		WriteFloat(Body, 1.f); // only a partial first bone follows
		const TArray<uint8> Packet = WrapWithEnvelope(Body);
		TestFalse(TEXT("Truncated bone data rejected"), FMOVINDatagramParser::Parse(Packet, Datagram));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINDatagramBoneCountLimitsTest, "MOVIN.Datagram.BoneCountLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINDatagramBoneCountLimitsTest::RunTest(const FString& Parameters)
{
	using namespace MOVINDatagramTestHelpers;

	FMOVINDatagram Datagram;

	// Negative bone count rejected
	{
		const TArray<uint8> Packet = BuildHeaderOnlyPacket(TEXT("Subj"), 1, -1);
		TestFalse(TEXT("Negative bone count rejected"), FMOVINDatagramParser::Parse(Packet, Datagram));
	}

	// Bone count above the 300 cap rejected at the header (before reading bones)
	{
		const TArray<uint8> Packet = BuildHeaderOnlyPacket(TEXT("Subj"), 1, 301);
		TestFalse(TEXT("Bone count over cap rejected"), FMOVINDatagramParser::Parse(Packet, Datagram));
	}

	// Declared count larger than the bones actually provided is rejected
	{
		TArray<uint8> Body;
		WriteString(Body, TEXT("Subj"));
		WriteInt32(Body, 1);
		WriteInt32(Body, 5); // claims 5 bones...
		WriteString(Body, TEXT("Only")); // ...but provides only 1 complete bone
		WriteFloat(Body, 0.f); WriteFloat(Body, 0.f); WriteFloat(Body, 0.f);
		WriteFloat(Body, 0.f); WriteFloat(Body, 0.f); WriteFloat(Body, 0.f); WriteFloat(Body, 1.f);
		WriteFloat(Body, 1.f); WriteFloat(Body, 1.f); WriteFloat(Body, 1.f);
		const TArray<uint8> Packet = WrapWithEnvelope(Body);
		TestFalse(TEXT("Bone count mismatch rejected"), FMOVINDatagramParser::Parse(Packet, Datagram));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINDatagramControlPacketTest, "MOVIN.Datagram.ValidationControlPacket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINDatagramControlPacketTest::RunTest(const FString& Parameters)
{
	using namespace MOVINDatagramTestHelpers;

	// A well-formed begin-session control packet
	{
		const TArray<uint8> Packet = BuildControlPacket(
			MOVINValidationProtocol::BeginSessionFrame,
			TEXT("session-123"), TEXT("Unreal_LiveLink"), 30, TEXT("C:/Temp/Validation"));

		FMOVINDatagram Datagram;
		TestTrue(TEXT("Parse should succeed"), FMOVINDatagramParser::Parse(Packet, Datagram));
		TestTrue(TEXT("Marked as control packet"), Datagram.bIsValidationControl);
		TestTrue(TEXT("Marked valid"), Datagram.bIsValid);
		TestEqual(TEXT("Frame index is begin sentinel"), Datagram.FrameIndex, MOVINValidationProtocol::BeginSessionFrame);
		TestEqual(TEXT("Bone count zero"), Datagram.BoneCount, 0);
		TestEqual(TEXT("Session id"), Datagram.ValidationSessionId, FString(TEXT("session-123")));
		TestEqual(TEXT("Target"), Datagram.ValidationTarget, FString(TEXT("Unreal_LiveLink")));
		TestEqual(TEXT("Duration"), Datagram.ValidationDurationSeconds, 30);
		TestEqual(TEXT("Directory"), Datagram.ValidationDirectory, FString(TEXT("C:/Temp/Validation")));
	}

	// The control subject with a non-sentinel frame index is rejected
	{
		const TArray<uint8> Packet = BuildControlPacket(
			12345, TEXT("session-123"), TEXT("Unreal_LiveLink"), 30, TEXT("C:/Temp/Validation"));

		FMOVINDatagram Datagram;
		TestFalse(TEXT("Non-sentinel control frame rejected"), FMOVINDatagramParser::Parse(Packet, Datagram));
	}

	// The control subject with a non-zero bone count is rejected
	{
		TArray<uint8> Body;
		WriteString(Body, FString(MOVINValidationProtocol::ControlSubject));
		WriteInt32(Body, MOVINValidationProtocol::BeginSessionFrame);
		WriteInt32(Body, 1); // illegal: control packets must declare 0 bones
		const TArray<uint8> Packet = WrapWithEnvelope(Body);

		FMOVINDatagram Datagram;
		TestFalse(TEXT("Control packet with bones rejected"), FMOVINDatagramParser::Parse(Packet, Datagram));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINDatagramMultiByteStringLengthTest, "MOVIN.Datagram.MultiByteStringLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMOVINDatagramMultiByteStringLengthTest::RunTest(const FString& Parameters)
{
	using namespace MOVINDatagramTestHelpers;

	// A 200-character subject name forces a 2-byte 7-bit length prefix (200 > 127),
	// exercising the multi-byte decode path in Read7BitEncodedInt.
	const FString LongName = FString::ChrN(200, TEXT('A'));

	TArray<FTestBone> Bones;
	Bones.Add(MakeBone(TEXT("Root"), 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f));

	const TArray<uint8> Packet = BuildMotionPacket(LongName, 7, Bones);

	FMOVINDatagram Datagram;
	TestTrue(TEXT("Parse should succeed"), FMOVINDatagramParser::Parse(Packet, Datagram));
	TestEqual(TEXT("Long subject name round-trips"), Datagram.SubjectName, LongName);
	TestEqual(TEXT("Subject name length"), Datagram.SubjectName.Len(), 200);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
