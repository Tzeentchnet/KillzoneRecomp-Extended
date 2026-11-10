// XeSS Vulkan requirements. Initialization is deliberately blocked: see the accompanying documentation.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_vendor_backend.h"

#include <xess/xess_vk.h>

#include <algorithm>
#include <cstring>

namespace
{
	using namespace kzgs::vendor;

	Result sdkFailure(xess_result_t code, KzgsProviderResult result, const char* message)
	{
		KzgsProviderReason reason = KZGS_PROVIDER_REASON_PROVIDER_FAILURE;
		if (code == XESS_RESULT_ERROR_UNSUPPORTED_DEVICE || code == XESS_RESULT_ERROR_UNSUPPORTED_DRIVER)
			reason = KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE;
		else if (code == XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY)
			reason = KZGS_PROVIDER_REASON_OUT_OF_MEMORY;
		return failure(result, reason, message, static_cast<int32_t>(code));
	}

	bool copyNames(Requirements& out, uint32_t count, const char* const* names)
	{
		if (count > 256 || (count && !names))
			return false;
		for (uint32_t i = 0; i < count; ++i)
		{
			if (!names[i] || !names[i][0] || strnlen_s(names[i], VK_MAX_EXTENSION_NAME_SIZE) == VK_MAX_EXTENSION_NAME_SIZE)
				return false;
			out.names.emplace_back(names[i]);
		}
		return true;
	}

