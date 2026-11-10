// SPDX-License-Identifier: GPL-3.0+
#include "kzgs.h"
#include "kzgs_internal.h"
#include "kzgs_mock_control.h"
#include "kzgs_provider_runtime.h"

#include "GS/Renderers/Common/GSDevice.h"
#include "common/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	std::atomic<int> validationErrors{0};
	std::atomic<bool> validationSeen{false};
	std::atomic<bool> validationUnavailable{false};

	void Check(bool condition, const char* name)
	{
		std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
		if (!condition)
			++failures;
	}

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

	struct Mock
	{
		HMODULE module = nullptr;
		KzgsMockSetMode set = nullptr;
		KzgsMockGetStats get = nullptr;
		KzgsProviderGetApi api = nullptr;

		KzgsMockStats Stats() const
		{
			KzgsMockStats value{};
			get(&value);
			return value;
		}
	};

	Mock LoadMock(const std::wstring& path)
	{
		Mock mock;
		mock.module = LoadLibraryExW(path.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (mock.module)
		{
			mock.set = reinterpret_cast<KzgsMockSetMode>(GetProcAddress(mock.module, "kzgs_mock_set_mode"));
			mock.get = reinterpret_cast<KzgsMockGetStats>(GetProcAddress(mock.module, "kzgs_mock_get_stats"));
			mock.api = reinterpret_cast<KzgsProviderGetApi>(GetProcAddress(mock.module, "kzgs_upscaler_get_api"));
		}
		return mock;
	}

	bool Open(HWND window, KzgsUpscalerReason reason, KzgsUpscaler provider = KzgsUpscaler::DLSS,
		int sharpening = 0)
	{
		alignas(16) static uint8_t regs[0x2000]{};
		KzgsConfig cfg;
		cfg.renderer = KzgsRenderer::Vulkan;
		cfg.upscaler = provider;
		cfg.upscalerSharpening = sharpening;
		cfg.debugDevice = true;
		cfg.vsync = false;
		std::string error;
		const bool opened = kzgsOpen(kz::host::NativeWindow::win32(window), cfg, regs, &error);
		if (!opened)
			std::fprintf(stderr, "Vulkan open failed: %s\n", error.c_str());
		Check(opened, "Vulkan device remains available");
		if (!opened)
			return false;
		KzgsUpscalerStatus status;
		const bool hasStatus = kzgsGetUpscalerStatus(status);
		if (!hasStatus || status.reason != reason)
			std::fprintf(stderr, "expected %s, got %s\n", kzgsUpscalerReasonName(reason),
				hasStatus ? kzgsUpscalerReasonName(status.reason) : "no status");
		Check(hasStatus && status.requested == provider && status.effective == KzgsUpscaler::Off &&
			status.requestedSharpening == sharpening && status.effectiveSharpening == 0 &&
			status.reason == reason, "requested/effective startup status and native fallback");
		return true;
	}

	uint32_t Color(int x, int y)
	{
		return static_cast<uint32_t>(30 + 40 * x) | (static_cast<uint32_t>(50 + 50 * y) << 8) |
			(90u << 16) | 0xff000000u;
	}

	std::unique_ptr<GSTexture> Upload()
	{
		std::vector<uint32_t> pixels(16);
		for (int y = 0; y < 4; ++y)
			for (int x = 0; x < 4; ++x)
				pixels[y * 4 + x] = Color(x, y);
		std::unique_ptr<GSTexture> texture(g_gs_device->CreateTexture(4, 4, 1, GSTexture::Format::Color));
		if (!texture || !texture->Update(GSVector4i(0, 0, 4, 4), pixels.data(), 16))
			return {};
		return texture;
	}

	bool Read(GSTexture* texture, uint32_t width, uint32_t height, std::vector<uint32_t>& pixels)
	{
		if (!texture || texture->GetWidth() != static_cast<int>(width) ||
			texture->GetHeight() != static_cast<int>(height))
			return false;
		auto download = g_gs_device->CreateDownloadTexture(width, height, GSTexture::Format::Color);
		if (!download)
			return false;
		const GSVector4i rect(0, 0, static_cast<int>(width), static_cast<int>(height));
		download->CopyFromTexture(rect, texture, rect, 0);
		download->Flush();
		if (!download->Map(rect))
			return false;
		pixels.resize(static_cast<size_t>(width) * height);
		StringUtil::StrideMemCpy(pixels.data(), width * sizeof(uint32_t), download->GetMapPointer(),
			download->GetMapPitch(), width * sizeof(uint32_t), height);
		download->Unmap();
		return true;
	}

	void CheckStatus(KzgsUpscalerReason reason, uint32_t result, int32_t native = 0,
		uint32_t providerReason = KZGS_PROVIDER_REASON_NONE, const char* message = nullptr)
	{
		KzgsUpscalerStatus status;
		Check(kzgsGetUpscalerStatus(status) && status.reason == reason &&
			status.providerResult == result && status.providerNativeCode == native &&
			status.providerReason == providerReason && (!message || std::strcmp(status.providerMessage, message) == 0),
			"structured provider status");
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const bool uncertainIdle = argc == 3 && std::strcmp(argv[2], "--uncertain-idle") == 0;
	const bool uncertainInit = argc == 3 && std::strcmp(argv[2], "--uncertain-init") == 0;
	const bool uncertainReset = argc == 3 && std::strcmp(argv[2], "--uncertain-reset") == 0;
	if (argc != 2 && !uncertainIdle && !uncertainInit && !uncertainReset)
		return 2;
	const std::wstring path = std::filesystem::absolute(argv[1]).wstring();
	Mock mock = LoadMock(path);
	if (!mock.module || !mock.set || !mock.get || !mock.api)
	{
		std::fprintf(stderr, "Could not load mock provider and control API\n");
		return 2;
	}

	struct SmallApi
	{
		uint32_t size;
		uint32_t version;
		uint8_t guard[32];
	} small{};
	small.size = 8;
	std::fill(std::begin(small.guard), std::end(small.guard), 0x5a);
	KzgsProviderStatus abiStatus{};
	abiStatus.struct_size = sizeof(abiStatus);
	Check(mock.api(KZGS_PROVIDER_ABI_VERSION, reinterpret_cast<KzgsProviderApi*>(&small), &abiStatus) ==
			KZGS_PROVIDER_OK && small.size == sizeof(KzgsProviderApi) && small.version == KZGS_PROVIDER_ABI_VERSION &&
		std::all_of(std::begin(small.guard), std::end(small.guard), [](uint8_t v) { return v == 0x5a; }),
		"older, smaller API buffer is not overrun");
	small.size = 8;
	Check(mock.api(KZGS_PROVIDER_VERSION(1, 0), reinterpret_cast<KzgsProviderApi*>(&small), &abiStatus) ==
			KZGS_PROVIDER_OK && small.version == KZGS_PROVIDER_VERSION(1, 0),
		"1.0 callers negotiate a compatible provider version");
	struct LargerApi
	{
		KzgsProviderApi api;
		uint8_t future[32];
	} larger{};
	larger.api.struct_size = sizeof(larger);
	std::fill(std::begin(larger.future), std::end(larger.future), 0xa5);
	Check(mock.api(KZGS_PROVIDER_ABI_VERSION, &larger.api, &abiStatus) == KZGS_PROVIDER_OK &&
		larger.api.struct_size == sizeof(KzgsProviderApi) &&
		std::all_of(std::begin(larger.future), std::end(larger.future), [](uint8_t v) { return v == 0xa5; }),
		"newer, larger API buffer preserves unknown trailing fields");
	Check(mock.api(KZGS_PROVIDER_VERSION(2, 0), &larger.api, &abiStatus) == KZGS_PROVIDER_ERROR_VERSION &&
		abiStatus.reason == KZGS_PROVIDER_REASON_VERSION, "major ABI mismatch is rejected");
	struct SmallInfo
	{
		uint32_t size;
		KzgsProviderKind kind;
		uint8_t guard[32];
	} smallInfo{};
	smallInfo.size = 8;
	std::fill(std::begin(smallInfo.guard), std::end(smallInfo.guard), 0x3c);
	Check(larger.api.query_info(reinterpret_cast<KzgsProviderInfo*>(&smallInfo), &abiStatus) == KZGS_PROVIDER_OK &&
		smallInfo.size == sizeof(KzgsProviderInfo) && smallInfo.kind == KZGS_PROVIDER_KIND_DLSS &&
		std::all_of(std::begin(smallInfo.guard), std::end(smallInfo.guard), [](uint8_t v) { return v == 0x3c; }),
		"smaller capability buffer is not overrun");
	KzgsProviderSync partialSync{};
	partialSync.struct_size = sizeof(uint32_t);
	KzgsProviderFrame partialFrame{};
	partialFrame.struct_size = sizeof(partialFrame);
	partialFrame.sync = &partialSync;
	uint8_t unusedContext = 0;
	Check(larger.api.evaluate(&unusedContext, &partialFrame, &abiStatus) == KZGS_PROVIDER_ERROR_ARGUMENT,
		"short nested synchronization descriptor is rejected before use");

	HWND window = CreateWindowExW(0, L"STATIC", L"kzgs provider test", WS_POPUP, 0, 0, 64, 64,
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!window)
		return 2;
	kzgsSetLogCallback(Log);
	if (uncertainIdle || uncertainInit || uncertainReset)
	{
		kzgs::ProviderTestSetPath(path.c_str());
		kzgs::ProviderTestAllowSpatialMock(true);
		mock.set(uncertainInit ? MOCK_CREATE_UNRESOLVED_SYNC :
			uncertainReset ? MOCK_RESET_UNRESOLVED_SYNC : 0);
		const KzgsMockStats beforeFailure = mock.Stats();
		if (Open(window, KzgsUpscalerReason::WaitingForFrame))
		{
			kzgs::RunOnGSThread([&]() {
				auto* device = static_cast<GSDeviceVK*>(g_gs_device.get());
				Check(vkDeviceWaitIdle(device->GetDevice()) == VK_SUCCESS,
					"simulated synchronization error starts with an actually idle GPU");
				if (uncertainInit || uncertainReset)
				{
					auto source = Upload();
					GSTexture* output = nullptr;
					Check(source && !kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 1, false, output),
						"unresolved provider operation fails closed");
					CheckStatus(uncertainInit ? KzgsUpscalerReason::InitializationFailed : KzgsUpscalerReason::ResetFailed,
						KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED, uncertainInit ? -72 : -73,
						KZGS_PROVIDER_REASON_SYNCHRONIZATION,
						uncertainInit ? "mock unresolved initialization" : "mock unresolved reset");
					Check(mock.Stats().destroys == beforeFailure.destroys,
						"unresolved partial context is abandoned without invoking destroy");
					const KzgsMockStats afterFailure = mock.Stats();
					Check(!kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 2, false, output) &&
						mock.Stats().creates == afterFailure.creates && mock.Stats().evaluates == afterFailure.evaluates,
						"unresolved completion is not retried on the same device");
					CheckStatus(uncertainInit ? KzgsUpscalerReason::InitializationFailed : KzgsUpscalerReason::ResetFailed,
						KZGS_PROVIDER_ERROR_SYNCHRONIZATION_UNRESOLVED, uncertainInit ? -72 : -73,
						KZGS_PROVIDER_REASON_SYNCHRONIZATION,
						uncertainInit ? "mock unresolved initialization" : "mock unresolved reset");
					device->ExecuteCommandBuffer(true);
					source.reset();
				}
				else
				{
					kzgs::ProviderDeviceDestroying(*device, VK_ERROR_UNKNOWN);
					CheckStatus(KzgsUpscalerReason::InitializationFailed, KZGS_PROVIDER_ERROR_INTERNAL,
						static_cast<int32_t>(VK_ERROR_UNKNOWN));
				}
			});
			kzgsClose();
		}
		const KzgsMockStats before = mock.Stats();
		Check(before.destroys == beforeFailure.destroys,
			"uncertain completion never destroys abandoned contexts during teardown");
		mock.set(0);
		if (Open(window, KzgsUpscalerReason::InitializationFailed))
		{
			kzgsClose();
			Check(mock.Stats().creates == before.creates && mock.Stats().evaluates == before.evaluates,
				"uncertain completion disables providers on later devices");
		}
		kzgs::ProviderTestSetPath(nullptr);
		kzgs::ProviderTestAllowSpatialMock(false);
		FreeLibrary(mock.module);
		Check(GetModuleHandleW(path.c_str()) != nullptr,
			"uncertain GPU completion retains the provider module");
		Check(validationSeen.load() && !validationUnavailable.load() && validationErrors.load() == 0,
			"uncertain-completion fallback passes Vulkan validation");
		DestroyWindow(window);
		std::printf("%s: uncertain-%s provider fallback\n", failures ? "FAIL" : "PASS",
			uncertainInit ? "initialization" : uncertainReset ? "reset" : "idle");
		return failures ? 1 : 0;
	}
	kzgs::ProviderTestSetPath((path + L".missing").c_str());
	for (const KzgsUpscaler provider : {KzgsUpscaler::DLSS, KzgsUpscaler::XeSS})
	{
		if (Open(window, KzgsUpscalerReason::ProviderMissing, provider, 37))
		{
			KzgsUpscalerStatus status;
			Check(kzgsGetUpscalerStatus(status) && !status.temporalObserved && !status.temporalValid &&
				status.temporalMissingInputs == KZGS_TEMPORAL_REQUIRED,
				"missing DLL still reports the unobserved temporal contract");
			kzgsClose();
		}
	}
	kzgs::ProviderTestSetPath(path.c_str());

	for (const KzgsUpscaler provider : {KzgsUpscaler::DLSS, KzgsUpscaler::XeSS})
	{
		const uint32_t kind = provider == KzgsUpscaler::XeSS ? MOCK_KIND_XESS : 0;
		for (const uint32_t capability : {0u, static_cast<uint32_t>(MOCK_TEMPORAL_COLOR_ONLY),
				static_cast<uint32_t>(MOCK_TEMPORAL_COLOR_ONLY | MOCK_DUAL_CAPABILITY),
				static_cast<uint32_t>(MOCK_CREATE_FAILURE | MOCK_EVALUATE_FAILURE)})
		{
			mock.set(kind | capability);
			const KzgsMockStats before = mock.Stats();
			if (Open(window, KzgsUpscalerReason::RequiredInputUnavailable, provider, 37))
			{
				KzgsUpscalerStatus status;
				Check(kzgsGetUpscalerStatus(status) && !status.temporalObserved && !status.temporalValid &&
					status.temporalMissingInputs == KZGS_TEMPORAL_REQUIRED &&
					std::strstr(status.providerMessage, "motion") &&
					std::strstr(status.providerMessage, "world_color"),
					"gate reports all named inputs before a frame is observed");
				kzgsVsync(0, false);
				kzgsSync();
				kzgsReset();
				kzgs::RunOnGSThread([] { kzgs::ProviderTestRedraw(); });
				kzgsVsync(0, false);
				kzgsSync();
				Check(kzgsGetUpscalerStatus(status) && status.temporalObserved && !status.temporalValid &&
					status.requested == provider && status.effective == KzgsUpscaler::Off &&
					status.effectiveSharpening == 0,
					"observed duplicate and reset retain native fallback");
				KzgsTemporalDiagnostics diagnostics;
				Check(kzgsGetTemporalDiagnostics(diagnostics) && !diagnostics.temporalValid &&
					!diagnostics.historyFrameId && !diagnostics.committedHistoryFrames &&
					status.temporalMissingInputs == diagnostics.missingInputs &&
					status.temporalInvalidInputs == diagnostics.invalidInputs &&
					status.temporalInvalidReasons == diagnostics.invalidReasons,
					"status identifies the exact last observed temporal input failures");
				kzgsClose();
				const KzgsMockStats after = mock.Stats();
				Check(after.creates == before.creates && after.evaluates == before.evaluates &&
					after.resets == before.resets,
					"unqualified DLSS/XeSS never create, evaluate, or reset vendor history");
			}
		}
	}
	mock.set(MOCK_QUERY_FAILURE);
	if (Open(window, KzgsUpscalerReason::ProviderInvalid))
	{
		CheckStatus(KzgsUpscalerReason::ProviderInvalid, KZGS_PROVIDER_ERROR_INTERNAL, -404,
			KZGS_PROVIDER_REASON_PROVIDER_FAILURE, "mock capability query failure");
		kzgsClose();
	}
	mock.set(MOCK_UNSUPPORTED_DEVICE);
	if (Open(window, KzgsUpscalerReason::DeviceExtensionUnavailable))
	{
		CheckStatus(KzgsUpscalerReason::DeviceExtensionUnavailable, KZGS_PROVIDER_ERROR_UNSUPPORTED, -405,
			KZGS_PROVIDER_REASON_UNSUPPORTED_DEVICE, "mock unsupported device");
		kzgsClose();
	}

	// The remaining tests exercise the generic ABI with a spatial blit, not a temporal provider.
	kzgs::ProviderTestAllowSpatialMock(true);
	const struct { uint32_t mode; KzgsUpscalerReason reason; const char* name; } fallbacks[] = {
		{MOCK_VERSION_MISMATCH, KzgsUpscalerReason::ProviderVersionMismatch, "version mismatch"},
		{MOCK_SHORT_API, KzgsUpscalerReason::ProviderVersionMismatch, "truncated provider API"},
		{MOCK_SHORT_INFO, KzgsUpscalerReason::ProviderInvalid, "truncated provider capabilities"},
		{MOCK_MISSING_INSTANCE_EXTENSION, KzgsUpscalerReason::InstanceExtensionUnavailable, "instance extension"},
		{MOCK_MISSING_DEVICE_EXTENSION, KzgsUpscalerReason::DeviceExtensionUnavailable, "device extension"},
		{MOCK_BAD_FEATURE, KzgsUpscalerReason::DeviceFeatureUnavailable, "feature negotiation"},
		{MOCK_MUTABLE_WITHOUT_EXTENSION, KzgsUpscalerReason::DeviceFeatureUnavailable, "mutable descriptor without extension"},
		{MOCK_MISALIGNED_FEATURE_POINTER, KzgsUpscalerReason::DeviceFeatureUnavailable, "misaligned feature pointer"},
		{MOCK_MISALIGNED_FEATURE_STRIDE, KzgsUpscalerReason::DeviceFeatureUnavailable, "misaligned feature stride"},
		{MOCK_MISALIGNED_FEATURE_BITS, KzgsUpscalerReason::DeviceFeatureUnavailable, "misaligned feature bits"},
		{MOCK_REQUIRE_DEPTH, KzgsUpscalerReason::RequiredInputUnavailable, "matched temporal inputs"},
		{MOCK_TEMPORAL_COLOR_ONLY, KzgsUpscalerReason::RequiredInputUnavailable, "temporal provider advertising color only"},
		{MOCK_TEMPORAL_COLOR_ONLY | MOCK_DUAL_CAPABILITY, KzgsUpscalerReason::RequiredInputUnavailable,
			"spatial/temporal provider advertising color only"},
		{MOCK_BAD_COLOR_ENCODING, KzgsUpscalerReason::UnsupportedColorEncoding, "unknown color encoding"},
	};
	for (const auto& test : fallbacks)
	{
		std::printf("== fallback: %s\n", test.name);
		mock.set(test.mode);
		const KzgsMockStats before = mock.Stats();
		if (Open(window, test.reason))
		{
			kzgsSync();
			kzgsClose();
			const KzgsMockStats after = mock.Stats();
			Check(after.creates == before.creates && after.evaluates == before.evaluates,
				"failed negotiation never creates or dispatches a provider");
		}
	}

	mock.set(MOCK_TEMPORAL_COLOR_ONLY | MOCK_DUAL_CAPABILITY | MOCK_KIND_XESS);
	const KzgsMockStats beforeTemporalXess = mock.Stats();
	if (Open(window, KzgsUpscalerReason::RequiredInputUnavailable, KzgsUpscaler::XeSS))
	{
		kzgsSync();
		kzgsClose();
		Check(mock.Stats().creates == beforeTemporalXess.creates &&
			mock.Stats().evaluates == beforeTemporalXess.evaluates,
			"XeSS temporal capability cannot bypass qualification with color-only requirements");
	}

	mock.set(MOCK_VALID_FEATURE);
	if (Open(window, KzgsUpscalerReason::WaitingForFrame))
	{
		const KzgsMockStats negotiated = mock.Stats();
		Check(negotiated.instance_requirements > 0 && negotiated.device_requirements > 0,
			"available instance/device extensions and larger-stride feature blocks negotiated");
		std::printf("Additional device extension requested: %s\n", negotiated.extra_extension_requested ? "yes" : "unavailable");
		std::unique_ptr<GSTexture> source;
		kzgs::RunOnGSThread([&]() {
			source = Upload();
			Check(source != nullptr, "synthetic source upload");
			if (!source)
				return;
			auto* device = static_cast<GSDeviceVK*>(g_gs_device.get());
			GSTexture* output = nullptr;
			const KzgsMockStats initial = mock.Stats();
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 9, 6, 10, false, output),
				"normal Vulkan provider dispatch");
			kzgs::ProviderTestPresented(10, true, true);
			CheckStatus(KzgsUpscalerReason::Active, KZGS_PROVIDER_OK);
			std::vector<uint32_t> pixels;
			Check(Read(output, 9, 6, pixels) && pixels.front() == Color(1, 0) && pixels.back() == Color(3, 2),
				"provider blit respects source crop and output extent");
			KzgsMockStats after = mock.Stats();
			Check(after.creates == initial.creates + 1 && after.evaluates == initial.evaluates + 1 &&
				after.resets == initial.resets + 1 && after.last_presentation_id == 10 &&
				after.last_valid_inputs == KZGS_PROVIDER_INPUT_COLOR, "create, reset, and explicit frame contract");

			const uint64_t firstFrame = after.last_frame_id;
			Check(!kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 9, 6, 11, true, output) &&
				output == nullptr && mock.Stats().evaluates == after.evaluates,
				"duplicate redraw falls back without advancing provider history");
			CheckStatus(KzgsUpscalerReason::DuplicatePresent, KZGS_PROVIDER_OK);
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 9, 6, 12, false, output) &&
				mock.Stats().last_frame_id == firstFrame + 1 &&
				mock.Stats().last_reset_reason == KZGS_PROVIDER_RESET_DISCONTINUITY,
				"new source frame resets after duplicate redraw");
			kzgs::ProviderTestPresented(12, true, true);
			device->ExecuteCommandBuffer(true);
			kzgs::ProviderTestCollect();

			kzgs::ProviderReset(KZGS_PROVIDER_RESET_CONFIGURATION);
			mock.set(MOCK_CREATE_FAILURE);
			Check(!kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 9, 6, 13, false, output) &&
				output == nullptr, "partial create failure safely falls back");
			CheckStatus(KzgsUpscalerReason::InitializationFailed, KZGS_PROVIDER_ERROR_CREATE, -101,
				KZGS_PROVIDER_REASON_PROVIDER_FAILURE, "mock create failure");
			mock.set(MOCK_RESET_FAILURE);
			Check(!kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 9, 6, 14, false, output),
				"reset failure safely falls back");
			CheckStatus(KzgsUpscalerReason::ResetFailed, KZGS_PROVIDER_ERROR_RESET, -303,
				KZGS_PROVIDER_REASON_PROVIDER_FAILURE, "mock reset failure");

			mock.set(0);
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 9, 6, 15, false, output),
				"provider recovers after create/reset failures");
			kzgs::ProviderTestPresented(15, true, true);
			mock.set(MOCK_EVALUATE_FAILURE);
			const KzgsMockStats beforeFailure = mock.Stats();
			Check(!kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 9, 6, 16, false, output) &&
				output == nullptr, "dispatch failure after recorded commands falls back");
			CheckStatus(KzgsUpscalerReason::DispatchFailed, KZGS_PROVIDER_ERROR_EVALUATE, -202,
				KZGS_PROVIDER_REASON_PROVIDER_FAILURE, "mock dispatch failure");
			Check(kzgs::ProviderTestRetiredCount() > 0 && mock.Stats().destroys == beforeFailure.destroys,
				"failed dispatch resources remain alive before fence completion");
			kzgs::ProviderTestCollect();
			Check(mock.Stats().destroys == beforeFailure.destroys, "polling an incomplete fence cannot retire");
			device->ExecuteCommandBuffer(true);
			kzgs::ProviderTestCollect();
			Check(kzgs::ProviderTestRetiredCount() == 0 && mock.Stats().destroys > beforeFailure.destroys &&
				mock.Stats().unsafe_destroys == 0, "completed PCSX2 fence retires failed dispatch safely");

			mock.set(0);
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 11, 9, 17, false, output),
				"dispatch before resize");
			kzgs::ProviderTestPresented(17, true, true);
			const KzgsMockStats beforeResize = mock.Stats();
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 18, false, output) &&
				mock.Stats().last_reset_reason == KZGS_PROVIDER_RESET_RESIZE,
				"resize recreates context with reset reason");
			kzgs::ProviderTestPresented(18, true, true);
			Check(kzgs::ProviderTestRetiredCount() > 0 && mock.Stats().destroys == beforeResize.destroys,
				"resized context survives until its actual fence");
			device->ExecuteCommandBuffer(true);
			kzgs::ProviderTestCollect();
			Check(kzgs::ProviderTestRetiredCount() == 0 && mock.Stats().unsafe_destroys == 0,
				"resized context retired after fence completion");

			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 19, false, output),
				"dispatch before failed present");
			kzgs::ProviderTestPresented(19, false, true);
			CheckStatus(KzgsUpscalerReason::PresentFailed, KZGS_PROVIDER_ERROR_EVALUATE);
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 20, false, output) &&
				mock.Stats().last_reset_reason == KZGS_PROVIDER_RESET_PRESENT_FAILED,
				"failed present discards speculative temporal history");
			kzgs::ProviderTestPresented(20, true, true);
			device->ExecuteCommandBuffer(true);
			kzgs::ProviderTestCollect();
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 22, false, output) &&
				mock.Stats().last_reset_reason == KZGS_PROVIDER_RESET_DISCONTINUITY,
				"a presentation-ID gap resets temporal history");
			kzgs::ProviderTestPresented(22, true, true);
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 23, false, output),
				"dispatch awaiting presentation");
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 24, false, output) &&
				mock.Stats().last_reset_reason == KZGS_PROVIDER_RESET_DISCONTINUITY,
				"missing present completion resets temporal history");
			kzgs::ProviderTestPresented(24, true, true);
			device->ExecuteCommandBuffer(true);
			kzgs::ProviderTestCollect();

			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 25, false, output),
				"dispatch before a split presentation command buffer");
			const uint64_t evaluationFence = device->GetCurrentFenceCounter();
			device->ExecuteCommandBuffer(false);
			const uint64_t outputFence = device->GetCurrentFenceCounter();
			std::unique_ptr<GSTexture> consumer(g_gs_device->CreateTexture(13, 11, 1, GSTexture::Format::Color));
			if (consumer && output)
				device->CopyRect(output, consumer.get(), GSVector4i(0, 0, 13, 11), 0, 0);
			kzgs::ProviderTestPresented(25, true, true);
			const uint32_t destroyedBeforeSplit = mock.Stats().destroys;
			kzgs::ProviderReset(KZGS_PROVIDER_RESET_CONFIGURATION);
			device->WaitForFenceCounter(evaluationFence);
			kzgs::ProviderTestCollect();
			Check(consumer && outputFence > evaluationFence && device->GetCompletedFenceCounter() < outputFence &&
				kzgs::ProviderTestRetiredCount() > 0 && mock.Stats().destroys == destroyedBeforeSplit,
				"evaluation completion does not retire output used by a later command buffer");
			device->ExecuteCommandBuffer(true);
			kzgs::ProviderTestCollect();
			Check(kzgs::ProviderTestRetiredCount() == 0 && mock.Stats().destroys > destroyedBeforeSplit &&
				mock.Stats().unsafe_destroys == 0, "later output-use fence safely retires the context");
			consumer.reset();
		});

		kzgsReset();
		kzgs::RunOnGSThread([&]() {
			if (!source)
				return;
			GSTexture* output = nullptr;
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 21, false, output) &&
				mock.Stats().last_reset_reason == KZGS_PROVIDER_RESET_GAME,
				"queued GS reset recreates provider history");
			kzgs::ProviderTestPresented(21, true, true);
			static_cast<GSDeviceVK*>(g_gs_device.get())->ExecuteCommandBuffer(true);
			kzgs::ProviderTestCollect();
		});
		kzgsResize(80, 80);
		kzgs::RunOnGSThread([&]() {
			if (!source)
				return;
			auto* device = static_cast<GSDeviceVK*>(g_gs_device.get());
			GSTexture* output = nullptr;
			Check(kzgs::ProviderTestUpscale(source.get(), 1, 0, 4, 3, 13, 11, 22, false, output) &&
				mock.Stats().last_reset_reason == KZGS_PROVIDER_RESET_RESIZE,
				"queued window resize recreates provider history");
			kzgs::ProviderTestPresented(22, true, true);

			// Ensure the GPU is actually idle while leaving PCSX2's completed counter conservative.
			const uint64_t submitted = mock.Stats().last_submission_token;
			device->ExecuteCommandBuffer(false);
			const VkResult idle = vkDeviceWaitIdle(device->GetDevice());
			Check(idle == VK_SUCCESS && device->GetCompletedFenceCounter() < submitted,
				"loss simulation uses an idle GPU and an unretired counter");
			kzgs::ProviderDeviceDestroying(*device, VK_ERROR_DEVICE_LOST);
			Check(mock.Stats().last_destroy_reason == KZGS_PROVIDER_DESTROY_DEVICE_LOST &&
				mock.Stats().unsafe_destroys == 0, "terminal device-loss cleanup is distinguished from completion");
			source.reset();
		});
		kzgsClose();
		Check(mock.Stats().unsafe_destroys == 0, "no provider object was destroyed before completion");
	}

	mock.set(0);
	if (Open(window, KzgsUpscalerReason::WaitingForFrame))
	{
		kzgs::RunOnGSThread([&]() {
			auto source = Upload();
			GSTexture* output = nullptr;
			Check(source && kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 25, false, output),
				"provider reloads on a recreated Vulkan device");
			kzgs::ProviderTestPresented(25, true, true);
			mock.set(MOCK_DEVICE_LOST);
			Check(!kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 26, false, output),
				"provider-reported device loss falls back");
			CheckStatus(KzgsUpscalerReason::DeviceLost, KZGS_PROVIDER_ERROR_DEVICE_LOST, -4,
				KZGS_PROVIDER_REASON_DEVICE_LOST, "mock device loss");
			const uint32_t evaluatesAfterLoss = mock.Stats().evaluates;
			mock.set(0);
			Check(!kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 27, false, output) &&
				mock.Stats().evaluates == evaluatesAfterLoss,
				"device loss is not retried on the old device");
			static_cast<GSDeviceVK*>(g_gs_device.get())->ExecuteCommandBuffer(true);
			kzgs::ProviderTestCollect();
			source.reset();
		});
		kzgsClose();
	}
	if (Open(window, KzgsUpscalerReason::WaitingForFrame))
	{
		kzgs::RunOnGSThread([&]() {
			auto source = Upload();
			GSTexture* output = nullptr;
			Check(source && kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 28, false, output),
				"a new Vulkan device can recover after provider-reported loss");
			kzgs::ProviderTestPresented(28, true, true);
			static_cast<GSDeviceVK*>(g_gs_device.get())->ExecuteCommandBuffer(true);
			source.reset();
		});
		kzgsClose();
	}
	for (const uint32_t lossMode : {MOCK_CREATE_DEVICE_LOST, MOCK_RESET_DEVICE_LOST})
	{
		mock.set(lossMode);
		if (!Open(window, KzgsUpscalerReason::WaitingForFrame))
			continue;
		kzgs::RunOnGSThread([&]() {
			auto source = Upload();
			GSTexture* output = nullptr;
			Check(source && !kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 28, false, output),
				lossMode == MOCK_CREATE_DEVICE_LOST ? "device loss during create falls back" :
					"device loss during reset falls back");
			CheckStatus(KzgsUpscalerReason::DeviceLost, KZGS_PROVIDER_ERROR_DEVICE_LOST, -4,
				KZGS_PROVIDER_REASON_DEVICE_LOST, lossMode == MOCK_CREATE_DEVICE_LOST ?
					"mock create device loss" : "mock reset device loss");
			Check(mock.Stats().last_destroy_reason == KZGS_PROVIDER_DESTROY_DEVICE_LOST,
				"partial context is destroyed with terminal-loss semantics");
			const uint32_t creates = mock.Stats().creates;
			mock.set(0);
			Check(!kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 29, false, output) &&
				mock.Stats().creates == creates, "create/reset loss remains latched");
			static_cast<GSDeviceVK*>(g_gs_device.get())->ExecuteCommandBuffer(true);
			source.reset();
		});
		kzgsClose();
	}
	mock.set(MOCK_ABI_1_0);
	if (Open(window, KzgsUpscalerReason::WaitingForFrame))
	{
		kzgs::RunOnGSThread([&]() {
			auto source = Upload();
			GSTexture* output = nullptr;
			Check(source && kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 29, false, output),
				"1.0 provider works with a 1.1 host");
			kzgs::ProviderTestPresented(29, true, true);
			static_cast<GSDeviceVK*>(g_gs_device.get())->ExecuteCommandBuffer(true);
			source.reset();
		});
		kzgsClose();
	}
	mock.set(0);
	if (Open(window, KzgsUpscalerReason::ProviderInvalid, KzgsUpscaler::XeSS))
		kzgsClose();
	mock.set(MOCK_KIND_XESS);
	if (Open(window, KzgsUpscalerReason::WaitingForFrame, KzgsUpscaler::XeSS))
	{
		kzgs::RunOnGSThread([&]() {
			auto source = Upload();
			GSTexture* output = nullptr;
			Check(source && kzgs::ProviderTestUpscale(source.get(), 0, 0, 4, 4, 8, 8, 29, false, output),
				"XeSS-kind provider negotiates and dispatches");
			kzgs::ProviderTestPresented(29, true, true);
			KzgsUpscalerStatus status;
			Check(kzgsGetUpscalerStatus(status) && status.effective == KzgsUpscaler::XeSS &&
				status.reason == KzgsUpscalerReason::Active, "XeSS provider reports effective status");
			static_cast<GSDeviceVK*>(g_gs_device.get())->ExecuteCommandBuffer(true);
			source.reset();
		});
		kzgsClose();
	}
	Check(mock.Stats().unsafe_destroys == 0, "device teardown waits before provider destruction");
	kzgs::ProviderTestSetPath(nullptr);
	kzgs::ProviderTestAllowSpatialMock(false);
	FreeLibrary(mock.module);
	Check(GetModuleHandleW(path.c_str()) == nullptr, "host releases the provider DLL after device teardown");
	DestroyWindow(window);
	Check(validationSeen.load() && !validationUnavailable.load() && validationErrors.load() == 0,
		"Vulkan synchronization validation");
	std::printf("%s: provider ABI and Vulkan lifecycle (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
