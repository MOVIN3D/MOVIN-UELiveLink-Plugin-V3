#pragma once
#include "CoreMinimal.h"
#include "MOVINDatagram.h"

namespace MOVINStreamStatus {
    bool read_request(const TArray<uint8>& bytes, FString& token, int32& port, int32& motion_port);
    FString bone_signature(const TArray<FMOVINJointData>& bones);
    TArray<uint8> reply(const FString& token, const FString& subject, int32 frame, int32 bones,
        float received, float published, float age, int32 errors, const FString& signature, bool same_source);
}
