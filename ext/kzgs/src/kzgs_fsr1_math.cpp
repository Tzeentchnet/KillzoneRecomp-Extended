// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_fsr1.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#define A_CPU 1
#include "../third_party/fsr1/ffx_a.h"
#include "../third_party/fsr1/ffx_fsr1.h"

namespace kzgs::fsr1
{
	PlanResult MakePlan(int textureWidth, int textureHeight, Rect source,
		float left, float top, float right, float bottom,
		uint32_t maxTextureSize, uint32_t maxGroupsX, uint32_t maxGroupsY, Plan& plan)
	{
		plan = {};
		if (textureWidth <= 0 || textureHeight <= 0 || source.left < 0 || source.top < 0 ||
			source.right <= source.left || source.bottom <= source.top ||
			source.right > textureWidth || source.bottom > textureHeight ||
			!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom))
			return PlanResult::Invalid;

		const double destinationWidth = static_cast<double>(right) - left;
		const double destinationHeight = static_cast<double>(bottom) - top;
		const double width = std::ceil(destinationWidth);
		const double height = std::ceil(destinationHeight);
		if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() ||
			height > std::numeric_limits<int>::max())
			return PlanResult::Invalid;

		plan.source = source;
		plan.sourceWidth = textureWidth;
		plan.sourceHeight = textureHeight;
		plan.outputWidth = static_cast<int>(width);
		plan.outputHeight = static_cast<int>(height);
		const int viewportWidth = source.right - source.left;
		const int viewportHeight = source.bottom - source.top;
		if (destinationWidth < viewportWidth || destinationHeight < viewportHeight ||
			(destinationWidth == viewportWidth && destinationHeight == viewportHeight))
			return PlanResult::NoUpscaleNeeded;

		plan.groupsX = (static_cast<uint32_t>(plan.outputWidth) + 7u) / 8u;
		plan.groupsY = (static_cast<uint32_t>(plan.outputHeight) + 7u) / 8u;
		if (static_cast<uint32_t>(plan.outputWidth) > maxTextureSize ||
			static_cast<uint32_t>(plan.outputHeight) > maxTextureSize ||
			plan.groupsX > maxGroupsX || plan.groupsY > maxGroupsY)
			return PlanResult::Invalid;
		return PlanResult::Ready;
	}

	Constants EasuConstants(const Plan& plan)
	{
		Constants constants{};
		FsrEasuConOffset(constants[0].data(), constants[1].data(), constants[2].data(), constants[3].data(),
			static_cast<float>(plan.source.right - plan.source.left),
			static_cast<float>(plan.source.bottom - plan.source.top),
			static_cast<float>(plan.sourceWidth), static_cast<float>(plan.sourceHeight),
			static_cast<float>(plan.outputWidth), static_cast<float>(plan.outputHeight),
			static_cast<float>(plan.source.left), static_cast<float>(plan.source.top));
		return constants;
	}

	float RcasStops(int sharpening)
	{
		return 2.0f * static_cast<float>(100 - std::clamp(sharpening, 1, 100)) / 99.0f;
	}

	std::array<uint32_t, 4> RcasConstants(int sharpening)
	{
		std::array<uint32_t, 4> constants{};
		FsrRcasCon(constants.data(), RcasStops(sharpening));
		return constants;
	}
}
