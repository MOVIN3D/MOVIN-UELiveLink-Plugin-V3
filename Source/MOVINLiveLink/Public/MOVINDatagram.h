// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Represents a single joint's data parsed from a MOVIN Studio packet.
 */
struct FMOVINJointData
{
	FName BoneName;
	FVector LocalPosition;
	FQuat LocalRotation;
	FVector LocalScale;

	FMOVINJointData()
		: BoneName(NAME_None)
		, LocalPosition(FVector::ZeroVector)
		, LocalRotation(FQuat::Identity)
		, LocalScale(FVector::OneVector)
	{
	}
};

/**
 * Represents a fully parsed MOVIN Studio datagram.
 *
 * Packet layout (all little-endian, strings use C# 7-bit encoded length prefix):
 *   [4 bytes] packetSize (int32, envelope)
 *   --- datagram body ---
 *   [1-5 bytes] subjectName length (7-bit encoded) + [N bytes] subjectName (UTF-8)
 *   [4 bytes] frameIdx (int32)
 *   [4 bytes] boneCount (int32)
 *   --- per bone (boneCount times) ---
 *     [1-5 bytes] boneName length (7-bit encoded) + [N bytes] boneName (UTF-8)
 *     [12 bytes] localPosition  (3x float: x, y, z)
 *     [16 bytes] localRotation  (4x float: x, y, z, w)
 *     [12 bytes] localScale     (3x float: x, y, z)
 */
struct FMOVINDatagram
{
	bool bIsValid;
	bool bIsValidationControl;
	FString SubjectName;
	int32 FrameIndex;
	int32 BoneCount;
	FString ValidationSessionId;
	FString ValidationTarget;
	int32 ValidationDurationSeconds;
	FString ValidationDirectory;
	TArray<FMOVINJointData> Bones;

	FMOVINDatagram()
		: bIsValid(false)
		, bIsValidationControl(false)
		, FrameIndex(0)
		, BoneCount(0)
		, ValidationDurationSeconds(0)
	{
	}
};

/**
 * Parser for MOVIN Studio binary UDP packets.
 * All data is little-endian (native x86 byte order).
 * Strings use C# BinaryWriter 7-bit encoded length prefix.
 */
class MOVINLIVELINK_API FMOVINDatagramParser
{
public:

	/**
	 * Parse raw UDP data into a MOVIN datagram.
	 * The input data should include the 4-byte packet size prefix.
	 * @param RawData  The raw bytes received from UDP
	 * @param OutDatagram  The parsed result
	 * @return true if parsing succeeded
	 */
	static bool Parse(const TArray<uint8>& RawData, FMOVINDatagram& OutDatagram);

private:

	/** Helper: read a little-endian int32 from buffer at Offset, advance Offset */
	static bool ReadInt32(const uint8* Data, int32 DataLen, int32& Offset, int32& OutValue);

	/** Helper: read a little-endian float from buffer at Offset, advance Offset */
	static bool ReadFloat(const uint8* Data, int32 DataLen, int32& Offset, float& OutValue);

	/** Helper: read a C# 7-bit encoded variable-length integer (used as string length prefix) */
	static bool Read7BitEncodedInt(const uint8* Data, int32 DataLen, int32& Offset, int32& OutValue);

	/** Helper: read a length-prefixed UTF-8 string (7-bit encoded length + N bytes, C# BinaryWriter format) */
	static bool ReadString(const uint8* Data, int32 DataLen, int32& Offset, FString& OutString);
};
