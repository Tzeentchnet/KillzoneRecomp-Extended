# kzgs: PCSX2's hardware GS renderer as a static library

kzgs compiles PCSX2's `pcsx2/GS` renderer into one static library, `kzgs.lib`. It replaces the recompiled
game's CPU software rasterizer. It uses the D3D11, D3D12 and Vulkan hardware renderers. The rest of the emulator is
replaced by small stubs in `shim/`, so the library needs no BIOS, EE/IOP, VIF, DMAC or MTGS.

- PCSX2 source: `ext/pcsx2`, pinned in `config/dependencies.lock.json` to
  [Tzeentchnet/pcsx2](https://github.com/Tzeentchnet/pcsx2) companion commit
  `1fb920189c12766af55bdc9178619c0c66eb1a5e`. It includes generic presentation, Vulkan device-negotiation,
  completed-frame, and GS observation hooks. The revision is published on `feature/kzgs-upscaler-foundation` and is
  available for clean remote acquisition. It is a separate checkout, not the PS2Recomp submodule.
- Dependencies: the official `pcsx2-windows-dependencies.7z` from the locked PCSX2 Windows dependency release,
  extracted to `ext/kzgs/deps` (so the headers are in
  `ext/kzgs/deps/deps/include`). It is not committed.

## Build

Windows x64 MSVC and clang-cl with the MSVC ABI have been validated, using CMake 3.24 or newer with Ninja. Provision
the exact PCSX2 checkout and dependency archive described in the [building guide](../../docs/building.md) first;
do not replace the pinned archive with an unverified latest release.

```powershell
.\tools\scripts\vsenv.bat cmake -S .\ext\kzgs -B .\ext\kzgs\build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
.\tools\scripts\vsenv.bat cmake --build .\ext\kzgs\build -j 4
.\ext\kzgs\build\kzgs_test.exe
.\ext\kzgs\build\kzgs_test.exe --renderer d3d11 --warp --golden --out .\ext\kzgs\build\test-output\d3d11
```

The standalone build produces `kzgs.lib`.

Using it from the game's CMake:

```cmake
add_subdirectory(ext/kzgs)                 # KZGS_BUILD_TEST defaults to OFF when not top-level
target_link_libraries(killzone PRIVATE kzgs)
kzgs_stage_resources(killzone)             # copies resources/shaders + runtime DLLs next to killzone.exe
```

CMake options:

| Option | Default | Purpose |
|---|---|---|
| `KZGS_PCSX2_DIR` | `ext/pcsx2` | Location of the PCSX2 source tree |
| `KZGS_DEPS_DIR` | `ext/kzgs/deps/deps` | Location of the extracted dependency bundle |
| `KZGS_ISA` | `AVX2` | Instruction set the GS code is compiled for: `SSE4`, `AVX` or `AVX2` (see Known gaps) |
| `KZGS_BUILD_TEST` | on when top-level | Builds `kzgs_test` |
| `KZGS_BUILD_TOOLS` | on when top-level | Builds `kzgs_replay`, the trace debugger (see Debugging) |
| `KZGS_ADDRESS_SANITIZER` | off | Instruments kzgs with clang-cl AddressSanitizer; the top-level executable links the runtime |

`--golden` runs the deterministic synthetic scene at 1x and 2x, compares every pixel outside a narrow
rasterization-edge exclusion zone against the algorithmic reference, and exits before stress and renderer-switch
coverage. Root CTest registers isolated D3D11, D3D12, Vulkan, and software golden runs; the software run checks 1x.

All of kzgs' compile flags are private: `/permissive-`, `/Zc:preprocessor`, `/arch:AVX2`, C++20, PCSX2's defines and
the ImGui rename header. The only thing a consumer inherits is the include directory, plus these link libraries:
system D3D/DXGI, `delayimp`, and import libraries for `zstd`, `libpng16`, `libwebp`, `jpeg62` and `dxcompiler`. All
five of those are `/DELAYLOAD`ed.

`kzgs_stage_resources()` copies these DLLs next to the executable:
- `zstd.dll`
- `libpng16.dll` and `z.dll`
- `libwebp.dll` and `libsharpyuv.dll`
- `jpeg62.dll`
- `dxcompiler.dll` (needed by D3D12)
- `shaderc_shared.dll` (Vulkan loads it at runtime)

A D3D11-only run touches none of them, except when screenshots, dumps or texture replacements are used.

## API (`include/kzgs.h`)

```cpp
bool kzgsOpen(const kz::host::NativeWindow& window, const KzgsConfig& cfg,
              uint8_t* privRegs, std::string* err = nullptr);
bool kzgsOpenEx(const kz::host::NativeWindow& window, const void* config,
                size_t configSize, uint32_t version, uint8_t* privRegs, std::string* err = nullptr);
void kzgsClose();
bool kzgsIsOpen();
bool kzgsGetUpscalerStatus(KzgsUpscalerStatus& out);  // last applied request, effective provider and fallback reason
void kzgsGifTransfer(int path /*1..3*/, const uint8_t* data, uint32_t qwords);
void kzgsVsync(int field, bool regsWritten = true, bool wide = false);
bool kzgsVsyncTry(int field, bool regsWritten = true, bool wide = false);
void kzgsVsyncTemporal(int field, bool regsWritten, bool wide, const KzgsTemporalGuestObservation* temporal);
bool kzgsVsyncTryTemporal(int field, bool regsWritten, bool wide, const KzgsTemporalGuestObservation* temporal);
void kzgsWriteCSR(uint32_t csr);                      // RESET bit
void kzgsSoftReset(uint32_t mask = 7);                // GIF path soft reset
void kzgsReset();                                     // full GS reset
void kzgsResize(int width, int height);
bool kzgsGetResolutionInfo(KzgsResolutionInfo& out);  // latest exact render and physical output dimensions
void kzgsUpdateConfig(const KzgsConfig& cfg);         // any setting, including a renderer switch
bool kzgsUpdateConfigEx(const void* config, size_t configSize, uint32_t version, std::string* err = nullptr);
void kzgsSync();                                      // wait until the GS thread has drained the queue
bool kzgsReadback(std::vector<uint8_t>& rgba, int& w, int& h,
                  int presentWidth = 0, int presentHeight = 0);  // RGBA8; internal resolution or a presentation-sized snapshot
void kzgsSetLogCallback(KzgsLogFn fn);
void kzgsSetSmaaThreshold(float threshold);              // SMAA luma edge threshold, 0.02..0.5 (default 0.1 = SMAA HIGH)
void kzgsBurstArm(int count, int presentWidth, int presentHeight); // measurement: capture the next N GS frames (kzgsBurstTake)
bool kzgsBurstTake(std::vector<KzgsBurstFrame>& out);
bool kzgsBurstTakeEx(std::vector<KzgsBurstFrame>& out, KzgsBurstCapturePoint& point);
```

`KzgsConfig` has these settings:
- `renderer`: D3D11, D3D12, Vulkan, or Software (PCSX2's software rasterizer, useful as a reference when debugging)
- `upscale`: 1 to 8
- `textureFiltering`: PCSX2 `BiFiltering`
- `anisotropy`
- `fxaa`
- `edgeAA`: PCSX2 HW AA1
- `aspect`: 4:3, 16:9 or stretch
- `vsync`: FIFO or off
- `bilinearPresent`
- `halfPixelOffset` and `nativeScaling`: default to the Killzone GameIndex fixes, `4` (Native) and `2`
  (Aggressive). They are written to `UserHacks_HalfPixelOffset` and `UserHacks_NativeScaling`.
- `adapter`, or `useWarp` to force the WARP software adapter
- `resourcesDir`: defaults to `<exe>/resources`
- `cacheDir`: defaults to `<exe>/cache`. Set it to a writable per-user folder if the game is installed read-only.
- `disableShaderCache`
- `maxQueuedFrames`: default 1
- `selfContainedGifPackets`: default true; see GIF packet semantics below
- `debugDevice`

The original aggregate and three-argument `kzgsVsync`/`kzgsVsyncTry` symbols retain their legacy layout and
function-pointer types. `kzgsOpen` and `kzgsUpdateConfig` consume only `KzgsConfig`; both use default extended
settings (Off/0 and temporal diagnostics disabled). Use the separately named temporal VSync entry points to send
guest observations.

New callers use `KzgsConfigV1` (derived from `KzgsConfig`) with `kzgsOpenEx` and `kzgsUpdateConfigEx`, passing
`sizeof(config)` and `KZGS_CONFIG_VERSION_1`. `kzgsOpenV1`/`kzgsUpdateConfigV1` are convenience wrappers that pass
those values. Unknown versions and truncated buffers are rejected before reading the configuration. V1 adds
`upscaler`, `upscalerSharpening`, and `temporalDiagnostics`. The experimental presentation request defaults to
Off/0 and is independent of integer `upscale` and `casSharpness`; FSR1 is implemented on Vulkan. No vendor binary
is bundled; all DLSS/XeSS requests currently fail closed regardless of provider capabilities.

Pass `hwnd = nullptr` to open without a window (a surfaceless device). This still renders and supports
`kzgsReadback`, so it can be used for headless screenshot tests.
`kzgsGetUpscalerStatus` returns false while closed. After either config update, call `kzgsSync` before querying the
new status. A Vulkan FSR1 request reports `waiting_for_frame` until a successful upscaled present. The provider
preserves the cropped source and destination aspect; it reports `no_upscale_needed` if either destination axis is
smaller than the source or both are equal. Initialization and dispatch failures fall back to normal presentation.
FSR1 sharpening 0 skips RCAS; 1..100 maps linearly from 2 to 0 attenuation stops, independently of existing CAS.
If a Vulkan idle wait fails for a reason other than device loss, FSR1 reports `idle_wait_failed` and abandons
potentially in-flight child resources to device teardown rather than explicitly destroying them. Their C++ texture
wrappers are intentionally leaked for process lifetime, so repeated failures can accumulate host allocations.
Presented-size readbacks and Vulkan negative-size bursts can recomposite a cached upscaler result without
redispatching. They are diagnostic snapshots, not literal swapchain-image readbacks: a skipped or failed present may
instead yield a pre-CAS or current-frame snapshot. For a negative-size Vulkan burst, `kzgsBurstTakeEx` reports
`presentation_recomposition`, never `swapchain_backbuffer`; positive-size snapshots retain the `presented_output`
label. The legacy `kzgsBurstTake` remains available.
Surfaceless Vulkan retains the existing readback behavior and reports `no_surface`.

The [provider ABI](../../docs/upscaler-provider-abi.md) negotiates optional Vulkan extensions/features before device
creation, records into a borrowed command buffer, and retires provider contexts only after actual GPU completion.
Current frames supply composed color only and identify its encoding as unknown. Every DLSS/XeSS request falls back
before provider context creation, even when a DLL claims spatial or color-only capability; the
[temporal-input gate](../../docs/findings.md#temporal-qualification-instrumentation-fail--no-go-2026-11-10) is
FAIL/NO-GO. The in-tree mock is a test-only blit, enabled only by test hooks. The separate
[local SDK adapters](../../docs/vendor-upscalers.md) are unqualified, and the current host provides no SDK
initialization scope. Neither real SDKs nor vendor GPUs were tested.

The reviewed companion hook contract requires the original, pre-sharp-prescale source and its crop/aspect destination
to reach EASU, optionally RCAS, then existing CAS/final presentation. Ordinary sharp prescale is used only when the
hook falls back. The currently locked companion revision predates this fix, so the reviewed behavior requires the
separate companion update and pin/revalidation.
The presentation callback confirms success before the effective provider is reported; GPU completion
fences govern resource retirement. The GS-thread temporal observer records bounded draw/display/present metadata and
conservative resets, but does not promote candidate depth, motion, camera, or masks to qualified inputs.

The EASU and RCAS pipelines use independently compiled entry points, not Vulkan specialization constants. AMD's
unmodified MIT-licensed FSR 1 headers, source commit, hashes, and notice are in `third_party/fsr1`; resource staging
includes the shaders and notice in `resources/shaders/kzgs/fsr1`.

### Privileged registers

`privRegs` is the caller's own 0x2000-byte block, laid out like PCSX2's `GSPrivRegSet`:

| Offset | Registers |
|---|---|
| `+0x0000`, `+0x0010`, `+0x0020`, ... | PMODE, SMODE1, SMODE2, ... (one every 0x10 bytes) |
| `+0x0070` | DISPFB1 |
| `+0x0080` | DISPLAY1 |
| `+0x00E0` | BGCOLOR |
| `+0x1000` | CSR |
| `+0x1010` | IMR |
| `+0x1080` | SIGLBLID |

kzgs never writes to this block. The GS thread works on its own copy, which PCSX2's MTGS also does. At every
`kzgsVsync()` it snapshots 0x000-0x0EF, CSR, IMR and SIGLBLID into the command ring. The game can therefore write its
registers at any time without racing the renderer.

Mode notes that were checked with the test:
- PCSX2 identifies NTSC from `SMODE1.CMOD == 2`.
- A 448-line full-frame buffer uses `SMODE2 = INT=1, FFMD=0`.
- `FFMD=1` makes PCSX2 read a half-height field (224 lines) and line-double it.

### GIF packet semantics

By default (`selfContainedGifPackets = true`), every `kzgsGifTransfer()` call must be a self-contained sequence of GIF
packets that starts with a GIFtag. This matches what PS2Recomp's GIF arbiter forwards, and what its own CPU GS
frontend expects.

A GIFtag that is still open at the end of a call is dropped. This matters for one case in particular. When a VIF1
DIRECT ends with an IMAGE tag whose data arrives in a later DMA chunk, `ps2_vif1_interpreter.cpp` sends that data as
a new packet with its own synthesized IMAGE tag. PCSX2 keeps GIF path state across calls (MTGS semantics), so without
this mode it would read the synthesized tag as pixel data. The last data qword would then be parsed as a garbage
GIFtag, which corrupted every PATH2 texture upload and the GS state after it. That was the cause of the all-black
Killzone frames. Dropping the open tag is safe because the image transfer's own progress (TRXREG) is tracked
separately and continues with the re-wrapped data.

Set `selfContainedGifPackets = false` for a raw, hardware-exact per-path byte stream, where a packet may continue
across calls.

## Threading model

kzgs owns one GS thread. The D3D11/D3D12/Vulkan device, the ImGui context and every PCSX2 GS call live on that thread.
The public calls work as producers:

- They copy their arguments into a 32 MB single-consumer ring buffer and return immediately. GIF data is copied, so
  the caller can reuse its DMA buffer.
- Producers are serialized by a mutex. Because packets from different threads would interleave, drive kzgs from the
  one thread that owns the GIF arbiter.
- `kzgsOpen`, `kzgsClose`, `kzgsSync` and `kzgsReadback` block until the GS thread has handled them.
- `kzgsVsync` blocks only when more than `maxQueuedFrames` frames are still queued. This is frame pacing, as in
  PCSX2's MTGS.
- Wake-ups use a flag hand-shake instead of one atomic notify per command: the GS thread announces that it is about to
  sleep, and the producer only calls `notify_one` after a publish when that flag is set (and the other way round for a
  producer waiting for ring space). The VIF1 worker publishes ~4000 GIF packets per frame, and a `WakeByAddress` call
  per packet cost ~5 % of its time. `KZGS_NOTIFY_ALWAYS=1` restores the unconditional notifies.
- A transfer larger than 1 MB is split into several ring commands. That is safe because PCSX2's GIF path state
  machine resumes partial packets. The test sends every packet in 7-qword pieces to check this.

## What is stubbed (shim/)

| Area | kzgs behavior | Matters for correctness? |
|---|---|---|
| `Host::AcquireRenderWindow` / `ReleaseRenderWindow` / `BeginPresentFrame` | Returns the HWND given to `kzgsOpen`, or a surfaceless window if it was nullptr. | Yes, and it is implemented. |
| `Host::` OSD, error and translation calls | OSD text goes to the log. Errors go to the log and are returned by `kzgsOpen`'s `err`. Translation returns the input unchanged. | No |
| `Host::` settings getters | Always return the default value. kzgs is configured only through `KzgsConfig`. | No |
| `Host::IsFullscreen` / `SetFullscreen` | Always windowed; exclusive fullscreen is not supported. | No |
| `ImGuiManager`, `FullscreenUI` | A minimal ImGui context (default font, no OSD or menus), because the devices call `ImGui::Render()` on every present. | No |
| `VMManager` | Reports serial `SCUS-97402`, title `Killzone` and CRC 0. These are used only for dump and texture-replacement folder names. | No |
| `PerformanceMetrics`, `GSDumpReplayer`, `MTGS`, `SPU2`, `USB`, `Pad`, `FileMcd`, `AudioStream`, `CrashHandler` | No-ops. The MTGS entries are reached only from PCSX2 hotkeys, which kzgs never fires. | No |
| `eeHw`, `_cpuRegistersPack`, `FMVstarted`, `BuildVersion` | Dummy globals that PCSX2 headers bind references to. | No |
| `SysMemory::GetCodePtr` | Reserves only the 64 MB RWX "SWrec" region, near the exe, for the software renderer's JIT. | Only for the Software renderer |
| **SIGNAL / FINISH / LABEL, CSR interrupt bits, IMR, INTC_GS** | Not handled. PCSX2's `GSState` ignores these A+D writes too; the EE-side GIF unit handles them. **The caller's GIF arbiter must process them and raise INTC_GS.** | **Yes** |
| **CSR.FIELD / vsync field** | The caller supplies `field` to `kzgsVsync` and keeps CSR in its own block. | **Yes** |

Two parts of PCSX2 are compiled for real rather than stubbed:
- `Pcsx2Config.cpp`, which provides `GSOptions`, `EmuConfig` and `EmuFolders`.
- `SourceLog.cpp`.

### Dear ImGui isolation

The game links its own Dear ImGui (1.92.7-docking), while PCSX2 ships 1.92.9b. To keep them apart, CMake generates
`build/kzgs_generated/kzgs_imgui_rename.h`, and every kzgs C++ file force-includes it:
- The header `#define`s every `ImGui`, `Im*` and `GImGui` identifier found in PCSX2's imgui headers and sources to
  `Kz<name>`, about 1,760 identifiers.
- Identifiers that imgui uses as `#ifndef` hooks are left alone. These are the `ImDrawIdx`/`ImTextureID` typedefs and
  the inline `ImQsort`.

This was verified by linking `kzgs.lib` together with the game's fetched imgui 1.92.7 into one executable. It linked
with no duplicate symbols, and the game's `ImGui::GetCurrentContext()` was unchanged after `kzgsOpen`, a vsync and
`kzgsClose`.

## License

PCSX2 is GPL-3.0-or-later, and kzgs statically links it. `kzgs.lib`, and any executable that links it (the Killzone
PC port), is therefore a derivative work under **GPL-3.0-or-later**. Distributing binaries requires offering the
corresponding source. The shims and the kzgs sources carry `SPDX-License-Identifier: GPL-3.0+`.

The DLLs from the dependency bundle keep their own licenses (zlib, libpng, libwebp, libjpeg-turbo, zstd, DXC,
shaderc); see `deps/deps/licenses`.

## Debugging (kzgs_replay)

`kzgs_replay` replays a game GS trace recorded with `KZ_GS_TRACE`, in the same format that `tools/gs_replay` reads,
and can look inside PCSX2's GS:

```sh
ext/kzgs/build/kzgs_replay.exe work/trace_full2.bin work/kzdbg/out --every 100 [--upscale 2] [--renderer sw]
    [--pcrtc] [--vram 0:8,70:8,e0:8] [--dump-draws 1000:60] [--last N]
```

- `--pcrtc` prints PCSX2's display-circuit state: display and framebuffer rects, offsets and magnification.
- `--vram` reads the texture cache back into local memory. It then prints nonzero bytes per 256 KB of VRAM and
  writes each requested CT32 buffer (FBP:FBW) as a PNG.
- `--targets` prints the texture cache's render targets (TBP, size, valid/drawn rects) and writes each as a PNG.
- `--thumbs FILE --nopng` writes one 64x56 RGB thumbnail per sampled frame to a binary file, to compare two runs
  (renderers, settings) frame by frame without storing every PNG. `--from N` only samples frames >= N.
- `--present WxH [--aspect 16:9|4:3|stretch] [--crop l,t,r,b]` also writes `present_<frame>.png`, the frame scaled and
  aspect-corrected as presented in a window of that size. `--adapter NAME`, `--hpo N`, `--native N`, `--fxaa`,
  `--antiblur 0|1`, `--interlace N` set the matching `KzgsConfig` fields.
- `--window` (with `--present WxH`) opens a hidden window of that size. `--backbuffer` writes `bb_<frame>.png`
  only for a Direct3D swap-chain capture. Vulkan diagnostic recompositions are named `recomposed_<frame>.png`
  and labeled `presentation_recomposition`; they do not prove which image was presented.
  `--sharp 0|1`, `--bilinear 0|1`, `--cas N`, `--filter N` set the final-scaling and texture-filter fields. `--skip-idle` skips
  vblanks without new GIF packets like the game does, `--single-circuit` applies the game's display-register rewrite
  (read circuit 2 := read circuit 1, see docs/findings.md "Menu jitter and blur").
- `--upscaler off|fsr1|dlss|xess` and `--upscaler-sharpening 0..100` request the experimental presentation contract;
  Vulkan FSR1 runs when upscaling is needed, while other providers/backends fall back. These do not change `--upscale`
  or `--cas`. The game and both replay tools share the same strict CLI/environment parsing.
- `--sleep MS [--sleep-from N]` waits between vsyncs, so the GS thread has idle time as in the game (a replay without
  pacing keeps the GS thread saturated and hides pacing-dependent renderer bugs).
- `--dump-draws F:N` turns on PCSX2's own per-draw dumps (context registers, vertices, transfers) for N draws
  starting at frame F.
- `--tex TBP:TBW:PSM:W:H[:CBP]` decodes a texture from GS local memory into color and alpha PNGs. PSM can be
  PSMT8 (0x13) or PSMT4 (0x14) with a CT32 CSM1 CLUT, or CT32 (0).
- `--defrost GS.bin` loads the GS part of a PCSX2 savestate (unzip the `.p2s`) instead of replaying a trace. It can be
  combined with `--tex`, or with `--save-vram FILE`, which writes the raw 4 MB of local memory; `--save-vram` also
  works at the end of a trace replay. This lets you diff VRAM between real PCSX2 and the port.
- It uses the internal hook `kzgs::RunOnGSThread()`, declared in `shim/include/kzgs_internal.h`.

## Known gaps

- **Local-to-host transfers are not exposed.** These are `TRXDIR=1` reads of VRAM back through the GIF FIFO or BUSDIR
  (`GSInitAndReadFIFO`), and there is no kzgs call for them yet. The GS processes the TRXDIR write, but the data is
  never handed back to the caller.
- **Savestates are not exposed.** `GSfreeze` is compiled but there is no kzgs call for it.
- The GS code is compiled for one ISA (`KZGS_ISA`, default AVX2), not PCSX2's runtime multi-ISA dispatch. CPUs
  without AVX2 need a rebuild with `-DKZGS_ISA=SSE4`.
- D3D12 uses the system D3D12 runtime, not the Agility SDK. PCSX2 exports `D3D12SDKVersion` from its exe; kzgs does
  not. The log shows "Agility version: 616", which is the system runtime.
- No OSD, performance overlay, video capture UI or PCSX2 hotkeys. Video capture and GS dump code is compiled but has
  not been tested through kzgs.
- `kzgsUpdateConfig` changes vsync between FIFO and off only. Mailbox mode is not exposed.
- `QueryDisplayConfig() failed: Access is denied` is logged when the window's refresh rate can't be queried, for
  example in a locked or remote session. It is harmless.
- Only one kzgs instance per process, because PCSX2's GS uses globals.
- ps2xRuntime has its own global `struct GSVertex` (and `GSRegisters` and similar). PCSX2's `GSVertex` has the same
  name. No shared template instantiations were found, so there is no ODR clash today. Renaming the runtime's struct
  would remove the risk.
- In the test, putting around 16,000 full-screen 2x fills into a single frame hit the 2-second GPU watchdog on the
  Intel UHD 770 under D3D12 and Vulkan: the frame read back black, and the device was reset. No real frame comes close
  to that. The ring stress test uses IMAGE uploads for bulk data instead.
