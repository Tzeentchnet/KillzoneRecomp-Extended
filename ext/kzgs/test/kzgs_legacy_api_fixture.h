// The public KzGS configuration and declarations before the upscaler changes.
// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kz_host/core.h"

#include <cstdint>
#include <string>

enum class KzgsRenderer : int { D3D11 = 0, D3D12 = 1, Vulkan = 2, Software = 3 };
enum class KzgsAspect : int { Ratio4_3 = 0, Ratio16_9 = 1, Stretch = 2 };
enum class KzgsTextureFilter : int { Nearest = 0, Forced = 1, PS2 = 2, ForcedExceptSprites = 3 };
enum class KzgsMipmapMode : int { PS2 = 0, BaseOnly = 1 };

struct KzgsConfig
{
	KzgsRenderer renderer = KzgsRenderer::D3D11;
	int upscale = 1;
	KzgsTextureFilter textureFiltering = KzgsTextureFilter::PS2;
	int anisotropy = 0;
	KzgsMipmapMode mipmapMode = KzgsMipmapMode::PS2;
	bool fxaa = false;
	bool smaa = false;
	bool loadTextureReplacements = false;
	bool dumpTextures = false;
	bool edgeAA = false;
	KzgsAspect aspect = KzgsAspect::Ratio4_3;
	bool vsync = true;
	bool bilinearPresent = true;
	bool sharpPresent = false;
	int casSharpness = 0;
	int crop[4] = {0, 0, 0, 0};
	bool vertexShaderExpand = true;
	bool pcrtcAntiBlur = true;
	int interlaceMode = 0;
	int halfPixelOffset = 4;
	int nativeScaling = 2;
	std::string adapter;
	bool useWarp = false;
	std::string resourcesDir;
	std::string cacheDir;
	bool disableShaderCache = false;
	int maxQueuedFrames = 1;
	bool selfContainedGifPackets = true;
	bool debugDevice = false;
};

bool kzgsOpen(const kz::host::NativeWindow&, const KzgsConfig&, uint8_t*, std::string* = nullptr);
void kzgsClose();
void kzgsUpdateConfig(const KzgsConfig&);
void kzgsVsync(int, bool = true, bool = false);
bool kzgsVsyncTry(int, bool = true, bool = false);
void kzgsSync();
