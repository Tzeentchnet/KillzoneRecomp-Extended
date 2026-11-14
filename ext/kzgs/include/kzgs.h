// kzgs - PCSX2's hardware GS renderer as a library.
// SPDX-License-Identifier: GPL-3.0+   (links PCSX2 code; see README.md)
//
// Threading model
// ---------------
// kzgs owns a dedicated GS thread. The D3D11/D3D12/Vulkan device is created, used and destroyed on that thread only.
// Every kzgs* function below is a *producer* call: it copies its arguments into a ring buffer and returns
// (asynchronous), except kzgsOpen / kzgsClose / kzgsReadback / kzgsSync, which wait for the GS thread.
// Producer calls are serialized by an internal mutex, but GIF data from different threads would interleave at packet
// granularity, so in practice drive kzgs from ONE thread (the game/EE thread that owns the GIF arbiter).
//
// Privileged registers
// --------------------
// The caller owns the 0x2000-byte privileged register block (PCSX2 GSPrivRegSet layout: PMODE at +0x0000, SMODE1
// +0x0010, SMODE2 +0x0020, ..., DISPFB1 +0x0070, DISPLAY1 +0x0080, DISPFB2 +0x0090, DISPLAY2 +0x00A0, EXTBUF +0x00B0,
// EXTDATA +0x00C0, EXTWRITE +0x00D0, BGCOLOR +0x00E0, CSR +0x1000, IMR +0x1010, BUSDIR +0x1040, SIGLBLID +0x1080).
// Like PCSX2's MTGS, the GS thread works on its own copy: kzgsVsync() snapshots 0x0000-0x00EF, CSR, IMR and SIGLBLID
// from the caller's block into the ring, so the caller may keep writing its block at any time.
// SIGNAL / FINISH / LABEL, CSR interrupt bits and IMR masking are NOT handled by kzgs (PCSX2's GS ignores those
// A+D writes too; the EE-side GIF unit handles them). The caller's GIF arbiter must process them before or while
// forwarding packets, and raise INTC_GS itself.
#pragma once

#include "kzgs_upscaler.h"
#include "kzgs_temporal.h"
#include "kz_host/core.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class KzgsRenderer : int
{
	D3D11 = 0,
	D3D12 = 1,
	Vulkan = 2,
	Software = 3, // PCSX2's software rasterizer (reference/debugging; presents through D3D11)
};

enum class KzgsAspect : int
{
	Ratio4_3 = 0,
	Ratio16_9 = 1,
	Stretch = 2,
};

// Maps to PCSX2 BiFiltering.
enum class KzgsTextureFilter : int
{
	Nearest = 0,         // never bilinear
	Forced = 1,          // always bilinear
	PS2 = 2,             // bilinear only where the game asked for it (PCSX2 default)
	ForcedExceptSprites = 3,
};

enum class KzgsMipmapMode : int
{
	PS2 = 0,
	BaseOnly = 1,
};

struct KzgsConfig
{
	KzgsRenderer renderer = KzgsRenderer::D3D11;
	int upscale = 1;                                   // internal resolution multiplier, 1..8
	KzgsTextureFilter textureFiltering = KzgsTextureFilter::PS2;
	int anisotropy = 0;                                // 0 (off), 2, 4, 8, 16
	KzgsMipmapMode mipmapMode = KzgsMipmapMode::PS2;   // BaseOnly disables mip selection and samples level zero
	bool fxaa = false;                                 // post-process FXAA
	bool smaa = false;                                 // post-process SMAA 1x (Direct3D 11 only; others ignore it)
	// PCSX2 texture replacement: files in <exe dir>/textures/SCUS-97402/replacements (named by PCSX2's texture hash) replace
	// the game's textures; dumpTextures writes every texture the game uses to .../dumps (named the same way).
	bool loadTextureReplacements = false;
	bool dumpTextures = false;
	bool edgeAA = false;                               // GS AA1 (PS2 edge antialiasing emulation, "HWAA1")
	KzgsAspect aspect = KzgsAspect::Ratio4_3;
	bool vsync = true;
	bool bilinearPresent = true;                       // smooth scaling of the final image to the window
	bool sharpPresent = false;                         // with bilinearPresent: PCSX2 "Bilinear (Sharp)" (crisp pixel edges)
	int casSharpness = 0;                              // Contrast Adaptive Sharpening, 0 = off, 1..100
	// Presentation crop in native (1x) output pixels: left, top, right, bottom. Applies to the displayed image and to
	// kzgsReadback only, not to GS rendering. Hides the edge pixels PCSX2 shows that a CRT's overscan would hide.
	int crop[4] = {0, 0, 0, 0};
	// PCSX2 builds sprites and lines in the vertex shader (DisableVertexShaderExpand=false). On NVIDIA under D3D11 this
	// intermittently corrupts frames (measured in docs/findings.md); D3D12 and Vulkan do not show it.
	bool vertexShaderExpand = true;
	bool pcrtcAntiBlur = true;                         // PCSX2 "Anti-Blur" (PCRTCAntiBlur): merges near-identical display circuits
	int interlaceMode = 0;                             // PCSX2 GSInterlaceMode: 0 Automatic, 1 Off, 2/3 Weave TFF/BFF, 4/5 Bob, 6/7 Blend, 8/9 Adaptive

