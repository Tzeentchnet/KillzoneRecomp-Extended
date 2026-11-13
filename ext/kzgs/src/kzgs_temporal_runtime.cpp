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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace
{
	using namespace kzgs::temporal;
	constexpr size_t kMaxTargets = 256;
	constexpr size_t kMaxDisplays = 4;
	constexpr uint64_t kSceneResetReasons = KZGS_TEMPORAL_RESET_CAMERA_CHANGE |
		KZGS_TEMPORAL_RESET_CAMERA_CUT | KZGS_TEMPORAL_RESET_LOAD |
		KZGS_TEMPORAL_RESET_SCENE | KZGS_TEMPORAL_RESET_MOVIE |
		KZGS_TEMPORAL_RESET_WORLD_TRANSITION;
	constexpr float kPi = 3.14159265358979323846f;

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
		bool overflow = false, hasPreviousGuest = false, sceneResetPending = true;
		bool dispatchAttempted = false, dispatchBegan = false, dispatchRecorded = false;
		bool hasPreviousPresentedMatrices = false;
		uint64_t frameSequence = 0, sceneId = 0, deviceId = 0, providerId = 0;
		KzgsConfigV1 config{};
		KzgsTemporalGuestObservation guest{}, previousGuest{};
		KzgsTemporalGuestObservation previousPresentedMatrices{};
		KzgsTemporalFrame frame{};
		Tracker tracker{};
		Decision decision{};
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

	bool ValidGuestCameraParameters(const KzgsTemporalGuestObservation& guest)
	{
		return std::isfinite(guest.nearPlane) && std::isfinite(guest.farPlane) &&
			std::isfinite(guest.verticalFovDegrees) && std::isfinite(guest.aspect) &&
			guest.nearPlane > 0.0f && guest.farPlane > guest.nearPlane &&
			guest.verticalFovDegrees > 0.0f && guest.verticalFovDegrees < 170.0f &&
			guest.aspect > 0.0f;
	}

	bool ValidGuestMatrix(const float (&matrix)[16])
	{
		bool nonzero = false;
		for (const float value : matrix)
		{
			if (!std::isfinite(value))
				return false;
			nonzero |= value != 0.0f;
		}
		return nonzero;
	}

	uint32_t GuestMatrixTraits(const float (&matrix)[16])
	{
		if (!ValidGuestMatrix(matrix))
			return 0;
		const auto approximately = [](float value, float expected) {
			return std::abs(value - expected) <= 0.0001f;
		};
		uint32_t traits = KZGS_TEMPORAL_MATRIX_TRAIT_FINITE_NONZERO;
		if (approximately(matrix[12], 0.0f) && approximately(matrix[13], 0.0f) &&
			approximately(matrix[14], 0.0f) && approximately(matrix[15], 1.0f))
			traits |= KZGS_TEMPORAL_MATRIX_TRAIT_LAST_ROW_0001;
		if (approximately(matrix[3], 0.0f) && approximately(matrix[7], 0.0f) &&
			approximately(matrix[11], 0.0f) && approximately(matrix[15], 1.0f))
			traits |= KZGS_TEMPORAL_MATRIX_TRAIT_LAST_COLUMN_0001;
		if (approximately(matrix[12], 0.0f) && approximately(matrix[13], 0.0f) &&
			!approximately(matrix[14], 0.0f) && approximately(matrix[15], 0.0f))
			traits |= KZGS_TEMPORAL_MATRIX_TRAIT_LAST_ROW_00Z0;
		if (approximately(matrix[3], 0.0f) && approximately(matrix[7], 0.0f) &&
			!approximately(matrix[11], 0.0f) && approximately(matrix[15], 0.0f))
			traits |= KZGS_TEMPORAL_MATRIX_TRAIT_LAST_COLUMN_00Z0;
		return traits;
	}

	bool ValidGuestMatrixCandidate(const KzgsTemporalGuestObservation& guest)
	{
		return (guest.flags & KZGS_TEMPORAL_GUEST_CAMERA_MATRICES_CANDIDATE) &&
			!(guest.flags & (KZGS_TEMPORAL_GUEST_AMBIGUOUS_CAMERA |
				KZGS_TEMPORAL_GUEST_AMBIGUOUS_CAMERA_MATRICES)) &&
			ValidGuestMatrix(guest.renderCameraMatrix310) &&
			ValidGuestMatrix(guest.renderCameraMatrix400);
	}

	bool GuestMatrixChanged(const float (&current)[16], const float (&previous)[16])
	{
		for (size_t i = 0; i < 16; ++i)
			if (current[i] != previous[i])
				return true;
		return false;
	}

	bool ValidGuestFrameTime(const KzgsTemporalGuestObservation& guest)
	{
		return std::isfinite(guest.gameFrameTimeMs) &&
			guest.gameFrameTimeMs > 0.0f && guest.gameFrameTimeMs <= 1000.0f;
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
		image.sceneId = s.frame.sceneId;
		image.resourceId = color.resource;
		image.width = color.width;
		image.height = color.height;
		image.rect = {observation.source_rect.x, observation.source_rect.y,
			observation.source_rect.z, observation.source_rect.w};
		image.backend = color.backend;
		image.nativeFormat = color.nativeFormat;
		image.rendererFormat = color.rendererFormat;
		const float destinationWidth = observation.destination_rect.z - observation.destination_rect.x;
		const float destinationHeight = observation.destination_rect.w - observation.destination_rect.y;
		if (std::isfinite(destinationWidth) && std::isfinite(destinationHeight) &&
			destinationWidth > 0.0f && destinationHeight > 0.0f)
		{
			s.frame.outputWidth = static_cast<uint32_t>(std::ceil(destinationWidth));
			s.frame.outputHeight = static_cast<uint32_t>(std::ceil(destinationHeight));
		}
		s.frame.validInputs |= KZGS_TEMPORAL_COLOR;
		s.frame.qualifiedInputs |= KZGS_TEMPORAL_COLOR;
		if (color.backend == KzgsTemporalBackend::Vulkan &&
			color.nativeFormat == VK_FORMAT_R8G8B8A8_UNORM)
			s.frame.colorEncoding = KzgsTemporalColorEncoding::LinearSDR;

		if (s.frame.inputPolicy != KzgsTemporalInputPolicy::FSR3Experimental)
			return;
		const bool cameraAmbiguous = s.guest.flags & KZGS_TEMPORAL_GUEST_AMBIGUOUS_CAMERA;
		if ((s.guest.flags & KZGS_TEMPORAL_GUEST_CAMERA_CANDIDATE) &&
			!cameraAmbiguous && ValidGuestCameraParameters(s.guest))
		{
			s.frame.camera.frameId = s.frame.frameId;
			s.frame.camera.sceneId = s.frame.sceneId;
			s.frame.camera.cameraId = s.guest.cameraId;
			s.frame.camera.nearPlane = s.guest.nearPlane;
			s.frame.camera.farPlane = s.guest.farPlane;
			s.frame.camera.verticalFovRadians = s.guest.verticalFovDegrees * kPi / 180.0f;
			s.frame.camera.depthDirection = KzgsTemporalDepthDirection::Forward;
			s.frame.camera.validFields = KZGS_TEMPORAL_CAMERA_VALID_SCALARS;
			s.frame.validInputs |= KZGS_TEMPORAL_CAMERA;
			s.frame.approximateInputs |= KZGS_TEMPORAL_CAMERA;
		}
		if ((s.guest.flags & KZGS_TEMPORAL_GUEST_FRAME_TIME_CANDIDATE) &&
			ValidGuestFrameTime(s.guest))
		{
			s.frame.timeFrameId = s.frame.frameId;
			s.frame.timeSceneId = s.frame.sceneId;
			s.frame.frameTimeMs = s.guest.gameFrameTimeMs;
			s.frame.validInputs |= KZGS_TEMPORAL_FRAME_TIME;
			s.frame.approximateInputs |= KZGS_TEMPORAL_FRAME_TIME;
		}
		const uint32_t sourceWidth =
			static_cast<uint32_t>(std::max(0, image.rect.right - image.rect.left));
		const uint32_t sourceHeight =
			static_cast<uint32_t>(std::max(0, image.rect.bottom - image.rect.top));
		if ((s.guest.flags & KZGS_TEMPORAL_GUEST_JITTER_APPLIED) &&
			std::isfinite(s.guest.jitterX) && std::isfinite(s.guest.jitterY) &&
			s.guest.jitterPhaseCount &&
			s.guest.jitterSourceWidth == sourceWidth &&
			s.guest.jitterSourceHeight == sourceHeight &&
			s.guest.jitterDisplayWidth == s.frame.outputWidth &&
			s.guest.jitterDisplayHeight == s.frame.outputHeight)
		{
			s.frame.jitterFrameId = s.frame.frameId;
			s.frame.jitterSceneId = s.frame.sceneId;
			s.frame.jitterX = s.guest.jitterX;
			s.frame.jitterY = s.guest.jitterY;
			s.frame.jitterWorldOnly = true;
			s.frame.validInputs |= KZGS_TEMPORAL_JITTER;
			s.frame.approximateInputs |= KZGS_TEMPORAL_JITTER;
		}
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
		s.sceneResetPending = true;
		s.deviceId = ++s_deviceSequence;
		ClearSnapshot();
		TemporalConfigure(config);
	}

	void TemporalConfigure(const KzgsConfigV1& config)
	{
		const bool enabled = config.temporalDiagnostics || EnvironmentEnabled() ||
			kzgsIsTemporalUpscaler(config.upscaler);
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
			s.hasPreviousPresentedMatrices = false;
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
		if (s.enabled && s.config.fsr3InputMode != config.fsr3InputMode)
			TemporalReset(KZGS_TEMPORAL_RESET_PROVIDER);
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
		s.hasPreviousPresentedMatrices = false;
		if (reasons & kSceneResetReasons)
			s.sceneResetPending = true;
		{
			std::lock_guard lock(s_snapshotMutex);
			if (s_hasSnapshot)
			{
				s_snapshot.resetReasons |= reasons;
				s_snapshot.historyFrameId = 0;
				s_snapshot.temporalValid = false;
				s_snapshot.cameraMatrixPairCandidateAvailable = false;
				s_snapshot.cameraMatrixChangedMask = 0;
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
		s.dispatchAttempted = s.dispatchBegan = s.dispatchRecorded = false;
		s.decision = {};
		s.displayTargets = 0;
		s.guest = guest;
		s.frame = {};
		s.frame.frameId = ++s.frameSequence;
		s.frame.deviceId = s.deviceId;
		s.frame.providerId = s.providerId;
		s.frame.aspectId = aspectId;
		s.frame.resetReasons = guest.resetReasons;
		s.frame.inputPolicy =
			s.config.renderer == KzgsRenderer::Vulkan &&
				s.config.upscaler == KzgsUpscaler::FSR3 &&
				s.config.fsr3InputMode == KzgsFsr3InputMode::Experimental ?
			KzgsTemporalInputPolicy::FSR3Experimental : KzgsTemporalInputPolicy::Strict;
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
		if (s.sceneResetPending || (s.frame.resetReasons & kSceneResetReasons))
		{
			++s.sceneId;
			s.sceneResetPending = false;
		}
		if (!s.sceneId)
			s.sceneId = 1;
		s.frame.sceneId = s.sceneId;
		if (s.frame.resetReasons)
		{
			s.tracker.Invalidate(s.frame.resetReasons);
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
		if (s.dispatchBegan)
		{
			s.tracker.PresentResult(s.frame.presentationId, false);
			s.dispatchBegan = s.dispatchRecorded = false;
		}
		Decision decision = s.dispatchAttempted ? s.decision :
			(s.frame.inputPolicy == KzgsTemporalInputPolicy::FSR3Experimental ?
				ValidateFSR3Experimental(s.frame) : ValidateStrict(s.frame));
		decision.resetReasons |= s.tracker.PendingResetReasons();

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
		snapshot.qualifiedInputs = s.frame.qualifiedInputs;
		snapshot.approximateInputs = s.frame.approximateInputs;
		snapshot.inputPolicy = s.frame.inputPolicy;
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
		snapshot.depthResourceId = s.frame.depth.resourceId;
		snapshot.depthWidth = s.frame.depth.width;
		snapshot.depthHeight = s.frame.depth.height;
		snapshot.depthNativeFormat = s.frame.depth.nativeFormat;
		snapshot.depthDirection = s.frame.depthDirection;
		snapshot.motionResourceId = s.frame.motion.resourceId;
		snapshot.motionNativeFormat = s.frame.motion.nativeFormat;
		snapshot.motionUnits = s.frame.motionUnits;
		snapshot.motionDirection = s.frame.motionDirection;
		snapshot.motionJitterInclusionKnown = s.frame.validInputs & KZGS_TEMPORAL_MOTION;
		snapshot.motionIncludesJitter = s.frame.motionIncludesJitter;
		snapshot.reactiveResourceId = s.frame.reactive.resourceId;
		snapshot.reactiveNativeFormat = s.frame.reactive.nativeFormat;
		snapshot.compositionResourceId = s.frame.composition.resourceId;
		snapshot.compositionNativeFormat = s.frame.composition.nativeFormat;
		snapshot.uiResourceId = s.frame.ui.resourceId;
		snapshot.uiNativeFormat = s.frame.ui.nativeFormat;
		snapshot.jitterAvailable = s.frame.validInputs & KZGS_TEMPORAL_JITTER;
		snapshot.matchedFrameTimeAvailable = s.frame.validInputs & KZGS_TEMPORAL_FRAME_TIME;
		snapshot.cameraParametersAvailable = s.frame.validInputs & KZGS_TEMPORAL_CAMERA;
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
		const bool cameraAmbiguous = s.guest.flags & KZGS_TEMPORAL_GUEST_AMBIGUOUS_CAMERA;
		const bool matricesAmbiguous =
			s.guest.flags & KZGS_TEMPORAL_GUEST_AMBIGUOUS_CAMERA_MATRICES;
		snapshot.cameraParametersCandidateAvailable =
			(s.guest.flags & KZGS_TEMPORAL_GUEST_CAMERA_CANDIDATE) &&
			!cameraAmbiguous && ValidGuestCameraParameters(s.guest);
		snapshot.cameraMatricesCandidateAvailable =
			ValidGuestMatrixCandidate(s.guest);
		if (snapshot.cameraMatricesCandidateAvailable)
		{
			snapshot.cameraMatrix310Traits = GuestMatrixTraits(s.guest.renderCameraMatrix310);
			snapshot.cameraMatrix400Traits = GuestMatrixTraits(s.guest.renderCameraMatrix400);
		}
		if (s.presentSucceeded && snapshot.cameraMatricesCandidateAvailable &&
			s.hasPreviousPresentedMatrices && !s.frame.resetReasons &&
			s.guest.buildId && s.previousPresentedMatrices.buildId != UINT64_MAX &&
			s.guest.buildId == s.previousPresentedMatrices.buildId + 1 &&
			s.guest.cameraId && s.guest.cameraId == s.previousPresentedMatrices.cameraId &&
			(s.guest.flags & KZGS_TEMPORAL_GUEST_WORLD) &&
			(s.previousPresentedMatrices.flags & KZGS_TEMPORAL_GUEST_WORLD))
		{
			snapshot.cameraMatrixPairCandidateAvailable = true;
			if (GuestMatrixChanged(s.guest.renderCameraMatrix310,
				s.previousPresentedMatrices.renderCameraMatrix310))
				snapshot.cameraMatrixChangedMask |= KZGS_TEMPORAL_MATRIX_CHANGED_310;
			if (GuestMatrixChanged(s.guest.renderCameraMatrix400,
				s.previousPresentedMatrices.renderCameraMatrix400))
				snapshot.cameraMatrixChangedMask |= KZGS_TEMPORAL_MATRIX_CHANGED_400;
		}
		snapshot.gameFrameTimeCandidateAvailable =
			(s.guest.flags & KZGS_TEMPORAL_GUEST_FRAME_TIME_CANDIDATE) &&
			ValidGuestFrameTime(s.guest);
		snapshot.gameFrameTimeCandidateMs = snapshot.gameFrameTimeCandidateAvailable ?
			s.guest.gameFrameTimeMs : 0.0f;
		snapshot.candidateAmbiguous |= cameraAmbiguous || matricesAmbiguous ||
			((s.guest.flags & KZGS_TEMPORAL_GUEST_CAMERA_CANDIDATE) &&
				!snapshot.cameraParametersCandidateAvailable) ||
			((s.guest.flags & KZGS_TEMPORAL_GUEST_CAMERA_MATRICES_CANDIDATE) &&
				!snapshot.cameraMatricesCandidateAvailable) ||
			((s.guest.flags & KZGS_TEMPORAL_GUEST_FRAME_TIME_CANDIDATE) &&
				!snapshot.gameFrameTimeCandidateAvailable);
		snapshot.temporalValid = decision.ready;
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
				"guest_camera_candidate=%u guest_matrix_candidate=%u guest_matrix_traits=0x%x/0x%x "
				"guest_matrix_pair=%u guest_matrix_changed=0x%x guest_delta_candidate=%u/%.3f "
				"camera=%llu guest_flags=0x%x delivery_ms=%.3f valid=0x%llx qualified=0x%llx approximate=0x%llx policy=%u missing=0x%llx "
				"invalid_inputs=0x%llx invalid=0x%llx reset=0x%llx history=%llu/%llu temporal_valid=%u presented=%u overflow=%u\n",
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
				snapshot.cameraParametersCandidateAvailable ? 1u : 0u,
				snapshot.cameraMatricesCandidateAvailable ? 1u : 0u,
				snapshot.cameraMatrix310Traits, snapshot.cameraMatrix400Traits,
				snapshot.cameraMatrixPairCandidateAvailable ? 1u : 0u,
				snapshot.cameraMatrixChangedMask,
				snapshot.gameFrameTimeCandidateAvailable ? 1u : 0u, snapshot.gameFrameTimeCandidateMs,
				static_cast<unsigned long long>(snapshot.cameraId),
				snapshot.guestFlags, snapshot.deliveryDeltaMs, static_cast<unsigned long long>(snapshot.validInputs),
				static_cast<unsigned long long>(snapshot.qualifiedInputs),
				static_cast<unsigned long long>(snapshot.approximateInputs),
				static_cast<unsigned>(snapshot.inputPolicy),
				static_cast<unsigned long long>(snapshot.missingInputs),
				static_cast<unsigned long long>(snapshot.invalidInputs),
				static_cast<unsigned long long>(snapshot.invalidReasons),
				static_cast<unsigned long long>(snapshot.resetReasons),
				static_cast<unsigned long long>(snapshot.historyFrameId),
				static_cast<unsigned long long>(snapshot.committedHistoryFrames), snapshot.temporalValid ? 1u : 0u,
				snapshot.presentSucceeded ? 1u : 0u,
				snapshot.observationOverflow ? 1u : 0u);
		}
		if (!s.presentSucceeded || !snapshot.cameraMatricesCandidateAvailable || s.frame.resetReasons)
			s.hasPreviousPresentedMatrices = false;
		if (s.presentSucceeded && snapshot.cameraMatricesCandidateAvailable &&
			(!s.hasPreviousPresentedMatrices ||
				s.guest.buildId != s.previousPresentedMatrices.buildId))
		{
			s.previousPresentedMatrices = s.guest;
			s.hasPreviousPresentedMatrices = true;
		}
		s.inVsync = false;
		s.drawTargets = s.displayTargets = s.drawCount = 0;
		s.overflow = false;
	}

	bool TemporalGetPendingFrame(KzgsTemporalFrame& out)
	{
		if (!s.enabled || !s.inVsync || !s.gotPresent ||
			s.frame.inputPolicy != KzgsTemporalInputPolicy::FSR3Experimental ||
			s.dispatchAttempted)
			return false;
		out = s.frame;
		const uint64_t previous = s.tracker.HistoryFrame() ?
			s.tracker.HistoryFrame() : (out.frameId > 1 ? out.frameId - 1 : 0);
		out.motionPreviousFrameId = previous;
		out.camera.previousFrameId = previous;
		out.timePreviousFrameId = previous;
		return true;
	}

	temporal::Decision TemporalBeginDispatch(const KzgsTemporalFrame& frame)
	{
		s.dispatchAttempted = true;
		if (!s.enabled || !s.inVsync || !s.gotPresent ||
			frame.frameId != s.frame.frameId ||
			frame.presentationId != s.frame.presentationId ||
			frame.sceneId != s.frame.sceneId ||
			frame.deviceId != s.frame.deviceId ||
			frame.providerId != s.frame.providerId ||
			frame.inputPolicy != KzgsTemporalInputPolicy::FSR3Experimental)
		{
			s.tracker.Invalidate(KZGS_TEMPORAL_RESET_FRAME_ORDER);
			s.decision = {};
			s.decision.invalidReasons = KZGS_TEMPORAL_INVALID_IDENTITY |
				KZGS_TEMPORAL_INVALID_POLICY;
			s.decision.resetReasons = s.tracker.PendingResetReasons();
			return s.decision;
		}
		s.frame = frame;
		s.decision = s.tracker.Begin(s.frame);
		s.dispatchBegan = s.decision.ready;
		s.dispatchRecorded = false;
		return s.decision;
	}

	void TemporalDispatchResult(bool succeeded)
	{
		if (!s.dispatchBegan)
			return;
		s.tracker.DispatchResult(succeeded);
		s.dispatchRecorded = succeeded;
		if (!succeeded)
			s.dispatchBegan = false;
		s.decision.resetReasons |= s.tracker.PendingResetReasons();
	}

	void TemporalPresentationResult(uint64_t presentationId, bool succeeded)
	{
		if (!s.dispatchBegan)
			return;
		s.tracker.PresentResult(presentationId, succeeded && s.dispatchRecorded);
		s.dispatchBegan = s.dispatchRecorded = false;
		s.decision.resetReasons |= s.tracker.PendingResetReasons();
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
