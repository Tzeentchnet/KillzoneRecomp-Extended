// GS-thread temporal qualification contract. No vendor ABI is changed by this header.
// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>

// Bits 0..7 have the same meaning as KzgsProviderInputs in ABI 1.0.
constexpr uint64_t KZGS_TEMPORAL_COLOR = UINT64_C(1) << 0;
constexpr uint64_t KZGS_TEMPORAL_DEPTH = UINT64_C(1) << 1;
constexpr uint64_t KZGS_TEMPORAL_MOTION = UINT64_C(1) << 2;
constexpr uint64_t KZGS_TEMPORAL_JITTER = UINT64_C(1) << 3;
constexpr uint64_t KZGS_TEMPORAL_REACTIVE = UINT64_C(1) << 4;
constexpr uint64_t KZGS_TEMPORAL_COMPOSITION = UINT64_C(1) << 5;
constexpr uint64_t KZGS_TEMPORAL_CAMERA = UINT64_C(1) << 6;
constexpr uint64_t KZGS_TEMPORAL_FRAME_TIME = UINT64_C(1) << 7;
constexpr uint64_t KZGS_TEMPORAL_WORLD_COLOR = UINT64_C(1) << 8;
constexpr uint64_t KZGS_TEMPORAL_UI = UINT64_C(1) << 9;
constexpr uint64_t KZGS_TEMPORAL_REQUIRED = (UINT64_C(1) << 10) - 1;
constexpr uint64_t KZGS_TEMPORAL_FSR3_EXPERIMENTAL_REQUIRED =
	KZGS_TEMPORAL_COLOR | KZGS_TEMPORAL_DEPTH | KZGS_TEMPORAL_MOTION |
	KZGS_TEMPORAL_JITTER | KZGS_TEMPORAL_CAMERA | KZGS_TEMPORAL_FRAME_TIME;
constexpr uint64_t KZGS_TEMPORAL_FSR3_EXPERIMENTAL_OPTIONAL =
	KZGS_TEMPORAL_REACTIVE | KZGS_TEMPORAL_COMPOSITION;

constexpr uint64_t KZGS_TEMPORAL_RESET_INITIAL = UINT64_C(1) << 0;
constexpr uint64_t KZGS_TEMPORAL_RESET_SIZE = UINT64_C(1) << 1;
constexpr uint64_t KZGS_TEMPORAL_RESET_CROP = UINT64_C(1) << 2;
constexpr uint64_t KZGS_TEMPORAL_RESET_ASPECT = UINT64_C(1) << 3;
constexpr uint64_t KZGS_TEMPORAL_RESET_PROVIDER = UINT64_C(1) << 4;
constexpr uint64_t KZGS_TEMPORAL_RESET_DEVICE = UINT64_C(1) << 5;
constexpr uint64_t KZGS_TEMPORAL_RESET_GAME = UINT64_C(1) << 6;
constexpr uint64_t KZGS_TEMPORAL_RESET_CAMERA_CHANGE = UINT64_C(1) << 7;
constexpr uint64_t KZGS_TEMPORAL_RESET_CAMERA_CUT = UINT64_C(1) << 8;
constexpr uint64_t KZGS_TEMPORAL_RESET_TELEPORT = UINT64_C(1) << 9;
constexpr uint64_t KZGS_TEMPORAL_RESET_LOAD = UINT64_C(1) << 10;
constexpr uint64_t KZGS_TEMPORAL_RESET_SAVE = UINT64_C(1) << 11;
constexpr uint64_t KZGS_TEMPORAL_RESET_SCENE = UINT64_C(1) << 12;
constexpr uint64_t KZGS_TEMPORAL_RESET_MENU = UINT64_C(1) << 13;
constexpr uint64_t KZGS_TEMPORAL_RESET_MOVIE = UINT64_C(1) << 14;
constexpr uint64_t KZGS_TEMPORAL_RESET_DISPATCH_FAILED = UINT64_C(1) << 15;
constexpr uint64_t KZGS_TEMPORAL_RESET_PRESENT_FAILED = UINT64_C(1) << 16;
constexpr uint64_t KZGS_TEMPORAL_RESET_MISSING_INPUT = UINT64_C(1) << 17;
constexpr uint64_t KZGS_TEMPORAL_RESET_DUPLICATE = UINT64_C(1) << 18;
constexpr uint64_t KZGS_TEMPORAL_RESET_SKIPPED = UINT64_C(1) << 19;
constexpr uint64_t KZGS_TEMPORAL_RESET_FRAME_ORDER = UINT64_C(1) << 20;
constexpr uint64_t KZGS_TEMPORAL_RESET_WORLD_TRANSITION = UINT64_C(1) << 21;

