// SPDX-License-Identifier: GPL-3.0+
#include "kzgs_temporal_contract.h"
#include "kzgs_upscaler_provider.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
	int failures = 0;

	void Check(bool condition, const char* message)
	{
		std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
		failures += !condition;
	}

	KzgsTemporalImage Image(uint64_t frame, uint64_t id, uint32_t format)
	{
		KzgsTemporalImage image;
		image.frameId = frame;
		image.sceneId = 7;
		image.resourceId = id;
		image.width = 640;
		image.height = 448;
		image.rect = {4, 2, 636, 446};
		image.backend = KzgsTemporalBackend::Vulkan;
		image.nativeFormat = format;
		return image;
	}

	KzgsTemporalFrame Frame(uint64_t id, uint64_t previous, uint64_t presentation)
	{
		KzgsTemporalFrame frame;
		frame.frameId = id;
		frame.presentationId = presentation;
		frame.sceneId = 7;
		frame.deviceId = 1;
		frame.providerId = 1;
		frame.validInputs = KZGS_TEMPORAL_REQUIRED;
		frame.mergedColor = Image(id, 1, VK_FORMAT_R8G8B8A8_UNORM);
		frame.worldColor = Image(id, 2, VK_FORMAT_R8G8B8A8_UNORM);
		frame.depth = Image(id, 3, VK_FORMAT_D32_SFLOAT);
		frame.motion = Image(id, 4, VK_FORMAT_R16G16_SFLOAT);
		frame.reactive = Image(id, 5, VK_FORMAT_R8_UNORM);
		frame.composition = Image(id, 6, VK_FORMAT_R8_UNORM);
		frame.ui = Image(id, 7, VK_FORMAT_R8_UNORM);
		frame.colorEncoding = KzgsTemporalColorEncoding::LinearSDR;
		frame.depthDirection = KzgsTemporalDepthDirection::Reversed;
		frame.motionUnits = KzgsTemporalMotionUnits::Pixels;
		frame.motionDirection = KzgsTemporalMotionDirection::PreviousMinusCurrent;
		frame.motionPreviousFrameId = previous;
		frame.jitterFrameId = id;
		frame.jitterSceneId = 7;
		frame.jitterX = 0.25f;
		frame.jitterY = -0.25f;
		frame.jitterWorldOnly = true;
		frame.timeFrameId = id;
		frame.timePreviousFrameId = previous;
		frame.timeSceneId = 7;
		frame.frameTimeMs = 16.0f;
		frame.camera.frameId = id;
		frame.camera.previousFrameId = previous;
		frame.camera.sceneId = 7;
		frame.camera.cameraId = 3;
		frame.camera.nearPlane = 0.1f;
		frame.camera.farPlane = 1000.0f;
		frame.camera.verticalFovRadians = 1.0f;
		frame.camera.depthDirection = KzgsTemporalDepthDirection::Reversed;
		for (int i = 0; i < 16; ++i)
			frame.camera.worldToView[i] = frame.camera.previousWorldToView[i] =
				frame.camera.viewToClip[i] = frame.camera.previousViewToClip[i] = i % 5 == 0 ? 1.0f : 0.0f;
		frame.outputWidth = 1280;
		frame.outputHeight = 896;
		frame.aspectId = 1;
		return frame;
	}

	void Commit(kzgs::temporal::Tracker& tracker, const KzgsTemporalFrame& frame)
	{
		const auto decision = tracker.Begin(frame);
		Check(decision.ready, "synthetic fully matched frame is eligible");
		tracker.DispatchResult(true);
		tracker.PresentResult(frame.presentationId, true);
	}

	void Reject(const KzgsTemporalFrame& frame, uint64_t invalid, const char* name)
	{
		kzgs::temporal::Tracker tracker;
		const auto result = tracker.Begin(frame);
		Check(!result.ready && (result.invalidReasons & invalid) != 0 &&
			tracker.HistoryFrame() == 0 && tracker.CommittedFrames() == 0, name);
	}
}

