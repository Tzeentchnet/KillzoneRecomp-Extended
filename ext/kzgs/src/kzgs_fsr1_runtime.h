// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kzgs_upscaler.h"

#include <cstdint>
#include <vector>

class GSTexture;

namespace kzgs
{
	using Fsr1Report = void (*)(KzgsUpscalerReason reason, bool applied);

	void Fsr1InstallDeviceHooks(Fsr1Report report);
	void Fsr1RemoveDeviceHooks();
	void Fsr1Configure(bool enabled, int sharpening);
	void Fsr1InvalidateCapture();
	bool Fsr1CanCapture();

	enum class Fsr1ReadbackResult { Unavailable, Success, Failed };
	Fsr1ReadbackResult Fsr1ReadPresented(std::vector<uint32_t>& pixels, uint32_t& width, uint32_t& height,
		uint32_t requestedWidth, uint32_t requestedHeight);

#ifdef KZGS_FSR1_TESTING
	void Fsr1TestFailNextInitialization();
	uint64_t Fsr1TestDispatchCount();
	bool Fsr1TestUseNativeGather(bool enabled);
	bool Fsr1TestUpscale(GSTexture* source, int left, int top, int right, int bottom,
		int outputWidth, int outputHeight, int sharpening, GSTexture*& output);
#endif
}
