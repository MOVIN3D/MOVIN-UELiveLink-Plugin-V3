#if WITH_DEV_AUTOMATION_TESTS
#include "MOVINStreamStatus.h"
#include "MOVINLiveLinkSource.h"
#include "Misc/AutomationTest.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkSourceSettings.h"
#include "Common/UdpSocketBuilder.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINFrameOrderTest, "MOVIN.Stream.FrameOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMOVINFrameOrderTest::RunTest(const FString& Parameters) {
    FMOVINFrameOrder state;
    TestTrue(TEXT("First frame"), state.accept(100, TEXT("A"), 0));
    TestFalse(TEXT("Duplicate"), state.accept(100, TEXT("A"), .1));
    TestFalse(TEXT("Out of order"), state.accept(99, TEXT("A"), .2));
    TestFalse(TEXT("Competing sender"), state.accept(101, TEXT("B"), .3));
    TestFalse(TEXT("Restart waits"), state.accept(0, TEXT("A"), .9));
    TestTrue(TEXT("Restart after idle"), state.accept(0, TEXT("B"), 1));
    TestTrue(TEXT("New sender progresses"), state.accept(1, TEXT("B"), 1.1));
    TestFalse(TEXT("Old sender cannot return"), state.accept(200, TEXT("A"), 1.2));
    TestTrue(TEXT("Latest frame"), state.accept(MAX_int32, TEXT("B"), 1.3));
    TestFalse(TEXT("Negative frame"), state.accept(-1, TEXT("B"), 2.4));
    TestTrue(TEXT("Counter rollover recovers"), state.accept(0, TEXT("B"), 2.4));
    return true;
}

