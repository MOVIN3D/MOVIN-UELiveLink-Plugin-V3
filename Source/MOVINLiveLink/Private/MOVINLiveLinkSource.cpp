// Copyright 2025 MOVIN. All Rights Reserved.

#include "MOVINLiveLinkSource.h"
#include "MOVINLiveLinkModule.h"
#include "MOVINActorMesh.h"
#include "MOVINSkeletonDiagnostics.h"
#if MOVIN_STREAM_VALIDATION
#include "MOVINStreamValidation.h"
#endif
#include "MOVINStreamStatus.h"
#if MOVIN_STREAM_VALIDATION
#include "MOVINValidationProtocol.h"
#endif
#include "ILiveLinkClient.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Templates/Atomic.h"

#define LOCTEXT_NAMESPACE "MOVINLiveLinkSource"

FCriticalSection FMOVINLiveLinkSource::ActivePortsCriticalSection;
TSet<int32> FMOVINLiveLinkSource::ActivePorts;
static TAtomic<int32> GMOVINLiveLinkThreadCounter(0);
static constexpr double GMOVINExpectedReceiveFps = 60.0;
static constexpr double GMOVINReceiveFpsWarning = 57.0;
static constexpr double GMOVINReceiveFpsCritical = 50.0;
static FCriticalSection GMOVINInvalidDatagramLogCriticalSection;
static double GMOVINLastInvalidDatagramLogTimeSeconds = 0.0;

FMOVINLiveLinkSource::FMOVINLiveLinkSource(int32 InPort)
	: Port(InPort)
	, bStopping(false)
	, WaitTime(FTimespan::FromMilliseconds(100))
	, SourceStatus(LOCTEXT("SourceStatus_NotConnected", "Not Connected"))
#if MOVIN_STREAM_VALIDATION
	, StreamValidation(MakeUnique<FMOVINStreamValidation>())
#endif
{
	bPortRegistered = TryRegisterPort(Port);
	if (!bPortRegistered)
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_DuplicatePort", "Port {0} is already in use by another MOVIN source"), FText::AsNumber(Port)));
	}
	else
	{
		SetSourceStatus(SourceStatus);
	}
}

FMOVINLiveLinkSource::~FMOVINLiveLinkSource()
{
	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	if (Socket != nullptr)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}

	if (bPortRegistered)
	{
		UnregisterPort(Port);
		bPortRegistered = false;
	}
}

bool FMOVINLiveLinkSource::IsPortInUse(int32 InPort)
{
	FScopeLock Lock(&ActivePortsCriticalSection);
	return ActivePorts.Contains(InPort);
}

bool FMOVINLiveLinkSource::TryRegisterPort(int32 InPort)
{
	FScopeLock Lock(&ActivePortsCriticalSection);
	if (InPort < 1 || InPort > 65535 || ActivePorts.Contains(InPort))
	{
		return false;
	}

	ActivePorts.Add(InPort);
	return true;
}

void FMOVINLiveLinkSource::UnregisterPort(int32 InPort)
{
	FScopeLock Lock(&ActivePortsCriticalSection);
	ActivePorts.Remove(InPort);
}

void FMOVINLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	check(Client == nullptr && Thread == nullptr && Socket == nullptr);
	check(InClient != nullptr);
	Client = InClient;
	SourceGuid = InSourceGuid;
	if (Client != nullptr)
	{
		SubjectRemovedDelegateHandle = Client->OnLiveLinkSubjectRemoved().AddRaw(this, &FMOVINLiveLinkSource::HandleSubjectRemoved);
	}

	if (!bPortRegistered)
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_DuplicatePort", "Port {0} is already in use by another MOVIN source"), FText::AsNumber(Port)));
		return;
	}

	// Create socket and start listening
	FIPv4Endpoint Endpoint(FIPv4Address::Any, Port);
	int32 BufferSize = 2 * 1024 * 1024; // 2 MB receive buffer

	Socket = FUdpSocketBuilder(TEXT("MOVINLiveLinkSocket"))
		.AsNonBlocking()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(BufferSize);

	if (Socket == nullptr)
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_SocketError", "Socket Error (Port {0})"), FText::AsNumber(Port)));
		return;
	}

	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_Listening", "Listening on port {0}"), FText::AsNumber(Port)));

	StartThread();
}

