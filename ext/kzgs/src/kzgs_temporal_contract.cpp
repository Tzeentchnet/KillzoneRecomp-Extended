// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_temporal_contract.h"
#include "kzgs_upscaler_provider.h"

#include <cmath>
#include <initializer_list>
#include <utility>

namespace
{
	bool SameRect(const KzgsTemporalRect& a, const KzgsTemporalRect& b)
	{
		return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
	}

	bool ValidRect(const KzgsTemporalImage& image)
	{
		return image.width && image.height && image.rect.left >= 0 && image.rect.top >= 0 &&
			image.rect.right > image.rect.left && image.rect.bottom > image.rect.top &&
			static_cast<uint32_t>(image.rect.right) <= image.width &&
			static_cast<uint32_t>(image.rect.bottom) <= image.height;
	}

	uint64_t ValidateImage(const KzgsTemporalImage& image, const KzgsTemporalFrame& frame,
		const KzgsTemporalImage* reference)
	{
		uint64_t invalid = 0;
		if (!image.resourceId || image.frameId != frame.frameId || image.sceneId != frame.sceneId)
			invalid |= KZGS_TEMPORAL_INVALID_IDENTITY;
		if (!ValidRect(image) || (reference && (image.width != reference->width ||
			image.height != reference->height || !SameRect(image.rect, reference->rect))))
			invalid |= KZGS_TEMPORAL_INVALID_GEOMETRY;
		if (image.backend == KzgsTemporalBackend::Unknown || !image.nativeFormat ||
			(reference && image.backend != reference->backend))
			invalid |= KZGS_TEMPORAL_INVALID_FORMAT;
		return invalid;
	}

