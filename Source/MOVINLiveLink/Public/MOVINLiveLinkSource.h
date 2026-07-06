// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Common/UdpSocketBuilder.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#include "MOVINDatagram.h"

class ILiveLinkClient;
class FMOVINStreamValidation;

/**
 * LiveLink source that receives motion capture data from MOVIN Studio via UDP.
 * 
 * Runs a background thread that listens on a configurable UDP port,
 * parses incoming MOVIN Studio packets, and pushes skeleton/animation
 * data into the LiveLink system.
 */
class MOVINLIVELINK_API FMOVINLiveLinkSource : public ILiveLinkSource, public FRunnable
{
public:

	FMOVINLiveLinkSource(int32 InPort);
	virtual ~FMOVINLiveLinkSource();

	static bool IsPortInUse(int32 InPort);
	int32 GetPort() const { return Port; }

	// ~Begin ILiveLinkSource interface
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual bool IsSourceStillValid() const override;
	virtual bool RequestSourceShutdown() override;
	virtual FText GetSourceType() const override;
	virtual FText GetSourceStatus() const override;
	virtual FText GetSourceMachineName() const override;
	virtual void Update() override;
	// ~End ILiveLinkSource interface

	// ~Begin FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override {}
	// ~End FRunnable interface

private:

	/** Start the receiver thread */
	void StartThread();

	/** Process a fully received UDP datagram */
	void ProcessReceivedData(const TArray<uint8>& RawData);

	/** Update or create the skeleton static data for a subject. Returns true if static data was pushed (skeleton changed). */
	bool UpdateStaticData(const FMOVINDatagram& Datagram, const FName& SubjectName);

	/** Update packet counters used by the LiveLink source status monitor. */
	void NotePacketReceived();

	/** Refresh receive FPS and source status text. */
	void UpdateReceiveStatus();

	/** Update the LiveLink UI status text and log meaningful transitions. */
	void SetSourceStatus(const FText& InStatus);

	/** Clear local subject cache when the user removes a LiveLink subject. */
	void HandleSubjectRemoved(FLiveLinkSubjectKey RemovedSubjectKey);

	/** Remove LiveLink client delegate bindings owned by this source. */
	void UnregisterClientDelegates();

	static bool TryRegisterPort(int32 InPort);
	static void UnregisterPort(int32 InPort);

private:

	ILiveLinkClient* Client = nullptr;
	FGuid SourceGuid;

	/** UDP port to listen on */
	int32 Port;

	/** Whether this source successfully reserved its port in the global registry */
	bool bPortRegistered = false;

	/** The network socket */
	FSocket* Socket = nullptr;

	/** Socket subsystem reference */
	ISocketSubsystem* SocketSubsystem = nullptr;

	/** The receiver thread */
	FRunnableThread* Thread = nullptr;

	/** Flag indicating the thread should stop */
	FThreadSafeBool bStopping;

	/** Time to wait for incoming data before looping */
	FTimespan WaitTime;

	/** Source status text displayed in the LiveLink UI */
	FText SourceStatus;

	/** LiveLink delegate handle for subject removal notifications. */
	FDelegateHandle SubjectRemovedDelegateHandle;

	/** Last status category written to the log, used to avoid noisy status spam. */
	FString LastLoggedSourceStatusKey;

	/** Raw packet validation writer, attached to the newest MOVIN Studio validation session. */
	TUniquePtr<FMOVINStreamValidation> StreamValidation;

	/** Receiver FPS monitor state */
	FCriticalSection StatsCriticalSection;
	uint64 TotalPacketsReceived = 0;
	uint64 LastReceiveRateSamplePackets = 0;
	double LastPacketTimeSeconds = 0.0;
	double LastReceiveRateSampleTimeSeconds = 0.0;
	double ReceiveFps = 0.0;
	bool bHasReceiveRateSample = false;

	/** Track bone counts per subject to detect when static data needs to be re-pushed */
	TMap<FName, int32> SubjectBoneCounts;

	/** Track bone names per subject for change detection */
	TMap<FName, TArray<FName>> SubjectBoneNames;

	/** Monotonic skeleton revision per subject to discard stale queued frame updates */
	TMap<FName, int32> SubjectSkeletonRevisions;

	/** Whether a subject is ready to accept animation frames after a skeleton refresh */
	TMap<FName, bool> SubjectSkeletonReady;

	/** Critical section for thread-safe access */
	FCriticalSection CriticalSection;

	/** Global registry of active MOVIN LiveLink UDP ports */
	static FCriticalSection ActivePortsCriticalSection;
	static TSet<int32> ActivePorts;
};