bool FMOVINLiveLinkSource::IsSourceStillValid() const
{
	return !bStopping && bPortRegistered && Thread != nullptr && Socket != nullptr;
}

bool FMOVINLiveLinkSource::RequestSourceShutdown()
{
	Stop();
	return true;
}

FText FMOVINLiveLinkSource::GetSourceType() const
{
	return LOCTEXT("SourceType", "MOVIN LiveLink");
}

FText FMOVINLiveLinkSource::GetSourceStatus() const
{
	if (SourceStatus.ToString().IsEmpty())
	{
		return FText::Format(LOCTEXT("SourceStatus_Fallback", "MOVIN source on port {0}"), FText::AsNumber(Port));
	}

	return SourceStatus;
}

FText FMOVINLiveLinkSource::GetSourceMachineName() const
{
	return FText::Format(LOCTEXT("SourceMachineNameWithStatus", "MOVIN Studio - {0}"), GetSourceStatus());
}

void FMOVINLiveLinkSource::Update()
{
#if MOVIN_STREAM_VALIDATION
	if (StreamValidation) { StreamValidation->Tick(); }
#endif

	if (bPortRegistered && Socket != nullptr && !bStopping)
	{
		UpdateReceiveStatus();
		send_status();
	}

	// Deferred here because the scan walks Skeletal Mesh components, which is game thread only.
	TArray<FMOVINJointData> bones;
	FName name;
	{
		FScopeLock lock(&CriticalSection);
		bones = MoveTemp(pending_bones);
		pending_bones.Empty();
		name = pending_subject;
	}
	// Subject selection is game-thread-only on older supported engine versions.
	if (!bones.IsEmpty() && Client->IsSubjectEnabled(FLiveLinkSubjectKey(SourceGuid, name), false)) {
		FMOVINSkeletonDiagnostics::NoteSkeleton(SourceGuid, name, bones);
	}
	FMOVINSkeletonDiagnostics::Tick();

	// Same deferral, and it has to run after the diagnostics tick: the fit reads the calibration the
	// tracker maintains.
	FMOVINActorMesh::Tick();
}

// FRunnable interface

bool FMOVINLiveLinkSource::Init()
{
	return true;
}

void FMOVINLiveLinkSource::StartThread()
{
	FString ThreadName = TEXT("MOVINLiveLink UDP Receiver ");
	ThreadName.AppendInt(++GMOVINLiveLinkThreadCounter);
	Thread = FRunnableThread::Create(this, *ThreadName, 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask());
    if (Thread == nullptr) {
        UE_LOG(LogMOVINLiveLink, Error, TEXT("Could not start receiver thread on port %d"), Port);
        Stop();
    }
}

uint32 FMOVINLiveLinkSource::Run()
{
	TSharedRef<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();

	while (!bStopping)
	{
		if (!Socket->Wait(ESocketWaitConditions::WaitForRead, WaitTime))
		{
			continue;
		}

		uint32 PendingDataSize;
		while (!bStopping && Socket->HasPendingData(PendingDataSize))
		{
			TArray<uint8> RecvBuffer;
			RecvBuffer.SetNumUninitialized(FMath::Min(PendingDataSize, 65507u));

			int32 BytesRead = 0;
			if (Socket->RecvFrom(RecvBuffer.GetData(), RecvBuffer.Num(), BytesRead, *Sender))
			{
				if (BytesRead > 0)
				{
					RecvBuffer.SetNum(BytesRead);
					ProcessReceivedData(RecvBuffer, *Sender);
				}
			}
		}
	}

	return 0;
}

