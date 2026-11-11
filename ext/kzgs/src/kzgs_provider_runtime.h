// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kzgs_upscaler.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "kzgs_upscaler_provider.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef KZGS_PROVIDER_TESTING
#include "../test/kzgs_provider_test_hooks.h"
#endif

class GSTexture;
enum class KzgsFsr3InputMode : uint64_t;
struct KzgsFsr3JitterState;

namespace kzgs
{
	struct ProviderStatus
	{
		KzgsUpscalerReason reason = KzgsUpscalerReason::Disabled;
		uint32_t result = KZGS_PROVIDER_OK;
		uint32_t providerReason = KZGS_PROVIDER_REASON_NONE;
		int32_t nativeCode = 0;
		char message[192]{};
	};

	using ProviderReport = void (*)(const ProviderStatus& status, bool applied);

	void ProviderPrepare(KzgsUpscaler requested, bool vulkan, ProviderReport report);
	void ProviderRemove();
	bool ProviderHasNegotiation();
	ProviderStatus ProviderConfigure(KzgsUpscaler requested, bool vulkan, int sharpening,
		KzgsFsr3InputMode fsr3InputMode);
	void ProviderStop();
	void ProviderReset(KzgsProviderResetReason reason);
	void ProviderBeginVsync();
	void ProviderEndVsync();
	bool ProviderCanCapture();
	void ProviderCaptureUse(GSTexture* texture);
	bool ProviderGetFsr3JitterState(KzgsFsr3JitterState& out);

	void ProviderInstanceExtensions(void*, const std::vector<VkExtensionProperties>&, std::vector<std::string>&);
	void ProviderDeviceExtensions(void*, VkInstance, VkPhysicalDevice,
		const std::vector<VkExtensionProperties>&, std::vector<std::string>&);
	bool ProviderDeviceFeatures(void*, VkInstance, VkPhysicalDevice, GSDeviceVK::IntegrationDeviceFeatures&);
	void ProviderDeviceCreated(GSDeviceVK&, bool instance_extensions, bool device_extensions, bool features);
	void ProviderDeviceDestroying(GSDeviceVK&, VkResult idle_result);

}
