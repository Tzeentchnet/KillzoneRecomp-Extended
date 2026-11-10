// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kzgs_upscaler_provider.h"

#include <string>
#include <vector>

namespace kzgs::vendor
{
	struct Result
	{
		KzgsProviderResult code = KZGS_PROVIDER_OK;
		KzgsProviderReason reason = KZGS_PROVIDER_REASON_NONE;
		int32_t native = 0;
		const char* message = nullptr;
	};

	inline Result failure(KzgsProviderResult code, KzgsProviderReason reason, const char* message, int32_t native = 0)
	{
		return {code, reason, native, message};
	}

	using InitializationWork = Result (*)(void*);

	bool pinProviderModule();
	Result runInitialization(const KzgsProviderCreateInfo& info, uint32_t requiredGuarantees,
		InitializationWork work, void* workData);

	struct Requirements
	{
		std::vector<std::string> names;
		std::vector<const char*> namePointers;
		VkPhysicalDeviceFeatures core{};
		std::vector<std::vector<VkBool32>> bits;
		std::vector<KzgsProviderFeatureBlock> blocks;

		void addFeature(VkStructureType type, const VkBool32* values, size_t count)
		{
			bits.emplace_back(values, values + count);
			blocks.push_back({sizeof(KzgsProviderFeatureBlock), type, static_cast<uint32_t>(count), nullptr});
		}

		KzgsProviderRequirements describe()
		{
			namePointers.clear();
			for (const std::string& name : names)
				namePointers.push_back(name.c_str());
			for (size_t i = 0; i < blocks.size(); ++i)
				blocks[i].required = bits[i].data();
			KzgsProviderRequirements out{};
			out.struct_size = sizeof(out);
			out.extension_count = static_cast<uint32_t>(namePointers.size());
			out.extensions = namePointers.data();
			out.core_features = core;
			out.feature_block_count = static_cast<uint32_t>(blocks.size());
			out.feature_blocks = blocks.data();
			out.feature_block_stride = sizeof(KzgsProviderFeatureBlock);
			return out;
		}
	};

	struct Backend
	{
		KzgsProviderKind kind;
		const char* name;
		VkImageLayout inputLayout;
		Result (*instanceRequirements)(Requirements&);
		Result (*deviceRequirements)(const KzgsProviderDeviceQuery&, Requirements&);
		Result (*create)(const KzgsProviderCreateInfo&, void**);
		Result (*evaluate)(void*, const KzgsProviderFrame&, bool reset);
		void (*destroy)(void*, bool deviceLost);
		bool requireCreationThread;
	};

	// Each independent provider DLL defines exactly one backend.
	const Backend& backend();
}
