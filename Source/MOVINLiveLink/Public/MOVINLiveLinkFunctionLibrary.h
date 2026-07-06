// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MOVINLiveLinkFunctionLibrary.generated.h"

/**
 * Blueprint function library for creating MOVIN LiveLink sources at runtime.
 * Use this to programmatically start receiving motion capture data
 * from MOVIN Studio without requiring a LiveLink Preset.
 */
UCLASS()
class MOVINLIVELINK_API UMOVINLiveLinkFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Create and register a MOVIN LiveLink source that listens for
	 * motion capture data on the specified UDP port.
	 *
	 * @param Port  The UDP port to listen on (default: 11236)
	 * @return True if the source was successfully created and registered
	 */
	UFUNCTION(BlueprintCallable, Category = "MOVIN LiveLink", meta = (DisplayName = "Add MOVIN LiveLink Source"))
	static bool AddMOVINLiveLinkSource(int32 Port = 11236);
};