	// Killzone GameIndex gsHWFixes (SCUS-97402 / SCES-52004 / SCES-52893).
	int halfPixelOffset = 4;                           // GSHalfPixelOffset: 0 Off .. 4 Native, 5 Native+TexOffset
	int nativeScaling = 2;                             // GSNativeScaling: 0 Off, 1 Normal, 2 Aggressive, ...

	std::string adapter;                               // DXGI/Vulkan adapter name; empty = default GPU
	bool useWarp = false;                              // D3D11/D3D12: force the WARP software adapter

	// Folders. Empty resourcesDir = "<exe dir>/resources" (needs resources/shaders, see kzgs_stage_resources()).
	// Empty cacheDir = "<exe dir>/cache" (compiled shader cache). disableShaderCache skips the cache entirely.
	std::string resourcesDir;
	std::string cacheDir;
	bool disableShaderCache = false;

	int maxQueuedFrames = 1;                           // kzgsVsync blocks when this many frames are still queued (1: at most one frame waits behind the one the GS thread is on)

	// true: every kzgsGifTransfer() call is a self-contained GIF packet sequence that starts with a GIFtag, as
	// PS2Recomp's GIF arbiter produces (it re-wraps IMAGE data continued across VIF DIRECT/DMA chunks in a synthesized
	// IMAGE tag). A tag still open at the end of a call is dropped. false: a raw, hardware-exact GIF byte stream per
	// path, where a packet may continue in the next call (PCSX2 MTGS semantics).
	bool selfContainedGifPackets = true;
	bool debugDevice = false;                          // D3D/Vulkan validation layer
};

// The original KzgsConfig is kept unchanged for existing aggregate initializers and compiled callers.
// Extended configurations are passed to kzgsOpenEx/kzgsUpdateConfigEx with an explicit size and version.
// The fixed-width representation keeps this appended V1 field outside the legacy V1 tail padding.
enum class KzgsFsr3InputMode : uint64_t
{
	Strict = 0,
	Experimental = 1,
};

struct KzgsConfigV1 : KzgsConfig
{
	KzgsUpscaler upscaler = KzgsUpscaler::Off;         // requested presentation provider; independent of upscale
	int upscalerSharpening = 0;                        // provider-owned sharpening, 0..100; not CAS
	bool temporalDiagnostics = false;                  // metadata-only observation; never enables temporal providers
	KzgsFsr3InputMode fsr3InputMode = KzgsFsr3InputMode::Strict; // strict requires qualified temporal inputs
};
inline constexpr uint32_t KZGS_CONFIG_VERSION_1 = 1;

struct KzgsResolutionInfo
{
	int renderWidth = 0;                               // GS merged output before presentation crop/aspect/scale
	int renderHeight = 0;
	int outputWidth = 0;                               // current physical swap-chain output
	int outputHeight = 0;
};

struct KzgsFsr3JitterState
{
	uint64_t generation = 0;
	uint32_t sourceWidth = 0, sourceHeight = 0;
	uint32_t displayWidth = 0, displayHeight = 0;
	bool enabled = false;
};

// Creates the GS thread and render device on `window`, which must outlive kzgsClose. An empty handle is surfaceless.
// The current renderer backends accept Win32 windows and reject other native-window types explicitly.
// `privRegs` is the caller's 0x2000-byte privileged register block (see above); it is read at open and at every
// kzgsVsync. Returns false with a message in *err on failure. Only one kzgs instance can be open at a time.
bool kzgsOpen(const kz::host::NativeWindow& window, const KzgsConfig& cfg, uint8_t* privRegs, std::string* err = nullptr);

// Whether the compiled companion exposes the reviewed pre-sharp source API. Integration tests must also verify
// the hook ordering; older locked companions can still provide the legacy post-prescale hook.
bool kzgsHasPreSharpPresentationHook();

// config must point to a complete current KzgsConfigV1 or the exact legacy V1 prefix when version is
// KZGS_CONFIG_VERSION_1. Other short/unknown versions are rejected before their payload is read. A legacy V1 prefix
// defaults FSR 3 inputs to Strict. Legacy kzgsOpen uses Off/0, disables temporal diagnostics, and uses Strict.
bool kzgsOpenEx(const kz::host::NativeWindow& window, const void* config, size_t configSize, uint32_t version,
	uint8_t* privRegs, std::string* err = nullptr);
