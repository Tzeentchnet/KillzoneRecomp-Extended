// SPDX-License-Identifier: GPL-3.0+
#include "kzgs.h"
#include "kzgs_fsr1_runtime.h"
#include "kzgs_internal.h"

#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "common/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
	std::atomic<int> validationErrors{0};
	std::atomic<bool> validationSeen{false};
	std::atomic<bool> validationUnavailable{false};

	void Log(int, const char* message)
	{
		if (std::strstr(message, "Inserted device layer \"VK_LAYER_KHRONOS_validation\""))
			validationSeen.store(true);
		if (std::strstr(message, "validation/debug layers requested but are unavailable"))
			validationUnavailable.store(true);
		if (std::strstr(message, "SYNC-HAZARD") || std::strstr(message, "Validation Error") ||
			(std::strstr(message, "VUID-") && !std::strstr(message, "VUID-VkDeviceCreateInfo-enabledLayerCount-12384")))
		{
			std::fprintf(stderr, "%s\n", message);
			validationErrors.fetch_add(1);
		}
	}

	uint32_t Color(int r, int g, int b)
	{
		return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
			(static_cast<uint32_t>(b) << 16) | 0xff000000u;
	}

	std::unique_ptr<GSTexture> Upload(int width, int height, const std::vector<uint32_t>& pixels)
	{
		std::unique_ptr<GSTexture> texture(g_gs_device->CreateTexture(width, height, 1, GSTexture::Format::Color));
		if (!texture || !texture->Update(GSVector4i(0, 0, width, height), pixels.data(), width * 4))
			return {};
		return texture;
	}

	bool Read(GSTexture* texture, int width, int height, std::vector<uint32_t>& pixels)
	{
		if (!texture || texture->GetWidth() != width || texture->GetHeight() != height)
			return false;
		auto download = g_gs_device->CreateDownloadTexture(width, height, GSTexture::Format::Color);
		if (!download)
			return false;
		const GSVector4i rect(0, 0, width, height);
		download->CopyFromTexture(rect, texture, rect, 0);
		download->Flush();
		if (!download->Map(rect))
			return false;
		pixels.resize(static_cast<size_t>(width) * height);
		StringUtil::StrideMemCpy(pixels.data(), width * 4, download->GetMapPointer(),
			download->GetMapPitch(), width * 4, height);
		download->Unmap();
		return true;
	}

	int Difference(uint32_t a, uint32_t b)
	{
		int maximum = 0;
		for (int channel = 0; channel < 4; ++channel)
			maximum = std::max(maximum, std::abs(int((a >> (channel * 8)) & 255) - int((b >> (channel * 8)) & 255)));
		return maximum;
	}
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	HWND hwnd = CreateWindowExW(0, L"STATIC", L"kzgs FSR1 test", WS_POPUP, 0, 0, 64, 64,
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!hwnd)
		return 2;
	alignas(16) uint8_t regs[0x2000]{};
	KzgsConfigV1 cfg;
	cfg.renderer = KzgsRenderer::Vulkan;
	cfg.upscaler = KzgsUpscaler::FSR1;
	cfg.debugDevice = true;
	cfg.vsync = false;
	kzgsSetLogCallback(Log);
	std::string error;
	if (!kzgsOpenV1(kz::host::NativeWindow::win32(hwnd), cfg, regs, &error))
	{
		std::fprintf(stderr, "FAIL: Vulkan open: %s\n", error.c_str());
		DestroyWindow(hwnd);
		return 2;
	}

	int failures = 0;
	kzgs::RunOnGSThread([&]() {
		const auto check = [&](bool condition, const char* name) {
			std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
			if (!condition)
				++failures;
		};
		constexpr int width = 13, height = 11, left = 3, top = 2, cropWidth = 7, cropHeight = 5;
		constexpr int outputWidth = 19, outputHeight = 13;
		std::vector<uint32_t> padded(width * height, Color(255, 0, 255));
		std::vector<uint32_t> tight(cropWidth * cropHeight);
		for (int y = 0; y < cropHeight; ++y)
		{
			for (int x = 0; x < cropWidth; ++x)
			{
				const uint32_t value = Color(20 + 27 * x, 35 + 35 * y, (x + y) % 2 ? 190 : 60);
				padded[(top + y) * width + left + x] = tight[y * cropWidth + x] = value;
			}
		}
		auto paddedTexture = Upload(width, height, padded);
		auto tightTexture = Upload(cropWidth, cropHeight, tight);
		check(paddedTexture && tightTexture, "synthetic input upload");
		if (!paddedTexture || !tightTexture)
			return;

		GSTexture* output = nullptr;
		kzgs::Fsr1TestFailNextInitialization();
		check(!kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
			outputWidth, outputHeight, 0, output), "initialization failure falls back after partial allocation");
		kzgs::Fsr1Configure(false, 0);
		kzgs::Fsr1Configure(true, 0);
		std::vector<uint32_t> croppedEasu, tightEasu, nativeGatherEasu, sharpened;
		check(kzgs::Fsr1TestUpscale(paddedTexture.get(), left, top, left + cropWidth, top + cropHeight,
			outputWidth, outputHeight, 0, output) && Read(output, outputWidth, outputHeight, croppedEasu),
			"odd cropped EASU dispatch");
		check(kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
			outputWidth, outputHeight, 0, output) && Read(output, outputWidth, outputHeight, tightEasu),
			"tight EASU reference dispatch");
		bool matches = croppedEasu.size() == tightEasu.size() && !tightEasu.empty();
		for (size_t i = 0; matches && i < tightEasu.size(); ++i)
			matches = Difference(croppedEasu[i], tightEasu[i]) <= 2;
		check(matches, "crop offset and per-tap edge clamping match tight reference");
		const bool referenceOk = kzgs::Fsr1TestUseNativeGather(true) &&
			kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
				outputWidth, outputHeight, 0, output) && Read(output, outputWidth, outputHeight, nativeGatherEasu);
		kzgs::Fsr1TestUseNativeGather(false);
		check(referenceOk, "independent AMD native-gather reference dispatch");
		matches = nativeGatherEasu.size() == tightEasu.size() && !tightEasu.empty();
		for (size_t i = 0; matches && i < tightEasu.size(); ++i)
			matches = Difference(nativeGatherEasu[i], tightEasu[i]) <= 1;
		check(matches, "clamped EASU matches AMD native textureGather reference");

		check(kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
			outputWidth, outputHeight, 100, output) && Read(output, outputWidth, outputHeight, sharpened),
			"independent RCAS dispatch");
		check(!sharpened.empty() && sharpened != tightEasu, "RCAS changes non-flat edges");

		const uint32_t flat = Color(66, 112, 204);
		std::fill(padded.begin(), padded.end(), Color(255, 0, 255));
		for (int y = 0; y < cropHeight; ++y)
			for (int x = 0; x < cropWidth; ++x)
				padded[(top + y) * width + left + x] = flat;
		auto flatTexture = Upload(width, height, padded);
		std::vector<uint32_t> flatOutput;
		check(flatTexture && kzgs::Fsr1TestUpscale(flatTexture.get(), left, top, left + cropWidth, top + cropHeight,
			17, 11, 100, output) && Read(output, 17, 11, flatOutput), "resize and partial-tile RCAS dispatch");
		check(flatOutput.size() == 17u * 11u &&
			std::all_of(flatOutput.begin(), flatOutput.end(), [&](uint32_t pixel) { return Difference(pixel, flat) <= 2; }),
			"flat-field identity including crop edges and last partial tile");
		bool queued = true;
		for (int i = 0; i < 48 && queued; ++i)
			queued = kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
				i % 2 ? 19 : 17, i % 2 ? 13 : 11, i % 3 ? 100 : 0, output);
		std::vector<uint32_t> queuedOutput;
		check(queued && Read(output, 19, 13, queuedOutput), "queued resize and sharpening transitions");
		check(!kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
			cropWidth, cropHeight, 100, output) && output == nullptr, "identity does not dispatch");

		auto* device = static_cast<GSDeviceVK*>(g_gs_device.get());
		check(kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
			outputWidth, outputHeight, 100, output), "record FSR1 work before uncertain retirement");
		device->ExecuteCommandBuffer(false);
		auto beforeRetire = kzgs::Fsr1TestLifetime();
		check(kzgs::Fsr1TestRetireResources(VK_ERROR_UNKNOWN), "retain live resources on non-device-loss idle failure");
		auto afterRetire = kzgs::Fsr1TestLifetime();
		check(afterRetire.abandoned == beforeRetire.abandoned + 1 &&
			afterRetire.destroyed == beforeRetire.destroyed && afterRetire.deviceLost == beforeRetire.deviceLost,
			"uncertain completion never explicitly destroys FSR1 children or reports device loss");
		KzgsUpscalerStatus retirementStatus;
		check(kzgsGetUpscalerStatus(retirementStatus) &&
			retirementStatus.reason == KzgsUpscalerReason::InitializationFailed,
			"non-device-loss retirement failure reports initialization failure");
		const bool actuallyIdle = vkDeviceWaitIdle(device->GetDevice()) == VK_SUCCESS;
		check(actuallyIdle, "test GPU becomes idle before releasing retained fixtures");
		if (!actuallyIdle)
		{
			kzgs::Fsr1TestAbandonRetained();
			return;
		}
		check(kzgs::Fsr1TestReleaseRetainedAfterIdle(),
			"test-only retained fixtures are released only after proven completion");

		check(kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
			outputWidth, outputHeight, 0, output), "FSR1 can initialize after completed retirement");
		device->ExecuteCommandBuffer(false);
		check(vkDeviceWaitIdle(device->GetDevice()) == VK_SUCCESS, "completed-retirement GPU idle");
		beforeRetire = kzgs::Fsr1TestLifetime();
		check(kzgs::Fsr1TestRetireResources(VK_SUCCESS), "retire FSR1 resources after successful idle");
		afterRetire = kzgs::Fsr1TestLifetime();
		check(afterRetire.destroyed == beforeRetire.destroyed + 1 &&
			afterRetire.abandoned == beforeRetire.abandoned && afterRetire.deviceLost == beforeRetire.deviceLost,
			"successful idle destroys completed resources without a loss report");

		check(kzgs::Fsr1TestUpscale(tightTexture.get(), 0, 0, cropWidth, cropHeight,
			outputWidth, outputHeight, 0, output), "FSR1 can reinitialize before device-loss simulation");
		device->ExecuteCommandBuffer(false);
		check(vkDeviceWaitIdle(device->GetDevice()) == VK_SUCCESS, "device-loss simulation starts idle");
		beforeRetire = kzgs::Fsr1TestLifetime();
		check(kzgs::Fsr1TestRetireResources(VK_ERROR_DEVICE_LOST), "retire on terminal device loss");
		afterRetire = kzgs::Fsr1TestLifetime();
		check(afterRetire.destroyed == beforeRetire.destroyed + 1 &&
			afterRetire.deviceLost == beforeRetire.deviceLost + 1 &&
			afterRetire.abandoned == beforeRetire.abandoned,
			"terminal device loss is distinguished from uncertain completion");
		check(kzgsGetUpscalerStatus(retirementStatus) &&
			retirementStatus.reason == KzgsUpscalerReason::DeviceLost,
			"terminal retirement reports device loss distinctly");
	});

	kzgsClose();
	DestroyWindow(hwnd);
	if (!validationSeen.load() || validationUnavailable.load() || validationErrors.load())
	{
		std::fprintf(stderr, "FAIL: validation seen=%d unavailable=%d errors=%d\n",
			validationSeen.load(), validationUnavailable.load(), validationErrors.load());
		++failures;
	}
	std::printf("%s: FSR1 Vulkan vectors and synchronization validation (%d failures)\n",
		failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
