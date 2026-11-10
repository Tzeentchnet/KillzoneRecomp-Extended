// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_fsr1.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
	int failures = 0;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", message);
			++failures;
		}
	}

	bool Close(uint32_t bits, float expected)
	{
		return std::abs(std::bit_cast<float>(bits) - expected) < 0.000001f;
	}
}

int main()
{
	using namespace kzgs::fsr1;
	Plan plan;
	const Rect rect{3, 2, 8, 5};
	Check(MakePlan(12, 8, rect, 17.0f, 9.0f, 28.0f, 18.0f, 4096, 65535, 65535, plan) ==
		PlanResult::Ready, "odd cropped rectangle accepted");
	Check(plan.outputWidth == 11 && plan.outputHeight == 9 && plan.groupsX == 2 && plan.groupsY == 2,
		"ceil dispatch covers partial tiles");
	const Constants c = EasuConstants(plan);
	Check(Close(c[0][0], 5.0f / 11.0f) && Close(c[0][1], 3.0f / 9.0f) &&
		Close(c[0][2], 3.0f + 0.5f * 5.0f / 11.0f - 0.5f) &&
		Close(c[0][3], 2.0f + 0.5f * 3.0f / 9.0f - 0.5f),
		"EASU viewport, scale and offset reference vector");
	Check(Close(c[1][0], 1.0f / 12.0f) && Close(c[1][1], 1.0f / 8.0f) &&
		Close(c[1][2], 1.0f / 12.0f) && Close(c[1][3], -1.0f / 8.0f) &&
		Close(c[2][0], -1.0f / 12.0f) && Close(c[2][1], 2.0f / 8.0f) &&
		Close(c[2][2], 1.0f / 12.0f) && Close(c[2][3], 2.0f / 8.0f) &&
		Close(c[3][0], 0.0f) && Close(c[3][1], 4.0f / 8.0f) && c[3][2] == 0 && c[3][3] == 0,
		"EASU gather constants reference vector");

	const auto low = RcasConstants(1);
	const auto high = RcasConstants(100);
	Check(Close(low[0], 0.25f) && low[1] == 0x34003400u && low[2] == 0 && low[3] == 0,
		"RCAS minimum setting reference vector");
	Check(Close(high[0], 1.0f) && high[1] == 0x3c003c00u && high[2] == 0 && high[3] == 0,
		"RCAS maximum setting reference vector");
	Check(RcasStops(1) > RcasStops(50) && RcasStops(50) > RcasStops(100),
		"RCAS sharpening is monotonic");

	Check(MakePlan(12, 8, rect, 0, 0, 5, 3, 4096, 65535, 65535, plan) ==
		PlanResult::NoUpscaleNeeded, "identity does not dispatch");
	Check(MakePlan(12, 8, rect, 0, 0, 4, 9, 4096, 65535, 65535, plan) ==
		PlanResult::NoUpscaleNeeded, "no downscaling on either axis");
	Check(MakePlan(12, 8, rect, 0, 0, 4.9f, 9, 4096, 65535, 65535, plan) ==
		PlanResult::NoUpscaleNeeded, "fractional downscaling is not rounded into an upscale");
	Check(MakePlan(12, 8, rect, 0, 0, 5.1f, 3, 4096, 65535, 65535, plan) ==
		PlanResult::Ready && plan.outputWidth == 6, "fractional upscale rounds target up");
	Check(MakePlan(12, 8, rect, 0, 0, 11, 9, 10, 65535, 65535, plan) ==
		PlanResult::Invalid, "texture limit checked");
	Check(MakePlan(12, 8, rect, 0, 0, 11, 9, 4096, 1, 65535, plan) ==
		PlanResult::Invalid, "dispatch limit checked");
	Check(MakePlan(12, 8, {-1, 2, 8, 5}, 0, 0, 11, 9, 4096, 65535, 65535, plan) ==
		PlanResult::Invalid, "invalid source rectangle rejected");
	Check(MakePlan(12, 8, rect, 0, 0, std::numeric_limits<float>::quiet_NaN(), 9, 4096, 65535, 65535, plan) ==
		PlanResult::Invalid, "non-finite destination rejected");
	std::printf("%s: FSR1 constants and dispatch planning (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