inline bool kzgsOpenV1(const kz::host::NativeWindow& window, const KzgsConfigV1& cfg,
	uint8_t* privRegs, std::string* err = nullptr)
{
	return kzgsOpenEx(window, &cfg, sizeof(cfg), KZGS_CONFIG_VERSION_1, privRegs, err);
}

// Destroys the device and joins the GS thread. Safe to call when not open.
void kzgsClose();

bool kzgsIsOpen();

// Last GS-thread-applied request and actual provider. False while closed; after kzgsUpdateConfig, kzgsSync waits for
// the new status. Vulkan FSR1 becomes effective only after a successful upscaled present; unavailable providers,
// unsupported backends, and frames which do not need upscaling use the existing presentation path.
bool kzgsGetUpscalerStatus(KzgsUpscalerStatus& out);

// Most recent GS-thread metadata observation. False when disabled or before a VSync completes.
bool kzgsGetTemporalDiagnostics(KzgsTemporalDiagnostics& out);

// Exact geometry for the next experimental FSR 3 camera-jitter sequence. False
// until a capable Vulkan FSR 3 provider has observed a source/output pair.
bool kzgsGetFsr3JitterState(KzgsFsr3JitterState& out);

// Queues a GIF packet (sequence of GIFtags + data) for PATH 1, 2 or 3. `qwords` is the size in 16-byte units.
// The data is copied; the caller may reuse its buffer immediately. PATH1 transfers stop at the first EOP, as on
// hardware XGKICK.
void kzgsGifTransfer(int path, const uint8_t* data, uint32_t qwords);

// End of frame: snapshots the privileged registers and makes the GS read out the display circuits and present to the
// window. `field` is the interlace field being displayed (0 = even, 1 = odd; PCSX2 derives it from !(CSR & 0x2000)).
// `regsWritten` = whether the game wrote any privileged register since the last vsync (PCSX2 uses it to decide whether
// an idle frame still needs presenting; pass true if unsure).
//
// `wide`: the frame was drawn for an ultrawide display (3D view widened by the caller). With a wide ratio set by kzgsSetWideRatio such
// frames are stretched to that aspect ratio, other frames use KzgsConfig::aspect (pillarboxed in a wider window).
void kzgsVsync(int field, bool regsWritten = true, bool wide = false);
void kzgsVsyncTemporal(int field, bool regsWritten, bool wide, const KzgsTemporalGuestObservation* temporal);

// Non-blocking kzgsVsync() for callers that must not stall (the VIF1/GIF worker, which the EE waits for): when the GS thread
// still has more than maxQueuedFrames frames to finish (slow Present: an external frame limiter, a display slower than the
// guest vblank rate) nothing is queued and false is returned: the caller keeps its pending state and tries again at the
// next vblank. At most one thread may call kzgsVsync/kzgsVsyncTry.
bool kzgsVsyncTry(int field, bool regsWritten = true, bool wide = false);
bool kzgsVsyncTryTemporal(int field, bool regsWritten, bool wide, const KzgsTemporalGuestObservation* temporal);

// Aspect ratio (width / height, e.g. 64.0f / 27.0f) that wide frames are presented at; 0 = off (every frame uses KzgsConfig::aspect).
void kzgsSetWideRatio(float ratio);

// Write to GS CSR (0x12001000). Only the RESET bit (bit 9) has an effect on the GS itself.
void kzgsWriteCSR(uint32_t csr);

// GIF soft reset of the path state machines (mask bit0 = PATH1, bit1 = PATH2, bit2 = PATH3).
void kzgsSoftReset(uint32_t mask = 7);

// Full GS reset (clears VRAM/texture cache).
void kzgsReset();

// Window client area changed.
void kzgsResize(int width, int height);

// Returns the most recent completed frame's exact render and output dimensions without blocking the GS thread.
// False until the first frame has completed or after the renderer closes.
bool kzgsGetResolutionInfo(KzgsResolutionInfo& out);

// Applies legacy settings, resetting extended settings to Off/0, disabling temporal diagnostics, and using Strict.
// Renderer switches recreate the device on the same window.
void kzgsUpdateConfig(const KzgsConfig& cfg);
bool kzgsUpdateConfigEx(const void* config, size_t configSize, uint32_t version, std::string* err = nullptr);
inline bool kzgsUpdateConfigV1(const KzgsConfigV1& cfg, std::string* err = nullptr)
{
	return kzgsUpdateConfigEx(&cfg, sizeof(cfg), KZGS_CONFIG_VERSION_1, err);
}

// Blocks until the GS thread has processed everything queued so far.
void kzgsSync();

