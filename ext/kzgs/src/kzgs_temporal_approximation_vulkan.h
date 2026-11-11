// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "kzgs_upscaler_provider.h"

#include <cstdint>
#include <memory>
#include <string>

namespace kzgs
{
	struct TemporalApproximationImages
	{
		KzgsProviderImage color{};
		KzgsProviderImage depth{};
		KzgsProviderImage motion{};
		KzgsProviderImage reactive{};
		uint64_t colorId = 0;
		uint64_t depthId = 0;
		uint64_t motionId = 0;
		uint64_t reactiveId = 0;
	};

	class TemporalApproximationVulkan
	{
	public:
		static std::unique_ptr<TemporalApproximationVulkan> Create(
			GSDeviceVK& device, VkExtent2D extent, std::string& error);
		~TemporalApproximationVulkan();

		bool Record(const GSDeviceVK::ExternalCommands& commands,
			const KzgsProviderRect& sourceRect, float jitterX, float jitterY,
			bool reset, TemporalApproximationImages& output);
		void PresentationComplete(bool succeeded);
		void Abandon();
		const char* LastError() const;

	private:
		struct Impl;
		explicit TemporalApproximationVulkan(std::unique_ptr<Impl> impl);
		std::unique_ptr<Impl> m_impl;
	};
}
