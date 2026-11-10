// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_temporal_runtime.h"

#include "kzgs.h"
#include "kzgs_provider_runtime.h"
#include "kzgs_temporal_contract.h"

#include "GS/Renderers/Common/GSDevice.h"
#include "GS/Renderers/Common/GSRenderer.h"
#include "GS/Renderers/DX11/GSTexture11.h"
#include "GS/Renderers/DX12/GSTexture12.h"
#include "GS/Renderers/Vulkan/GSTextureVK.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace
{
	using namespace kzgs::temporal;
	constexpr size_t kMaxTargets = 256;
	constexpr size_t kMaxDisplays = 4;

	struct ImageObservation
	{
		uint64_t target = 0, resource = 0;
		uint32_t width = 0, height = 0, gsFormat = 0, rendererFormat = 0, nativeFormat = 0;
		KzgsTemporalBackend backend = KzgsTemporalBackend::Unknown;
	};

	struct Draws
	{
		ImageObservation color{}, depth{};
		uint32_t count = 0, depthCount = 0, depthWrites = 0, depthTestMask = 0;
		uint32_t depthCandidates = 0; // 2 means two or more
		bool ambiguous = false;
	};

	struct State
	{
		bool enabled = false, log = false, inVsync = false, gotPresent = false, presentSucceeded = false;
		bool overflow = false, hasPreviousGuest = false;
		uint64_t frameSequence = 0, deviceId = 0, providerId = 0;
		KzgsConfigV1 config{};
		KzgsTemporalGuestObservation guest{}, previousGuest{};
		KzgsTemporalFrame frame{};
		Tracker tracker{};
		std::array<Draws, kMaxTargets> draws{};
		size_t drawTargets = 0;
		std::array<ImageObservation, kMaxDisplays> displays{};
		size_t displayTargets = 0;
		uint32_t drawCount = 0;
		std::chrono::steady_clock::time_point lastDelivery{};
		float deliveryDeltaMs = 0.0f;
	};

	State s;
	uint64_t s_deviceSequence = 0;
	std::mutex s_snapshotMutex;
	KzgsTemporalDiagnostics s_snapshot;
	bool s_hasSnapshot = false;

	bool EnvironmentEnabled()
	{
		const char* value = std::getenv("KZGS_TEMPORAL_DIAGNOSTICS");
		return value && std::strcmp(value, "1") == 0;
	}

	void ClearSnapshot()
	{
		std::lock_guard lock(s_snapshotMutex);
		s_snapshot = {};
		s_hasSnapshot = false;
	}

	ImageObservation Observe(const GSTemporalTargetObservation& target)
	{
		ImageObservation result;
		result.target = target.target_id;
		result.gsFormat = target.gs_format;
		GSTexture* texture = target.texture;
		if (!texture || !g_gs_device)
			return result;
		result.resource = texture->GetObservationId();
		result.width = static_cast<uint32_t>(texture->GetWidth());
		result.height = static_cast<uint32_t>(texture->GetHeight());
		result.rendererFormat = static_cast<uint32_t>(texture->GetFormat());
		switch (g_gs_device->GetRenderAPI())
		{
			case RenderAPI::Vulkan:
				result.backend = KzgsTemporalBackend::Vulkan;
				result.nativeFormat = static_cast<uint32_t>(static_cast<GSTextureVK*>(texture)->GetVkFormat());
				break;
			case RenderAPI::D3D11:
				result.backend = KzgsTemporalBackend::D3D11;
				result.nativeFormat = static_cast<uint32_t>(GSTexture11::GetDXGIFormat(texture->GetFormat()));
				break;
			case RenderAPI::D3D12:
				result.backend = KzgsTemporalBackend::D3D12;
				result.nativeFormat = static_cast<uint32_t>(static_cast<GSTexture12*>(texture)->GetDXGIFormat());
				break;
			default:
				break;
		}
		return result;
	}

	void OnDraw(void*, const GSTemporalDrawObservation& observation)
	{
		++s.drawCount;
		const ImageObservation color = Observe(observation.color);
		if (!color.target || !color.resource)
			return;
		size_t index = 0;
		while (index < s.drawTargets &&
			(s.draws[index].color.target != color.target || s.draws[index].color.resource != color.resource))
			++index;
		if (index == s.drawTargets)
		{
			if (index == s.draws.size())
			{
				s.overflow = true;
				return;
			}
			s.draws[index] = {};
			s.draws[index].color = color;
			++s.drawTargets;
		}
		Draws& draws = s.draws[index];
		++draws.count;
		const ImageObservation depth = Observe(observation.depth);
		if (!depth.target || !depth.resource || observation.temporary_depth || !observation.depth_test_enabled)
			return;
		++draws.depthCount;
		draws.depthWrites += observation.depth_write_enabled ? 1u : 0u;
		if (observation.gs_depth_test < 32)
			draws.depthTestMask |= 1u << observation.gs_depth_test;
		if (!draws.depthCandidates)
		{
			draws.depth = depth;
			draws.depthCandidates = 1;
		}
		else if (draws.depth.target != depth.target || draws.depth.resource != depth.resource ||
			draws.depth.gsFormat != depth.gsFormat || draws.depth.nativeFormat != depth.nativeFormat)
		{
			draws.ambiguous = true;
			draws.depthCandidates = 2;
		}
	}

	void OnDisplay(void*, int, const GSTemporalTargetObservation& observation)
	{
		if (!s.inVsync)
			return;
		const ImageObservation display = Observe(observation);
		if (!display.target || !display.resource)
			return;
		for (size_t i = 0; i < s.displayTargets; ++i)
			if (s.displays[i].target == display.target && s.displays[i].resource == display.resource)
				return;
		if (s.displayTargets == s.displays.size())
		{
			s.overflow = true;
			return;
		}
		s.displays[s.displayTargets++] = display;
	}

	void OnPresent(void*, const GSTemporalPresentObservation& observation)
	{
		if (observation.is_redraw)
		{
			kzgs::TemporalReset(KZGS_TEMPORAL_RESET_DUPLICATE);
			return;
		}
		if (!s.inVsync)
			return;
		s.gotPresent = true;
		s.frame.presentationId = observation.presentation_id;
		s.frame.duplicate = observation.source_may_repeat;
		const ImageObservation color = Observe({0, observation.texture, 0});
		if (!color.resource)
			return;
		KzgsTemporalImage& image = s.frame.mergedColor;
		image.frameId = s.frame.frameId;
		image.resourceId = color.resource;
		image.width = color.width;
		image.height = color.height;
		image.rect = {observation.source_rect.x, observation.source_rect.y,
			observation.source_rect.z, observation.source_rect.w};
		image.backend = color.backend;
		image.nativeFormat = color.nativeFormat;
		image.rendererFormat = color.rendererFormat;
		s.frame.outputWidth = static_cast<uint32_t>(g_gs_device->GetWindowWidth());
		s.frame.outputHeight = static_cast<uint32_t>(g_gs_device->GetWindowHeight());
		s.frame.validInputs |= KZGS_TEMPORAL_COLOR;
	}

	void OnPresented(void*, uint64_t, bool succeeded, bool skipped, bool redraw)
	{
		if (redraw)
		{
			kzgs::TemporalReset(KZGS_TEMPORAL_RESET_DUPLICATE |
				(!succeeded ? KZGS_TEMPORAL_RESET_PRESENT_FAILED : 0));
			return;
		}
		if (!s.inVsync)
			return;
		s.presentSucceeded = succeeded && !skipped;
		if (skipped)
			s.frame.resetReasons |= KZGS_TEMPORAL_RESET_SKIPPED;
		else if (!succeeded)
			s.frame.resetReasons |= KZGS_TEMPORAL_RESET_PRESENT_FAILED;
	}

	void Install()
	{
		GSTemporalObservationHooks hooks;
		hooks.draw = OnDraw;
		hooks.display = OnDisplay;
		hooks.present = OnPresent;
		hooks.presented = OnPresented;
		GSSetTemporalObservationHooks(hooks);
	}

	uint64_t ProviderIdentity(KzgsUpscaler provider)
	{
		return static_cast<uint64_t>(provider) + 1;
	}
}