void FMOVINLiveLinkSource::Stop()
{
	UnregisterClientDelegates();

	bStopping = true;

    if (Thread != nullptr) {
        auto* thread = Thread;
        // FRunnableThread's destructor calls Stop() again.
        Thread = nullptr;
        thread->WaitForCompletion();
        delete thread;
    }
    if (Socket != nullptr) { Socket->Close(); }
    FMOVINSkeletonDiagnostics::forget_source(SourceGuid);
    if (bPortRegistered) {
        UnregisterPort(Port);
        bPortRegistered = false;
    }
#if MOVIN_STREAM_VALIDATION
    if (StreamValidation) { StreamValidation->Close(); }
#endif
}

void FMOVINLiveLinkSource::NotePacketReceived()
{
	FScopeLock Lock(&StatsCriticalSection);
	TotalPacketsReceived++;
	LastPacketTimeSeconds = FPlatformTime::Seconds();
}

void FMOVINLiveLinkSource::SetSourceStatus(const FText& InStatus)
{
	SourceStatus = InStatus;

	const FString StatusString = SourceStatus.ToString();
	FString StatusLogKey = StatusString;
	if (StatusString.StartsWith(TEXT("Receiving ")))
	{
		if (StatusString.Contains(TEXT("critical")))
		{
			StatusLogKey = TEXT("ReceivingCritical");
		}
		else if (StatusString.Contains(TEXT("warning")))
		{
			StatusLogKey = TEXT("ReceivingWarning");
		}
		else
		{
			StatusLogKey = TEXT("Receiving");
		}
	}
	else if (StatusString.StartsWith(TEXT("Listening ")))
	{
		StatusLogKey = TEXT("Listening");
	}
	else if (StatusString.StartsWith(TEXT("Waiting ")))
	{
		StatusLogKey = TEXT("Waiting");
	}

	if (StatusLogKey != LastLoggedSourceStatusKey)
	{
		LastLoggedSourceStatusKey = StatusLogKey;
		UE_LOG(LogMOVINLiveLink, Log, TEXT("Source status: %s"), *StatusString);
	}
}

void FMOVINLiveLinkSource::HandleSubjectRemoved(FLiveLinkSubjectKey RemovedSubjectKey)
{
	if (RemovedSubjectKey.Source != SourceGuid)
	{
		return;
	}

	bool bClearedCachedSubject = false;
	{
		FScopeLock Lock(&CriticalSection);
		bClearedCachedSubject |= SubjectBoneCounts.Remove(RemovedSubjectKey.SubjectName) > 0;
		bClearedCachedSubject |= SubjectBoneNames.Remove(RemovedSubjectKey.SubjectName) > 0;
		bClearedCachedSubject |= SubjectSkeletonRevisions.Remove(RemovedSubjectKey.SubjectName) > 0;
		bClearedCachedSubject |= SubjectSkeletonReady.Remove(RemovedSubjectKey.SubjectName) > 0;
	}

	if (bClearedCachedSubject)
	{
		UE_LOG(LogMOVINLiveLink, Log, TEXT("Subject '%s': Removed from LiveLink - cleared local skeleton cache for automatic recreation"),
			*RemovedSubjectKey.SubjectName.ToString());
	}
}

void FMOVINLiveLinkSource::UnregisterClientDelegates()
{
	if (Client != nullptr && SubjectRemovedDelegateHandle.IsValid())
	{
		Client->OnLiveLinkSubjectRemoved().Remove(SubjectRemovedDelegateHandle);
		SubjectRemovedDelegateHandle.Reset();
	}
}