// Reads back the last output frame (display circuits merged, before scaling to the window), at its exact internal
// resolution. Dimensions derive from the active GS display (for example, a 640x448 display becomes 1280x896 at 2x).
// RGBA8, top-down, w*h*4 bytes. Blocks (drains the queue first).
// With presentWidth/presentHeight > 0 the image is instead scaled and aspect-corrected the way it is presented in a
// window of that size (letterboxed with black to exactly presentWidth x presentHeight).
bool kzgsReadback(std::vector<uint8_t>& rgba, int& width, int& height, int presentWidth = 0, int presentHeight = 0);

// Optional sink for GS log lines (device info, warnings, errors). Called from the GS thread. nullptr = OutputDebugString.
using KzgsLogFn = void (*)(int level /*0 error, 1 warning, 2 info*/, const char* msg);
void kzgsSetLogCallback(KzgsLogFn fn);

// Optional frame-pipeline trace sink (timeline recorder, patch 0025). Called from the GS thread and from the producer
// thread, must be cheap and lock-free. id: 2 GS thread idle time in us since the previous frame (reported when a frame starts), 3/4 GS thread renders+presents a frame
// begin/end, 5/6 producer waits for the GS thread (maxQueuedFrames) begin/end, 7/8 producer waits for ring space
// begin/end, 9 frames queued (a) after a vsync was pushed, 10 accumulated GIF upload/draw CPU time (a us),
// 11 total GPU frame time (a us), 12/13 merge CPU begin/end, 14/15 presentation upscale CPU begin/end,
// 16/17 sharpening CPU begin/end, 18/19 final presentation CPU begin/end. nullptr = off.
using KzgsTraceFn = void (*)(int id, uint32_t a);
void kzgsSetTraceHook(KzgsTraceFn fn);

// Enables the renderer's asynchronous whole-frame GPU timestamp queries. May be changed while open; off by default.
void kzgsSetGpuTimingEnabled(bool enabled);

// SMAA edge-detection luma threshold (0.02 .. 0.5, default 0.1 = the HIGH preset); applied the next time the SMAA shaders are built.
void kzgsSetSmaaThreshold(float threshold);
bool kzgsSmaaAvailable();
bool kzgsSmaaEffective();

// Test/tooling only: forces the active D3D12 device through PCSX2's normal device-loss recovery path.
bool kzgsDebugInjectDeviceLoss();

// Frame-rate counter in the top-left corner of the window: presented frames per second, updated twice a second. Off by default.
void kzgsSetShowFps(bool show);

// Even frame pacing for external frame generation (docs/frame-generation.md). `hz` > 0: a frame that reaches the GS
// thread less than one 1/hz period after the previous present is held until that period has passed; late frames are
// never held. 0 (default) disables pacing. Takes effect at the next presented frame.
void kzgsSetPresentPacing(double hz);

// SMAA luma edge threshold (0.02..0.5, default 0.1 = SMAA's HIGH preset). Higher = only strong edges are smoothed and texture detail is kept;
// the shaders are rebuilt on the next frame when it changes. Direct3D 11 only. See docs/findings.md ("Menu jitter and blur").

// Measurement aid: capture up to `count` GS frames without making the caller wait. Zero dimensions read internal
// resolution; positive dimensions produce a presentation-sized snapshot. Negative dimensions request a Direct3D
// backbuffer or a Vulkan diagnostic recomposition. A snapshot is not proof of a successful present.
struct KzgsBurstFrame
{
	std::vector<uint8_t> rgba;
	int width = 0, height = 0;
	int field = 0;
	std::string tcInfo; // display mapping and texture-cache render/depth targets when the frame was presented
	int scanmask = 0;  // GSState::m_scanmask_used after the frame was merged (PCRTC anti-blur is disabled while it is non-zero)
	int rc[8] = {};    // display circuits after PCSX2's offset logic: framebufferRect (x,y,z,w) of circuit 1 then 2... y and w only: fb1.y fb1.w fb2.y fb2.w disp1.y disp1.w disp2.y disp2.w
	int gameDeint = 0; // PCSX2 'game_deinterlacing' (selects the MAD/bob path in Merge)
	uint64_t seq = 0; // count of vsync commands the GS thread has processed
};
enum class KzgsBurstCapturePoint
{
	Unavailable,
	GsInternal,
	PresentedOutput,
	SwapchainBackbuffer,
	PresentationRecomposition, // diagnostic snapshot; not proof of a successful present or final CAS output
};
const char* kzgsBurstCapturePointName(KzgsBurstCapturePoint point);
void kzgsBurstArm(int count, int presentWidth = 0, int presentHeight = 0);
bool kzgsBurstTake(std::vector<KzgsBurstFrame>& out); // true once the armed burst is complete (and moves it out)
bool kzgsBurstTakeEx(std::vector<KzgsBurstFrame>& out, KzgsBurstCapturePoint& point);