static TArray<uint8> request_packet(int32 port, int32 motion_port) {
    TArray<uint8> bytes;
    for (const ANSICHAR* s : {"/MOVIN/Unreal/Status/Request", ",sii", "0123456789abcdef0123456789abcdef"}) {
        bytes.Append(reinterpret_cast<const uint8*>(s), FCStringAnsi::Strlen(s) + 1);
        while (bytes.Num() % 4 != 0) { bytes.Add(0); }
    }
    bytes.Add(port >> 24); bytes.Add(port >> 16); bytes.Add(port >> 8); bytes.Add(port);
    bytes.Add(motion_port >> 24); bytes.Add(motion_port >> 16); bytes.Add(motion_port >> 8); bytes.Add(motion_port);
    return bytes;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINStatusProtocolTest, "MOVIN.Stream.StatusProtocol",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMOVINStatusProtocolTest::RunTest(const FString& Parameters) {
    auto bytes = request_packet(39581, 12345);
    FString token; int32 port = 0, motion_port = 0;
    TestTrue(TEXT("Valid request"), MOVINStreamStatus::read_request(bytes, token, port, motion_port));
    TestEqual(TEXT("Reply port"), port, 39581);
    TestEqual(TEXT("Motion sender port"), motion_port, 12345);
    for (int32 length = 0; length < bytes.Num(); ++length) {
        TArray<uint8> truncated(bytes.GetData(), length);
        TestFalse(TEXT("Truncated request"), MOVINStreamStatus::read_request(truncated, token, port, motion_port));
    }
    auto extra = bytes; extra.Add(0);
    TestFalse(TEXT("Trailing data"), MOVINStreamStatus::read_request(extra, token, port, motion_port));
    TestFalse(TEXT("Invalid port"), MOVINStreamStatus::read_request(request_packet(65536, 0), token, port, motion_port));
    TestFalse(TEXT("Zero port"), MOVINStreamStatus::read_request(request_packet(0, 0), token, port, motion_port));
    extra = bytes; extra[32] = 'z';
    TestFalse(TEXT("Invalid token or types"), MOVINStreamStatus::read_request(extra, token, port, motion_port));
    FMOVINJointData root, hips;
    root.BoneName = TEXT("Root"); hips.BoneName = TEXT("Hips");
    TestEqual(TEXT("Canonical bone order"), MOVINStreamStatus::bone_signature({root, hips}), MOVINStreamStatus::bone_signature({hips, root}));
    TestEqual(TEXT("Studio SHA256 contract"), MOVINStreamStatus::bone_signature({root, hips}), FString(TEXT("9586db745adfcdb553e8c9c30b1a17b8c6280e4500ba372feac94418784b55a1")));
    return true;
}

static TArray<uint8> motion_packet(int32 frame, const FString& subject, int32 bones, float x) {
    TArray<uint8> bytes;
    auto integer = [&](int32 value) { bytes.Append(reinterpret_cast<const uint8*>(&value), 4); };
    auto number = [&](float value) { bytes.Append(reinterpret_cast<const uint8*>(&value), 4); };
    auto text = [&](const FString& value) {
        FTCHARToUTF8 utf8(*value);
        uint32 length = utf8.Length();
        while (length >= 128) { bytes.Add(uint8(length) | 128); length >>= 7; }
        bytes.Add(uint8(length)); bytes.Append(reinterpret_cast<const uint8*>(utf8.Get()), utf8.Length());
    };
    integer(0); text(subject); integer(frame); integer(bones);
    for (int32 i = 0; i < bones; ++i) {
        text(i == 0 ? TEXT("Root") : TEXT("Hips"));
        number(x); number(2); number(3);
        number(0); number(0); number(0); number(1);
        number(1); number(1); number(1);
    }
    int32 size = bytes.Num() - 4; FMemory::Memcpy(bytes.GetData(), &size, 4);
    return bytes;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINSourceLifecycleTest, "MOVIN.Stream.SourceLifecycleAndStatusUDP",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMOVINSourceLifecycleTest::RunTest(const FString& Parameters) {
    auto* subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* probe = FUdpSocketBuilder(TEXT("MOVINTestProbe")).AsNonBlocking().BoundToEndpoint(FIPv4Endpoint(FIPv4Address::Any, 0)).Build();
    auto endpoint = subsystem->CreateInternetAddr(); probe->GetAddress(*endpoint);
    const int32 port = endpoint->GetPort();
    auto& client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    // A socket held by another app must make this source invalid.
    TSharedPtr<FMOVINLiveLinkSource> blocked = MakeShared<FMOVINLiveLinkSource>(port);
    const auto blocked_id = client.AddSource(blocked);
    TestFalse(TEXT("OS bind collision"), blocked->IsSourceStillValid());
    client.RemoveSource(blocked_id); blocked->RequestSourceShutdown(); blocked.Reset();
    probe->Close(); subsystem->DestroySocket(probe);
    endpoint->SetIp(FIPv4Address::InternalLoopback.Value);
    for (int32 cycle = 0; cycle < 2; ++cycle) {
        TSharedPtr<FMOVINLiveLinkSource> source = MakeShared<FMOVINLiveLinkSource>(port);
        const auto id = client.AddSource(source);
        client.GetSourceSettings(id)->Mode = ELiveLinkSourceMode::Latest;
        TestTrue(TEXT("Source starts and reuses released port"), source->IsSourceStillValid());
        TSharedPtr<FMOVINLiveLinkSource> duplicate = MakeShared<FMOVINLiveLinkSource>(port);
        TestFalse(TEXT("Duplicate source rejected"), duplicate->IsSourceStillValid());
        duplicate.Reset();
        auto* socket = FUdpSocketBuilder(TEXT("MOVINStatusProbe")).AsNonBlocking().BoundToEndpoint(FIPv4Endpoint(FIPv4Address::InternalLoopback, 0)).Build();
        auto local = subsystem->CreateInternetAddr(); socket->GetAddress(*local);
        auto packet = request_packet(local->GetPort(), local->GetPort()); int32 sent = 0;
        socket->SendTo(packet.GetData(), packet.Num(), sent, *endpoint);
        uint32 size = 0;
        const double until = FPlatformTime::Seconds() + 2;
        while (!socket->HasPendingData(size) && FPlatformTime::Seconds() < until) {
            source->Update(); FPlatformProcess::Sleep(.01f);
        }
        TestTrue(TEXT("Reply before streaming"), size > 0);
        if (size > 0) {
            TArray<uint8> reply; reply.SetNumUninitialized(size); int32 read = 0;
            socket->Recv(reply.GetData(), reply.Num(), read);
            auto expected = MOVINStreamStatus::reply(TEXT("0123456789abcdef0123456789abcdef"), TEXT(""), -1, 0, 0, 0, -1, 0, TEXT(""), false);
            TestTrue(TEXT("Reply token and unknown values"), reply == expected);
        }
        auto send_motion = [&](int32 frame, const FString& subject, int32 bones, float x) {
            auto motion = motion_packet(frame, subject, bones, x);
            socket->SendTo(motion.GetData(), motion.Num(), sent, *endpoint);
            FPlatformProcess::Sleep(.02f);
            client.ForceTick();
        };
        auto check_frame = [&](const FString& name, int32 bones, double x) {
            FLiveLinkSubjectFrameData evaluated;
            const bool exists = client.EvaluateFrameFromSource_AnyThread(FLiveLinkSubjectKey(id, FName(*name)), ULiveLinkAnimationRole::StaticClass(), evaluated);
            TestTrue(TEXT("Frame reaches LiveLink evaluation"), exists);
            if (exists) {
                const auto* animation = evaluated.FrameData.Cast<FLiveLinkAnimationFrameData>();
                TestEqual(TEXT("LiveLink bone count"), animation->Transforms.Num(), bones);
                // Unity X,Y,Z maps to Unreal Z,X,Y.
                TestEqual(TEXT("Latest accepted translation"), animation->Transforms[0].GetTranslation().Y, x);
            }
        };
        send_motion(100, TEXT("UDPTest"), 2, 10);
        send_motion(101, TEXT("UDPTest"), 2, 20);
        check_frame(TEXT("UDPTest"), 2, 20);
        send_motion(100, TEXT("UDPTest"), 2, 99);
        send_motion(101, TEXT("UDPTest"), 2, 99);
        check_frame(TEXT("UDPTest"), 2, 20);
        send_motion(102, TEXT("UDPTest"), 1, 30);
        send_motion(103, TEXT("UDPTest"), 1, 40);
        check_frame(TEXT("UDPTest"), 1, 40);
        send_motion(104, TEXT("OtherSubject"), 2, 50);
        send_motion(105, TEXT("OtherSubject"), 2, 60);
        check_frame(TEXT("OtherSubject"), 2, 60);
        TestFalse(TEXT("Old subject is removed"), client.GetSubjects(true, false).Contains(FLiveLinkSubjectKey(id, FName(TEXT("UDPTest")))));
        FPlatformProcess::Sleep(1.05f);
        send_motion(0, TEXT("OtherSubject"), 2, 70);
        check_frame(TEXT("OtherSubject"), 2, 70);

        // Optional captured packet from the Studio sender integration test.
        FString fixture;
        if (FParse::Value(FCommandLine::Get(), TEXT("MOVINStudioPacket="), fixture)) {
            TArray<uint8> motion;
            TestTrue(TEXT("Captured Studio packet loads"), FFileHelper::LoadFileToArray(motion, *fixture));
            socket->SendTo(motion.GetData(), motion.Num(), sent, *endpoint);
            FPlatformProcess::Sleep(.02f); client.ForceTick();
            // Resend with a newer frame after static data has been committed.
            FMOVINDatagram parsed;
            TestTrue(TEXT("Captured Studio packet parses"), FMOVINDatagramParser::Parse(motion, parsed));
            if (parsed.bIsValid) {
                FTCHARToUTF8 utf8(*parsed.SubjectName);
                int32 offset = 4;
                while ((motion[offset++] & 128) != 0) {}
                offset += utf8.Length();
                int32 frame = parsed.FrameIndex + 1;
                FMemory::Memcpy(motion.GetData() + offset, &frame, 4);
                socket->SendTo(motion.GetData(), motion.Num(), sent, *endpoint);
                FPlatformProcess::Sleep(.02f); client.ForceTick();
                check_frame(parsed.SubjectName, 2, 0);
            }
        }
        socket->Close(); subsystem->DestroySocket(socket);
        client.RemoveSource(id); source->RequestSourceShutdown();
        TestFalse(TEXT("Source stops"), source->IsSourceStillValid());
        TestFalse(TEXT("Registry releases port"), FMOVINLiveLinkSource::IsPortInUse(port));
        source.Reset();
    }
    return true;
}
#endif
