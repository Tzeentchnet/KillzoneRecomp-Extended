// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "kzgs_temporal.h"

namespace kzgs::temporal
{
	struct Decision
	{
		uint64_t missingInputs = 0;
		uint64_t invalidReasons = 0;
		uint64_t resetReasons = 0;
		bool ready = false;
	};

	class Tracker
	{
	public:
		Decision Begin(const KzgsTemporalFrame& frame);
		void DispatchResult(bool succeeded);
		void PresentResult(uint64_t presentationId, bool succeeded);
		void Invalidate(uint64_t reasons);

		uint64_t HistoryFrame() const { return m_historyFrame; }
		uint64_t CommittedFrames() const { return m_committedFrames; }
		uint64_t PendingResetReasons() const { return m_resetReasons; }

	private:
		KzgsTemporalFrame m_lastObserved{};
		KzgsTemporalFrame m_pending{};
		uint64_t m_lastFrame = 0;
		uint64_t m_historyFrame = 0;
		uint64_t m_historyScene = 0;
		uint64_t m_historyPresentation = 0;
		uint64_t m_lastSuccessfulPresentation = 0;
		uint64_t m_committedFrames = 0;
		uint64_t m_resetReasons = KZGS_TEMPORAL_RESET_INITIAL;
		bool m_hasObservation = false;
		bool m_pendingFrame = false;
		bool m_dispatched = false;
	};
}
