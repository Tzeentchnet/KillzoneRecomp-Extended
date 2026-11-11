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

	struct Validation
	{
		uint64_t inputs = 0;
		uint64_t reasons = 0;

		void Add(uint64_t input, uint64_t invalid)
		{
			if (invalid)
			{
				inputs |= input;
				reasons |= invalid;
			}
		}
	};

	enum class ValidationMode
	{
		Strict,
		FSR3Experimental
	};

	void ValidateProvenance(const KzgsTemporalFrame& frame, ValidationMode mode, Validation& result)
	{
		uint64_t invalid = (frame.validInputs & ~KZGS_TEMPORAL_REQUIRED) |
			(frame.qualifiedInputs & ~frame.validInputs) |
			(frame.approximateInputs & ~frame.validInputs) |
			(frame.qualifiedInputs & frame.approximateInputs);
		if (mode == ValidationMode::Strict)
		{
			if (frame.inputPolicy != KzgsTemporalInputPolicy::Strict)
				result.reasons |= KZGS_TEMPORAL_INVALID_POLICY;
			invalid |= frame.approximateInputs;
		}
		else
		{
			if (frame.inputPolicy != KzgsTemporalInputPolicy::FSR3Experimental)
				result.reasons |= KZGS_TEMPORAL_INVALID_POLICY;
			invalid |= frame.validInputs & ~(frame.qualifiedInputs | frame.approximateInputs);
		}
		if (invalid)
			result.Add(invalid, KZGS_TEMPORAL_INVALID_PROVENANCE);
	}

	bool InvalidCameraScalars(const KzgsTemporalCamera& camera, const KzgsTemporalFrame& frame)
	{
		return camera.frameId != frame.frameId || camera.sceneId != frame.sceneId || !camera.cameraId ||
			camera.previousFrameId != frame.motionPreviousFrameId ||
			(camera.depthDirection != KzgsTemporalDepthDirection::Forward &&
				camera.depthDirection != KzgsTemporalDepthDirection::Reversed) ||
			camera.depthDirection != frame.depthDirection || !std::isfinite(camera.nearPlane) ||
			!std::isfinite(camera.farPlane) || camera.nearPlane <= 0.0f ||
			(!camera.infiniteFar && camera.farPlane <= camera.nearPlane) ||
			!std::isfinite(camera.verticalFovRadians) || camera.verticalFovRadians <= 0.0f ||
			camera.verticalFovRadians >= 3.141592654f;
	}

	bool InvalidExperimentalCamera(const KzgsTemporalCamera& camera, const KzgsTemporalFrame& frame)
	{
		if (InvalidCameraScalars(camera, frame) ||
			(camera.validFields & KZGS_TEMPORAL_CAMERA_VALID_SCALARS) !=
				KZGS_TEMPORAL_CAMERA_VALID_SCALARS ||
			(camera.validFields & ~KZGS_TEMPORAL_CAMERA_VALID_ALL))
			return true;
		return ((camera.validFields & KZGS_TEMPORAL_CAMERA_VALID_WORLD_TO_VIEW) &&
				!ValidMatrix(camera.worldToView)) ||
			((camera.validFields & KZGS_TEMPORAL_CAMERA_VALID_PREVIOUS_WORLD_TO_VIEW) &&
				!ValidMatrix(camera.previousWorldToView)) ||
			((camera.validFields & KZGS_TEMPORAL_CAMERA_VALID_VIEW_TO_CLIP) &&
				!ValidMatrix(camera.viewToClip)) ||
			((camera.validFields & KZGS_TEMPORAL_CAMERA_VALID_PREVIOUS_VIEW_TO_CLIP) &&
				!ValidMatrix(camera.previousViewToClip));
	}

	Validation Validate(const KzgsTemporalFrame& frame, ValidationMode mode)
	{
		Validation result;
		const bool experimental = mode == ValidationMode::FSR3Experimental;
		ValidateProvenance(frame, mode, result);
		if (!frame.frameId || !frame.presentationId || !frame.sceneId || !frame.deviceId || !frame.providerId)
			result.reasons |= KZGS_TEMPORAL_INVALID_IDENTITY;
		if (!frame.outputWidth || !frame.outputHeight || !frame.aspectId)
			result.reasons |= KZGS_TEMPORAL_INVALID_GEOMETRY;
		if (frame.validInputs & KZGS_TEMPORAL_COLOR)
			result.Add(KZGS_TEMPORAL_COLOR, ValidateImage(frame.mergedColor, frame, nullptr));
		if (frame.validInputs & KZGS_TEMPORAL_WORLD_COLOR)
			result.Add(KZGS_TEMPORAL_WORLD_COLOR, ValidateImage(frame.worldColor, frame, &frame.mergedColor));
		if (frame.validInputs & KZGS_TEMPORAL_DEPTH)
		{
			const KzgsTemporalImage& reference = experimental ? frame.mergedColor : frame.worldColor;
			uint64_t invalid = ValidateImage(frame.depth, frame, &reference);
			const bool validFormat = frame.depth.nativeFormat == VK_FORMAT_D16_UNORM ||
				frame.depth.nativeFormat == VK_FORMAT_D32_SFLOAT ||
				(experimental && frame.depth.nativeFormat == VK_FORMAT_R32_SFLOAT);
			if ((frame.depthDirection != KzgsTemporalDepthDirection::Forward &&
					frame.depthDirection != KzgsTemporalDepthDirection::Reversed) ||
				frame.depth.backend != KzgsTemporalBackend::Vulkan || !validFormat)
				invalid |= KZGS_TEMPORAL_INVALID_DEPTH;
			result.Add(KZGS_TEMPORAL_DEPTH, invalid);
		}
		if (frame.validInputs & KZGS_TEMPORAL_MOTION)
		{
			const KzgsTemporalImage& reference = experimental ? frame.mergedColor : frame.worldColor;
			uint64_t invalid = ValidateImage(frame.motion, frame, &reference);
			if (frame.motionUnits != KzgsTemporalMotionUnits::Pixels ||
				frame.motionDirection != KzgsTemporalMotionDirection::PreviousMinusCurrent ||
				frame.motionIncludesJitter || !frame.motionPreviousFrameId ||
				frame.motionPreviousFrameId >= frame.frameId ||
				frame.motion.backend != KzgsTemporalBackend::Vulkan ||
				frame.motion.nativeFormat != VK_FORMAT_R16G16_SFLOAT)
				invalid |= KZGS_TEMPORAL_INVALID_MOTION;
			result.Add(KZGS_TEMPORAL_MOTION, invalid);
		}
		if (frame.validInputs & KZGS_TEMPORAL_JITTER)
		{
			if (frame.jitterFrameId != frame.frameId || frame.jitterSceneId != frame.sceneId ||
				!frame.jitterWorldOnly || !std::isfinite(frame.jitterX) || !std::isfinite(frame.jitterY))
				result.Add(KZGS_TEMPORAL_JITTER, KZGS_TEMPORAL_INVALID_JITTER);
		}
		for (const auto& pair : {std::pair{KZGS_TEMPORAL_REACTIVE, &frame.reactive},
				std::pair{KZGS_TEMPORAL_COMPOSITION, &frame.composition}, std::pair{KZGS_TEMPORAL_UI, &frame.ui}})
		{
			if (frame.validInputs & pair.first)
			{
				const KzgsTemporalImage& reference = experimental ? frame.mergedColor : frame.worldColor;
				const uint64_t imageInvalid = ValidateImage(*pair.second, frame, &reference);
				if (imageInvalid || pair.second->backend != KzgsTemporalBackend::Vulkan ||
					pair.second->nativeFormat != VK_FORMAT_R8_UNORM)
					result.Add(pair.first, imageInvalid | KZGS_TEMPORAL_INVALID_MASK);
			}
		}
		if (frame.validInputs & KZGS_TEMPORAL_CAMERA)
		{
			const KzgsTemporalCamera& camera = frame.camera;
			const bool invalid = experimental ? InvalidExperimentalCamera(camera, frame) :
				InvalidCameraScalars(camera, frame) || !ValidMatrix(camera.worldToView) ||
					!ValidMatrix(camera.previousWorldToView) || !ValidMatrix(camera.viewToClip) ||
					!ValidMatrix(camera.previousViewToClip);
			if (invalid)
				result.Add(KZGS_TEMPORAL_CAMERA, KZGS_TEMPORAL_INVALID_CAMERA);
		}
		if (frame.validInputs & KZGS_TEMPORAL_FRAME_TIME)
		{
			if (frame.timeFrameId != frame.frameId || frame.timeSceneId != frame.sceneId ||
				frame.timePreviousFrameId != frame.motionPreviousFrameId ||
				!std::isfinite(frame.frameTimeMs) || frame.frameTimeMs <= 0.0f)
				result.Add(KZGS_TEMPORAL_FRAME_TIME, KZGS_TEMPORAL_INVALID_FRAME_TIME);
		}
		if (frame.validInputs & KZGS_TEMPORAL_COLOR)
		{
			if (frame.mergedColor.backend != KzgsTemporalBackend::Vulkan ||
				(frame.mergedColor.nativeFormat != VK_FORMAT_R8G8B8A8_UNORM &&
					frame.mergedColor.nativeFormat != VK_FORMAT_R16G16B16A16_SFLOAT))
				result.Add(KZGS_TEMPORAL_COLOR, KZGS_TEMPORAL_INVALID_FORMAT);
			const KzgsTemporalImage& encoded = experimental ? frame.mergedColor : frame.worldColor;
			const bool linearSdr = frame.colorEncoding == KzgsTemporalColorEncoding::LinearSDR &&
				encoded.nativeFormat == VK_FORMAT_R8G8B8A8_UNORM;
			const bool linearHdr = frame.colorEncoding == KzgsTemporalColorEncoding::LinearHDR &&
				encoded.nativeFormat == VK_FORMAT_R16G16B16A16_SFLOAT;
			if (encoded.backend != KzgsTemporalBackend::Vulkan || (!linearSdr && !linearHdr))
				result.Add(experimental ? KZGS_TEMPORAL_COLOR :
						frame.validInputs & (KZGS_TEMPORAL_COLOR | KZGS_TEMPORAL_WORLD_COLOR),
					KZGS_TEMPORAL_INVALID_COLOR_ENCODING);
		}
		return result;
	}
}

