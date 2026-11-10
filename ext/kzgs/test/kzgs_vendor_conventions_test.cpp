// Numeric convention vectors without any vendor headers, SDK, or GPU.
// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_vendor_conventions.h"

#include <cstdio>
#include <limits>

namespace
{
	int failures = 0;
#define CHECK(expression) do { if (!(expression)) { \
	std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); ++failures; } } while (0)
}

int main()
{
	using namespace kzgs::vendor;
	TemporalConversion value{};
	CHECK(convertTemporal(0.25f, -0.375f, -640.0f, 360.0f,
		KZGS_PROVIDER_CAMERA_REVERSED_DEPTH, KZGS_PROVIDER_DEPTH_REVERSED, value));
	CHECK(value.jitterX == 0.25f && value.jitterY == -0.375f && value.reversedDepth);
	CHECK(value.motionScaleX == -640.0f && value.motionScaleY == 360.0f);
	float x = 0.0f, y = 0.0f;
	CHECK(motionInSourcePixels(-0.25f, 0.125f, value, x, y));
	CHECK(x == 160.0f && y == 45.0f);
	CHECK(motionInSourcePixels(0.0f, -0.5f, value, x, y));
	CHECK(x == 0.0f && y == -180.0f);

	CHECK(convertTemporal(-0.5f, 0.5f, 1.0f, -1.0f,
		KZGS_PROVIDER_CAMERA_INFINITE_FAR, KZGS_PROVIDER_DEPTH_FORWARD, value));
	CHECK(!value.reversedDepth && value.jitterX == -0.5f && value.jitterY == 0.5f);
	CHECK(convertTemporal(0.0f, 0.0f, 1.0f, 1.0f,
		KZGS_PROVIDER_CAMERA_REVERSED_DEPTH, KZGS_PROVIDER_DEPTH_UNKNOWN, value) && value.reversedDepth);

	const auto nan = std::numeric_limits<float>::quiet_NaN();
	const auto infinity = std::numeric_limits<float>::infinity();
	CHECK(!convertTemporal(0.0f, 0.0f, 1.0f, 1.0f,
		KZGS_PROVIDER_CAMERA_REVERSED_DEPTH, KZGS_PROVIDER_DEPTH_FORWARD, value));
	CHECK(!convertTemporal(0.0f, 0.0f, 1.0f, 1.0f, 0, KZGS_PROVIDER_DEPTH_REVERSED, value));
	CHECK(!convertTemporal(0.0f, 0.0f, 1.0f, 1.0f, 0x80000000u, KZGS_PROVIDER_DEPTH_UNKNOWN, value));
	CHECK(!convertTemporal(0.0f, 0.0f, 1.0f, 1.0f, 0, 99, value));
	CHECK(!convertTemporal(nan, 0.0f, 1.0f, 1.0f, 0, KZGS_PROVIDER_DEPTH_FORWARD, value));
	CHECK(!convertTemporal(0.0f, infinity, 1.0f, 1.0f, 0, KZGS_PROVIDER_DEPTH_FORWARD, value));
	CHECK(!convertTemporal(0.5001f, 0.0f, 1.0f, 1.0f, 0, KZGS_PROVIDER_DEPTH_FORWARD, value));
	CHECK(!convertTemporal(0.0f, 0.0f, 0.0f, 1.0f, 0, KZGS_PROVIDER_DEPTH_FORWARD, value));
	CHECK(!convertTemporal(0.0f, 0.0f, 1.0f, infinity, 0, KZGS_PROVIDER_DEPTH_FORWARD, value));
	CHECK(!motionInSourcePixels(nan, 1.0f, value, x, y));
	CHECK(!motionInSourcePixels(infinity, 1.0f, value, x, y));

	if (!failures)
		std::puts("vendor temporal conventions: PASS (no SDK or GPU)");
	return failures ? 1 : 0;
}
