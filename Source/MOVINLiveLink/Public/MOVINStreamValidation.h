// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

/**
 * Writes raw MOVIN stream validation packets received by the Unreal LiveLink source.
 *
 * The sender owns validation session creation by writing <sessionId>_App.
 * The receiver attaches to the newest App file and writes <sessionId>_Plugin
 * with the same packet header format for byte-level comparison.
 */
class MOVINLIVELINK_API FMOVINStreamValidation
{
public:

	FMOVINStreamValidation();
	~FMOVINStreamValidation();

	void WriteRawPacket(const TArray<uint8>& RawData);
	void BeginSession(const FString& InSessionId, const FString& InDirectory);
	void EndSession(const FString& InSessionId);
	void Tick();
	void Close();

	bool IsOpen() const;
	FString GetSessionId() const;
	FString GetLogPath() const;

	static FString GetDefaultLogDirectory();

private:

	bool TryAttachToLatestAppFileLocked();
	bool OpenSessionLocked(const FString& InSessionId, const FString& InDirectory);
	void CloseLocked();
	void WriteLineLocked(const FString& Line);
	void FlushLocked();

private:

	mutable FCriticalSection CriticalSection;
	TUniquePtr<FArchive> PacketWriter;
	FString SessionId;
	FString ClosedSessionId;
	FString LogDirectory;
	FString LogPath;
	int32 PacketIndex = 0;
	int32 LinesSinceFlush = 0;
	FDateTime LastFlushUtc;

	/** Last time the validation directory was scanned for the active _App file. */
	FDateTime LastAttachScanUtc;
};
