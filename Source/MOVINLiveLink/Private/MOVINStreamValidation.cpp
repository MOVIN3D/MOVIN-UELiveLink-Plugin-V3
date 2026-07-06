// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINStreamValidation.h"
#include "MOVINValidationProtocol.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* ValidationPacketHeader = TEXT("MOVIN_STREAM_VALIDATION_PACKET_V1");
	const TCHAR* ValidationPacketFormat = TEXT("base64_udp_datagram");
	const TCHAR* AppSuffix = TEXT("_App");
	const TCHAR* PluginSuffix = TEXT("_Plugin");
	const int32 FlushLineInterval = 512;
	const FTimespan FlushInterval = FTimespan::FromMilliseconds(500);

	// Re-scanning the validation directory to find the active _App file is expensive.
	// Once attached, only re-scan this often to notice a session switch.
	const FTimespan AttachScanInterval = FTimespan::FromMilliseconds(1000);
}

FMOVINStreamValidation::FMOVINStreamValidation()
	: LastFlushUtc(FDateTime::UtcNow())
	, LastAttachScanUtc(FDateTime::MinValue())
{
}

FMOVINStreamValidation::~FMOVINStreamValidation()
{
	Close();
}

void FMOVINStreamValidation::WriteRawPacket(const TArray<uint8>& RawData)
{
	FScopeLock Lock(&CriticalSection);

	const FDateTime Now = FDateTime::UtcNow();

	// Discovering the active _App file requires a directory scan, which is too
	// expensive to run on every packet at 60 fps. While unattached we scan each
	// packet so a new session is picked up immediately (no leading packets lost);
	// once attached we only re-scan periodically to notice a session switch.
	if (!PacketWriter.IsValid() || Now - LastAttachScanUtc >= AttachScanInterval)
	{
		LastAttachScanUtc = Now;
		TryAttachToLatestAppFileLocked();
	}

	if (!PacketWriter)
	{
		return;
	}

	WriteLineLocked(FString::Printf(TEXT("%06d|%s"), PacketIndex, *FBase64::Encode(RawData.GetData(), RawData.Num())));
	PacketIndex++;
	LinesSinceFlush++;

	if (LinesSinceFlush >= FlushLineInterval || Now - LastFlushUtc >= FlushInterval)
	{
		FlushLocked();
	}
}

void FMOVINStreamValidation::BeginSession(const FString& InSessionId, const FString& InDirectory)
{
	FScopeLock Lock(&CriticalSection);
	const FString Directory = InDirectory.IsEmpty() ? GetDefaultLogDirectory() : InDirectory;
	if (InSessionId.IsEmpty() || Directory.IsEmpty())
	{
		return;
	}

	ClosedSessionId.Empty();
	OpenSessionLocked(InSessionId, Directory);
}

void FMOVINStreamValidation::EndSession(const FString& InSessionId)
{
	FScopeLock Lock(&CriticalSection);
	if (InSessionId.IsEmpty() || SessionId.IsEmpty() || SessionId == InSessionId)
	{
		const FString SessionToClose = InSessionId.IsEmpty() ? SessionId : InSessionId;
		CloseLocked();
		ClosedSessionId = SessionToClose;
	}
}

void FMOVINStreamValidation::Tick()
{
	FScopeLock Lock(&CriticalSection);
	if (PacketWriter && FDateTime::UtcNow() - LastFlushUtc >= FlushInterval)
	{
		FlushLocked();
	}
}

void FMOVINStreamValidation::Close()
{
	FScopeLock Lock(&CriticalSection);
	CloseLocked();
}

bool FMOVINStreamValidation::IsOpen() const
{
	FScopeLock Lock(&CriticalSection);
	return PacketWriter.IsValid();
}

FString FMOVINStreamValidation::GetSessionId() const
{
	FScopeLock Lock(&CriticalSection);
	return SessionId;
}

FString FMOVINStreamValidation::GetLogPath() const
{
	FScopeLock Lock(&CriticalSection);
	return LogPath;
}

