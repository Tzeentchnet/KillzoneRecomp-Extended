// SDK-independent mapping from the provider ABI to temporal SDK parameters.
// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kzgs_upscaler_provider.h"

#include <cmath>

namespace kzgs::vendor
{
	struct TemporalConversion
	{
		float jitterX, jitterY;
		float motionScaleX, motionScaleY;
		bool reversedDepth;
	};

	inline bool convertTemporal(float jitterX, float jitterY, float motionScaleX, float motionScaleY,
		uint32_t cameraFlags, KzgsProviderDepthConvention expectedDepth, TemporalConversion& out)
	{
		const bool reversed = (cameraFlags & KZGS_PROVIDER_CAMERA_REVERSED_DEPTH) != 0;
		if ((cameraFlags & ~(KZGS_PROVIDER_CAMERA_REVERSED_DEPTH | KZGS_PROVIDER_CAMERA_INFINITE_FAR)) ||
			(expectedDepth != KZGS_PROVIDER_DEPTH_UNKNOWN && expectedDepth != KZGS_PROVIDER_DEPTH_FORWARD &&
				expectedDepth != KZGS_PROVIDER_DEPTH_REVERSED) ||
			(expectedDepth == KZGS_PROVIDER_DEPTH_FORWARD && reversed) ||
			(expectedDepth == KZGS_PROVIDER_DEPTH_REVERSED && !reversed) ||
			!std::isfinite(jitterX) || !std::isfinite(jitterY) ||
			std::abs(jitterX) > 0.5f || std::abs(jitterY) > 0.5f ||
			!std::isfinite(motionScaleX) || !std::isfinite(motionScaleY) ||
			motionScaleX == 0.0f || motionScaleY == 0.0f)
			return false;
		out = {jitterX, jitterY, motionScaleX, motionScaleY, reversed};
		return true;
	}

	inline bool motionInSourcePixels(float sampleX, float sampleY, const TemporalConversion& conversion,
		float& pixelX, float& pixelY)
	{
		const float x = sampleX * conversion.motionScaleX;
		const float y = sampleY * conversion.motionScaleY;
		if (!std::isfinite(x) || !std::isfinite(y))
			return false;
		pixelX = x;
		pixelY = y;
		return true;
	}
}
