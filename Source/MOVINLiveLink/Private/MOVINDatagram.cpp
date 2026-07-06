// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINDatagram.h"
#include "MOVINValidationProtocol.h"

bool FMOVINDatagramParser::ReadInt32(const uint8* Data, int32 DataLen, int32& Offset, int32& OutValue)
{
	if (Offset + 4 > DataLen) return false;
	FMemory::Memcpy(&OutValue, Data + Offset, sizeof(int32));
	Offset += 4;
	return true;
}

bool FMOVINDatagramParser::ReadFloat(const uint8* Data, int32 DataLen, int32& Offset, float& OutValue)
{
	if (Offset + 4 > DataLen) return false;
	FMemory::Memcpy(&OutValue, Data + Offset, sizeof(float));
	Offset += 4;
	return true;
}

bool FMOVINDatagramParser::Read7BitEncodedInt(const uint8* Data, int32 DataLen, int32& Offset, int32& OutValue)
{
	OutValue = 0;
	int32 Shift = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		if (Offset >= DataLen) return false;
		uint8 Byte = Data[Offset++];
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
	if (StrLen < 0 || Offset + StrLen > DataLen) return false;

	if (StrLen == 0)
	{
		OutString = TEXT("");
		return true;
	}

	FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data + Offset), StrLen);
	OutString = FString(Converter.Length(), Converter.Get());
	Offset += StrLen;
	return true;
}

bool FMOVINDatagramParser::Parse(const TArray<uint8>& RawData, FMOVINDatagram& OutDatagram)
{
	OutDatagram = FMOVINDatagram();

	const uint8* Data = RawData.GetData();
	const int32 DataLen = RawData.Num();
	int32 Offset = 0;

	// Read packet size envelope (4 bytes int32)
	int32 PacketSize = 0;
	if (!ReadInt32(Data, DataLen, Offset, PacketSize)) return false;
	if (PacketSize <= 0 || Offset + PacketSize > DataLen) return false;

	// Read subjectName
	if (!ReadString(Data, DataLen, Offset, OutDatagram.SubjectName)) return false;

	// Read frameIdx
	if (!ReadInt32(Data, DataLen, Offset, OutDatagram.FrameIndex)) return false;

	// Read boneCount
	if (!ReadInt32(Data, DataLen, Offset, OutDatagram.BoneCount)) return false;

	if (OutDatagram.BoneCount < 0 || OutDatagram.BoneCount > 300) return false;

	if (OutDatagram.SubjectName == MOVINValidationProtocol::ControlSubject)
	{
		if (OutDatagram.BoneCount != 0) return false;
		if (OutDatagram.FrameIndex != MOVINValidationProtocol::BeginSessionFrame && OutDatagram.FrameIndex != MOVINValidationProtocol::EndSessionFrame) return false;
		if (!ReadString(Data, DataLen, Offset, OutDatagram.ValidationSessionId)) return false;
		if (!ReadString(Data, DataLen, Offset, OutDatagram.ValidationTarget)) return false;
		if (!ReadInt32(Data, DataLen, Offset, OutDatagram.ValidationDurationSeconds)) return false;
		if (!ReadString(Data, DataLen, Offset, OutDatagram.ValidationDirectory)) return false;

		OutDatagram.bIsValid = true;
		OutDatagram.bIsValidationControl = true;
		return true;
	}

	// Parse per-bone data
	OutDatagram.Bones.Reserve(OutDatagram.BoneCount);
	for (int32 i = 0; i < OutDatagram.BoneCount; ++i)
	{
		FMOVINJointData Joint;

		FString BoneNameStr;
		if (!ReadString(Data, DataLen, Offset, BoneNameStr)) return false;
		Joint.BoneName = FName(*BoneNameStr);

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

		// localScale (x, y, z)
		float ScaleX, ScaleY, ScaleZ;
		if (!ReadFloat(Data, DataLen, Offset, ScaleX)) return false;
		if (!ReadFloat(Data, DataLen, Offset, ScaleY)) return false;
		if (!ReadFloat(Data, DataLen, Offset, ScaleZ)) return false;
		// Unity(X,Y,Z) -> UE(Z,X,Y): Y-up to Z-up axis swap
		Joint.LocalScale = FVector(ScaleZ, ScaleX, ScaleY);

		OutDatagram.Bones.Add(MoveTemp(Joint));
	}

	if (OutDatagram.Bones.Num() != OutDatagram.BoneCount)
	{
		return false;
	}

	OutDatagram.bIsValid = true;
	return true;
}
