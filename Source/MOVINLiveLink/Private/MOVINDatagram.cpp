// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINDatagram.h"
#include <cstring>
#if MOVIN_STREAM_VALIDATION
#include "MOVINValidationProtocol.h"
#endif

bool FMOVINDatagramParser::ReadInt32(const uint8* Data, int32 DataLen, int32& Offset, int32& OutValue)
{
	if (DataLen - Offset < 4) return false;
	FMemory::Memcpy(&OutValue, Data + Offset, sizeof(int32));
	Offset += 4;
	return true;
}

bool FMOVINDatagramParser::ReadFloat(const uint8* Data, int32 DataLen, int32& Offset, float& OutValue)
{
	if (DataLen - Offset < 4) return false;
	FMemory::Memcpy(&OutValue, Data + Offset, sizeof(float));
	Offset += 4;
	return FMath::IsFinite(OutValue);
}

bool FMOVINDatagramParser::Read7BitEncodedInt(const uint8* Data, int32 DataLen, int32& Offset, int32& OutValue)
{
	OutValue = 0;
	int32 Shift = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		if (Offset >= DataLen) return false;
		uint8 Byte = Data[Offset++];
		if (i == 4 && (Byte & 0xF8) != 0) return false;
		OutValue |= static_cast<int32>(Byte & 0x7F) << Shift;
		if ((Byte & 0x80) == 0) return true;
		Shift += 7;
	}
	return false;
}

bool FMOVINDatagramParser::ReadString(const uint8* Data, int32 DataLen, int32& Offset, FString& OutString)
{
	int32 StrLen = 0;
	if (!Read7BitEncodedInt(Data, DataLen, Offset, StrLen)) return false;
	if (StrLen < 0 || StrLen > 1024 || StrLen > DataLen - Offset) return false;

	if (StrLen == 0)
	{
		OutString = TEXT("");
		return true;
	}

	if (std::memchr(Data + Offset, 0, StrLen) != nullptr) return false;
	FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data + Offset), StrLen);
	OutString = FString(Converter.Length(), Converter.Get());
	FTCHARToUTF8 encoded(*OutString);
	if (encoded.Length() != StrLen || FMemory::Memcmp(encoded.Get(), Data + Offset, StrLen) != 0) return false;
	Offset += StrLen;
	return true;
}

bool FMOVINDatagramParser::Parse(const TArray<uint8>& RawData, FMOVINDatagram& OutDatagram)
{
	OutDatagram = FMOVINDatagram();

	const uint8* Data = RawData.GetData();
	const int32 DataLen = RawData.Num();
	if (DataLen > 65507) return false;
	int32 Offset = 0;

	// Read packet size envelope (4 bytes int32)
	int32 PacketSize = 0;
	if (!ReadInt32(Data, DataLen, Offset, PacketSize)) return false;
	if (PacketSize <= 0 || PacketSize != DataLen - Offset) return false;

	// Read subjectName
	if (!ReadString(Data, DataLen, Offset, OutDatagram.SubjectName)) return false;
	if (OutDatagram.SubjectName.IsEmpty() || OutDatagram.SubjectName.Len() > 256 || OutDatagram.SubjectName.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return false;

	// Read frameIdx
	if (!ReadInt32(Data, DataLen, Offset, OutDatagram.FrameIndex)) return false;

	// Read boneCount
	if (!ReadInt32(Data, DataLen, Offset, OutDatagram.BoneCount)) return false;

	if (OutDatagram.BoneCount < 0 || OutDatagram.BoneCount > 300) return false;

#if MOVIN_STREAM_VALIDATION
	if (OutDatagram.SubjectName == MOVINValidationProtocol::ControlSubject)
	{
		if (OutDatagram.BoneCount != 0) return false;
		if (OutDatagram.FrameIndex != MOVINValidationProtocol::BeginSessionFrame && OutDatagram.FrameIndex != MOVINValidationProtocol::EndSessionFrame) return false;
		if (!ReadString(Data, DataLen, Offset, OutDatagram.ValidationSessionId)) return false;
		if (!ReadString(Data, DataLen, Offset, OutDatagram.ValidationTarget)) return false;
		if (!ReadInt32(Data, DataLen, Offset, OutDatagram.ValidationDurationSeconds)) return false;
		if (!ReadString(Data, DataLen, Offset, OutDatagram.ValidationDirectory)) return false;

		if (Offset != DataLen || !OutDatagram.ValidationTarget.Equals(MOVINValidationProtocol::Target, ESearchCase::IgnoreCase)) return false;
		OutDatagram.bIsValid = true;
		OutDatagram.bIsValidationControl = true;
		return true;
	}

#endif
	if (OutDatagram.BoneCount == 0) return false;
#if !MOVIN_STREAM_VALIDATION
	if (OutDatagram.FrameIndex < 0) return false;
#endif
	TSet<FName> names;
	// Parse per-bone data
	OutDatagram.Bones.Reserve(OutDatagram.BoneCount);
	for (int32 i = 0; i < OutDatagram.BoneCount; ++i)
	{
		FMOVINJointData Joint;

		FString BoneNameStr;
		if (!ReadString(Data, DataLen, Offset, BoneNameStr)) return false;
		if (BoneNameStr.IsEmpty() || BoneNameStr.Len() > 256) return false;
		Joint.wire_name = BoneNameStr;
		Joint.BoneName = FName(*BoneNameStr);
		if (Joint.BoneName.IsNone() || names.Contains(Joint.BoneName)) return false;
		names.Add(Joint.BoneName);

		// localPosition (x, y, z)
		float PosX, PosY, PosZ;
		if (!ReadFloat(Data, DataLen, Offset, PosX)) return false;
		if (!ReadFloat(Data, DataLen, Offset, PosY)) return false;
		if (!ReadFloat(Data, DataLen, Offset, PosZ)) return false;
		// Unity(X,Y,Z) -> UE(Z,X,Y): Y-up to Z-up axis swap
		Joint.LocalPosition = FVector(PosZ, PosX, PosY);

		// localRotation (x, y, z, w)
		float RotX, RotY, RotZ, RotW;
		if (!ReadFloat(Data, DataLen, Offset, RotX)) return false;
		if (!ReadFloat(Data, DataLen, Offset, RotY)) return false;
		if (!ReadFloat(Data, DataLen, Offset, RotZ)) return false;
		if (!ReadFloat(Data, DataLen, Offset, RotW)) return false;
		// Unity(X,Y,Z,W) -> UE(Z,X,Y,W): Y-up to Z-up axis swap
		Joint.LocalRotation = FQuat(RotZ, RotX, RotY, RotW);
		if (Joint.LocalRotation.SizeSquared() < SMALL_NUMBER) return false;
		Joint.LocalRotation.Normalize();

		// localScale (x, y, z)
		float ScaleX, ScaleY, ScaleZ;
		if (!ReadFloat(Data, DataLen, Offset, ScaleX)) return false;
		if (!ReadFloat(Data, DataLen, Offset, ScaleY)) return false;
		if (!ReadFloat(Data, DataLen, Offset, ScaleZ)) return false;
		// Unity(X,Y,Z) -> UE(Z,X,Y): Y-up to Z-up axis swap
		Joint.LocalScale = FVector(ScaleZ, ScaleX, ScaleY);

		OutDatagram.Bones.Add(MoveTemp(Joint));
	}

	if (Offset != DataLen || OutDatagram.Bones.Num() != OutDatagram.BoneCount)
	{
		return false;
	}

	OutDatagram.bIsValid = true;
	return true;
}
