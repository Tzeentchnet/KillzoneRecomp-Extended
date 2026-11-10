// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_legacy_api_fixture.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <new>
#include <type_traits>

static_assert(std::is_aggregate_v<KzgsConfig>);
static_assert(std::is_same_v<decltype(&kzgsVsync), void (*)(int, bool, bool)>);
static_assert(std::is_same_v<decltype(&kzgsVsyncTry), bool (*)(int, bool, bool)>);

extern bool CheckLegacyStatus();

size_t LegacyConfigSize()
{
	return sizeof(KzgsConfig);
}

size_t LegacyConfigOffset(int index)
{
	KzgsConfig config;
	const auto* base = reinterpret_cast<const char*>(&config);
	switch (index)
	{
		case 0: return reinterpret_cast<const char*>(&config.textureFiltering) - base;
		case 1: return reinterpret_cast<const char*>(&config.aspect) - base;
		case 2: return reinterpret_cast<const char*>(&config.crop) - base;
		case 3: return reinterpret_cast<const char*>(&config.adapter) - base;
		case 4: return reinterpret_cast<const char*>(&config.resourcesDir) - base;
		case 5: return reinterpret_cast<const char*>(&config.maxQueuedFrames) - base;
		case 6: return reinterpret_cast<const char*>(&config.debugDevice) - base;
		default: return static_cast<size_t>(-1);
	}
}

bool ExerciseLegacyClient()
{
	SYSTEM_INFO info{};
	GetSystemInfo(&info);
	auto* memory = static_cast<unsigned char*>(
		VirtualAlloc(nullptr, info.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!memory)
		return false;
	if (sizeof(KzgsConfig) > info.dwPageSize)
	{
		VirtualFree(memory, 0, MEM_RELEASE);
		return false;
	}
	DWORD oldProtection = 0;
	if (!VirtualProtect(memory + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &oldProtection))
	{
		VirtualFree(memory, 0, MEM_RELEASE);
		return false;
	}

	// This old-style aggregate ends exactly at an inaccessible page, detecting extended-field overreads.
	auto* cfg = new (memory + info.dwPageSize - sizeof(KzgsConfig))
		KzgsConfig{KzgsRenderer::D3D11, 1, KzgsTextureFilter::PS2, 0, KzgsMipmapMode::PS2};
	cfg->useWarp = true;
	cfg->vsync = false;
	alignas(16) uint8_t regs[0x2000]{};
	std::string error;
	bool ok = kzgsOpen({}, *cfg, regs, &error);
	if (ok)
	{
		ok = CheckLegacyStatus();
		kzgsUpdateConfig(*cfg);
		void (*vsync)(int, bool, bool) = &kzgsVsync;
		bool (*tryVsync)(int, bool, bool) = &kzgsVsyncTry;
		vsync(0, true, false);
		ok = tryVsync(1, true, false) && ok;
		kzgsSync();
		ok = CheckLegacyStatus() && ok;
		kzgsClose();
	}
	else
		std::fprintf(stderr, "legacy kzgsOpen failed: %s\n", error.c_str());
	cfg->~KzgsConfig();
	VirtualFree(memory, 0, MEM_RELEASE);
	return ok;
}