constexpr uint64_t KZGS_TEMPORAL_INVALID_IDENTITY = UINT64_C(1) << 0;
constexpr uint64_t KZGS_TEMPORAL_INVALID_GEOMETRY = UINT64_C(1) << 1;
constexpr uint64_t KZGS_TEMPORAL_INVALID_FORMAT = UINT64_C(1) << 2;
constexpr uint64_t KZGS_TEMPORAL_INVALID_DEPTH = UINT64_C(1) << 3;
constexpr uint64_t KZGS_TEMPORAL_INVALID_MOTION = UINT64_C(1) << 4;
constexpr uint64_t KZGS_TEMPORAL_INVALID_JITTER = UINT64_C(1) << 5;
constexpr uint64_t KZGS_TEMPORAL_INVALID_CAMERA = UINT64_C(1) << 6;
constexpr uint64_t KZGS_TEMPORAL_INVALID_FRAME_TIME = UINT64_C(1) << 7;
constexpr uint64_t KZGS_TEMPORAL_INVALID_MASK = UINT64_C(1) << 8;
constexpr uint64_t KZGS_TEMPORAL_INVALID_COLOR_ENCODING = UINT64_C(1) << 9;
constexpr uint64_t KZGS_TEMPORAL_INVALID_PREVIOUS_FRAME = UINT64_C(1) << 10;
constexpr uint64_t KZGS_TEMPORAL_INVALID_PROVENANCE = UINT64_C(1) << 11;
constexpr uint64_t KZGS_TEMPORAL_INVALID_POLICY = UINT64_C(1) << 12;

enum class KzgsTemporalBackend : uint32_t { Unknown, Vulkan, D3D11, D3D12, Software };
enum class KzgsTemporalColorEncoding : uint32_t { Unknown, SRGB, LinearSDR, LinearHDR, PQ };
enum class KzgsTemporalDepthDirection : uint32_t { Unknown, Forward, Reversed };
enum class KzgsTemporalMotionUnits : uint32_t { Unknown, Pixels };
enum class KzgsTemporalMotionDirection : uint32_t { Unknown, PreviousMinusCurrent };
enum class KzgsTemporalInputPolicy : uint32_t { Strict, FSR3Experimental };

struct KzgsTemporalRect
{
	int32_t left = 0, top = 0, right = 0, bottom = 0;
};

// A resource ID is process-local and never an address. Frame/scene IDs identify
// the contents, because an underlying texture can be recycled or overwritten.
struct KzgsTemporalImage
{
	uint64_t frameId = 0;
	uint64_t sceneId = 0;
	uint64_t resourceId = 0;
	uint32_t width = 0, height = 0;
	KzgsTemporalRect rect{};
	KzgsTemporalBackend backend = KzgsTemporalBackend::Unknown;
	uint32_t nativeFormat = 0; // VkFormat or DXGI_FORMAT for the named backend
	uint32_t rendererFormat = 0; // PCSX2 GSTexture::Format, when observed
	uint32_t gsFormat = 0; // PS2 PSM, when observed; zero is also a valid PSM
};

struct KzgsTemporalCamera
{
	uint64_t frameId = 0, previousFrameId = 0, sceneId = 0, cameraId = 0;
	float nearPlane = 0.0f, farPlane = 0.0f, verticalFovRadians = 0.0f;
	float worldToView[16]{}, previousWorldToView[16]{};
	float viewToClip[16]{}, previousViewToClip[16]{};
	KzgsTemporalDepthDirection depthDirection = KzgsTemporalDepthDirection::Unknown;
	bool infiniteFar = false;
	uint32_t validFields = 0;
};

constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_NEAR_PLANE = 1u << 0;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_FAR_PLANE = 1u << 1;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_VERTICAL_FOV = 1u << 2;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_VIEW_TO_CLIP = 1u << 3;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_PREVIOUS_VIEW_TO_CLIP = 1u << 4;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_WORLD_TO_VIEW = 1u << 5;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_PREVIOUS_WORLD_TO_VIEW = 1u << 6;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_SCALARS =
	KZGS_TEMPORAL_CAMERA_VALID_NEAR_PLANE | KZGS_TEMPORAL_CAMERA_VALID_FAR_PLANE |
	KZGS_TEMPORAL_CAMERA_VALID_VERTICAL_FOV;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_MATRICES =
	KZGS_TEMPORAL_CAMERA_VALID_VIEW_TO_CLIP | KZGS_TEMPORAL_CAMERA_VALID_PREVIOUS_VIEW_TO_CLIP |
	KZGS_TEMPORAL_CAMERA_VALID_WORLD_TO_VIEW | KZGS_TEMPORAL_CAMERA_VALID_PREVIOUS_WORLD_TO_VIEW;
constexpr uint32_t KZGS_TEMPORAL_CAMERA_VALID_ALL =
	KZGS_TEMPORAL_CAMERA_VALID_SCALARS | KZGS_TEMPORAL_CAMERA_VALID_MATRICES;

struct KzgsTemporalFrame
{
	uint64_t frameId = 0, presentationId = 0, sceneId = 0;
	uint64_t deviceId = 0, providerId = 0;
	uint64_t validInputs = 0;
	uint64_t resetReasons = 0;
	KzgsTemporalImage mergedColor{}, worldColor{}, depth{}, motion{}, reactive{}, composition{}, ui{};
	KzgsTemporalColorEncoding colorEncoding = KzgsTemporalColorEncoding::Unknown;
	KzgsTemporalDepthDirection depthDirection = KzgsTemporalDepthDirection::Unknown;
	KzgsTemporalMotionUnits motionUnits = KzgsTemporalMotionUnits::Unknown;
	KzgsTemporalMotionDirection motionDirection = KzgsTemporalMotionDirection::Unknown;
	uint64_t motionPreviousFrameId = 0;
	bool motionIncludesJitter = false;
	uint64_t jitterFrameId = 0, jitterSceneId = 0;
	float jitterX = 0.0f, jitterY = 0.0f; // source pixels, positive right/down
	bool jitterWorldOnly = false;
	uint64_t timeFrameId = 0, timePreviousFrameId = 0, timeSceneId = 0;
	float frameTimeMs = 0.0f;
	KzgsTemporalCamera camera{};
	uint32_t outputWidth = 0, outputHeight = 0;
	uint64_t aspectId = 0;
	bool duplicate = false;
	uint64_t qualifiedInputs = 0;
	uint64_t approximateInputs = 0;
	KzgsTemporalInputPolicy inputPolicy = KzgsTemporalInputPolicy::Strict;
};

constexpr uint32_t KZGS_TEMPORAL_GUEST_WORLD = 1u << 0;
constexpr uint32_t KZGS_TEMPORAL_GUEST_2D = 1u << 1;
constexpr uint32_t KZGS_TEMPORAL_GUEST_VIEWMODEL = 1u << 2;
constexpr uint32_t KZGS_TEMPORAL_GUEST_CAMERA_CANDIDATE = 1u << 3;
constexpr uint32_t KZGS_TEMPORAL_GUEST_AMBIGUOUS_CAMERA = 1u << 4;
constexpr uint32_t KZGS_TEMPORAL_GUEST_CAMERA_MATRICES_CANDIDATE = 1u << 5;
constexpr uint32_t KZGS_TEMPORAL_GUEST_FRAME_TIME_CANDIDATE = 1u << 6;
constexpr uint32_t KZGS_TEMPORAL_GUEST_AMBIGUOUS_CAMERA_MATRICES = 1u << 7;
constexpr uint32_t KZGS_TEMPORAL_GUEST_JITTER_APPLIED = 1u << 8;

