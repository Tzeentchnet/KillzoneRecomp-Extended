// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kzgs_temporal.h"

struct KzgsConfigV1;

namespace kzgs
{
	void TemporalStart(const KzgsConfigV1& config);
	void TemporalConfigure(const KzgsConfigV1& config);
	void TemporalStop();
	void TemporalBeginVsync(const KzgsTemporalGuestObservation& guest, uint64_t aspectId);
	void TemporalEndVsync();
	void TemporalReset(uint64_t reasons);
	void TemporalDeviceCreated();
	void TemporalDeviceDestroying();
	bool TemporalGetDiagnostics(KzgsTemporalDiagnostics& out);
}
