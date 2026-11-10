// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_upscaler.h"

#include <charconv>
#include <ostream>
#include <system_error>

namespace
{
	struct ProviderName
	{
		KzgsUpscaler provider;
		std::string_view name;
	};

	constexpr ProviderName kProviders[] = {
		{KzgsUpscaler::Off, "off"},
		{KzgsUpscaler::FSR1, "fsr1"},
		{KzgsUpscaler::DLSS, "dlss"},
		{KzgsUpscaler::XeSS, "xess"},
	};
	constexpr const char* kProviderExpected = "expected off, fsr1, dlss or xess";
	constexpr const char* kSharpeningExpected = "expected an integer from 0 to 100";

	char asciiLower(char c)
	{
		return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
	}

	bool equalIgnoringCase(std::string_view left, std::string_view right)
	{
		if (left.size() != right.size())
			return false;
		for (size_t i = 0; i < left.size(); ++i)
			if (asciiLower(left[i]) != asciiLower(right[i]))
				return false;
		return true;
	}

	void rejected(std::ostream& diagnostics, std::string_view name, std::string_view value, const char* expected)
	{
		diagnostics << "[kz-config] error: " << name << " rejected value '" << value << "': " << expected << '\n';
	}
}

const char* kzgsUpscalerName(KzgsUpscaler provider)
{
	for (const ProviderName& entry : kProviders)
		if (entry.provider == provider)
			return entry.name.data();
	return "unknown";
}

const char* kzgsUpscalerReasonName(KzgsUpscalerReason reason)
{
	switch (reason)
	{
		case KzgsUpscalerReason::Disabled: return "disabled";
		case KzgsUpscalerReason::NotImplemented: return "not_implemented";
		case KzgsUpscalerReason::WaitingForFrame: return "waiting_for_frame";
		case KzgsUpscalerReason::Active: return "active";
		case KzgsUpscalerReason::NoSurface: return "no_surface";
		case KzgsUpscalerReason::NoUpscaleNeeded: return "no_upscale_needed";
		case KzgsUpscalerReason::InvalidDimensions: return "invalid_dimensions";
		case KzgsUpscalerReason::InitializationFailed: return "initialization_failed";
		case KzgsUpscalerReason::DispatchFailed: return "dispatch_failed";
		case KzgsUpscalerReason::PresentFailed: return "present_failed";
		case KzgsUpscalerReason::ProviderMissing: return "provider_missing";
		case KzgsUpscalerReason::ProviderVersionMismatch: return "provider_version_mismatch";
		case KzgsUpscalerReason::ProviderInvalid: return "provider_invalid";
		case KzgsUpscalerReason::InstanceExtensionUnavailable: return "instance_extension_unavailable";
		case KzgsUpscalerReason::DeviceExtensionUnavailable: return "device_extension_unavailable";
		case KzgsUpscalerReason::DeviceFeatureUnavailable: return "device_feature_unavailable";
		case KzgsUpscalerReason::RequiredInputUnavailable: return "required_input_unavailable";
		case KzgsUpscalerReason::UnsupportedColorEncoding: return "unsupported_color_encoding";
		case KzgsUpscalerReason::DeviceRecreationRequired: return "device_recreation_required";
		case KzgsUpscalerReason::DuplicatePresent: return "duplicate_present";
		case KzgsUpscalerReason::ResetFailed: return "reset_failed";
		case KzgsUpscalerReason::DeviceLost: return "device_lost";
		case KzgsUpscalerReason::IdleWaitFailed: return "idle_wait_failed";
	}
	return "unknown";
}

bool kzgsIsValidUpscaler(KzgsUpscaler provider)
{
	for (const ProviderName& entry : kProviders)
		if (entry.provider == provider)
			return true;
	return false;
}

bool kzgsParseUpscaler(std::string_view text, KzgsUpscaler& provider)
{
	for (const ProviderName& entry : kProviders)
		if (equalIgnoringCase(text, entry.name))
		{
			provider = entry.provider;
			return true;
		}
	return false;
}

bool kzgsParseUpscalerSharpening(std::string_view text, int& sharpening)
{
	if (text.empty())
		return false;
	int value = 0;
	const char* const end = text.data() + text.size();
	const auto result = std::from_chars(text.data(), end, value);
	if (result.ec != std::errc() || result.ptr != end || value < 0 || value > 100)
		return false;
	sharpening = value;
	return true;
}

KzgsUpscalerOverrides kzgsUpscalerEnvironmentOverrides(
	const char* provider, const char* sharpening, std::ostream& diagnostics)
{
	KzgsUpscalerOverrides overrides;
	if (provider)
	{
		KzgsUpscaler parsed;
		if (kzgsParseUpscaler(provider, parsed))
			overrides.provider = parsed;
		else
			rejected(diagnostics, "KZ_UPSCALER", provider, kProviderExpected);
	}
	if (sharpening)
	{
		int parsed = 0;
		if (kzgsParseUpscalerSharpening(sharpening, parsed))
			overrides.sharpening = parsed;
		else
			rejected(diagnostics, "KZ_UPSCALER_SHARPENING", sharpening, kSharpeningExpected);
	}
	return overrides;
}

void kzgsMergeUpscalerOverrides(KzgsUpscalerOverrides& lower, const KzgsUpscalerOverrides& higher)
{
	if (higher.provider)
		lower.provider = higher.provider;
	if (higher.sharpening)
		lower.sharpening = higher.sharpening;
}

KzgsUpscalerOptionResult kzgsParseUpscalerOption(
	std::string_view argument, const char* next, KzgsUpscalerOverrides& overrides,
	bool& consumeNext, std::ostream& diagnostics)
{
	consumeNext = false;
	constexpr std::string_view providerOption = "--upscaler";
	constexpr std::string_view sharpeningOption = "--upscaler-sharpening";
	const bool provider = argument == providerOption || argument.starts_with("--upscaler=");
	const bool sharpening = argument == sharpeningOption || argument.starts_with("--upscaler-sharpening=");
	if (!provider && !sharpening)
	{
		if (!argument.starts_with("--upscaler"))
			return KzgsUpscalerOptionResult::NotHandled;
		diagnostics << "[kz-config] error: unknown upscaler option '" << argument << "'\n";
		return KzgsUpscalerOptionResult::Error;
	}

	const std::string_view option = provider ? providerOption : sharpeningOption;
	std::string_view value;
	if (argument == option)
	{
		if (!next)
		{
			diagnostics << "[kz-config] error: " << option << " requires a value\n";
			return KzgsUpscalerOptionResult::Error;
		}
		value = next;
		consumeNext = true;
	}
	else
	{
		value = argument.substr(option.size() + 1);
	}

	if (provider)
	{
		KzgsUpscaler parsed;
		if (!kzgsParseUpscaler(value, parsed))
		{
			rejected(diagnostics, option, value, kProviderExpected);
			return KzgsUpscalerOptionResult::Error;
		}
		overrides.provider = parsed;
	}
	else
	{
		int parsed = 0;
		if (!kzgsParseUpscalerSharpening(value, parsed))
		{
			rejected(diagnostics, option, value, kSharpeningExpected);
			return KzgsUpscalerOptionResult::Error;
		}
		overrides.sharpening = parsed;
	}
	return KzgsUpscalerOptionResult::Parsed;
}