namespace kzgs
{
	void TemporalStart(const KzgsConfigV1& config)
	{
		s = {};
		s.deviceId = ++s_deviceSequence;
		ClearSnapshot();
		TemporalConfigure(config);
	}

	void TemporalConfigure(const KzgsConfigV1& config)
	{
		const bool enabled = config.temporalDiagnostics || EnvironmentEnabled() ||
			config.upscaler == KzgsUpscaler::DLSS || config.upscaler == KzgsUpscaler::XeSS;
		if (s.enabled && !enabled)
		{
			GSSetTemporalObservationHooks({});
			s.enabled = false;
			s.drawTargets = s.displayTargets = s.drawCount = 0;
			s.tracker.Invalidate(KZGS_TEMPORAL_RESET_PROVIDER);
			ClearSnapshot();
		}
		if (!s.enabled && enabled)
		{
			s.enabled = true;
			s.tracker = {};
			s.drawTargets = s.displayTargets = s.drawCount = 0;
			s.hasPreviousGuest = false;
			Install();
		}
		if (s.enabled && s.providerId && s.providerId != ProviderIdentity(config.upscaler))
			TemporalReset(KZGS_TEMPORAL_RESET_PROVIDER);
		if (s.enabled && s.config.aspect != config.aspect)
			TemporalReset(KZGS_TEMPORAL_RESET_ASPECT);
		if (s.enabled && std::memcmp(s.config.crop, config.crop, sizeof(config.crop)) != 0)
			TemporalReset(KZGS_TEMPORAL_RESET_CROP);
		if (s.enabled && s.config.renderer != config.renderer)
			TemporalReset(KZGS_TEMPORAL_RESET_DEVICE);
		s.providerId = ProviderIdentity(config.upscaler);
		s.config = config;
		s.log = EnvironmentEnabled();
	}