void FMOVINLiveLinkSource::UpdateReceiveStatus()
{
	const double Now = FPlatformTime::Seconds();
	uint64 PacketCount = 0;
	double LastPacketSeconds = 0.0;
	double CurrentFps = 0.0;
	bool bHasSample = false;

	{
		FScopeLock Lock(&StatsCriticalSection);
		PacketCount = TotalPacketsReceived;
		LastPacketSeconds = LastPacketTimeSeconds;

		if (LastReceiveRateSampleTimeSeconds <= 0.0)
		{
			LastReceiveRateSampleTimeSeconds = Now;
			LastReceiveRateSamplePackets = PacketCount;
		}
		else
		{
			const double DeltaSeconds = Now - LastReceiveRateSampleTimeSeconds;
			if (DeltaSeconds >= 0.25)
			{
				ReceiveFps = static_cast<double>(PacketCount - LastReceiveRateSamplePackets) / DeltaSeconds;
				LastReceiveRateSampleTimeSeconds = Now;
				LastReceiveRateSamplePackets = PacketCount;
                published_fps = static_cast<double>(published_frames - sampled_published) / DeltaSeconds;
                sampled_published = published_frames;
				bHasReceiveRateSample = true;
			}
		}

		CurrentFps = ReceiveFps;
		bHasSample = bHasReceiveRateSample;
	}

	if (PacketCount == 0 || LastPacketSeconds <= 0.0)
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_ListeningWithFps", "Listening on port {0} (expected {1} fps)"),
			FText::AsNumber(Port),
			FText::AsNumber(GMOVINExpectedReceiveFps)));
		return;
	}

	const double PacketAgeSeconds = Now - LastPacketSeconds;
	if (PacketAgeSeconds >= 1.0)
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_WaitingWithFps", "Waiting on port {0} (last packet {1}s ago)"),
			FText::AsNumber(Port),
			FText::AsNumber(PacketAgeSeconds)));
		return;
	}

	if (!bHasSample)
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_ReceivingNoSample", "Receiving on port {0}"),
			FText::AsNumber(Port)));
		return;
	}

	if (CurrentFps < GMOVINReceiveFpsCritical)
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_ReceiveFpsCritical", "Receiving {0} fps on port {1} (critical, expected 60)"),
			FText::AsNumber(CurrentFps),
			FText::AsNumber(Port)));
	}
	else if (CurrentFps < GMOVINReceiveFpsWarning)
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_ReceiveFpsWarning", "Receiving {0} fps on port {1} (warning, expected 60)"),
			FText::AsNumber(CurrentFps),
			FText::AsNumber(Port)));
	}
	else
	{
		SetSourceStatus(FText::Format(LOCTEXT("SourceStatus_ReceiveFpsOk", "Receiving {0} fps on port {1}"),
			FText::AsNumber(CurrentFps),
			FText::AsNumber(Port)));
	}
}

// Data processing

