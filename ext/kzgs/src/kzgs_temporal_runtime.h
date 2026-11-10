// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kzgs_temporal.h"

struct KzgsConfig;

namespace kzgs
{
	void TemporalStart(const KzgsConfig& config);
	void TemporalConfigure(const KzgsConfig& config);
	void TemporalStop();
	void TemporalBeginVsync(const KzgsTemporalGuestObservation& guest, uint64_t aspectId);
	void TemporalEndVsync();
	void TemporalReset(uint64_t reasons);
	void TemporalDeviceCreated();
	void TemporalDeviceDestroying();
	bool TemporalGetDiagnostics(KzgsTemporalDiagnostics& out);
}