int main()
{
	using namespace kzgs::temporal;
	Tracker empty;
	const Decision absent = empty.Begin({});
	Check(!absent.ready && absent.missingInputs == KZGS_TEMPORAL_REQUIRED &&
		empty.CommittedFrames() == 0 && empty.HistoryFrame() == 0,
		"absent inputs fail closed without history");
	for (uint64_t bit = 1; bit <= KZGS_TEMPORAL_UI; bit <<= 1)
	{
		Tracker tracker;
		KzgsTemporalFrame frame = Frame(10, 9, 1);
		frame.validInputs &= ~bit;
		const Decision decision = tracker.Begin(frame);
		Check(!decision.ready && (decision.missingInputs & bit) &&
			tracker.CommittedFrames() == 0 && tracker.HistoryFrame() == 0,
			"each missing required input fails closed");
	}

	KzgsTemporalFrame bad = Frame(10, 9, 1);
	bad.depth.frameId = 9;
	Reject(bad, KZGS_TEMPORAL_INVALID_IDENTITY, "unmatched depth frame is rejected");
	bad = Frame(10, 9, 1);
	bad.worldColor.sceneId = 8;
	Reject(bad, KZGS_TEMPORAL_INVALID_IDENTITY, "unmatched world scene is rejected");
	bad = Frame(10, 9, 1);
	bad.motion.resourceId = 0;
	Reject(bad, KZGS_TEMPORAL_INVALID_IDENTITY, "missing motion resource is rejected");
	bad = Frame(10, 9, 1);
	bad.depth.rect.right--;
	Reject(bad, KZGS_TEMPORAL_INVALID_GEOMETRY, "mismatched depth crop is rejected");
	bad = Frame(10, 9, 1);
	bad.depth.nativeFormat = VK_FORMAT_D24_UNORM_S8_UINT;
	Reject(bad, KZGS_TEMPORAL_INVALID_DEPTH, "unqualified packed depth is rejected");
	bad = Frame(10, 9, 1);
	bad.depthDirection = KzgsTemporalDepthDirection::Unknown;
	Reject(bad, KZGS_TEMPORAL_INVALID_DEPTH, "unknown depth direction is rejected");
	bad = Frame(10, 9, 1);
	bad.camera.depthDirection = KzgsTemporalDepthDirection::Forward;
	Reject(bad, KZGS_TEMPORAL_INVALID_CAMERA, "inconsistent camera/depth direction is rejected");
	bad = Frame(10, 9, 1);
	bad.motionIncludesJitter = true;
	Reject(bad, KZGS_TEMPORAL_INVALID_MOTION, "jitter-inclusive motion is rejected by ABI 1.0");
	bad = Frame(10, 9, 1);
	bad.motionDirection = static_cast<KzgsTemporalMotionDirection>(99);
	Reject(bad, KZGS_TEMPORAL_INVALID_MOTION, "unknown motion direction is rejected");
	bad = Frame(10, 9, 1);
	bad.motionUnits = KzgsTemporalMotionUnits::Unknown;
	Reject(bad, KZGS_TEMPORAL_INVALID_MOTION, "unknown motion units are rejected");
	bad = Frame(10, 9, 1);
	bad.jitterWorldOnly = false;
	Reject(bad, KZGS_TEMPORAL_INVALID_JITTER, "whole-frame UI jitter is rejected");
	bad = Frame(10, 9, 1);
	bad.reactive.sceneId = 8;
	Reject(bad, KZGS_TEMPORAL_INVALID_MASK, "unmatched reactive mask is rejected");
	bad = Frame(10, 9, 1);
	bad.ui.nativeFormat = VK_FORMAT_R8G8B8A8_UNORM;
	Reject(bad, KZGS_TEMPORAL_INVALID_MASK, "unqualified UI mask format is rejected");
	bad = Frame(10, 9, 1);
	bad.camera.viewToClip[0] = std::numeric_limits<float>::quiet_NaN();
	Reject(bad, KZGS_TEMPORAL_INVALID_CAMERA, "non-finite camera is rejected");
	bad = Frame(10, 9, 1);
	for (float& value : bad.camera.worldToView)
		value = 1.0f;
	Reject(bad, KZGS_TEMPORAL_INVALID_CAMERA, "singular camera matrix is rejected");
	bad = Frame(10, 9, 1);
	bad.frameTimeMs = 0.0f;
	Reject(bad, KZGS_TEMPORAL_INVALID_FRAME_TIME, "zero frame delta is rejected");
	bad = Frame(10, 9, 1);
	bad.colorEncoding = KzgsTemporalColorEncoding::Unknown;
	Reject(bad, KZGS_TEMPORAL_INVALID_COLOR_ENCODING, "unknown color encoding is rejected");
	bad = Frame(10, 9, 1);
	bad.mergedColor.nativeFormat = VK_FORMAT_R8_UNORM;
	Reject(bad, KZGS_TEMPORAL_INVALID_FORMAT, "unqualified merged color format is rejected");

	Tracker tracker;
	const KzgsTemporalFrame first = Frame(10, 9, 1);
	Decision ready = tracker.Begin(first);
	Check(ready.ready && (ready.resetReasons & KZGS_TEMPORAL_RESET_INITIAL) &&
		tracker.HistoryFrame() == 0, "first use resets and does not commit before dispatch");
	tracker.DispatchResult(true);
	Check(tracker.HistoryFrame() == 0 && tracker.CommittedFrames() == 0,
		"recorded dispatch alone does not commit history");
	tracker.PresentResult(1, true);
	Check(tracker.HistoryFrame() == 10 && tracker.CommittedFrames() == 1,
		"successful dispatch and present commit exactly once");
	Tracker duplicatePresentation;
	Commit(duplicatePresentation, first);
	duplicatePresentation.Invalidate(KZGS_TEMPORAL_RESET_GAME);
	ready = duplicatePresentation.Begin(Frame(11, 10, 1));
	Check(!ready.ready && (ready.resetReasons & KZGS_TEMPORAL_RESET_DUPLICATE) &&
		duplicatePresentation.CommittedFrames() == 1,
		"a successful presentation identity cannot be reused after reset");
	bad = Frame(11, 9, 2);
	ready = tracker.Begin(bad);
	Check(!ready.ready && (ready.invalidReasons & KZGS_TEMPORAL_INVALID_PREVIOUS_FRAME) &&
		tracker.CommittedFrames() == 1 && tracker.HistoryFrame() == 0,
		"motion referring to an unpresented frame invalidates history");
	Commit(tracker, Frame(12, 11, 2));
	bad = Frame(12, 11, 3);
	ready = tracker.Begin(bad);
	Check(!ready.ready && (ready.resetReasons & KZGS_TEMPORAL_RESET_DUPLICATE) &&
		tracker.CommittedFrames() == 2, "duplicate source never dispatches or advances history");
	Commit(tracker, Frame(13, 12, 3));
	ready = tracker.Begin(Frame(14, 13, 4));
	tracker.DispatchResult(false);
	Check(ready.ready && tracker.CommittedFrames() == 3 && tracker.HistoryFrame() == 0 &&
		(tracker.PendingResetReasons() & KZGS_TEMPORAL_RESET_DISPATCH_FAILED),
		"failed dispatch discards speculative history");
	ready = tracker.Begin(Frame(15, 14, 4));
	tracker.DispatchResult(true);
	tracker.PresentResult(4, false);
	Check(ready.ready && tracker.CommittedFrames() == 3 && tracker.HistoryFrame() == 0 &&
		(tracker.PendingResetReasons() & KZGS_TEMPORAL_RESET_PRESENT_FAILED),
		"failed present discards speculative history");
	ready = tracker.Begin(Frame(16, 15, 4));
	tracker.DispatchResult(true);
	tracker.PresentResult(5, true);
	Check(ready.ready && tracker.CommittedFrames() == 3 && tracker.HistoryFrame() == 0,
		"wrong present identity cannot commit");
	ready = tracker.Begin(Frame(15, 14, 5));
	Check(!ready.ready && (ready.resetReasons & KZGS_TEMPORAL_RESET_FRAME_ORDER),
		"out-of-order source is rejected");

	Tracker lifecycle;
	Commit(lifecycle, Frame(20, 19, 1));
	KzgsTemporalFrame changed = Frame(21, 20, 2);
	changed.outputWidth++;
	changed.aspectId++;
	changed.deviceId++;
	changed.providerId++;
	changed.camera.cameraId++;
	changed.resetReasons = KZGS_TEMPORAL_RESET_CAMERA_CUT | KZGS_TEMPORAL_RESET_TELEPORT |
		KZGS_TEMPORAL_RESET_LOAD | KZGS_TEMPORAL_RESET_SAVE | KZGS_TEMPORAL_RESET_SCENE |
		KZGS_TEMPORAL_RESET_MENU | KZGS_TEMPORAL_RESET_MOVIE | KZGS_TEMPORAL_RESET_SKIPPED |
		KZGS_TEMPORAL_RESET_GAME | KZGS_TEMPORAL_RESET_WORLD_TRANSITION;
	ready = lifecycle.Begin(changed);
	const uint64_t expected = changed.resetReasons | KZGS_TEMPORAL_RESET_SIZE |
		KZGS_TEMPORAL_RESET_ASPECT | KZGS_TEMPORAL_RESET_DEVICE |
		KZGS_TEMPORAL_RESET_PROVIDER | KZGS_TEMPORAL_RESET_CAMERA_CHANGE;
	Check(ready.ready && (ready.resetReasons & expected) == expected &&
		lifecycle.CommittedFrames() == 1 && lifecycle.HistoryFrame() == 0,
		"geometry, device, provider and explicit discontinuities reset before dispatch");
	lifecycle.DispatchResult(true);
	lifecycle.PresentResult(2, true);
	changed = Frame(22, 21, 3);
	changed.outputWidth++;
	changed.aspectId++;
	changed.deviceId++;
	changed.providerId++;
	changed.camera.cameraId++;
	for (KzgsTemporalImage* image : {&changed.mergedColor, &changed.worldColor, &changed.depth,
			&changed.motion, &changed.reactive, &changed.composition, &changed.ui})
		image->rect.left++;
	ready = lifecycle.Begin(changed);
	Check(ready.ready && (ready.resetReasons & KZGS_TEMPORAL_RESET_CROP),
		"crop changes reset history");

	std::printf("Temporal contract: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