	bool ValidMatrix(const float* matrix)
	{
		double rows[4][4];
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				const float value = matrix[row * 4 + column];
				if (!std::isfinite(value))
					return false;
				rows[row][column] = value;
			}
		}
		for (int column = 0; column < 4; ++column)
		{
			int pivot = column;
			for (int row = column + 1; row < 4; ++row)
				if (std::fabs(rows[row][column]) > std::fabs(rows[pivot][column]))
					pivot = row;
			if (rows[pivot][column] == 0.0)
				return false;
			if (pivot != column)
				for (int i = column; i < 4; ++i)
					std::swap(rows[pivot][i], rows[column][i]);
			for (int row = column + 1; row < 4; ++row)
			{
				const double factor = rows[row][column] / rows[column][column];
				for (int i = column + 1; i < 4; ++i)
					rows[row][i] -= factor * rows[column][i];
			}
		}
		return true;
	}

	uint64_t Validate(const KzgsTemporalFrame& frame)
	{
		uint64_t invalid = 0;
		if (!frame.frameId || !frame.presentationId || !frame.sceneId || !frame.deviceId || !frame.providerId)
			invalid |= KZGS_TEMPORAL_INVALID_IDENTITY;
		if (!frame.outputWidth || !frame.outputHeight || !frame.aspectId)
			invalid |= KZGS_TEMPORAL_INVALID_GEOMETRY;
		if (frame.validInputs & KZGS_TEMPORAL_COLOR)
			invalid |= ValidateImage(frame.mergedColor, frame, nullptr);
		if (frame.validInputs & KZGS_TEMPORAL_WORLD_COLOR)
			invalid |= ValidateImage(frame.worldColor, frame, &frame.mergedColor);
		if (frame.validInputs & KZGS_TEMPORAL_DEPTH)
		{
			invalid |= ValidateImage(frame.depth, frame, &frame.worldColor);
			if ((frame.depthDirection != KzgsTemporalDepthDirection::Forward &&
					frame.depthDirection != KzgsTemporalDepthDirection::Reversed) ||
				frame.depth.backend != KzgsTemporalBackend::Vulkan ||
				(frame.depth.nativeFormat != VK_FORMAT_D16_UNORM && frame.depth.nativeFormat != VK_FORMAT_D32_SFLOAT))
				invalid |= KZGS_TEMPORAL_INVALID_DEPTH;
		}
		if (frame.validInputs & KZGS_TEMPORAL_MOTION)
		{
			invalid |= ValidateImage(frame.motion, frame, &frame.worldColor);
			if (frame.motionUnits != KzgsTemporalMotionUnits::Pixels ||
				frame.motionDirection != KzgsTemporalMotionDirection::PreviousMinusCurrent ||
				frame.motionIncludesJitter || !frame.motionPreviousFrameId ||
				frame.motionPreviousFrameId >= frame.frameId ||
				frame.motion.backend != KzgsTemporalBackend::Vulkan ||
				frame.motion.nativeFormat != VK_FORMAT_R16G16_SFLOAT)
				invalid |= KZGS_TEMPORAL_INVALID_MOTION;
		}
		if (frame.validInputs & KZGS_TEMPORAL_JITTER)
		{
			if (frame.jitterFrameId != frame.frameId || frame.jitterSceneId != frame.sceneId ||
				!frame.jitterWorldOnly || !std::isfinite(frame.jitterX) || !std::isfinite(frame.jitterY))
				invalid |= KZGS_TEMPORAL_INVALID_JITTER;
		}
		for (const auto& pair : {std::pair{KZGS_TEMPORAL_REACTIVE, &frame.reactive},
				std::pair{KZGS_TEMPORAL_COMPOSITION, &frame.composition}, std::pair{KZGS_TEMPORAL_UI, &frame.ui}})
		{
			if (frame.validInputs & pair.first)
			{
				const uint64_t imageInvalid = ValidateImage(*pair.second, frame, &frame.worldColor);
				if (imageInvalid || pair.second->backend != KzgsTemporalBackend::Vulkan ||
					pair.second->nativeFormat != VK_FORMAT_R8_UNORM)
					invalid |= imageInvalid | KZGS_TEMPORAL_INVALID_MASK;
			}
		}
		if (frame.validInputs & KZGS_TEMPORAL_CAMERA)
		{
			const KzgsTemporalCamera& camera = frame.camera;
			if (camera.frameId != frame.frameId || camera.sceneId != frame.sceneId || !camera.cameraId ||
				camera.previousFrameId != frame.motionPreviousFrameId ||
				(camera.depthDirection != KzgsTemporalDepthDirection::Forward &&
					camera.depthDirection != KzgsTemporalDepthDirection::Reversed) ||
				camera.depthDirection != frame.depthDirection || !std::isfinite(camera.nearPlane) ||
				!std::isfinite(camera.farPlane) || camera.nearPlane <= 0.0f ||
				(!camera.infiniteFar && camera.farPlane <= camera.nearPlane) ||
				!std::isfinite(camera.verticalFovRadians) || camera.verticalFovRadians <= 0.0f ||
				camera.verticalFovRadians >= 3.141592654f || !ValidMatrix(camera.worldToView) ||
				!ValidMatrix(camera.previousWorldToView) || !ValidMatrix(camera.viewToClip) ||
				!ValidMatrix(camera.previousViewToClip))
				invalid |= KZGS_TEMPORAL_INVALID_CAMERA;
		}
		if (frame.validInputs & KZGS_TEMPORAL_FRAME_TIME)
		{
			if (frame.timeFrameId != frame.frameId || frame.timeSceneId != frame.sceneId ||
				frame.timePreviousFrameId != frame.motionPreviousFrameId ||
				!std::isfinite(frame.frameTimeMs) || frame.frameTimeMs <= 0.0f)
				invalid |= KZGS_TEMPORAL_INVALID_FRAME_TIME;
		}
		if (frame.validInputs & KZGS_TEMPORAL_COLOR)
		{
			if (frame.mergedColor.backend != KzgsTemporalBackend::Vulkan ||
				(frame.mergedColor.nativeFormat != VK_FORMAT_R8G8B8A8_UNORM &&
					frame.mergedColor.nativeFormat != VK_FORMAT_R16G16B16A16_SFLOAT))
				invalid |= KZGS_TEMPORAL_INVALID_FORMAT;
			const bool linearSdr = frame.colorEncoding == KzgsTemporalColorEncoding::LinearSDR &&
				frame.worldColor.nativeFormat == VK_FORMAT_R8G8B8A8_UNORM;
			const bool linearHdr = frame.colorEncoding == KzgsTemporalColorEncoding::LinearHDR &&
				frame.worldColor.nativeFormat == VK_FORMAT_R16G16B16A16_SFLOAT;
			if (frame.worldColor.backend != KzgsTemporalBackend::Vulkan || (!linearSdr && !linearHdr))
				invalid |= KZGS_TEMPORAL_INVALID_COLOR_ENCODING;
		}
		return invalid;
	}
}

