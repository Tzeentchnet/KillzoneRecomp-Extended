// Even frame pacing: minimum spacing between presented frames.
// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>

namespace kzgs
{
	// Minimum spacing as a fraction of the game's frame period. A full period made pacing worse when the game ran
	// close to its target rate (SCES-52004 chapter 1 at 60 fps: presented rate 56 -> 47 fps, 2.4x more long
	// intervals), because holding a frame for nearly a whole period pushed later frames past the next guest VSync.
	// Three quarters still separates the catch-up frame of a hitch, the bunching that makes interpolation judder,
	// while leaving a quarter period for the GS to keep up. The caller also never holds while another frame is
	// queued. Measurements are in docs/frame-generation.md.
	constexpr int64_t kPacingMinSpacingNum = 3, kPacingMinSpacingDen = 4;

	// Time (steady-clock ns) until which a frame arriving at `nowNs` is held, or 0 to present immediately.
	// `lastNs` is the previous paced present (0 = none yet). Only early frames are held, and a late frame is never
	// delayed further, so this removes bunching without adding a steady frame of latency.
	constexpr int64_t PacingHoldUntilNs(int64_t lastNs, int64_t nowNs, int64_t periodNs)
	{
		if (lastNs <= 0 || periodNs <= 0)
			return 0;
		const int64_t until = lastNs + periodNs * kPacingMinSpacingNum / kPacingMinSpacingDen;
		return nowNs < until ? until : 0;
	}
} // namespace kzgs