void FMOVINLiveLinkSource::ProcessReceivedData(const TArray<uint8>& RawData, const FInternetAddr& sender)
{
    const double now = FPlatformTime::Seconds();
    FString token;
    int32 reply_port = 0, motion_port = 0;
    if (MOVINStreamStatus::read_request(RawData, token, reply_port, motion_port)) {
        FScopeLock lock(&StatsCriticalSection);
        request = token;
        reply_to = sender.Clone();
        reply_to->SetPort(motion_port);
        requested_sender = reply_to->ToString(true);
        reply_to->SetPort(reply_port);
        requested_at = now;
        return;
    }
	FMOVINDatagram Datagram;
	if (!FMOVINDatagramParser::Parse(RawData, Datagram))
	{
        FScopeLock lock(&StatsCriticalSection);
        errors = FMath::Min(errors, MAX_int32 - 1) + 1;
        if (now - last_error_log >= 2) {
            last_error_log = now;
            UE_LOG(LogMOVINLiveLink, Warning, TEXT("Rejected invalid UDP datagram (%d bytes); total errors: %d"), RawData.Num(), errors);
        }
		return;
	}

#if MOVIN_STREAM_VALIDATION
	if (Datagram.bIsValidationControl)
	{
		if (StreamValidation)
		{
			if (Datagram.FrameIndex == MOVINValidationProtocol::BeginSessionFrame)
			{
				StreamValidation->BeginSession(Datagram.ValidationSessionId, Datagram.ValidationDirectory);
				UE_LOG(LogMOVINLiveLink, Log, TEXT("Stream validation begin: session '%s'"),
					*Datagram.ValidationSessionId);
			}
			else if (Datagram.FrameIndex == MOVINValidationProtocol::EndSessionFrame)
			{
				StreamValidation->EndSession(Datagram.ValidationSessionId);
				UE_LOG(LogMOVINLiveLink, Log, TEXT("Stream validation end: session '%s'"),
					*Datagram.ValidationSessionId);
			}
		}

		return;
	}

	if (Datagram.FrameIndex < 0 && StreamValidation)
	{
		StreamValidation->WriteRawPacket(RawData);
	}

#endif
	const int32 ParsedBoneCount = Datagram.Bones.Num();
	if (!Datagram.bIsValid || Datagram.BoneCount <= 0 || ParsedBoneCount <= 0 || ParsedBoneCount != Datagram.BoneCount)
	{
		const double Now = FPlatformTime::Seconds();
		bool bShouldLog = false;
		{
			FScopeLock Lock(&GMOVINInvalidDatagramLogCriticalSection);
			if (Now - GMOVINLastInvalidDatagramLogTimeSeconds >= 2.0)
			{
				GMOVINLastInvalidDatagramLogTimeSeconds = Now;
				bShouldLog = true;
			}
		}

		if (bShouldLog)
		{
			UE_LOG(LogMOVINLiveLink, Warning,
				TEXT("Received invalid or incomplete datagram for subject '%s' (Valid=%d, DeclaredBoneCount=%d, ParsedBones=%d)"),
				*Datagram.SubjectName, Datagram.bIsValid, Datagram.BoneCount, ParsedBoneCount);
		}
		return;
	}

    FString previous;
    {
        FScopeLock lock(&StatsCriticalSection);
        const int32 frame = Datagram.FrameIndex < 0 ? -(Datagram.FrameIndex + 1) : Datagram.FrameIndex;
        if (!order.accept(frame, sender.ToString(true), now)) { return; }
        previous = subject;
        subject = Datagram.SubjectName;
        received_bones = Datagram.BoneCount;
        signature = MOVINStreamStatus::bone_signature(Datagram.Bones);
    }
    NotePacketReceived();
    if (!previous.IsEmpty() && !previous.Equals(Datagram.SubjectName, ESearchCase::IgnoreCase)) {
        FMOVINSkeletonDiagnostics::forget_source(SourceGuid);
        {
            FScopeLock lock(&CriticalSection);
            SubjectBoneCounts.Empty(); SubjectBoneNames.Empty();
            SubjectSkeletonRevisions.Empty(); SubjectSkeletonReady.Empty();
            pending_bones.Empty();
        }
        Client->RemoveSubject_AnyThread(FLiveLinkSubjectKey(SourceGuid, FName(*previous)));
    }
	// Use SubjectName from the packet directly as the LiveLink subject name
	const FName SubjectName(*Datagram.SubjectName);
	const FLiveLinkSubjectKey SubjectKey(SourceGuid, SubjectName);

	// Update static data if skeleton changed
	const bool bSkeletonChanged = UpdateStaticData(Datagram, SubjectName);
	int32 SkeletonRevision = 0;
	bool bSkeletonReady = false;
	{
		FScopeLock Lock(&CriticalSection);
		SkeletonRevision = SubjectSkeletonRevisions.FindRef(SubjectName);
		bSkeletonReady = SubjectSkeletonReady.FindRef(SubjectName);
	}

	// Skip frame data on the same packet where the skeleton changed.
	// LiveLink needs one tick to commit the new static data before accepting
	// frames with the new bone layout. The next packet will push normally.
	if (bSkeletonChanged)
	{
		UE_LOG(LogMOVINLiveLink, Log, TEXT("Subject '%s': Skeleton changed - skipping frame data this packet to let LiveLink commit new static data"),
			*SubjectName.ToString());
		return;
	}

	if (!bSkeletonReady)
	{
		UE_LOG(LogMOVINLiveLink, Verbose, TEXT("Subject '%s': Skeleton refresh still pending - skipping frame data"), *SubjectName.ToString());
		return;
	}

	{
		FScopeLock Lock(&CriticalSection);
		const int32 CurrentRevision = SubjectSkeletonRevisions.FindRef(SubjectName);
		const int32 CurrentBoneCount = SubjectBoneCounts.FindRef(SubjectName);
		const bool bCurrentReady = SubjectSkeletonReady.FindRef(SubjectName);
		if (CurrentRevision != SkeletonRevision || CurrentBoneCount != Datagram.Bones.Num() || !bCurrentReady)
		{
			UE_LOG(LogMOVINLiveLink, Verbose, TEXT("Subject '%s': Dropping stale frame data (%d bones, revision %d; current %d bones, revision %d, ready=%d)"),
				*SubjectName.ToString(), Datagram.Bones.Num(), SkeletonRevision, CurrentBoneCount, CurrentRevision, bCurrentReady ? 1 : 0);
			return;
		}
	}

	// Watches the streamed bone lengths for the calibration offset. Called per frame rather than
	// only on static data because a recalibration keeps the same bone names, so it would not
	// otherwise be noticed.
	if (FMOVINSkeletonDiagnostics::IsActorSubject(SubjectName)) {
		FScopeLock lock(&CriticalSection);
		pending_subject = SubjectName;
		pending_bones = Datagram.Bones;
	}

	FLiveLinkFrameDataStruct FrameData(FLiveLinkAnimationFrameData::StaticStruct());
	FLiveLinkAnimationFrameData& AnimFrameData = *FrameData.Cast<FLiveLinkAnimationFrameData>();
	TArray<FTransform>& Transforms = AnimFrameData.Transforms;
	Transforms.Reserve(Datagram.Bones.Num());

	for (const FMOVINJointData& Bone : Datagram.Bones)
	{
		FTransform Transform;
		Transform.SetLocation(Bone.LocalPosition);
		Transform.SetRotation(Bone.LocalRotation);
		Transform.SetScale3D(Bone.LocalScale);
		Transforms.Add(Transform);
	}

	AnimFrameData.WorldTime = FLiveLinkWorldTime(now);
	Client->PushSubjectFrameData_AnyThread(SubjectKey, MoveTemp(FrameData));
    { FScopeLock lock(&StatsCriticalSection); ++published_frames; }
}