FString FMOVINStreamValidation::GetDefaultLogDirectory()
{
	FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (UserProfile.IsEmpty())
	{
		const FString UserName = FPlatformMisc::GetEnvironmentVariable(TEXT("USERNAME"));
		if (!UserName.IsEmpty())
		{
			UserProfile = FPaths::Combine(TEXT("C:/Users"), UserName);
		}
	}

	if (!UserProfile.IsEmpty())
	{
		return FPaths::Combine(UserProfile, TEXT("Documents"), TEXT("MOVIN Studio"), TEXT("StreamValidation"), MOVINValidationProtocol::Target);
	}

	return FString();
}

bool FMOVINStreamValidation::TryAttachToLatestAppFileLocked()
{
	const FString Directory = GetDefaultLogDirectory();
	if (Directory.IsEmpty())
	{
		return PacketWriter.IsValid();
	}

	TArray<FString> AppFileNames;
	IFileManager::Get().FindFiles(AppFileNames, *FPaths::Combine(Directory, TEXT("*_App")), true, false);
	if (AppFileNames.Num() == 0)
	{
		return PacketWriter.IsValid();
	}

	FString LatestPath;
	FDateTime LatestTimestamp = FDateTime::MinValue();
	for (const FString& AppFileName : AppFileNames)
	{
		const FString AppPath = FPaths::Combine(Directory, AppFileName);
		const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*AppPath);
		if (LatestPath.IsEmpty() || Timestamp > LatestTimestamp)
		{
			LatestPath = AppPath;
			LatestTimestamp = Timestamp;
		}
	}

	FString LatestSessionId = FPaths::GetCleanFilename(LatestPath);
	if (!LatestSessionId.RemoveFromEnd(AppSuffix))
	{
		return PacketWriter.IsValid();
	}

	if (!ClosedSessionId.IsEmpty() && LatestSessionId == ClosedSessionId)
	{
		return false;
	}

	if (PacketWriter.IsValid() && SessionId == LatestSessionId && LogDirectory == Directory)
	{
		return true;
	}

	return OpenSessionLocked(LatestSessionId, Directory);
}

bool FMOVINStreamValidation::OpenSessionLocked(const FString& InSessionId, const FString& InDirectory)
{
	CloseLocked();

	IFileManager::Get().MakeDirectory(*InDirectory, true);
	const FString RawPath = FPaths::Combine(InDirectory, FString::Printf(TEXT("%s%s"), *InSessionId, PluginSuffix));
	PacketWriter.Reset(IFileManager::Get().CreateFileWriter(*RawPath, FILEWRITE_AllowRead));
	if (!PacketWriter)
	{
		SessionId.Empty();
		LogDirectory.Empty();
		LogPath.Empty();
		return false;
	}

	SessionId = InSessionId;
	LogDirectory = InDirectory;
	LogPath = RawPath;
	PacketIndex = 0;
	LinesSinceFlush = 0;
	LastFlushUtc = FDateTime::UtcNow();

	WriteLineLocked(FString(ValidationPacketHeader));
	WriteLineLocked(FString::Printf(TEXT("session=%s"), *SessionId));
	WriteLineLocked(FString::Printf(TEXT("target=%s"), MOVINValidationProtocol::Target));
	WriteLineLocked(FString::Printf(TEXT("packet_format=%s"), ValidationPacketFormat));
	FlushLocked();
	return true;
}

void FMOVINStreamValidation::CloseLocked()
{
	if (PacketWriter)
	{
		FlushLocked();
		PacketWriter->Close();
		PacketWriter.Reset();
	}

	SessionId.Empty();
	LogDirectory.Empty();
	LogPath.Empty();
	PacketIndex = 0;
	LinesSinceFlush = 0;
}

void FMOVINStreamValidation::WriteLineLocked(const FString& Line)
{
	if (!PacketWriter)
	{
		return;
	}

	const FString LineWithNewline = Line + TEXT("\n");
	FTCHARToUTF8 Utf8(*LineWithNewline);
	PacketWriter->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
}

void FMOVINStreamValidation::FlushLocked()
{
	if (PacketWriter)
	{
		PacketWriter->Flush();
	}

	LinesSinceFlush = 0;
	LastFlushUtc = FDateTime::UtcNow();
}