namespace kzgs::temporal
{
	Decision ValidateStrict(const KzgsTemporalFrame& frame)
	{
		Decision result;
		result.missingInputs = KZGS_TEMPORAL_REQUIRED & ~frame.validInputs;
		const Validation validation = Validate(frame, ValidationMode::Strict);
		result.invalidInputs = validation.inputs;
		result.invalidReasons = validation.reasons;
		result.ready = !result.missingInputs && !result.invalidReasons;
		result.resetReasons = frame.resetReasons |
			(result.ready ? 0 : KZGS_TEMPORAL_RESET_MISSING_INPUT);
		return result;
	}

	Decision ValidateFSR3Experimental(const KzgsTemporalFrame& frame)
	{
		Decision result;
		result.missingInputs = KZGS_TEMPORAL_FSR3_EXPERIMENTAL_REQUIRED & ~frame.validInputs;
		const Validation validation = Validate(frame, ValidationMode::FSR3Experimental);
		result.invalidInputs = validation.inputs;
		result.invalidReasons = validation.reasons;
		result.ready = !result.missingInputs && !result.invalidReasons;
		result.resetReasons = frame.resetReasons |
			(result.ready ? 0 : KZGS_TEMPORAL_RESET_MISSING_INPUT);
		return result;
	}

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

		Decision result = frame.inputPolicy == KzgsTemporalInputPolicy::FSR3Experimental ?
			ValidateFSR3Experimental(frame) : ValidateStrict(frame);
		if (m_historyFrame && (frame.motionPreviousFrameId != m_historyFrame ||
			frame.sceneId != m_historyScene || frame.presentationId != m_historyPresentation + 1))
		{
			result.invalidReasons |= KZGS_TEMPORAL_INVALID_PREVIOUS_FRAME;
			result.invalidInputs |= frame.validInputs & KZGS_TEMPORAL_MOTION;
		}
		if (duplicate || outOfOrder)
			result.invalidReasons |= KZGS_TEMPORAL_INVALID_IDENTITY;
		result.ready = false;
		if (result.missingInputs || result.invalidReasons || duplicate || outOfOrder)
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
