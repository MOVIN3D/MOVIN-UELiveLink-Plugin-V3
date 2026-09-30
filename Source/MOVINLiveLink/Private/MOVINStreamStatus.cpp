#include "MOVINStreamStatus.h"
#include "MOVINLiveLinkModule.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"

bool FMOVINFrameOrder::accept(int32 next, const FString& endpoint, double now) {
    const bool accept = next >= 0 && (accepted_at < 0 || now - accepted_at >= 1.0 || (sender == endpoint && next > frame));
    if (accept) {
        frame = next;
        sender = endpoint;
        accepted_at = now;
    }
    return accept;
}

bool MOVINStreamStatus::read_request(const TArray<uint8>& bytes, FString& token, int32& port, int32& motion_port) {
    int32 offset = 0;
    auto read = [&](const ANSICHAR* expected) {
        const int32 length = FCStringAnsi::Strlen(expected) + 1;
        const int32 padded = (length + 3) & ~3;
        if (padded > bytes.Num() - offset || FMemory::Memcmp(bytes.GetData() + offset, expected, length) != 0) return false;
        for (int32 i = length; i < padded; ++i) { if (bytes[offset + i] != 0) return false; }
        offset += padded;
        return true;
    };
    if (!read("/MOVIN/Unreal/Status/Request") || !read(",sii") || bytes.Num() - offset != 44) return false;
    token.Empty();
    for (int32 i = 0; i < 32; ++i) {
        const uint8 c = bytes[offset + i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        token.AppendChar(c);
    }
    for (int32 i = 32; i < 36; ++i) { if (bytes[offset + i] != 0) return false; }
    offset += 36;
    const uint32 value = (uint32(bytes[offset]) << 24) | (uint32(bytes[offset+1]) << 16) | (uint32(bytes[offset+2]) << 8) | bytes[offset+3];
    if (value < 1 || value > 65535) return false;
    port = static_cast<int32>(value);
    offset += 4;
    const uint32 motion = (uint32(bytes[offset]) << 24) | (uint32(bytes[offset+1]) << 16) | (uint32(bytes[offset+2]) << 8) | bytes[offset+3];
    if (motion > 65535) return false;
    motion_port = static_cast<int32>(motion);
    return true;
}

FString MOVINStreamStatus::bone_signature(const TArray<FMOVINJointData>& bones) {
    TArray<FString> names;
    for (const auto& bone : bones) { names.Add(bone.wire_name.IsEmpty() ? bone.BoneName.ToString() : bone.wire_name); }
    names.Sort([](const FString& a, const FString& b) { return a.Compare(b, ESearchCase::CaseSensitive) < 0; });
    TArray<uint8> bytes;
    for (const auto& name : names) {
        FTCHARToUTF8 utf8(*name);
        uint32 length = utf8.Length();
        while (length >= 128) { bytes.Add(uint8(length) | 128); length >>= 7; }
        bytes.Add(uint8(length));
        bytes.Append(reinterpret_cast<const uint8*>(utf8.Get()), utf8.Length());
    }
    uint8 signature[32];
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto result = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (result >= 0) { result = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0); }
    if (result >= 0) { result = BCryptHashData(hash, bytes.GetData(), bytes.Num(), 0); }
    if (result >= 0) { result = BCryptFinishHash(hash, signature, sizeof(signature), 0); }
    if (hash != nullptr) { BCryptDestroyHash(hash); }
    if (algorithm != nullptr) { BCryptCloseAlgorithmProvider(algorithm, 0); }
    if (result < 0) { UE_LOG(LogMOVINLiveLink, Fatal, TEXT("SHA-256 failed: 0x%08x"), uint32(result)); }
    return BytesToHex(signature, sizeof(signature)).ToLower();
}

TArray<uint8> MOVINStreamStatus::reply(const FString& token, const FString& subject, int32 frame, int32 bones,
    float received, float published, float age, int32 errors, const FString& signature, bool same_source) {
    TArray<uint8> bytes;
    auto text = [&](const FString& value) {
        FTCHARToUTF8 utf8(*value);
        bytes.Append(reinterpret_cast<const uint8*>(utf8.Get()), utf8.Length());
        bytes.Add(0);
        while (bytes.Num() % 4 != 0) { bytes.Add(0); }
    };
    auto integer = [&](uint32 value) {
        bytes.Add(value >> 24); bytes.Add(value >> 16); bytes.Add(value >> 8); bytes.Add(value);
    };
    auto number = [&](float value) { uint32 bits; FMemory::Memcpy(&bits, &value, 4); integer(bits); };
    text(TEXT("/MOVIN/Unreal/Status")); text(TEXT(",sisiifffisi"));
    text(token); integer(1); text(subject); integer(frame); integer(bones);
    number(received); number(published); number(age); integer(errors); text(signature); integer(same_source ? 1 : 0);
    return bytes;
}
