// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kzgs_temporal.h"
#include "kzgs_temporal_contract.h"

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
	bool TemporalGetPendingFrame(KzgsTemporalFrame& out);
	temporal::Decision TemporalBeginDispatch(const KzgsTemporalFrame& frame);
	void TemporalDispatchResult(bool succeeded);
	void TemporalPresentationResult(uint64_t presentationId, bool succeeded);
}
