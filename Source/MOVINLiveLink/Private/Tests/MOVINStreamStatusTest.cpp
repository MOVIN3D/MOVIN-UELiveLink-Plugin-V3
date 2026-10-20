#if WITH_DEV_AUTOMATION_TESTS
#include "MOVINStreamStatus.h"
#include "MOVINLiveLinkSource.h"
#include "MOVINLiveLinkSourceFactory.h"
#include "MOVINLiveLinkSourceSettings.h"
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
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

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

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMOVINGlobalRotationTest, "MOVIN.Stream.GlobalRotationOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMOVINGlobalRotationTest::RunTest(const FString& Parameters) {
    auto* subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* probe = FUdpSocketBuilder(TEXT("MOVINRotationPort")).BoundToEndpoint(FIPv4Endpoint(FIPv4Address::InternalLoopback, 0)).Build();
    auto endpoint = subsystem->CreateInternetAddr();
    probe->GetAddress(*endpoint);
    const auto port = endpoint->GetPort();
    probe->Close(); subsystem->DestroySocket(probe);
    auto* socket = FUdpSocketBuilder(TEXT("MOVINRotationSender")).BoundToEndpoint(FIPv4Endpoint(FIPv4Address::InternalLoopback, 0)).Build();
    auto& client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    TSharedPtr<FMOVINLiveLinkSource> source = MakeShared<FMOVINLiveLinkSource>(port);
    const auto id = client.AddSource(source);
    auto* settings = CastChecked<UMOVINLiveLinkSourceSettings>(client.GetSourceSettings(id));
    settings->Mode = ELiveLinkSourceMode::Latest;
    settings->Factory = UMOVINLiveLinkSourceFactory::StaticClass();
    settings->ConnectionString = FString::Printf(TEXT("Port=%d"), port);
    TestTrue(TEXT("Existing sources default to zero offset"), settings->global_rotation_offset.IsZero());

    const auto position = FVector(130, -70, 95);
    const auto orientation = FRotator(23, -37, 11).Quaternion();
    const auto child = FTransform(FRotator(-13, 21, 9), FVector(4, 8, 17), FVector(1, 1, 1));
    const auto scale = FVector(1.2, 1.2, 1.2);
    // Studio's default (Unity Euler 0, -90, -90), after the parser's axis permutation.
    // Its local Z points along Unreal X, so rotating Hips locally cannot implement global yaw.
    const auto basis = FTransform(FQuat(-.5, .5, -.5, .5), FVector(17, -23, 9));
    TestTrue(TEXT("Fixture reproduces the reported axis mismatch"), basis.GetRotation().RotateVector(FVector::UpVector).Equals(FVector::ForwardVector, .001));
    auto frame = 0;
    auto synthetic = true;
    auto send = [&](const FGuid& key, bool actor, const FVector& location, const FQuat& rotation, bool evaluate) {
        TArray<uint8> bytes;
        auto integer = [&](int32 value) { bytes.Append(reinterpret_cast<const uint8*>(&value), sizeof(value)); };
        auto number = [&](float value) { bytes.Append(reinterpret_cast<const uint8*>(&value), sizeof(value)); };
        auto text = [&](const ANSICHAR* value) {
            bytes.Add(uint8(FCStringAnsi::Strlen(value)));
            bytes.Append(reinterpret_cast<const uint8*>(value), FCStringAnsi::Strlen(value));
        };
        integer(0); text(actor ? "MOVINMan" : "RotationTest"); integer(frame++); integer(actor && synthetic ? 3 : 2);
        if (actor && synthetic) {
            text("RootBone");
            const auto p = basis.GetLocation();
            const auto q = basis.GetRotation();
            number(p.Y); number(p.Z); number(p.X);
            number(q.Y); number(q.Z); number(q.X); number(q.W);
            number(1); number(1); number(1);
        }
        text(actor ? "Hips" : "Root");
        // Undo the parser's Unity -> Unreal axis permutation to test the full receive path.
        number(location.Y); number(location.Z); number(location.X);
        number(orientation.Y); number(orientation.Z); number(orientation.X); number(orientation.W);
        number(scale.Y); number(scale.Z); number(scale.X);
        text("Spine");
        const auto p = child.GetLocation();
        const auto q = child.GetRotation();
        number(p.Y); number(p.Z); number(p.X);
        number(q.Y); number(q.Z); number(q.X); number(q.W);
        number(1); number(1); number(1);
        auto size = bytes.Num() - 4;
        FMemory::Memcpy(bytes.GetData(), &size, sizeof(size));
        int32 sent = 0;
        TestTrue(TEXT("Motion packet sent"), socket->SendTo(bytes.GetData(), bytes.Num(), sent, *endpoint));
        TestEqual(TEXT("Complete UDP frame sent"), sent, bytes.Num());
        FPlatformProcess::Sleep(.03f);
        client.ForceTick();
        FLiveLinkSubjectFrameData evaluated;
        if (evaluate && client.EvaluateFrameFromSource_AnyThread(FLiveLinkSubjectKey(key, FName(actor ? TEXT("MOVINMan") : TEXT("RotationTest"))), ULiveLinkAnimationRole::StaticClass(), evaluated)) {
            const auto& transforms = evaluated.FrameData.Cast<FLiveLinkAnimationFrameData>()->Transforms;
            const auto original = actor && synthetic ? basis : FTransform(orientation, location, scale);
            const auto local = actor && synthetic ? FTransform(orientation, location, scale) : child;
            TestTrue(TEXT("Travel path rotates about the stream origin"), transforms[0].GetLocation().Equals(rotation.RotateVector(original.GetLocation()), .001));
            TestTrue(TEXT("Global rotation precedes the original root orientation"), transforms[0].GetRotation().Equals(rotation * original.GetRotation(), .001));
            TestTrue(TEXT("Root scale is preserved"), transforms[0].GetScale3D().Equals(original.GetScale3D(), .001));
            TestTrue(TEXT("Child local pose is unchanged"), transforms[1].Equals(local, .001));
            const auto expected = rotation.RotateVector(original.TransformPosition(local.GetLocation()));
            TestTrue(TEXT("Child follows the rotated root exactly once"), transforms[0].TransformPosition(transforms[1].GetLocation()).Equals(expected, .001));
            if (actor && synthetic) {
                TestTrue(TEXT("Grandchild local pose is unchanged"), transforms[2].Equals(child, .001));
                const auto expected_tip = rotation.RotateVector(original.TransformPosition(local.TransformPosition(child.GetLocation())));
                const auto tip = transforms[0].TransformPosition(transforms[1].TransformPosition(transforms[2].GetLocation()));
                TestTrue(TEXT("Entire chain follows global axes despite the imported root basis"), tip.Equals(expected_tip, .001));
                const auto actual = transforms[0].GetRotation() * transforms[1].GetRotation();
                TestTrue(TEXT("Entire chain rotates in component space"), actual.Equals(rotation * original.GetRotation() * local.GetRotation(), .001));
            }
        }
        return evaluated.FrameData.IsValid();
    };

    // Registration packets can still evaluate the previous layout in older LiveLink versions.
    // Only inspect the following packet, which is the first published frame of the new layout.
    send(id, false, position, FQuat::Identity, false);
    TestTrue(TEXT("Zero offset frame evaluated"), send(id, false, position, FQuat::Identity, true));
    auto* property = FindFProperty<FProperty>(UMOVINLiveLinkSourceSettings::StaticClass(), GET_MEMBER_NAME_CHECKED(UMOVINLiveLinkSourceSettings, global_rotation_offset));
    auto event = FPropertyChangedEvent(property);
    // Use explicit axis-angle expectations, independently of FRotator's constructor order.
    settings->global_rotation_offset = FRotator::ZeroRotator;
    settings->global_rotation_offset.Yaw = 90;
    source->OnSettingsChanged(settings, event);
    TestTrue(TEXT("Z / Yaw turns about Unreal Up"), send(id, false, position, FQuat(FVector::UpVector, PI / 2), true));
    settings->global_rotation_offset = FRotator::ZeroRotator;
    settings->global_rotation_offset.Pitch = 90;
    source->OnSettingsChanged(settings, event);
    TestTrue(TEXT("Y / Pitch turns about Unreal Right"), send(id, false, position, FQuat(FVector::RightVector, -PI / 2), true));
    settings->global_rotation_offset = FRotator::ZeroRotator;
    settings->global_rotation_offset.Roll = 90;
    source->OnSettingsChanged(settings, event);
    TestTrue(TEXT("X / Roll turns about Unreal Forward"), send(id, false, position, FQuat(FVector::ForwardVector, -PI / 2), true));
    for (const auto offset : {FRotator(0, 90, 0), FRotator(90, 0, 0), FRotator(0, 0, 90), FRotator(31, -68, 17), FRotator::ZeroRotator}) {
        settings->global_rotation_offset = offset;
        source->OnSettingsChanged(settings, event);
        TestTrue(TEXT("Offset edits apply without reconnecting"), send(id, false, position, offset.Quaternion(), true));
        TestTrue(TEXT("Next motion frame is not rotated cumulatively"), send(id, false, position + FVector(30, 20, 10), offset.Quaternion(), true));
    }
    settings->global_rotation_offset = FRotator(31, -68, 17);
    source->OnSettingsChanged(settings, event);
    send(id, true, position, settings->global_rotation_offset.Quaternion(), false);
    TestTrue(TEXT("Actor with the imported root basis evaluates"), send(id, true, position, settings->global_rotation_offset.Quaternion(), true));
    for (const auto offset : {FRotator(0, 90, 0), FRotator(90, 0, 0), FRotator(0, 0, 90), FRotator::ZeroRotator}) {
        settings->global_rotation_offset = offset;
        source->OnSettingsChanged(settings, event);
        TestTrue(TEXT("Actor offset uses Unreal axes with a rotated parent"), send(id, true, position, offset.Quaternion(), true));
    }
    settings->global_rotation_offset = FRotator(31, -68, 17);
    source->OnSettingsChanged(settings, event);
    synthetic = false;
    send(id, true, position, settings->global_rotation_offset.Quaternion(), false);
    TestTrue(TEXT("Actor without a synthetic root also rotates once"), send(id, true, position, settings->global_rotation_offset.Quaternion(), true));
    auto preset = client.GetSourcePreset(id, GetTransientPackage());
    TestTrue(TEXT("Preset captures the custom settings"), CastChecked<UMOVINLiveLinkSourceSettings>(preset.Settings)->global_rotation_offset.Equals(settings->global_rotation_offset));
    client.RemoveSource(id); source->RequestSourceShutdown(); source.Reset(); client.ForceTick();
    // A new guid avoids LiveLink's deferred removal; the same port must already be reusable.
    preset.Guid = FGuid::NewGuid();
    TestTrue(TEXT("Source recreates from its saved preset"), client.CreateSource(preset));
    const auto restored = CastChecked<UMOVINLiveLinkSourceSettings>(client.GetSourceSettings(preset.Guid))->global_rotation_offset;
    TestTrue(TEXT("Preset restores all three axes"), restored.Equals(FRotator(31, -68, 17)));
    send(preset.Guid, false, position, restored.Quaternion(), false);
    TestTrue(TEXT("First published frame uses the restored offset"), send(preset.Guid, false, position, restored.Quaternion(), true));
    client.RemoveSource(preset.Guid); client.ForceTick();
    preset.Guid = FGuid::NewGuid();
    preset.Settings = NewObject<ULiveLinkSourceSettings>();
    preset.Settings->Factory = UMOVINLiveLinkSourceFactory::StaticClass();
    preset.Settings->ConnectionString = FString::Printf(TEXT("Port=%d"), port);
    preset.Settings->Mode = ELiveLinkSourceMode::Latest;
    TestTrue(TEXT("Older presets still create a source"), client.CreateSource(preset));
    TestTrue(TEXT("Older presets receive a zero offset"), CastChecked<UMOVINLiveLinkSourceSettings>(client.GetSourceSettings(preset.Guid))->global_rotation_offset.IsZero());
    send(preset.Guid, false, position, FQuat::Identity, false);
    TestTrue(TEXT("Older presets preserve the original motion"), send(preset.Guid, false, position, FQuat::Identity, true));
    client.RemoveSource(preset.Guid); client.ForceTick();
    socket->Close(); subsystem->DestroySocket(socket);
    return true;
}
#endif
#endif
