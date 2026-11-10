// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <array>
#include <cstdint>

namespace kzgs::fsr1
{
	struct Rect
	{
		int left;
		int top;
		int right;
		int bottom;
	};

	enum class PlanResult
	{
		Ready,
		NoUpscaleNeeded,
		Invalid,
	};

	struct Plan
	{
		Rect source{};
		int sourceWidth = 0;
		int sourceHeight = 0;
		int outputWidth = 0;
		int outputHeight = 0;
		uint32_t groupsX = 0;
		uint32_t groupsY = 0;
	};

	PlanResult MakePlan(int textureWidth, int textureHeight, Rect source,
		float left, float top, float right, float bottom,
		uint32_t maxTextureSize, uint32_t maxGroupsX, uint32_t maxGroupsY, Plan& plan);

	using Constants = std::array<std::array<uint32_t, 4>, 4>;
	Constants EasuConstants(const Plan& plan);
	std::array<uint32_t, 4> RcasConstants(int sharpening);
	float RcasStops(int sharpening);
}