	void TemporalStop()
	{
		if (s.enabled)
			GSSetTemporalObservationHooks({});
		s = {};
		ClearSnapshot();
	}

	void TemporalReset(uint64_t reasons)
	{
		if (!s.enabled || !reasons)
			return;
		s.tracker.Invalidate(reasons);
		{
			std::lock_guard lock(s_snapshotMutex);
			if (s_hasSnapshot)
			{
				s_snapshot.resetReasons |= reasons;
				s_snapshot.historyFrameId = 0;
				s_snapshot.temporalValid = false;
			}
		}
		if (s.inVsync)
			s.frame.resetReasons |= reasons;
		else
		{
			s.drawTargets = s.displayTargets = s.drawCount = 0;
			s.overflow = false;
		}
	}

	void TemporalDeviceCreated()
	{
		if (!s.enabled)
			return;
		s.deviceId = ++s_deviceSequence;
		TemporalReset(KZGS_TEMPORAL_RESET_DEVICE);
	}

	void TemporalDeviceDestroying()
	{
		TemporalReset(KZGS_TEMPORAL_RESET_DEVICE);
	}

	void TemporalBeginVsync(const KzgsTemporalGuestObservation& guest, uint64_t aspectId)
	{
		if (!s.enabled)
			return;
		s.inVsync = true;
		s.gotPresent = s.presentSucceeded = false;
		s.displayTargets = 0;
		s.guest = guest;
		s.frame = {};
		s.frame.frameId = ++s.frameSequence;
		s.frame.deviceId = s.deviceId;
		s.frame.providerId = s.providerId;
		s.frame.aspectId = aspectId;
		s.frame.resetReasons = guest.resetReasons;
		if (guest.buildId && s.hasPreviousGuest && guest.buildId != s.previousGuest.buildId)
		{
			if ((guest.flags & KZGS_TEMPORAL_GUEST_WORLD) !=
				(s.previousGuest.flags & KZGS_TEMPORAL_GUEST_WORLD))
				s.frame.resetReasons |= KZGS_TEMPORAL_RESET_WORLD_TRANSITION;
			if (guest.cameraId && s.previousGuest.cameraId && guest.cameraId != s.previousGuest.cameraId)
				s.frame.resetReasons |= KZGS_TEMPORAL_RESET_CAMERA_CHANGE;
		}
		if (guest.buildId)
		{
			s.previousGuest = guest;
			s.hasPreviousGuest = true;
		}
		if (s.frame.resetReasons)
		{
			TemporalReset(s.frame.resetReasons);
			ProviderReset(KZGS_PROVIDER_RESET_DISCONTINUITY);
		}
		const auto now = std::chrono::steady_clock::now();
		s.deliveryDeltaMs = s.lastDelivery.time_since_epoch().count() ?
			std::chrono::duration<float, std::milli>(now - s.lastDelivery).count() : 0.0f;
		s.lastDelivery = now;
	}