bool FMOVINLiveLinkSource::UpdateStaticData(const FMOVINDatagram& Datagram, const FName& SubjectName)
{
	const int32 ParsedBoneCount = Datagram.Bones.Num();

	// Build current bone name list
	TArray<FName> CurrentBoneNames;
	CurrentBoneNames.Reserve(ParsedBoneCount);
	for (const FMOVINJointData& Bone : Datagram.Bones)
	{
		CurrentBoneNames.Add(Bone.BoneName);
	}

	// Check if we need to push new static data
	bool bNeedsUpdate = false;
	{
		FScopeLock Lock(&CriticalSection);
		const int32* ExistingCount = SubjectBoneCounts.Find(SubjectName);
		const TArray<FName>* ExistingNames = SubjectBoneNames.Find(SubjectName);

		if (ExistingCount == nullptr)
		{
			UE_LOG(LogMOVINLiveLink, Log, TEXT("Subject '%s': First time seen - registering MOVIN skeleton with %d bones"),
				*SubjectName.ToString(), ParsedBoneCount);
			bNeedsUpdate = true;
		}
		else if (*ExistingCount != ParsedBoneCount)
		{
			UE_LOG(LogMOVINLiveLink, Warning, TEXT("Subject '%s': Bone count changed from %d to %d - re-pushing static data"),
				*SubjectName.ToString(), *ExistingCount, ParsedBoneCount);
			bNeedsUpdate = true;
		}
		else if (ExistingNames == nullptr || *ExistingNames != CurrentBoneNames)
		{
			UE_LOG(LogMOVINLiveLink, Warning, TEXT("Subject '%s': Bone names changed (count still %d) - re-pushing static data"),
				*SubjectName.ToString(), ParsedBoneCount);
			bNeedsUpdate = true;
		}

		if (bNeedsUpdate)
		{
			SubjectBoneCounts.Add(SubjectName, ParsedBoneCount);
			SubjectBoneNames.Add(SubjectName, CurrentBoneNames);
			SubjectSkeletonRevisions.FindOrAdd(SubjectName)++;
			SubjectSkeletonReady.Add(SubjectName, false);
		}
	}

	if (!bNeedsUpdate)
	{
		return false;
	}

	int32 SkeletonRevision = 0;
	{
		FScopeLock Lock(&CriticalSection);
		SkeletonRevision = SubjectSkeletonRevisions.FindRef(SubjectName);
	}

	FLiveLinkSubjectKey SubjectKey(SourceGuid, SubjectName);

	FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
	FLiveLinkSkeletonStaticData& SkeletonData = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

	SkeletonData.BoneNames.Reserve(Datagram.Bones.Num());
	SkeletonData.BoneParents.Reserve(Datagram.Bones.Num());

	// MOVIN streams a flat bone list; the real joint hierarchy lives in the target
	// Skeletal Mesh. The "Live Link Pose" node retargets incoming transforms onto
	// the mesh by *bone name* and rebuilds component space from the mesh's own
	// reference skeleton, so the parent indices declared here are not used by the
	// primary animation path - matching bone names is what matters. We still publish
	// a valid (flattened) hierarchy: bone 0 is the root and every other bone is a
	// direct child of it. Consumers that read LiveLink's BoneParents directly (e.g.
	// the Live Link debugger preview) will show this flattened skeleton, which is
	// expected. If MOVIN Studio ever streams parent indices, build the real
	// hierarchy here instead.
	for (int32 i = 0; i < Datagram.Bones.Num(); ++i)
	{
		SkeletonData.BoneNames.Add(Datagram.Bones[i].BoneName);
		SkeletonData.BoneParents.Add(i == 0 ? INDEX_NONE : 0);
	}

	UE_LOG(LogMOVINLiveLink, Log, TEXT("Subject '%s': Pushing static data with %d bones to LiveLink"),
		*SubjectName.ToString(), Datagram.Bones.Num());

	Client->PushSubjectStaticData_AnyThread(SubjectKey, ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));

	{
		FScopeLock Lock(&CriticalSection);
		if (SubjectSkeletonRevisions.FindRef(SubjectName) == SkeletonRevision)
		{
			SubjectSkeletonReady.Add(SubjectName, true);
		}
	}

	return true;
}

void FMOVINLiveLinkSource::send_status() {
    TSharedPtr<FInternetAddr> endpoint;
    TArray<uint8> bytes;
    const double now = FPlatformTime::Seconds();
    {
        FScopeLock lock(&StatsCriticalSection);
        if (request.IsEmpty() || now - replied_at < 0.25) { return; }
        const FString token = MoveTemp(request);
        request.Empty();
        if (now - requested_at > 3) { return; }
        endpoint = reply_to;
        const float age = order.accepted_at < 0 ? -1.f : float(now - order.accepted_at);
        bytes = MOVINStreamStatus::reply(token, subject, order.frame, received_bones,
            age >= 0 && age < 1 ? ReceiveFps : 0, age >= 0 && age < 1 ? published_fps : 0,
            age, errors, signature, order.sender == requested_sender);
        replied_at = now;
    }
    int32 sent = 0;
    if (!Socket->SendTo(bytes.GetData(), bytes.Num(), sent, *endpoint) || sent != bytes.Num()) {
        UE_LOG(LogMOVINLiveLink, Warning, TEXT("Failed to send Studio status reply"));
    }
}

#undef LOCTEXT_NAMESPACE
