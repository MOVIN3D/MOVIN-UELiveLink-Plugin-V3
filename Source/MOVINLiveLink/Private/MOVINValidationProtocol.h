// Copyright 2025 MOVIN. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Shared wire-protocol constants for MOVIN stream validation.
 *
 * These values are part of the contract with MOVIN Studio and must stay in sync
 * with the sender. They are centralized here so the datagram parser and the
 * LiveLink source can never disagree on the magic values.
 */
namespace MOVINValidationProtocol
{
	/** Reserved subject name that marks a validation control packet (begin/end session). */
	inline constexpr const TCHAR* ControlSubject = TEXT("__MOVIN_STREAM_VALIDATION__");

	/** Sentinel frame index identifying a "begin validation session" control packet. */
	inline constexpr int32 BeginSessionFrame = -2147483601;

	/** Sentinel frame index identifying an "end validation session" control packet. */
	inline constexpr int32 EndSessionFrame = -2147483602;

	/** Validation target identifier this plugin records under. */
	inline constexpr const TCHAR* Target = TEXT("Unreal_LiveLink");
}