namespace kzgs::temporal
{
	void Tracker::Invalidate(uint64_t reasons)
	{
		if (!reasons)
			return;
		m_resetReasons |= reasons;
		m_historyFrame = m_historyScene = m_historyPresentation = 0;
		m_pendingFrame = m_dispatched = false;
	}

	Decision Tracker::Begin(const KzgsTemporalFrame& frame)
	{
		const bool duplicate = frame.duplicate || (m_lastFrame && frame.frameId == m_lastFrame) ||
			(m_lastSuccessfulPresentation && frame.presentationId <= m_lastSuccessfulPresentation);
		const bool outOfOrder = m_lastFrame && frame.frameId < m_lastFrame;
		if (m_pendingFrame)
			Invalidate(KZGS_TEMPORAL_RESET_PRESENT_FAILED);
		if (frame.resetReasons)
			Invalidate(frame.resetReasons);
		if (duplicate)
			Invalidate(KZGS_TEMPORAL_RESET_DUPLICATE);
		else if (outOfOrder)
			Invalidate(KZGS_TEMPORAL_RESET_FRAME_ORDER);

		if (m_hasObservation)
		{
			const auto& previous = m_lastObserved;
			if (frame.mergedColor.width != previous.mergedColor.width ||
				frame.mergedColor.height != previous.mergedColor.height ||
				frame.outputWidth != previous.outputWidth || frame.outputHeight != previous.outputHeight)
				Invalidate(KZGS_TEMPORAL_RESET_SIZE);
			if (!SameRect(frame.mergedColor.rect, previous.mergedColor.rect))
				Invalidate(KZGS_TEMPORAL_RESET_CROP);
			if (frame.aspectId != previous.aspectId)
				Invalidate(KZGS_TEMPORAL_RESET_ASPECT);
			if (frame.providerId != previous.providerId)
				Invalidate(KZGS_TEMPORAL_RESET_PROVIDER);
			if (frame.deviceId != previous.deviceId)
				Invalidate(KZGS_TEMPORAL_RESET_DEVICE);
			if (frame.sceneId && previous.sceneId && frame.sceneId != previous.sceneId)
				Invalidate(KZGS_TEMPORAL_RESET_SCENE);
			if (frame.camera.cameraId && previous.camera.cameraId &&
				frame.camera.cameraId != previous.camera.cameraId)
				Invalidate(KZGS_TEMPORAL_RESET_CAMERA_CHANGE);
		}
		m_lastObserved = frame;
		m_hasObservation = true;
		if (frame.frameId > m_lastFrame)
			m_lastFrame = frame.frameId;

		Decision result;
		result.missingInputs = KZGS_TEMPORAL_REQUIRED & ~frame.validInputs;
		result.invalidReasons = Validate(frame);
		if (m_historyFrame && (frame.motionPreviousFrameId != m_historyFrame ||
			frame.sceneId != m_historyScene || frame.presentationId != m_historyPresentation + 1))
			result.invalidReasons |= KZGS_TEMPORAL_INVALID_PREVIOUS_FRAME;
		if (duplicate || outOfOrder)
			result.invalidReasons |= KZGS_TEMPORAL_INVALID_IDENTITY;
		if (result.missingInputs || result.invalidReasons)
			Invalidate(KZGS_TEMPORAL_RESET_MISSING_INPUT);
		else
		{
			m_pending = frame;
			m_pendingFrame = true;
			m_dispatched = false;
			result.ready = true;
		}
		result.resetReasons = m_resetReasons;
		return result;
	}

	void Tracker::DispatchResult(bool succeeded)
	{
		if (!m_pendingFrame || !succeeded)
		{
			Invalidate(KZGS_TEMPORAL_RESET_DISPATCH_FAILED);
			return;
		}
		m_dispatched = true;
	}

	void Tracker::PresentResult(uint64_t presentationId, bool succeeded)
	{
		if (!m_pendingFrame || !m_dispatched || !succeeded || presentationId != m_pending.presentationId)
		{
			Invalidate(KZGS_TEMPORAL_RESET_PRESENT_FAILED);
			return;
		}
		m_historyFrame = m_pending.frameId;
		m_historyScene = m_pending.sceneId;
		m_historyPresentation = presentationId;
		m_lastSuccessfulPresentation = presentationId;
		++m_committedFrames;
		m_pendingFrame = m_dispatched = false;
		m_resetReasons = 0;
	}
}