// Guest build/kick observations travel with a VSync command, after its queued
// GIF work. They are candidates, not proof that the displayed frame matches.
struct alignas(16) KzgsTemporalGuestObservation
{
	uint64_t guestVsync = 0, buildId = 0, cameraId = 0, resetReasons = 0;
	float nearPlane = 0.0f, farPlane = 0.0f, verticalFovDegrees = 0.0f, aspect = 0.0f;
	uint32_t flags = 0;
	uint32_t reserved[3]{};
	float gameFrameTimeMs = 0.0f;
	uint32_t provenanceReserved[3]{};
	// Raw game render-camera matrices at object offsets +0x310 and +0x400.
	// Their coordinate/layout semantics are not yet qualified for providers.
	float renderCameraMatrix310[16]{};
	float renderCameraMatrix400[16]{};
	float jitterX = 0.0f, jitterY = 0.0f;
	uint32_t jitterPhase = 0, jitterPhaseCount = 0;
	uint32_t jitterSourceWidth = 0, jitterSourceHeight = 0;
	uint32_t jitterDisplayWidth = 0, jitterDisplayHeight = 0;
};

// Safe to expose/log: counts, dimensions, format codes, process-local IDs,
// timing scalars, and validity. It never contains game pixels, guest addresses,
// matrices, or strings.
struct KzgsTemporalDiagnostics
{
	uint64_t frameId = 0, presentationId = 0, sceneId = 0, deviceId = 0, providerId = 0;
	uint64_t guestVsync = 0, guestBuildId = 0;
	uint64_t mergedResourceId = 0, displayTargetId = 0, displayResourceId = 0;
	uint64_t depthTargetId = 0, depthResourceId = 0, cameraId = 0;
	uint64_t worldResourceId = 0, motionResourceId = 0;
	uint64_t reactiveResourceId = 0, compositionResourceId = 0, uiResourceId = 0;
	uint64_t validInputs = 0, missingInputs = KZGS_TEMPORAL_REQUIRED, invalidInputs = 0;
	uint64_t invalidReasons = 0, resetReasons = 0;
	uint64_t committedHistoryFrames = 0, historyFrameId = 0;
	uint32_t width = 0, height = 0, nativeFormat = 0, rendererFormat = 0;
	uint32_t displayWidth = 0, displayHeight = 0, depthWidth = 0, depthHeight = 0;
	uint32_t displayGsFormat = 0, depthGsFormat = 0, depthNativeFormat = 0;
	uint32_t worldNativeFormat = 0, motionNativeFormat = 0;
	uint32_t reactiveNativeFormat = 0, compositionNativeFormat = 0, uiNativeFormat = 0;
	uint32_t drawCount = 0, displayedDrawCount = 0, depthDrawCount = 0, depthWriteCount = 0;
	uint32_t depthTestMask = 0, displayCount = 0, depthCandidateCount = 0;
	uint32_t guestFlags = 0;
	float deliveryDeltaMs = 0.0f; // GS delivery interval, not a matched game-frame delta
	float gameFrameTimeCandidateMs = 0.0f;
	KzgsTemporalRect sourceRect{};
	KzgsTemporalBackend backend = KzgsTemporalBackend::Unknown;
	KzgsTemporalDepthDirection depthDirection = KzgsTemporalDepthDirection::Unknown;
	KzgsTemporalMotionUnits motionUnits = KzgsTemporalMotionUnits::Unknown;
	KzgsTemporalMotionDirection motionDirection = KzgsTemporalMotionDirection::Unknown;
	bool motionJitterInclusionKnown = false;
	bool motionIncludesJitter = false;
	bool jitterAvailable = false, matchedFrameTimeAvailable = false, cameraParametersAvailable = false;
	bool cameraParametersCandidateAvailable = false;
	bool cameraMatricesCandidateAvailable = false;
	bool gameFrameTimeCandidateAvailable = false;
	bool candidateAmbiguous = false;
	bool temporalValid = false;
	bool presentSucceeded = false;
	bool observationOverflow = false;
	uint64_t qualifiedInputs = 0, approximateInputs = 0;
	KzgsTemporalInputPolicy inputPolicy = KzgsTemporalInputPolicy::Strict;
};
