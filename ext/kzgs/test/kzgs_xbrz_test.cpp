// SPDX-License-Identifier: GPL-3.0+

#include "kz_xbrz.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
	int failures = 0;

	void expectResult(const char* name, KzXbrzScaleResult actual, KzXbrzScaleResult expected)
	{
		if (actual == expected)
			return;

		std::fprintf(stderr, "%s: expected %s, got %s\n", name, kzXbrzScaleResultName(expected),
			kzXbrzScaleResultName(actual));
		failures++;
	}

	void expectTrue(const char* name, bool value)
	{
		if (value)
			return;

		std::fprintf(stderr, "%s\n", name);
		failures++;
	}
}

int main()
{
	KzXbrzImageSize output;
	expectResult("2x size result", kzXbrzGetOutputSize(2, 2, 2, &output), KzXbrzScaleResult::Success);
	expectTrue("2x size dimensions", output.width == 4 && output.height == 4);
	expectTrue("2x size storage", output.pixels == 16 && output.bytes == 64);

	constexpr std::array<std::uint32_t, 4> source = {
		0xff0000ffu,
		0xff00ff00u,
		0xffff0000u,
		0x80ffffffu,
	};
	std::array<std::uint32_t, 16> scaled_a = {};
	std::array<std::uint32_t, 16> scaled_b = {};
	expectResult("2x scale", kzXbrzScaleRgba(source.data(), source.size(), 2, 2, 2, scaled_a.data(),
								  scaled_a.size()),
		KzXbrzScaleResult::Success);
	expectResult("2x deterministic scale", kzXbrzScaleRgba(source.data(), source.size(), 2, 2, 2, scaled_b.data(),
										 scaled_b.size()),
		KzXbrzScaleResult::Success);
	expectTrue("2x deterministic output", scaled_a == scaled_b);
	expectTrue("2x output populated",
		scaled_a.front() != 0 && scaled_a[3] != 0 && scaled_a[12] != 0 && scaled_a.back() != 0);

	constexpr std::uint32_t translucent = 0x4080c0ffu;
	std::array<std::uint32_t, 4> single = {};
	expectResult("single-pixel alpha scale",
		kzXbrzScaleRgba(&translucent, 1, 1, 1, 2, single.data(), single.size()), KzXbrzScaleResult::Success);
	expectTrue("single-pixel alpha preserved",
		single[0] == translucent && single[1] == translucent && single[2] == translucent && single[3] == translucent);

	expectResult("null size output", kzXbrzGetOutputSize(1, 1, 2, nullptr), KzXbrzScaleResult::NullArgument);
	expectResult("empty width", kzXbrzGetOutputSize(0, 1, 2, &output), KzXbrzScaleResult::EmptyImage);
	expectResult("unsupported 1x", kzXbrzGetOutputSize(1, 1, 1, &output), KzXbrzScaleResult::UnsupportedFactor);
	expectResult("unsupported 3x", kzXbrzGetOutputSize(1, 1, 3, &output), KzXbrzScaleResult::UnsupportedFactor);
	expectResult("dimension overflow",
		kzXbrzGetOutputSize(std::numeric_limits<std::uint32_t>::max(), 1, 2, &output),
		KzXbrzScaleResult::DimensionOverflow);
	expectResult("null source",
		kzXbrzScaleRgba(nullptr, source.size(), 2, 2, 2, scaled_a.data(), scaled_a.size()),
		KzXbrzScaleResult::NullArgument);
	expectResult("null destination",
		kzXbrzScaleRgba(source.data(), source.size(), 2, 2, 2, nullptr, scaled_a.size()),
		KzXbrzScaleResult::NullArgument);
	expectResult("source too small",
		kzXbrzScaleRgba(source.data(), source.size() - 1, 2, 2, 2, scaled_a.data(), scaled_a.size()),
		KzXbrzScaleResult::SourceTooSmall);
	expectResult("destination too small",
		kzXbrzScaleRgba(source.data(), source.size(), 2, 2, 2, scaled_a.data(), scaled_a.size() - 1),
		KzXbrzScaleResult::DestinationTooSmall);

	if (failures != 0)
	{
		std::fprintf(stderr, "kzgs xBRZ scaler: %d failure(s)\n", failures);
		return 1;
	}

	std::puts("kzgs xBRZ scaler: PASS");
	return 0;
}
