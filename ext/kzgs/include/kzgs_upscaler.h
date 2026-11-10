// Experimental presentation-upscaler configuration shared by the game and replay tools.
// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <iosfwd>
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
};

struct KzgsUpscalerStatus
{
	KzgsUpscaler requested = KzgsUpscaler::Off;
	KzgsUpscaler effective = KzgsUpscaler::Off;
	int requestedSharpening = 0;
	int effectiveSharpening = 0;
	KzgsUpscalerReason reason = KzgsUpscalerReason::Disabled;
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
