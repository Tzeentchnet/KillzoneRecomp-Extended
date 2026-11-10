// SPDX-License-Identifier: GPL-3.0+

#include "GS/Renderers/HW/GSTextureCache.h"

#include <array>
#include <cstdio>

int main()
{
	std::array<u32, 256> clut{};
	for (u32 i = 0; i < clut.size(); ++i)
		clut[i] = i << 24;

	GSTextureCache::Palette palette16(clut.data(), 16, false);
	const std::pair<u8, u8> full = palette16.GetAlphaMinMax();
	if (full != std::pair<u8, u8>{0, 15})
	{
		std::fprintf(stderr, "unexpected full 16-entry alpha range: %u-%u\n", full.first, full.second);
		return 1;
	}
	if (palette16.GetAlphaMinMax(3, 9) != std::pair<u8, u8>{3, 9})
	{
		std::fprintf(stderr, "valid palette subrange was not preserved\n");
		return 1;
	}
	if (palette16.GetAlphaMinMax(9, 3) != full)
	{
		std::fprintf(stderr, "inverted palette subrange did not fall back to the full range\n");
		return 1;
	}
	if (palette16.GetAlphaMinMax(16, 16) != full)
	{
		std::fprintf(stderr, "out-of-bounds palette subrange did not fall back to the full range\n");
		return 1;
	}

	std::puts("kzgs palette range guard: PASS");
	return 0;
}
