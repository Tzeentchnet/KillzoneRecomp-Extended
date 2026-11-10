// Experimental presentation-upscaler configuration shared by the game and replay tools.
// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <iosfwd>
#include <cstdint>
#include <optional>
#include <string_view>

enum class KzgsUpscaler
{
	Off,
	FSR1,
	DLSS,
	XeSS,
};

struct KzgsUpscalerOverrides
{
	std::optional<KzgsUpscaler> provider;
	std::optional<int> sharpening;
};

enum class KzgsUpscalerReason
{
	Disabled,
	NotImplemented,
	WaitingForFrame,
	Active,
	NoSurface,
	NoUpscaleNeeded,
	InvalidDimensions,
	InitializationFailed,
	DispatchFailed,
	PresentFailed,
	ProviderMissing,
	ProviderVersionMismatch,
	ProviderInvalid,
	InstanceExtensionUnavailable,
	DeviceExtensionUnavailable,
	DeviceFeatureUnavailable,
	RequiredInputUnavailable,
	UnsupportedColorEncoding,
	DeviceRecreationRequired,
	DuplicatePresent,
	ResetFailed,
	DeviceLost,
	IdleWaitFailed,
};

struct KzgsUpscalerStatus
{
	KzgsUpscaler requested = KzgsUpscaler::Off;
	KzgsUpscaler effective = KzgsUpscaler::Off;
	int requestedSharpening = 0;
	int effectiveSharpening = 0;
	KzgsUpscalerReason reason = KzgsUpscalerReason::Disabled;
	uint32_t providerResult = 0;
	uint32_t providerReason = 0;
	int32_t providerNativeCode = 0;
	char providerMessage[192]{};
	uint64_t temporalFrameId = 0;
	uint64_t temporalValidInputs = 0;
	uint64_t temporalMissingInputs = 0;
	uint64_t temporalInvalidInputs = 0;
	uint64_t temporalInvalidReasons = 0;
	bool temporalObserved = false;
	bool temporalValid = false;
};

const char* kzgsUpscalerName(KzgsUpscaler provider);
const char* kzgsUpscalerReasonName(KzgsUpscalerReason reason);
bool kzgsIsValidUpscaler(KzgsUpscaler provider);
bool kzgsParseUpscaler(std::string_view text, KzgsUpscaler& provider);
bool kzgsParseUpscalerSharpening(std::string_view text, int& sharpening);

// Missing environment variables are passed as nullptr. Invalid values are reported and not applied.
KzgsUpscalerOverrides kzgsUpscalerEnvironmentOverrides(
	const char* provider, const char* sharpening, std::ostream& diagnostics);
void kzgsMergeUpscalerOverrides(KzgsUpscalerOverrides& lower, const KzgsUpscalerOverrides& higher);

enum class KzgsUpscalerOptionResult
{
	NotHandled,
	Parsed,
	Error,
};

// Supports --option value and --option=value. Only a separate value sets consumeNext.
KzgsUpscalerOptionResult kzgsParseUpscalerOption(
	std::string_view argument, const char* next, KzgsUpscalerOverrides& overrides,
	bool& consumeNext, std::ostream& diagnostics);