	void TemporalEndVsync()
	{
		if (!s.enabled)
			return;
		if (!s.gotPresent)
			s.frame.resetReasons |= KZGS_TEMPORAL_RESET_SKIPPED;
		if (s.overflow)
			s.frame.resetReasons |= KZGS_TEMPORAL_RESET_MISSING_INPUT;
		const Decision decision = s.tracker.Begin(s.frame);

		KzgsTemporalDiagnostics snapshot;
		snapshot.frameId = s.frame.frameId;
		snapshot.presentationId = s.frame.presentationId;
		snapshot.sceneId = s.frame.sceneId;
		snapshot.deviceId = s.frame.deviceId;
		snapshot.providerId = s.frame.providerId;
		snapshot.guestVsync = s.guest.guestVsync;
		snapshot.guestBuildId = s.guest.buildId;
		snapshot.cameraId = s.guest.cameraId;
		snapshot.guestFlags = s.guest.flags;
		snapshot.mergedResourceId = s.frame.mergedColor.resourceId;
		snapshot.width = s.frame.mergedColor.width;
		snapshot.height = s.frame.mergedColor.height;
		snapshot.sourceRect = s.frame.mergedColor.rect;
		snapshot.nativeFormat = s.frame.mergedColor.nativeFormat;
		snapshot.rendererFormat = s.frame.mergedColor.rendererFormat;
		snapshot.backend = s.frame.mergedColor.backend;
		snapshot.validInputs = s.frame.validInputs;
		snapshot.missingInputs = decision.missingInputs;
		snapshot.invalidInputs = decision.invalidInputs;
		snapshot.invalidReasons = decision.invalidReasons;
		snapshot.resetReasons = decision.resetReasons;
		snapshot.committedHistoryFrames = s.tracker.CommittedFrames();
		snapshot.historyFrameId = s.tracker.HistoryFrame();
		snapshot.drawCount = s.drawCount;
		snapshot.displayCount = static_cast<uint32_t>(s.displayTargets);
		snapshot.observationOverflow = s.overflow;
		snapshot.presentSucceeded = s.presentSucceeded;
		snapshot.deliveryDeltaMs = s.deliveryDeltaMs;
		for (size_t i = 0; i < s.displayTargets; ++i)
		{
			const ImageObservation& display = s.displays[i];
			if (!snapshot.displayTargetId)
			{
				snapshot.displayTargetId = display.target;
				snapshot.displayResourceId = display.resource;
				snapshot.displayWidth = display.width;
				snapshot.displayHeight = display.height;
				snapshot.displayGsFormat = display.gsFormat;
			}
			for (size_t j = 0; j < s.drawTargets; ++j)
			{
				const Draws& draws = s.draws[j];
				if (draws.color.target != display.target || draws.color.resource != display.resource)
					continue;
				snapshot.displayedDrawCount += draws.count;
				snapshot.depthDrawCount += draws.depthCount;
				snapshot.depthWriteCount += draws.depthWrites;
				snapshot.depthTestMask |= draws.depthTestMask;
				if (draws.depthCandidates)
				{
					if (snapshot.depthTargetId && (snapshot.depthTargetId != draws.depth.target ||
						snapshot.depthResourceId != draws.depth.resource))
						snapshot.candidateAmbiguous = true;
					if (!snapshot.depthTargetId)
					{
						snapshot.depthTargetId = draws.depth.target;
						snapshot.depthResourceId = draws.depth.resource;
						snapshot.depthWidth = draws.depth.width;
						snapshot.depthHeight = draws.depth.height;
						snapshot.depthGsFormat = draws.depth.gsFormat;
						snapshot.depthNativeFormat = draws.depth.nativeFormat;
					}
					snapshot.depthCandidateCount = snapshot.candidateAmbiguous || draws.ambiguous ?
						2u : 1u;
					snapshot.candidateAmbiguous |= draws.ambiguous;
				}
			}
		}
		// Metadata correlations and guest camera candidates are deliberately not
		// promoted to matched temporal resources or sent to a provider.
		snapshot.temporalValid = false;
		{
			std::lock_guard lock(s_snapshotMutex);
			s_snapshot = snapshot;
			s_hasSnapshot = true;
		}
		if (s.log)
		{
			std::fprintf(stderr,
				"[kz-temporal] frame=%llu present=%llu scene=%llu device=%llu provider=%llu guest_vsync=%llu build=%llu "
				"merged=%llu format=%u renderer_format=%u backend=%u size=%ux%u rect=%d,%d,%d,%d "
				"display=%llu/%llu psm=%u size=%ux%u "
				"draws=%u displayed_draws=%u depth_draws=%u depth_writes=%u depth_candidates=%u "
				"depth=%llu/%llu depth_psm=%u depth_format=%u size=%ux%u depth_direction=%u ztest_mask=0x%x "
				"world=%llu/%u motion=%llu/%u units=%u direction=%u jitter_inclusion_known=%u "
				"masks=%llu/%u,%llu/%u,%llu/%u jitter_available=%u matched_delta_available=%u camera_parameters_available=%u "
				"camera=%llu guest_flags=0x%x delivery_ms=%.3f valid=0x%llx missing=0x%llx "
				"invalid_inputs=0x%llx invalid=0x%llx reset=0x%llx history=%llu/%llu temporal_valid=0 presented=%u overflow=%u\n",
				static_cast<unsigned long long>(snapshot.frameId),
				static_cast<unsigned long long>(snapshot.presentationId),
				static_cast<unsigned long long>(snapshot.sceneId),
				static_cast<unsigned long long>(snapshot.deviceId),
				static_cast<unsigned long long>(snapshot.providerId),
				static_cast<unsigned long long>(snapshot.guestVsync),
				static_cast<unsigned long long>(snapshot.guestBuildId),
				static_cast<unsigned long long>(snapshot.mergedResourceId), snapshot.nativeFormat, snapshot.rendererFormat,
				static_cast<unsigned>(snapshot.backend), snapshot.width, snapshot.height,
				snapshot.sourceRect.left, snapshot.sourceRect.top, snapshot.sourceRect.right, snapshot.sourceRect.bottom,
				static_cast<unsigned long long>(snapshot.displayTargetId),
				static_cast<unsigned long long>(snapshot.displayResourceId), snapshot.displayGsFormat,
				snapshot.displayWidth, snapshot.displayHeight,
				snapshot.drawCount, snapshot.displayedDrawCount, snapshot.depthDrawCount, snapshot.depthWriteCount,
				snapshot.depthCandidateCount, static_cast<unsigned long long>(snapshot.depthTargetId),
				static_cast<unsigned long long>(snapshot.depthResourceId), snapshot.depthGsFormat,
				snapshot.depthNativeFormat, snapshot.depthWidth, snapshot.depthHeight,
				static_cast<unsigned>(snapshot.depthDirection), snapshot.depthTestMask,
				static_cast<unsigned long long>(snapshot.worldResourceId), snapshot.worldNativeFormat,
				static_cast<unsigned long long>(snapshot.motionResourceId), snapshot.motionNativeFormat,
				static_cast<unsigned>(snapshot.motionUnits), static_cast<unsigned>(snapshot.motionDirection),
				snapshot.motionJitterInclusionKnown ? 1u : 0u,
				static_cast<unsigned long long>(snapshot.reactiveResourceId), snapshot.reactiveNativeFormat,
				static_cast<unsigned long long>(snapshot.compositionResourceId), snapshot.compositionNativeFormat,
				static_cast<unsigned long long>(snapshot.uiResourceId), snapshot.uiNativeFormat, snapshot.jitterAvailable ? 1u : 0u,
				snapshot.matchedFrameTimeAvailable ? 1u : 0u, snapshot.cameraParametersAvailable ? 1u : 0u,
				static_cast<unsigned long long>(snapshot.cameraId),
				snapshot.guestFlags, snapshot.deliveryDeltaMs, static_cast<unsigned long long>(snapshot.validInputs),
				static_cast<unsigned long long>(snapshot.missingInputs),
				static_cast<unsigned long long>(snapshot.invalidInputs),
				static_cast<unsigned long long>(snapshot.invalidReasons),
				static_cast<unsigned long long>(snapshot.resetReasons),
				static_cast<unsigned long long>(snapshot.historyFrameId),
				static_cast<unsigned long long>(snapshot.committedHistoryFrames), snapshot.presentSucceeded ? 1u : 0u,
				snapshot.observationOverflow ? 1u : 0u);
		}
		s.inVsync = false;
		s.drawTargets = s.displayTargets = s.drawCount = 0;
		s.overflow = false;
	}

	bool TemporalGetDiagnostics(KzgsTemporalDiagnostics& out)
	{
		std::lock_guard lock(s_snapshotMutex);
		if (!s_hasSnapshot)
			return false;
		out = s_snapshot;
		return true;
	}
}
