// SPDX-License-Identifier: GPL-3.0+
#include "kzgs.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>
#include <type_traits>

static_assert(std::is_aggregate_v<KzgsConfig>);
static_assert(static_cast<int>(KzgsUpscaler::Off) == 0);
static_assert(static_cast<int>(KzgsUpscaler::FSR1) == 1);
static_assert(static_cast<int>(KzgsUpscaler::DLSS) == 2);
static_assert(static_cast<int>(KzgsUpscaler::XeSS) == 3);
static_assert(static_cast<int>(KzgsUpscaler::FSR3) == 4);
static_assert(KZGS_TEMPORAL_MATRIX_TRAIT_FINITE_NONZERO == 1u);
static_assert(KZGS_TEMPORAL_MATRIX_TRAIT_LAST_ROW_0001 == 2u);
static_assert(KZGS_TEMPORAL_MATRIX_TRAIT_LAST_COLUMN_0001 == 4u);
static_assert(KZGS_TEMPORAL_MATRIX_CHANGED_310 == 1u);
static_assert(KZGS_TEMPORAL_MATRIX_CHANGED_400 == 2u);
static_assert(std::is_same_v<decltype(&kzgsVsync), void (*)(int, bool, bool)>);
static_assert(std::is_same_v<decltype(&kzgsVsyncTry), bool (*)(int, bool, bool)>);
static_assert(std::is_same_v<decltype(&kzgsVsyncTemporal),
	void (*)(int, bool, bool, const KzgsTemporalGuestObservation*)>);
static_assert(std::is_same_v<decltype(&kzgsVsyncTryTemporal),
	bool (*)(int, bool, bool, const KzgsTemporalGuestObservation*)>);

extern size_t LegacyConfigSize();
extern size_t LegacyConfigOffset(int index);
extern bool ExerciseLegacyClient();

bool CheckLegacyStatus()
{
	KzgsUpscalerStatus status;
	return kzgsGetUpscalerStatus(status) && status.requested == KzgsUpscaler::Off &&
		status.effective == KzgsUpscaler::Off && status.reason == KzgsUpscalerReason::Disabled;
}

int main()
{
	int failures = 0;
	const auto check = [&](bool ok, const char* message) {
		std::printf("%s: %s\n", ok ? "PASS" : "FAIL", message);
		failures += !ok;
	};
	KzgsConfig legacy{KzgsRenderer::D3D11, 1, KzgsTextureFilter::PS2, 0, KzgsMipmapMode::PS2};
	const auto* base = reinterpret_cast<const char*>(&legacy);
	const size_t offsets[] = {
		static_cast<size_t>(reinterpret_cast<const char*>(&legacy.textureFiltering) - base),
		static_cast<size_t>(reinterpret_cast<const char*>(&legacy.aspect) - base),
		static_cast<size_t>(reinterpret_cast<const char*>(&legacy.crop) - base),
		static_cast<size_t>(reinterpret_cast<const char*>(&legacy.adapter) - base),
		static_cast<size_t>(reinterpret_cast<const char*>(&legacy.resourcesDir) - base),
		static_cast<size_t>(reinterpret_cast<const char*>(&legacy.maxQueuedFrames) - base),
		static_cast<size_t>(reinterpret_cast<const char*>(&legacy.debugDevice) - base),
	};
	bool layout = LegacyConfigSize() == sizeof(legacy);
	for (int i = 0; i < static_cast<int>(std::size(offsets)); ++i)
		layout = layout && offsets[i] == LegacyConfigOffset(i);
	check(layout, "legacy aggregate size and member offsets");
	check(ExerciseLegacyClient(), "separately compiled legacy open, update and VSync function pointers");

	alignas(16) uint8_t regs[0x2000]{};
	std::string error;
	void* inaccessible = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
	check(inaccessible != nullptr, "allocate inaccessible configuration buffer");
	if (inaccessible)
	{
		check(!kzgsOpenEx({}, inaccessible, 0, KZGS_CONFIG_VERSION_1, regs, &error) &&
			error.find("too small") != std::string::npos, "short open buffer rejected without reading it");
		check(!kzgsOpenEx({}, inaccessible, sizeof(KzgsConfigV1), 999, regs, &error) &&
			error.find("version") != std::string::npos, "unknown open version rejected without reading it");
		check(!kzgsUpdateConfigEx(inaccessible, 0, KZGS_CONFIG_VERSION_1, &error) &&
			error.find("too small") != std::string::npos, "short update buffer rejected without reading it");
		VirtualFree(inaccessible, 0, MEM_RELEASE);
	}
	check(!kzgsOpenEx({}, nullptr, sizeof(KzgsConfigV1), KZGS_CONFIG_VERSION_1, regs, &error) &&
		error.find("null") != std::string::npos, "null extended configuration rejected");

	KzgsConfigV1 extended;
	extended.useWarp = true;
	extended.vsync = false;
	extended.upscaler = KzgsUpscaler::FSR1;
	extended.upscalerSharpening = 42;
	if (!kzgsOpenEx({}, &extended, sizeof(extended), KZGS_CONFIG_VERSION_1, regs, &error))
	{
		std::fprintf(stderr, "extended kzgsOpen failed: %s\n", error.c_str());
		++failures;
	}
	else
	{
		KzgsUpscalerStatus status;
		check(kzgsGetUpscalerStatus(status) && status.requested == KzgsUpscaler::FSR1 &&
			status.requestedSharpening == 42 && status.reason == KzgsUpscalerReason::NotImplemented,
			"versioned open applies extended fields");
		check(!kzgsUpdateConfigEx(&extended, sizeof(extended) - 1, KZGS_CONFIG_VERSION_1, &error),
			"short update is rejected while open");
		kzgsSync();
		check(kzgsGetUpscalerStatus(status) && status.requested == KzgsUpscaler::FSR1,
			"rejected update leaves active configuration unchanged");
		extended.upscaler = KzgsUpscaler::FSR3;
		check(kzgsUpdateConfigEx(&extended, sizeof(extended), KZGS_CONFIG_VERSION_1, &error),
			"versioned update accepts a complete configuration");
		kzgsSync();
		check(kzgsGetUpscalerStatus(status) && status.requested == KzgsUpscaler::FSR3,
			"versioned update applies extended fields");
		legacy.useWarp = true;
		legacy.vsync = false;
		kzgsUpdateConfig(legacy);
		kzgsSync();
		check(CheckLegacyStatus(), "legacy update uses only legacy fields and disables extensions");
		kzgsClose();
	}
	std::printf("%s: KzGS API compatibility (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