	Result instanceRequirements(Requirements& out)
	{
		xess_version_t version{};
		xess_result_t result = xessGetVersion(&version);
		if (result != XESS_RESULT_SUCCESS)
			return sdkFailure(result, KZGS_PROVIDER_ERROR_VERSION, "Could not query the XeSS runtime version.");
		if (version.major != 3)
			return failure(KZGS_PROVIDER_ERROR_VERSION, KZGS_PROVIDER_REASON_VERSION,
				"XeSS runtime major version 3 is required.", version.major);
		uint32_t count = 0;
		uint32_t apiVersion = 0;
		const char* const* names = nullptr;
		result = xessVKGetRequiredInstanceExtensions(&count, &names, &apiVersion);
		if (result != XESS_RESULT_SUCCESS)
			return sdkFailure(result, KZGS_PROVIDER_ERROR_EXTENSIONS, "XeSS instance extension query failed.");
		if (VK_API_VERSION_VARIANT(apiVersion) != 0 || VK_API_VERSION_MAJOR(apiVersion) != 1 ||
			VK_API_VERSION_MINOR(apiVersion) > 1)
			return failure(KZGS_PROVIDER_ERROR_VERSION, KZGS_PROVIDER_REASON_VERSION,
				"XeSS reported an invalid or unsupported minimum Vulkan instance API; this host uses Vulkan 1.1.", apiVersion);
		if (!copyNames(out, count, names))
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_MISSING_EXTENSION,
				"XeSS returned a malformed instance extension list.");
		return {};
	}

	bool ensureExtension(const KzgsProviderDeviceQuery& query, Requirements& out, const char* name)
	{
		if (std::find(out.names.begin(), out.names.end(), name) != out.names.end())
			return true;
		const auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
			query.get_instance_proc_addr(query.instance, "vkEnumerateDeviceExtensionProperties"));
		if (!enumerate)
			return false;
		uint32_t count = 0;
		if (enumerate(query.physical_device, nullptr, &count, nullptr) != VK_SUCCESS || count > 4096)
			return false;
		std::vector<VkExtensionProperties> properties(count);
		if (count && enumerate(query.physical_device, nullptr, &count, properties.data()) != VK_SUCCESS)
			return false;
		properties.resize(count);
		for (const VkExtensionProperties& property : properties)
		{
			if (std::strcmp(property.extensionName, name) == 0)
			{
				out.names.emplace_back(name);
				return true;
			}
		}
		return false;
	}

	Result feature(Requirements& out, const KzgsProviderDeviceQuery& query, VkStructureType type,
		const VkBool32* values, size_t count, const char* extension)
	{
		for (size_t i = 0; i < count; ++i)
		{
			if (values[i] > VK_TRUE)
				return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
					"XeSS returned a malformed Vulkan feature bit.");
		}
		if (std::any_of(out.blocks.begin(), out.blocks.end(),
			[type](const KzgsProviderFeatureBlock& block) { return block.s_type == type; }))
			return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
				"XeSS returned duplicate Vulkan feature structures.");
		if (extension && !ensureExtension(query, out, extension))
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_MISSING_EXTENSION,
				"A Vulkan extension needed for an XeSS feature is unavailable.");
		out.addFeature(type, values, count);
		return {};
	}

	template <typename T>
	bool onlySelectedBits(const T& value, size_t first, size_t last, size_t selected1, size_t selected2)
	{
		const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
		for (size_t offset = first; offset <= last; offset += sizeof(VkBool32))
		{
			VkBool32 bit = 0;
			std::memcpy(&bit, bytes + offset, sizeof(bit));
			if (bit > VK_TRUE || (bit && offset != selected1 && offset != selected2))
				return false;
		}
		return true;
	}

	Result deviceRequirements(const KzgsProviderDeviceQuery& query, Requirements& out)
	{
		uint32_t count = 0;
		const char* const* names = nullptr;
		xess_result_t result = xessVKGetRequiredDeviceExtensions(query.instance, query.physical_device, &count, &names);
		if (result != XESS_RESULT_SUCCESS)
			return sdkFailure(result, KZGS_PROVIDER_ERROR_EXTENSIONS, "XeSS device extension query failed.");
		if (!copyNames(out, count, names))
			return failure(KZGS_PROVIDER_ERROR_EXTENSIONS, KZGS_PROVIDER_REASON_MISSING_EXTENSION,
				"XeSS returned a malformed device extension list.");

		VkPhysicalDeviceFeatures2 core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		void* chain = &core;
		result = xessVKGetRequiredDeviceFeatures(query.instance, query.physical_device, &chain);
		if (result != XESS_RESULT_SUCCESS)
			return sdkFailure(result, KZGS_PROVIDER_ERROR_FEATURES, "XeSS device feature query failed.");
		bool coreSeen = false;
		bool mutableSeen = false;
		uint32_t nodes = 0;
		for (auto* node = static_cast<VkBaseOutStructure*>(chain); node; node = node->pNext)
		{
			if (++nodes > 32)
				return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
					"XeSS feature chain is too long or cyclic.");
			Result converted;
			switch (node->sType)
			{
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
				if (coreSeen)
					return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
						"XeSS returned duplicate core features.");
				out.core = reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node)->features;
				coreSeen = true;
				break;
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT:
			{
				const VkBool32 bit = reinterpret_cast<const VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT*>(node)->mutableDescriptorType;
				mutableSeen = bit == VK_TRUE;
				converted = feature(out, query, node->sType, &bit, 1, VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME);
				break;
			}
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES:
			{
				const auto& value = *reinterpret_cast<const VkPhysicalDeviceShaderFloat16Int8Features*>(node);
				const VkBool32 bits[] = {value.shaderFloat16, value.shaderInt8};
				converted = feature(out, query, node->sType, bits, 2, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
				break;
			}
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES:
			{
				const VkBool32 bit = reinterpret_cast<const VkPhysicalDeviceShaderIntegerDotProductFeatures*>(node)->shaderIntegerDotProduct;
				converted = feature(out, query, node->sType, &bit, 1, VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME);
				break;
			}
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
			{
				const auto& value = *reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node);
				if (!onlySelectedBits(value, offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge),
					offsetof(VkPhysicalDeviceVulkan12Features, subgroupBroadcastDynamicId),
					offsetof(VkPhysicalDeviceVulkan12Features, shaderFloat16),
					offsetof(VkPhysicalDeviceVulkan12Features, shaderInt8)))
					return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
						"XeSS requested Vulkan 1.2 features not representable by this host.");
				const VkBool32 bits[] = {value.shaderFloat16, value.shaderInt8};
				if (bits[0] || bits[1])
					converted = feature(out, query, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
						bits, 2, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
				break;
			}
			case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
			{
				const auto& value = *reinterpret_cast<const VkPhysicalDeviceVulkan13Features*>(node);
				if (!onlySelectedBits(value, offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess),
					offsetof(VkPhysicalDeviceVulkan13Features, maintenance4),
					offsetof(VkPhysicalDeviceVulkan13Features, shaderIntegerDotProduct),
					offsetof(VkPhysicalDeviceVulkan13Features, shaderIntegerDotProduct)))
					return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
						"XeSS requested Vulkan 1.3 features not representable by this host.");
				const VkBool32 bit = value.shaderIntegerDotProduct;
				if (bit)
					converted = feature(out, query, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES,
						&bit, 1, VK_KHR_SHADER_INTEGER_DOT_PRODUCT_EXTENSION_NAME);
				break;
			}
			default:
				return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
					"XeSS requested an unsupported Vulkan feature structure.", static_cast<int32_t>(node->sType));
			}
			if (converted.code != KZGS_PROVIDER_OK)
				return converted;
		}
		if (!coreSeen || !out.core.shaderStorageImageWriteWithoutFormat || !mutableSeen)
			return failure(KZGS_PROVIDER_ERROR_FEATURES, KZGS_PROVIDER_REASON_MISSING_FEATURE,
				"XeSS did not report its documented required core and mutable-descriptor features.");
		return {};
	}

	Result create(const KzgsProviderCreateInfo&, void**)
	{
		// xessVKInit may create and submit copy-queue work; ABI 1.0 explicitly forbids this in create/evaluate.
		return failure(KZGS_PROVIDER_ERROR_CREATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
			"XeSS initialization may submit GPU work, which provider ABI 1.0 does not permit.");
	}

	Result evaluate(void*, const KzgsProviderFrame&, bool)
	{
		return failure(KZGS_PROVIDER_ERROR_EVALUATE, KZGS_PROVIDER_REASON_PROVIDER_FAILURE,
			"XeSS initialization is blocked by the provider synchronization contract.");
	}

	void destroy(void*, bool) {}

	const Backend XessBackend{KZGS_PROVIDER_KIND_XESS, "Intel XeSS (initialization blocked)",
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, instanceRequirements, deviceRequirements, create, evaluate, destroy};
}

namespace kzgs::vendor { const Backend& backend() { return XessBackend; } }
