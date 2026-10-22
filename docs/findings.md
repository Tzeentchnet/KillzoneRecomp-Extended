# Reverse-engineering findings (SCUS-97402, CRC CAAEC49C)

Addresses are EE virtual addresses in `SCUS_974.02`. Function names are Ghidra auto-names.

## Scope
- `SCUS_974.02` = entire offline game. `cdrom0:\LOADER.ELF` is exec'd only for Killzone Online; `LOADER.ELF` loads
  `MODULES\BLGFX.REL` (online loading screen), `DNASMC.REL`, `NETWORK.REL`, `ONLINE.REL`. All online-only → out of scope.
- Game data: `FILES.DAT`, `FILES01-04.DAT`.
- ~~Native USB mouse support~~ — **wrong, corrected 2026-09-29**: `Use Mouse` / `Mouse-Look X/Y` are reflection properties of the
  engine camera classes (`Camera.Input: Use Mouse/Use Joystick/Use Keyboard`, `CamFirstPerson: Speed/Forward/Strafe/Mouse-Look X/Y/Min Pitch/Max Pitch`,
  `CamOrbit`), i.e. Guerrilla's PC tool/debug cameras. See "USB keyboard/mouse (lgkbm)" below: nothing reads mouse data.

## Frame timing (key for high refresh)
- `DAT_0055a6e0` — vsync counter (60 Hz NTSC fields).
- `DAT_0055a6e4` — float, seconds per vsync (presumably 1/60).
- `FUN_001bfe88(timer)` — timer init:
  - `+0x50` = `2 * DAT_0055a6e4` → seconds per tick (1/30 s)
  - `+0x54` = 1.0f (time scale), `+0x60` = 1, `+0x68`/`+0x6c` = `vsync >> 1`
- `FUN_001bff10(timer)` — frame limiter / delta:
  - loop: `now = vsync >> 1` (30 Hz ticks); `+0x6c = prev`; `+0x68 = now`; `elapsed = min(now - prev, +0x70)`; `+0x64 = elapsed`; spin while `elapsed < 1`.
- Community 60 fps patch: `0x1BFF3C` → `srl v0,v0,0` (drop the `>>1`, ticks become 60 Hz) and `0x1BFEAC` → `lui at,0x3F00` (0.5f scale on the tick length).
- **Conclusion:** simulation dt = `elapsed_ticks * seconds_per_tick`, clamped. Sim is dt-scaled → high refresh plan = replace the vsync-tick source
  with a finer host-clock tick and matching `+0x50`, then validate timing-sensitive systems at 60/144/240 (movement, fire rate, reload, anims).

## Widescreen / noise filter
- `0x005DA364` = 1 → native widescreen (pnach). `0x0055DF6C` byte → noise filter (only when `0x0057BA88 == 4`).
  No direct xrefs in Ghidra (accessed via base+offset) — locate owning struct later.

## USB keyboard/mouse (lgkbm)
- `FUN_002de720` loads `cdrom0:\IOP\USBD.IRX` ("usbd") and `LGKBM.IRX` ("lgkbm", arg `MaxKbd=1`), then `FUN_003d7490(1,0,1,cb)` = Logitech
  lgkbm lib init (max kbd, max mice clamped to 4; here 1 keyboard, 0 mice).
- RPC sid `0x61766973`, client @0x590D90, async with end func `0x3D9360` (iSignalSema 0x590DB8). Thread `FUN_003d8cf0`:
  cmd 3 (0x20) handshake {u16 maxKbd, u16 maxMouse, u32 ver 0x80001, u16 1} → IOP must echo ver at +0xA, 1 at +0xE, 0 at +0x10;
  cmd 1 (0x90) device list (count @+2, entries @+4 x 0x10: +2 type 0/1 kbd/2 mouse, +4 id, +0xE flags 0x8000 reportId/0x4000 wheel);
  cmd 2 (0x1D0) events (count @+2, records @+4 x 0x1C: +4 type, +6 id, +0xC err, +0x14 HID boot report); cmd 4 ack.
- Keyboard API (lock 0x590DBC, slots 0x590E18 x 0x110): `FUN_003d7708` open, `FUN_003d6f38`, `FUN_003d7058` read, `FUN_003d7918` key
  bitmap, `FUN_003d7858` close, `FUN_003d7828` changed-flag. **No mouse reader exists** (mouse ring 0x591258 x 0x68 is written, never read).
- Consequence: mouse aim must be an engine patch (player yaw/pitch); keyboard movement via the pad layer. The keyboard lib is likely online chat.

## Mouse (first pass, superseded)
- `Use Mouse` @0x5570FB ← `FUN_003c8a20`; `Mouse-Look X/Y` @0x557249/56, 0x55730A/17 ← `FUN_003ca130`, `FUN_003cb580` (options/menu construction).
- `MsgMouse` @0x5516A7 ← `FUN_00309a88` (registers 4 message handlers at 0x582B80..0x582C60 via `FUN_00172238`).
- Next: follow `MsgMouse` handler → where mouse deltas feed player aim.

## Library bindings
The ELF is stripped, so Sony library functions are identified by `ps2_analyzer`'s signature DB (131 bound automatically by
`tools/scripts/regen.py`). Functions the DB misses are bound manually in `config/killzone.toml`:
- `0x2B6060` `sceSifLoadModule(path, arg_len, args)` — thin wrapper over `0x2B5E38` `_SifLoadModule(path, arg_len, args, res, fno)`,
  which binds the IOP LOADFILE RPC (sid 0x80000006) via `0x2B5928` and spins until `client.server != 0`.
  Without the binding the boot hangs there: the runtime has no BIOS LOADFILE server. Caller: `FUN_00175548` (module loader loop).
- Loadfile neighbours already DB-matched: `sceSifLoadFileReset@0x2B5AB8`, `sceSifStopModule@0x2B5AF0`, `sceSifUnloadModule@0x2B5CF8`,
  `sceSifSearchModuleByName@0x2B5D88`.

## High refresh implementation (2026-09-29)
- Vsync counter `0x55A6E0` is written only by the two vblank handlers `FUN_00152018` and `FUN_0018f0d0`; `FUN_0018f0d0` also
  flips DISPFB1/2 every vblank → **displayed frame rate = vblank rate**, so the port raises the guest vblank rate R.
- Frame timer lives in the game object `*(0x559178)` at +0x50..+0x70, driven by main loop `FUN_001402b0`
  (`FUN_001bfe88` init once per session, `FUN_001bff10` wait+delta every frame).
- Instruction patches (config/killzone.toml [patches]): drop the `>>1` 30 Hz tick at 0x1BFEB4/0x1BFEB8/0x1BFF3C and the
  `add.s f0,f0,f0` doubling at 0x1BFEA0 → tick = 1 vsync, seconds/tick = `0x55A6E4`.
- `src/kz_timing.cpp` per vblank: `0x55A6E4` = 1/R; clamp `+0x70` scaled by R/30; fade level `0x55A7C8`
  (±8/+4 per vblank in `FUN_0018f0d0`) corrected to 60 Hz speed.
- FMV/attract playback (`FUN_0026ecc8`) times itself with `FUN_001466f8` (ms timer), only uses vsync parity for fields.
- Runtime: `ps2SetVblankPeriodMicros()` (runtime/ps2_host_gs.h) replaces the fixed 16667 us vblank period.

## Widescreen (found 2026-09-29 with PCSX2 PINE)
- Native 16:9 flag = word at `*(0x559178) + 0x34` (the global game object, which also holds the frame timer at +0x50..+0x70).
  The community pnach address 0x5DA364 is that field in PCSX2's heap layout; the runtime's heap differs, and writing
  0x5DA364 there corrupted rendering (grey, grainy menus). Game option label strings: "Aspect", "(4:3; 16:9)", "IsWideScreen".
- Noise filter pnach: clear byte 0x55DF6C while halfword 0x57BA88 == 4 (in a level). Forcing 0xFF elsewhere was wrong.
- Tool: tools/scripts/pine.py (PCSX2 PINE client; EnablePINE=true, PINESlot=28011 in tools/pcsx2/inis/PCSX2.ini).

## Mouse aim (2026-09-29)
- Profile control options are reflected properties (FUN_001d3478): `YawSensitivity` float at settings+0x138, `PitchSensitivity` +0x13C,
  (getters FUN_001d6cb8 / FUN_001d6cc8; settings = `*(*(obj+0x10))`), crouch toggle byte +0x142.
- `FUN_0023ee08` = JoystickControllerPlayerImp update (ctrl): ctrl+0x18 = player entity, ctrl+0x1C = local player index (-1 unused).
  Look rates: ctrl+0x3C = ctrl+0xA8 = (yawSens*1.5+0.5) * clamp(stickX,-1,1); ctrl+0xAC = (pitchSens*1.5+0.5) * clamp(-stickY,-1,1)
  (negated again if the invert option FUN_001d6ca8 is set). The control state handed to the player is ctrl+0x24 (rates at +0x84/+0x88).
- `FUN_0021a8c8(dt, player)` = player update: `FUN_0021c550(dt, rate84 * maxYaw(vt+0x190) * dt, rate88 * maxPitch(vt+0x194) * dt, player)`.
  `FUN_0021c550` adds the deltas: yaw = player+0x148 + d (setter FUN_00218818), pitch = player+0x14C + d (setter FUN_00218970, clamps).
  So turning is linear in the rate; no acceleration curve after the stick read.
- `FUN_0023e9c8` = pitch auto-centre while walking with no look input (gated by profile option FUN_001d6c68) - a pad assist.
- Patch (src/kz_aim.cpp): hooks 0x23EE08 (remember player of local index 0), 0x21C550 (add mouse counts * 0.05 deg * sensitivity to
  f13/f14 for that player), 0x23E9C8 (skipped while keyboard/mouse is the active device). KZ_AIM=off disables, KZ_AIM_LOG=1 logs angles.

## Controls (2026-09-29)
- Action enum (reflection string at 0x5494B4): walk, strafe, pitch, turn, allowlook, sprint, primaryfire, secondaryfire,
  action, selectweapon, grenadethrow, specialitem, reload, stancechange, zoommode, zoom (+2 pause/objectives entries).
- Profile settings object (found at 0x102C880 in a gameplay RAM dump) holds the controller map at +0xEC (one int per
  action, same order; getter FUN_001d6c58 returns settings+0xEC), YawSensitivity +0x138, PitchSensitivity +0x13C
  (default 0.333). Button ids use the DS2 pressure order: 0 right, 1 left, 2 up, 3 down, 4 triangle, 5 circle,
  6 cross, 7 square, 8 L1, 9 R1, 10 L2, 11 R2, 12/13 select/start, 14 L3, 15 R3; axes are 100+ (negative = inverted).
- Default map: fire R1, secondary R2, action X, switch weapon O, grenade L1, special square, reload triangle,
  crouch L2, sprint L3, zoom mode R3. No jump in Killzone 1. Buttons are read as analog values (FUN_001b33b8,
  pressed if > 0.1 in FUN_001b3548), so the pad reply fills the DS2 pressure bytes too.
- Virtual keyboard (profile name): FUN_001af620. obj+0x228 = selected 3x3 block (8 = centre), picked with the left
  stick while held; the face buttons type the block's 4 letters (triangle top, circle right, cross bottom, square left).
  Centre block: X enter, triangle cancel, square backspace, O space.

## VU0 micro mode (2026-09-29)
- VCALLMS targets in gameplay (first mission, microVU0 at clamp 3, calls per 10 s at ~9 frames/s): 0x870 ~0.84M,
  0x778/0x7B0 ~0.42M each (all three in FUN_00505a78, a loop: `cfc2.i $v0,$vi1` after 0x870 reads a test result),
  0x020/0x270/0x520/0xD18 ~75-90k, 0xC80 ~55k, 0x2C0/0x4F0/0x000/0x6C8 < 1k. About 2M VU0 calls per 10 s, ~270 ms of
  host time. Captured programs take 4-92 VU cycles; none uses M bits or VU1 registers.
- VU0 now runs on kzvu's microVU0 (src/kz_vu.cpp, runtime patch 0007). Old path (KZ_VU0=interp) vs microVU0 in the
  same headless run: ~7.0 vs ~10.0 vif frames/s in gameplay.
- NaN/Inf in guest memory: from level load on (~t=125 s headless), VU0 inputs loaded from memory contain 0x7FC00000
  (the x86 default NaN; the PS2 has no NaN). VU0 never produced one from finite inputs (KZ_VU0_STATS=1 counts both), so
  the source is on the EE side; the generated FPU macros are plain IEEE (`FPU_DIV_S` = a / b in
  ps2_runtime_macros.h, no PS2 max-value clamping). First seen in the input of program 0xC80 called at 0x3AC464
  (`lqc2 $vf1, 0($a1)`). Not traced further.
- VCALLMSR (0x31A8D8, 0x31A994 in FUN_0031a068) was generated as `ctx->vi[27]`, past the 16-entry vi[] (reads vu0_r);
  the runtime now uses vu0_cmsar0 (patch 0007). No capture came from these two sites, so the fix is untested in-game.
- PS2Recomp's interpreter vs microVU0/PCSX2 interpreter on clean captured inputs: identical except program 0xD18,
  where `FMAND vi1, vi3` sees a different MAC flag and the program branches the other way (ext/kzvu/README.md).

## Gameplay 3D (2026-09-29)
- Symptom: in the first mission only the HUD, tutorial prompts and post-effect passes rendered, over a red scene buffer.
  The EE was not submitting the world. Measured per frame: ours had 108 MSCAL, 468 UNPACK and ~250 DMA tags. PCSX2 had
  915 MSCAL, 6803 UNPACK and 4381 tags. The PCSX2 figures come from the frame chain in a gameplay savestate.
- **PCSX2 gameplay reference:** `tools/pcsx2/sstates/SCUS-97402 (CAAEC49C).09.p2s` and `.10.p2s`, taken at the first
  tutorial prompt ("Press R1"). `tools/scripts/pcsx2_drive.py` produced them, driving PCSX2 through PINE and the pnach
  pad hook.
  - Useful addresses (same in both runtimes, static data):
    - `0x559040` holds the start of the frame's VIF1 chain. `chain2buf.py` walks it and `vifparse.py` parses it.
    - RenderZoneManager vtable `0x531448`: +0x30 zone count, +0x34 zone array.
    - RenderZone world AABB: min at +0xA0, max at +0xAC.
    - Player entity vtable `0x525710`: position at +0x210.
- **Chain of causes, each fix measured in a run:**
  1. **Unaligned LQ.** FUN_003107a8 builds each RenderZone's world AABB with FUN_002c84d8, which loads the local box
     max with the unaligned `lq/lq/qfsrv` idiom. The generated LQ did not align the address, so every zone box came out
     flat and the camera was in no zone.
     - After the fix, the zone boxes match PCSX2 to 1 ulp, and the weapon plus the world batches (27k strip vertices per
       12 vsyncs) are submitted.
     - The camera was then 1.7e5 units below the level: the player fell.
  2. **SQRT.S read fs.** After the fix, the player x/y equal PCSX2's exactly (95.354, -480.852); before, they were off
     by ~0.02. The player still fell.
  3. **MMI lane bugs (PEXEW and others).** After the fix, the player stands at z 2.654 (PCSX2: 2.659), and the level,
     weapon and HUD render.
  4. **VIF1 DMA chains cut at 4096 tags.** A gameplay frame is ~4400 tags. After the fix, textures are clean and there
     are no white screens or host crashes (5 of 5 runs clean, against 3 crashes in ~6 runs before).
  5. **VIF1 UNPACK semantics.** The legacy code bends geometry.
- Before/after in-game rate (vif counter per second, t=150-230 s, `KZ_FPS=66`, `PS2X_IOP_BATCH=1`):
  - main build without these fixes, no 3D: 12.1
  - with the fixes, full scene: 10.7 and 10.3 (two runs)
- **Boot hang:** with `PS2X_IOP_BATCH` at its default (4096), 2 of 4 boots hung at dma=18. The game thread spins in
  FUN_00149f30 at 0x14a028, waiting for an IOP file read. With `PS2X_IOP_BATCH=1`, 0 of 4 boots hung.
  Root cause found and fixed 2026-09-29 (patch 0013), see "FilePS2 / PFILE_R.IRX" below; it hangs at batch 1 too.

## FilePS2 / PFILE_R.IRX (2026-09-29)
- EE side: FUN_00149f30 double-buffers a file into two halves. Each half has a pending-bytes counter, at +0x34 and
  +0x38 of the stream object. The EE sets a counter to the request size, then sends the request with
  FUN_0014bc08/0014bd00/0014bdf8: nowait sceSifCallRpc on client 0x55B710, sid 0x66662012, fn 800 (one request) or
  0x330 (two). The 0x20-byte request is {mode 0x8100000/0x1100000, device, file, offset, size, EE buffer, EE
  counter address}. The loops at 0x14a028/0x14a1c0 spin until both counters are <= 0.
- IOP side (PFILE_R.IRX, loaded at 0x10000; offsets below are module offsets):
  - The RPC server FUN_00002f00 (0x800/0x330 -> FUN_00001c1c) appends a node to the streaming queue: head 0x4094,
    tail 0x4098, queued counts 0x406C/0x4070. The queue lock is semaphore 0x409C.
  - The streaming thread FUN_000022a4 (prio 0x33) picks a request (FUN_00001e9c), reads with sceCdRead or ioman,
    and DMAs the data with sceSifSetDma.
  - Per 32 KB chunk it reports progress with `sceSifSendCmd(0, {counter, remaining, ...})` (FUN_00002c64). The
    game's EE cmd-0 handler stores `remaining` into the EE counter, and the final chunk sends 0.
  - The thread then unlinks the node under the lock.
- The hang: the emulator runs RPC server functions outside any IOP thread, where `WaitSema` did not block. An append
  that landed while the streaming thread was preempted between `v0 = cur->next` (0x26C4) and `if (v0 == 0) tail = 0`
  (0x26D8) was lost. Its completion command was never sent, so the counter stayed positive. Details, evidence and
  hang counts: patches/README.md, 0013.
- PSOUND_R.IRX's RPC also waits on a held semaphore from RPC context (seen in traces, ra 0x71260); patch 0013 covers
  it. From code reading, PFILE_R's close path (FUN_00001278, RPC 0x210) polls with DelayThread while a file has
  running requests. DelayThread outside a thread still returns at once. In traced boots it ran outside a thread only
  once per boot, from USBD at start, never from PFILE_R.

## VIF1 worker thread (2026-09-29, patch 0016)

VIF1 DMA, VU1 (microVU1), XGKICK and the GIF arbiter run on a worker thread. The design and every EE <-> worker interaction
point are in patches/README.md (0016). This section has the measurements.

**Method.** The `tools/scripts/run_headless.ps1` scene (`KZ_FPS=60`, `KZ_IPU=off`, the task's `KZ_INPUT_SCRIPT`), 235 s;
in-game rate = increase of the heartbeat's `vif=` counter per second between t=170 s and t=230 s. The scene is not
deterministic (tutorial events fire on wall-clock time), so single runs of one configuration differ by about +-10 %. The
machine load of every run was logged once a second (`work/<run>/load.csv`: total CPU %, `cl.exe` and `killzone*`
process counts). No other game instance or compile ran during any run listed below; the background CPU load (other
agents' python/ghidra/steam processes, not game instances) was 32-50 %, higher for the runs marked `*` (> 40 %), which
read low. "Before" = `build/vifth_base`, the synchronous build from just before this change (same tree otherwise,
including the IOP thread code, which is selectable with `PS2X_IOP_THREAD` in both).

| VIF1/VU1/GIF | IOP thread on (default): frames/s | IOP thread off (`PS2X_IOP_THREAD=0`): frames/s |
|---|---|---|
| synchronous, before this change | 31.3, 24.4* | 23.9*, 15.9* |
| synchronous, this change (`PS2X_VIF1_THREAD=0`) | 32.0, 30.7* | 27.9, 29.9 |
| threaded (default) | 46.0, 44.9 | 33.4*, 34.2 |

More clean runs of the final code, before the IOP switch was set explicitly (IOP thread state as the tree had it at the
time): threaded 43.9, 45.4, 50.8, 48.0 (worker not pinned to P-cores), 44.4 (vblank waits for the worker); synchronous
before the change 32.8, 33.1, 28.8. Threaded is about 1.45x the synchronous path of the same build and 1.5x the old
build (IOP thread on); the synchronous fallback is not slower than the old code.

**What the worker costs** (`PS2X_VIF1_STATS=1`, gameplay, ~45 frames/s): the worker is busy 23-60 % of a core (VU1 ~80 %
of that, including XGKICK; the host-GS hook ~10 %). The EE thread pays ~1.9 % for the chain walk and snapshot copy
(~900 MB of chains per 10 s), <0.1 % for dispatch, and barriers wait < 60 ms per 10 s in total (the one VIF1 register
write per job found the worker idle in more than 98 % of the cases). The guest vblank never waits for the worker. A job
takes about 10 ms on the worker on average, so a D1 completion interrupt arrives that long after the CHCR store instead of inside it.

**Things that did not matter.** Waiting for the worker at every vblank (`PS2X_VIF1_VSYNC_SYNC=1`) instead of ordering
the GS half of the vsync behind the worker's queue: 44.4 vs 43.9-50.8 (the ordered form is the default: it costs
nothing and never stalls the EE). Restricting the worker to P-cores on the i9-12900K (`PS2X_VIF1_CORES=perf`): 43.9 with,
48.0 without; off by default. An early threaded run measured 20 frames/s; it overlapped a full rebuild by another agent
(12 `cl.exe`) and is not comparable.

**Checks.** 6 + 6 boots of 50 s (`dma=` advanced in every 10 s interval, exit code 0), more than 10 full 235 s threaded
gameplay runs, 60 s boots with `KZ_VU=interp` (PCSX2's interpreter on the worker) and `KZ_VU=builtin` (falls back to synchronous, as designed), stress runs
with a slow worker (`PS2X_VIF1_DELAY_US=700`: gameplay reached, frames intact; 4000 us busy-waits stretched the loading screen
because the game waits for each GIF DMA to go idle before starting the next), kzvu_test 575 checks and kzvu0_test 303
checks (0 failures, both after the VU0/VU1 split), kzvu_linkcheck_ab/ba link. Rendering compared with the old build at
the same timestamps (menus t=10-130, weapon/HUD/level/explosions t=140-230): same screens, same artifacts (the
banded highlight bars in the profile and character menus are in the old build too), no flicker, missing geometry or
corruption. The GIF stream cannot be compared byte for byte across runs (the game is not deterministic): with
`KZ_GS_HASH=1`, 493 menu frames have byte-identical packet streams in a synchronous and a threaded run, the others are
animation frames that differ between any two runs; packets per frame have the same distribution, but threaded runs had more
empty vsyncs (390 of 4500 vs 90) and more vsyncs with two chains (147 vs 58): the game's kicks fall differently between
the vblanks when the completion interrupt is late.

**VU1 recompiles** (asked for: `mVUcompileJIT<1>` ~6 % in steady-state gameplay). `KZ_VU_STATS=1`, threaded, first level,
per 10 s:

| t (s) | 100 | 120 | 130 | 140 | 150 | 160 | 170 | 180 | 190 | 200 | 210 | 220 | 230 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MSCAL (k) | 45 | 41 | 176 | 178 | 353 | 412 | 441 | 406 | 410 | 327 | 375 | 240 | 71 |
| MSCALs that emitted JIT code | 1 | 0 | 38 | 203 | 230 | 166 | 121 | 70 | 47 | 19 | 19 | 293 | 128 |
| JIT emitted (KB) | 13 | 0 | 1408 | 5358 | 3857 | 2372 | 1720 | 706 | 445 | 125 | 216 | 5014 | 981 |
| new microPrograms | 3 | 0 | 311 | 1080 | 535 | 289 | 219 | 74 | 57 | 16 | 21 | 607 | 164 |
| MPG code-generation changes (k) | 0 | 0 | 9.1 | 13.1 | 29.6 | 35.7 | 36.2 | 32.7 | 32.8 | 28.1 | 31.3 | 23.3 | 4.7 |

Cache resets: 0 in 235 s (22 of 61 MB used, 3700 programs). Cause: microVU keeps one program list per MSCAL start PC,
and Killzone enters its skinning/lighting programs at ~540 different start PCs (entry addresses 0x30 bytes apart, which looks like an
unrolled loop entered `6*n` instructions before its end), times several microcode versions. Every (start PC, code) pair is
compiled once, on first use, from scratch, so the cost comes in bursts whenever new content appears (level start at
t=130-160, new effects at t=220) and is near zero in between (19 compile events in a whole 10 s at t=200). It is not repeated
compilation: of 951 new programs logged with the first differing micro-memory word, 540 were the first program for their
start PC and the other 411 all differ from the older program for the same PC inside its compiled ranges (different microcode
uploaded to the same addresses); none matched a cached program's content. The ~3300 MPG uploads per second do force
a program search per start PC after each change, which is cheap (`recMicroVU1::Clear` 0.2-0.7 % of the worker's busy
time, the search itself is inlined into `mVUexecute<1>`, 1 %); only ~5 % of the uploads are byte-identical to the code already in VU1 memory (those no longer invalidate
anything; `PS2X_MPG_SKIP_SAME=0` restores the old behaviour). With the worker, the compile bursts are off the EE thread
(`mVUcompileJIT<1>` ~4 % of a core in the profile window). Not fixed: a long session may fill the 61 MB cache, and
microVU then resets and recompiles everything (not seen yet); sharing compiled blocks between start PCs would need a change
inside microVU.

**Render-feeding costs on the EE thread, before -> after** (asked for: vector insert ~7 %, `vif1UnpackPcsx2` ~6 %):
- DMA chain walk + snapshot (`std::vector::insert` per tag into a fresh ~3 MB buffer per frame, freed on another thread):
  measured with `PS2X_VIF1_STATS=1` in the same binary, `PS2X_CHAIN_POOL=0` vs default, ~800-900 MB of chains per 10 s:
  616-671 ms per 10 s (6.2-6.7 % of the EE thread) -> 181-202 ms (1.8-2.0 %). Most of the old cost was page faults and
  reallocation copies, not visible as `_Insert_counted_range` self time (0.2 % in the profile); with the pool the worker's
  `NtFreeVirtualMemory` (0.4 %) is gone too.
- `vif1UnpackPcsx2` (`KZ_PROFILE=190,30`, share of the thread's wall time): synchronous EE thread 5.0 % -> 1.0 %; threaded worker
  4.9 % of a core (16 % of its busy time) -> 0.9 % (2.6 % of busy). Plain UNPACK (no mask, STMOD 0, WL <= CL) takes one SSE
  load/convert/store per vector; masked UNPACKs are 13 % of Killzone's ~8700 per frame. `PS2X_VIF_UNPACK_CHECK=4` ran every
  4th UNPACK of a boot + gameplay run both ways (5.8 M checked): 0 differences in VU1 memory and VIF registers.
- Heap traffic on the EE thread (`RtlAllocateHeap` 2.5-3.2 %, `RtlFreeHeap` 1.4-1.5 %) is not from these buffers; unchanged.

## EE thread frame budget (2026-09-30, patch 0019)
Measured in-process (`PS2X_SCHED_STATS=1`: counters and rdtsc, no sampling) in the gameplay window t=150..200 s, `KZ_FPS=120`.
The machine was shared with 2-3 other game instances and builds, so absolute numbers are 10-20 % worse than a quiet
machine (the least loaded run of the final binary measured 73 fps with the sampling profiler on).
- **The EE thread never idles.** `idleWaits=0` in every 10 s window. Per second of wall time: ~70-80 % in guest functions,
  ~5-6 % in the scheduler loop around them (~9 % before patch 0019), **10-25 % (100-240 ms, ~100 waits/s) asleep in
  `EeScheduler::processDueDeadlines`**: the guest clock reached a vblank's cycle deadline before the host reached its
  time, so the scheduler sleeps until the vblank's host deadline. That sleep is what the 32 % "wait site" in the earlier
  profile was.
- **Frame time is a staircase.** The finished frame list is kicked at a vblank, so a frame whose EE work ends at 14 ms
  starts its successor at the next vblank (16.7 ms) and the EE sleeps the difference. EE host time per displayed frame,
  (1000 ms - pacing sleep) / fps: 9.6-13.8 ms in sequential runs (70.8-58.6 fps), 12.1-15.3 ms in concurrent pairs (57-66 fps). fps only moves when
  frames cross a vblank boundary: 6 concurrent pairs of "all patch-0019 switches off" against "on" gave EE ms/frame -0.83,
  -0.83, -0.60, -0.48, -0.47, +0.06 (median -0.54 ms, -3.8 %) and fps 60.3 -> 59.6 (median of six, noise). Getting from
  ~14 ms to under 8.33 ms needs the guest-code side (about 80 % of the thread), not more fixed-cost trimming. On a
  quieter machine the EE has more slack: the least loaded run (73 fps, profiler on) had the EE thread asleep in
  `processDueDeadlines` for 47 % of all samples (busy 51.5 %, ~7 ms per frame including the wait loops), and the sequential
  runs of the final binary measured 9.6, 11.2 and 13.8 ms/frame at 70.8, 66.6 and 58.6 fps as the other instances on the
  machine came and went (pre-patch binary, same sequence: 68.3, 65.8, 64.6 fps; final: 70.8, 66.6, 58.6).
- **The VIF1 worker is at least as much of a limiter.** `PS2X_VIF1_STATS=1` (68 fps, t=150..200): one job per frame
  (601-673 per 10 s), worker busy 5.84-6.37 s per 10 s = 9.1-10.0 ms per frame, of which VU1 (microVU1 run + JIT compile)
  4.88-5.30 s = 7.6-8.3 ms and the host-GS hook 0.64-0.70 s; the EE's chain walk is 0.32-0.36 s per 10 s. The game kicks the
  next frame list only when D1 is idle (`FUN_00151fc8`, docs/perf_research.md section 2.6; not re-checked here), so a worker that needs more than one vblank period (8.33 ms) per frame gives
  2-vblank frames whatever the EE does. 120 fps needs the worker's per-frame time (mostly VU1) well under 8.3 ms too,
  not only the EE thread.
- **The guest cycle estimate is not the limiter.** (Superseded once the EE was no longer the busiest stage: it is, see "Frame pipeline timeline".) `PS2X_EE_CYCLE_SCALE=0.5` (charge half the cycles per call/back edge; a
  temporary knob, not kept) cut the estimated cycles from 215 to 135 M/s and left fps at 57.5 vs 58.8 in a concurrent pair.
- **Where the ~1 M scheduler dispatches/s come from** (`PS2X_SCHED_STATS=2` histograms; every dispatch is an entry into
  a guest function from the scheduler loop): tail jumps between `entry_*` fragments and the returns after a
  non-local unwind. `ps2CgAfterCallSlow` (ps2_cg.h) sets `g_ps2GuestUnwinding` when a callee returns with `ctx->pc` != the
  return address, so every enclosing guest frame returns to the scheduler, which dispatches the target and then, one
  dispatch per level, each caller's resume label. Top dispatch pcs (per second, one window): 0x17f8e0 31k, 0x185e20 30k,
  0x33cd84 29k, 0x3fb18c/3fb184/3fb108 20k each, 0x401750/4017a4/401720 17k each, 0x3a3c18/3a3ee4 12k each, and, when the
  game sits in its flag-wait loop `FUN_0014fd90` (spins on a byte cleared by an interrupt handler, reading COP0 Count),
  0x14fe28/14fe38/14fe54/14fec0/14fec8 at 47-152k each. Untried because it edits ps2_cg.h (full rebuild): let
  `ps2CgAfterCallSlow` call a table function at `ctx->pc` in a loop (a trampoline) while the pc is a table entry other than
  `fall`, instead of unwinding. That turns each of those dispatches into a plain C call and keeps the caller frames alive.
- **Locks.** Before the DMAC-drain fast path (another agent's change to `consumeCompletedDmacCauses`, PS2X_DMAC_DRAIN_FAST)
  `RtlAcquire/ReleaseSRWLockExclusive` + `Mtx_lock/unlock` were 5.0 % of the EE thread, all under `PS2Runtime::Store32` ->
  `drainCompletedDmacHandlers`. A profile of the final binary (`work/profile_pa2.txt` style, gameplay) shows no SRW/mutex
  entry above 0.1 % on the EE thread except `IopSubsystem::lockExec` in `handleRpc` (0.2 %, RPC path). Nothing else takes a
  lock per checkpoint, per dispatch or per call.
- **VU0 calls in gameplay:** ~470 k/s (`KZ_VU0_STATS=1`), start PCs by share of calls: 0x520 21 %, 0x270 15 %, 0x020 14 %,
  0xD18 12 %, 0x870 9 %, 0xC80 9 %, 0x910 5 %, 0x7B0 4 %, 0x778 4 %. The three programs of `FUN_00505a78`
  (0x870/0x778/0x7B0) are 17 % of the calls in this window, not the 84 % of the first measurement (that was a different
  scene), so hand-translating them would not remove most of the cost.
  `kzvu0_test --bench` (host cost of one call incl. the context copies, min of 3 runs, 11 captured start PCs): original
  path 136-194 ns (median 160), `kzvu0Call` 64-117 ns (median 79). Where the rest goes: ~45 ns is microVU's fixed
  dispatch cost (an E-bit-only program at 0x000 takes ~45 ns to run), ~10 ns the MXCSR switch, the copies ~20 ns.
- **EE timers.** `advanceEeTimers` was called at every checkpoint (~0.8 M/s): 28-30 ns each (rdtsc pair, ~10 ns of it
  instrumentation); lazy: 14-15 ns. Verified with `PS2X_LAZY_TIMERS=verify` (eager shadow copy compared at every flush and
  register read; an interrupt found by the shadow must be raised by the same call): 102 508 comparisons, 0 mismatches,
  2 014 interrupts, 200 M early returns checked in a 235 s run. `KZ_VU0_VERIFY=8` in the same run: 5.04 M calls checked
  (both paths from the same state, all registers/flags/TPC/VPU_STAT and VU0 data memory), 0 mismatches.
- **Boot flake (not from these changes).** About 1 boot in 30-40 under load ends in `[IOP] module arena exhausted` (the
  game loads `MCSERV.IRX` again and again until the IOP module arena is gone, then exits): 3 of ~115 boots, with the
  patch-0019 switches on (2) and off (1), always when several instances were booting at once. Log signature: a
  `load-emulated id=N path=...MCSERV.IRX` line repeating in stdout, hundreds of thousands of `module arena exhausted`
  lines in stderr. IOP module loading is not part of this patch.

## IOP RPC offload (2026-09-30, patch 0021)

The EE thread used to run the IOP's RPC server functions inline (`SifCallRpc` -> `handleIopRpc` -> `IopEmulator::handleRpc` ->
`callFunction` -> interpreter). Design and switches: patches/README.md, 0021. Measurements:

**What the game calls** (`PS2X_IOP_RPC_STATS=1`, before the patch, headless gameplay scene at 120 Hz vblank, 235 s, 40 326 calls
in the run; wall time on the EE thread; `insns` counts every IOP instruction executed during the call, including other IOP
threads that run while the server function waits on a semaphore or event flag). Gameplay window t=150..200 s (differences of the
cumulative tables):

| sid / function | mode | calls | wall in window | avg per call | insns per call |
|---|---|---|---|---|---|
| 0x06662012 fn 0x600 (sound driver, per-vblank command list, send 16-64 B, no end function) | nowait | 3 219 | 3 422 ms (6.8 % of the window) | 1 063 us | ~46 500 |
| 0x66662012 (PFILE_R) fn 0x400 | nowait | 3 219 | 34 ms | 11 us | ~215 |
| 0x66662012 fn 0x320 (read request) | nowait | 0 in the window, 623 in the run (load time) | | 16 us | 200 |
| all of `SifCallRpc` on the EE thread | | 6 438 | 3 752 ms (7.5 %) | 627 us (incl. the two above) | |

Blocking calls are 32 in the whole run (sid 0x06662012 fn 0x110 once, 5-8 ms at boot; sid 0x66662012 fn 0x100, 0x200 and 0x210 once
each; fn 0x600 28 times at 4 us): ~7 ms in total, so only nowait calls matter. Waiting for the IOP lock was 67 ms of 21.6 s: the cost is
interpretation (IOP thread profile, gameplay: `executeInstruction` 27 %, SPU2 mix 13 %, `IopMemory::read32` 13 %, `runCpu` 13 %,
`IopImportRegistry::decode` 6 %, `takeDmaStart` 5 %). The sound RPC also advances IOP time by ~1.2 ms per call (46 k instructions at
36.9 MHz), which is where the "IOP lead" of ~30 s per 235 s in `PS2X_IOP_THREAD_STATS` comes from (patches 0013/0015). Whole run:
24.9 s of the EE thread in `SifCallRpc`, 22.0 s of it inside `handleRpc`.

**After** (nowait calls to emulated servers queued to the IOP thread): EE-thread time in `SifCallRpc` 0.32 s per 235 s (8 us per
call). Without the `selectRpcAbi` change (it took the IOP lock on every call, and the IOP thread now holds it for ~1 ms per sound
command) the EE waited 3.2 s in the lock, 1 233 waits > 1 ms. EE-side IOP lock acquisitions per run: 80 584 -> 75. Offloaded calls wait
0.7 ms on average in the queue (max 40 ms, on a machine with 10 other game instances). IOP thread busy 7-10 % -> 17-19 % of wall.

**Frame cost, same-time pairs.** The machine was shared with 4-12 other game instances (50-85 % CPU load) for every run, so absolute
rates moved between 35 and 65 fps and only runs made at the same time compare. Two copies of one binary (offload default, and
`PS2X_IOP_RPC_ASYNC=0`) started together, roles swapped between pairs; CPU time per frame of the game process's threads between t=150 s
and t=200 s (`Process.Threads`, divided by the frames of that window; the EE thread is `GameThread`):

| pair | load | EE thread offload / inline | IOP thread offload / inline | fps offload / inline |
|---|---|---|---|---|
| 1 | 57 % | 11.18 / 11.67 ms | 3.04 / 1.93 ms | 59.1 / 60.0 |
| 2 | 64 % | 10.63 / 12.29 ms | 2.82 / 1.89 ms | 61.9 / 59.7 |
| 3 | 60 % | 12.29 / 12.16 ms | 3.17 / 1.84 ms | 54.5 / 57.5 |
| 4 | 85 % | 12.91 / 14.99 ms | 4.22 / 3.28 ms | 36.9 / 34.2 |

(Pairs 1 and 2 were logged before thread names were captured: their EE column is the busiest thread of the process, which is the game thread in the pairs with names; their IOP column is the fourth busiest.) The IOP thread gains 0.9-1.3 ms per frame,
the work that left the EE thread; the EE thread's own difference is -0.5, -1.7, +0.1, -2.1 ms (mean -1.0, noise about +-1). The frame
rate does not change measurably: at ~60 fps the frames are quantised by the 120 Hz vblank, and this machine state never let the EE
thread reach 8.3 ms per frame. Frame rates of the older interleaved runs, for the record (the baseline binary was built before other
agents' changes to `ps2_memory.cpp`/`ps2_runtime.cpp` that the later builds include, so these mix their work in and are not a clean A/B):
baseline binary 44.5 / 38.1 / 44.0, 45.0 / 48.1 / 59.3, 59.7 / 64.3 / 57.4 (median 48.1); offload builds 51.0 / 20.2 / 55.1,
42.3 / 61.2 / 65.4, 63.6 / 58.3 / 60.9 (median 58.3); the newest binary with `PS2X_IOP_RPC_ASYNC=0`: 64.4 / 57.8 / 64.0.

**Client reuse.** `PS2X_IOP_RPC_STATS=1` also counts nowait calls that arrive while the client's previous call has not completed (a
call the inline path could never see): 0 of 27 684 calls in a full run at 80 % machine load, where the queue latency averaged 1.5 ms
and reached 112 ms. Killzone waits for a call to complete before it reuses the client.

**Checks.** 35 headless boots of 50-60 s (12 with the flag set, 12 default, 8 at `KZ_FPS=120`, 3 with movies on; one baseline with
movies on), three instances at a time on a loaded machine: `dma=` advanced in every 5 s interval of all of them, no `TIMED OUT` from the
outside-thread semaphore wait. Movies: the intro plays in both builds (work/vid_base, work/vid_async montages). Audio, `KZ_AUDIO_WAV`,
gameplay scene, first 235 s, two runs each of baseline and offload (a baseline and an offload run at the same time, twice):

| segment | baseline vs offload | baseline vs baseline |
|---|---|---|
| 0-20 s | silent in all runs | silent |
| 20-130 s (menu music) RMS / peak | within 0.1-0.3 dB / 0.0-0.7 dB; correlation 1.000 (0.997 at 80-90 s, 0.62-0.64 in the 90-100 s segment, where a menu button press lands differently) at a constant offset that differs per pair of runs | the same figures |
| 130-235 s (gameplay) | correlation 0.03-0.3 | correlation 0.04-0.27: two baseline runs differ as much |
| overall RMS | -14.8 / -14.8 (baseline), -14.6 / -14.8 dBFS (offload) | |
| silent gaps > 20 ms inside sound | 2 in each of the four runs | |
| `kz_audio` underruns | 0 (headless drops the SDL output; real-time factor 1.00-1.02x in both) | |

The gameplay segments are not deterministic between any two runs (scripted input, wall-clock events), which is why the baseline pair
correlates as badly as the baseline/offload pair; the menu segments are deterministic and match.

**Not changed / left.** (1) The 46 k-instruction sound command still costs ~1 ms of one core per call, now on the IOP thread; the
hot spots are the interpreter (`takeDmaStart`/`schedulePendingDma` after every instruction, `IopMemory::read32` not inlined, import
decode per instruction) and the SPU2 register hooks; speeding those up would shrink the IOP thread's share (not attempted, not
measured). (2) Blocking calls (32 per run) and HLE-served RPCs stay inline. (3) A game that calls a nowait RPC and reads the reply
before the completion (`sceSifCheckStatRpc`, end function, semaphore) now sees the reply up to ~1 ms late, as on hardware; no
symptom of that was seen in Killzone (menus, loading, movies, gameplay, sound).

## Guest memory/IO and DMA chain copies (2026-09-30, patch 0020)
Measured with the switches below (same binary, A/B by environment). The machine was shared with 2-3 other game instances and
builds, so absolute numbers are 10-20 % worse than a quiet machine; every comparison is a concurrent pair. "EE ms/frame" is
the EE thread's non-sleeping host time per displayed frame (`tools/scripts/perf_metric.py`: (1000 ms - pacing sleep) / fps,
`PS2X_SCHED_STATS=1`), or, for the profile pairs, EE busy samples / frames in the 40 s window.

- **What leaves the inline RAM window** (`KZ_MEMSTATS=1`: every 10 s the loads/stores that reach `PS2Runtime::Load*/Store*`,
  by class and the 40 hottest addresses). Gameplay window, 4 windows of a 235 s run: 7.9-9.0 M accesses/s (with the stats
  mutex, ~50 fps), of which **99.93-99.96 % scratchpad** (0x70000000..0x70003FFF: the guest's stack and hot working set;
  hottest 0x70003D3C, then 0x70000000..0x7000006F and 0x700002AC..0x700002BC, 8-bit loads at 0x70000220..223; the 40 hottest
  addresses are 32-47 % of the accesses, and of those 82-94 % are loads (62-73 % 32-bit, 8-32 % 8-bit) and 6-18 % 32-bit
  stores). Everything else together is < 0.06 %: DMA channel registers 0.03-0.05 % (~4 k/s),
  GS privileged registers 0.009 % (~730/s), timers 0.003 % (~250/s), VIF/DMAC control 0.001 %. So there is no hot IO register
  worth an array or a hash-free dispatch: `m_ioRegisters` lookups are ~5 k/s in total. (Accesses that take the out-of-line
  `ps2CgRd*Slow/Wr*Slow` but are not special, i.e. kseg0/uncached RAM mirrors, cannot be counted from the runtime; the
  `ps2CgWr32Slow` profile entries are the scratchpad stores.)
- **Completed-DMAC drain (the big one).** `PS2Runtime::Store32` ran `drainCompletedDmacHandlers` after every special-address
  store, and `consumeCompletedDmacCauses` took `m_completedDmacMutex` twice each time (at least 0.5 M scratchpad stores/s):
  `PS2Runtime::Store32 -> consumeCompletedDmacCauses` was **5.2 % of the EE thread's busy samples** (Mtx_lock/unlock 2.2/2.1 %,
  SRW lock acquire/release 1.6/1.7 % self). Now a global `g_ps2CompletedDmacPending` (set by `queueCompletedDmacCause`, cleared
  by the consumer, both under the mutex) lets the consumer return without the lock while nothing is queued
  (`PS2X_DMAC_DRAIN_FAST=0` = old). After: no lock entry above 0.1 % on the EE thread. A cause queued at the moment of the check is
  drained by the next store or scheduler event, as if the drain had run a moment earlier.
- **Scratchpad fast path in `PS2Runtime::Load*/Store*`.** Aligned accesses inside 0x70000000..0x70003FFF go straight to the host
  scratchpad pointer (`PS2X_SPR_FAST=0` = old path through `PS2Memory::read*/write*`: GS-priv test, scratchpad test twice,
  `translateAddress`, `loadScalar` range checks). Profile self shares (concurrent pair, same window): `PS2Memory::read32` 1.4 %
  -> 0, `translateAddress` 0.5 -> 0.1, `write32` 0.4 -> 0, `Load32` 0.6 -> 1.2, `Store32` 0.3 -> 0.7 (the work moved into
  them). Alone it is below the noise of a paired run (EE ms/frame -1.08, +0.41, +0.01 for `PS2X_SPR_FAST=0` -> on).
- **Together** (`PS2X_SPR_FAST=0 PS2X_DMAC_DRAIN_FAST=0` -> defaults), 3 concurrent pairs with the sched-stats metric: EE ms/frame
  15.46 / 13.38 / 14.53 -> 14.15 / 12.24 / 12.92 (median -1.31 ms, -9 %; 3 of 3 pairs lower), fps 55.2 / 62.4 / 57.3 ->
  55.6 / 62.0 / 57.3. 3 more pairs with the profiler on, made when the pooled-memcpy chain copy below was still switched on (EE busy ms/frame): 11.37 / 13.28 / 12.57 -> 9.96 / 11.23 / 11.86
  (median -1.41 ms), fps 64.3 / 59.3 / 57.4 -> 66.6 / 64.7 / 60.4. The EE thread sleeps more (pacing sleep 147-167 ->
  213-259 ms/s in the first set) but fps only moves when frames cross a vblank boundary, see the patch 0019 section above.
  Drain alone with the scratchpad path on (`PS2X_DMAC_DRAIN_FAST=0` only): 14.52 / 14.51 / 13.64 -> n/a (boot flake) / 14.35 /
  13.63, i.e. not resolvable on its own; the profile share above is the direct evidence for it.
- **DMA chain snapshot copy** (`KZ_CHAINSTATS=1`; per VIF1 kick in gameplay: 2.86 MB in ~3 600 segments: REF (tag id 3)
  2.06 MB in 1 807 segments of ~1.1 KB, RET (id 6) 0.73 MB in 880, CNT (id 1) 0.18 MB in 921, tag words 8 B each).
  The walk costs 0.55-0.8 ms per kick (`PS2X_VIF1_STATS=1` walk time / jobs; 3-4 % of the EE thread). What did **not** help (each measured as walk ms per job in concurrent runs, or EE ms/frame):
  a pre-sized pooled buffer written with `memcpy` and no zero-fill (0.668 -> 0.660 and 1.117 -> 1.020 ms/job; EE ms/frame
  12.18 / 11.24 / 12.96 -> 12.52 / 11.80 / 12.70), non-temporal AVX2 stores (0.72-0.87 vs 0.71-0.82), a two-phase walk
  that collects the segments and copies them with software prefetch 3 segments ahead (0.79 vs 0.67, worse), both together
  (0.90). Skipping only the copy of REF segments (measurement hack, the pooled buffer then holds an older chain) took the
  walk from 0.533 to 0.211 ms/job, so REF is ~60 % of the walk and the rest is tag parsing plus the small hot segments. The
  copy is memory bound, and none of the kept switches changes it: nothing was kept.
- **Zero-copy is not free of hazards** (`PS2X_CHAIN_VERIFY=1`: the walk registers each segment's source pointer; when the job's
  buffer is recycled the copy is compared with the live source). IPU off, boot + menus + gameplay, 235 s: 24 670 jobs, **0
  with a changed source**, kick-to-done latency avg 0.1-12 ms (max 209 ms under load). IPU on (movies), 10 013 jobs: 170 jobs
  (1.7 %) had changed sources, 3 737 REF segments / 107 MB in total, all REF (tag id 3), none in CNT/RET/REFE: the FMV player
  overwrites a frame buffer that is still referenced by an unfinished job. So handing the worker pointers into RDRAM would
  corrupt movie frames unless the movie path waits for completion, and `PS2Memory::processVIF1Data` needs one contiguous
  chain (an UNPACK command in a CNT segment and its data in the next REF segment straddle segment borders; only DIRECT
  continuation is resumable), so a scatter list also needs interpreter work. Neither was attempted.
- **Inline scratchpad window in `ps2_cg.h`: prototyped, no gain, not adopted.** After the fixes above the guest scratchpad
  accesses still go through `ps2CgRd32Slow`/`ps2CgWr32Slow`/... (noinline, `isSpecialAddress`) and `PS2Runtime::Load*/Store*`:
  6.7 % of the EE thread's busy samples in one gameplay profile (`ps2CgWr32Slow` 1.1, `Load32` 1.2, `ps2CgWr128Slow` 1.0,
  `Store32` 0.7, `ps2CgRd32Slow` 0.6, `ps2CgRd128Slow` 0.4, the rest < 0.4). A second inline window
  `(a - 0x70000000) <= 0x4000 - size` with an alignment check, reading `ps2GetScratchpadHostPtr()`, added to `PS2CG_RD/PS2CG_WR`
  and `ps2CgRd128/Wr128` (a private copy of `ps2_cg.h` placed in a copy of `generated/`, whose quoted include wins over the
  runtime's, built in its own build dir so nobody's incremental build was touched;
  `patches/ps2recomp/experimental/ps2_cg-scratchpad-window.diff`) takes those entries down to 1.7 % (`ps2CgWr32Slow` 0.7 and
  `ps2CgWr128Slow` 0.7 remain: stores that are neither in the RAM window nor scratchpad, presumably kseg0/uncached RAM
  aliases, which the runtime cannot count). But the paired EE ms/frame did not move: prototype vs current build, 3 concurrent
  pairs, 12.79 / 11.17 / 10.89 -> 12.16 / 11.78 / 11.04 (differences -0.63, +0.61, +0.15 ms, fps 64.1 / 73.6 / 71.4 -> 68.5 /
  71.1 / 71.8), and a profiled pair 11.36 -> 11.99 ms/frame (fps 69.8 vs 68.2). The extra branches in ~100 k generated
  functions cost about what the calls saved; a full rebuild for this is not worth it.
- **What is left on the EE thread in this area.** The guest-code side (about 80 % of the thread) and the scheduler/VU0 work
  owned elsewhere. The remaining memory/IO items are each below 1.5 % of the EE thread: chain walk + snapshot (3-4 %, memory
  bound, see above), the two slow-path store entries above (1.4 %), `PS2Runtime::Load32/Store32` themselves (1.2 / 0.7 %).
  Worker-side note from the same runs (`PS2X_VIF1_STATS=1`): the VIF1 worker is busy 4.2-5.2 s per 10 s at 45-55 fps
  (about 10 ms per job, of which VU1 ~8 ms), so at 120 fps the worker, not only the EE thread, is a limit.

## Guest control flow without the scheduler (2026-09-30, patch 0023)
The follow-up to "EE thread frame budget": ~1 M scheduler dispatches per second in gameplay, each ~50 ns plus a table probe and a
cold switch at the target. Counted first, then removed. All numbers: headless, `KZ_FPS=120`, scripted input, gameplay window
t=150..200 s, on a machine shared with other builds and game instances (other agents' regens and 3-5 game runs at the same time), so
absolute frame rates are 56-64 and only concurrent pairs compare.
- **Why each dispatch happened** (`PS2X_SCHED_STATS=2`, `[sched-stats] dispatch reasons`, classified in `EeScheduler::run` from how the
  previous guest entry ended; mean of the five 10 s windows, per second and per displayed frame):

  | build | dispatches/s | per frame | scheduler-originated | yield | return-resume | jump | tail jumps followed | local recursive returns |
  |---|---|---|---|---|---|---|---|---|
  | before (generated code of `build\RelWithDebInfo`, instrumented runtime) | 922 657 | 16 041 | 364 | 166 | 365 255 | 556 872 | 0 | 0 |
  | call-site trampolines + local recursive returns (no scheduler follow, no folding) | 214 483 | 3 675 | 355 | 180 | 9 486 | 204 462 | 370 593 | 125 329 |
  | + scheduler follow, no folding (`PS2X_CODEGEN_FOLD=0`; this run at 73 fps on a quiet machine) | 9 947 | 136 | 358 | 198 | 9 391 | 0 | 726 864 | 158 136 |
  | final (+ scheduler follow + fragment folding) | 8 440 | 146 | 347 | 186 | 7 907 | 0 | 1 190 | 123 720 |

  *Yield* = a checkpoint gave control back (166-186/s). *Return-resume* = the guest returned through
  `$ra` to a call's return address after everything above it had been unwound. *Jump* = any other transfer: tail jump into another
  function or `entry_*` fragment, fall-through into the next fragment, indirect jump. Top pairs before: 0x17f8e0 and 0x185e20
  returning to themselves (self-recursive functions: `jal` to the own function is a `goto`, so its `jr $ra` left the function) 25-30 k/s
  each; fragment loops 0x401720/401750/4017a4 (18 k/s each), 0x3fb108/170/184/18c (13-19 k/s each), 0x3b0c48/6c (7-14 k/s); the
  pair 0x341648 -> 0x33cd84 (jump) and 0x33cd84 -> 0x341648 / 0x341674 (return-resume), 12 k/s each: the loop of `FUN_00341548`
  (virtual calls at 0x341640 and 0x34166c; none of these pairs is left after the change);
  the flag-wait loop `FUN_0014fd90` (0x14fe28/38/54/c0/c8: 7-28 k/s each in gameplay windows, 400-700 k/s each while the game
  waits in the menu, because after one yield the function table resumed it in a nested `entry_*` copy of its tail and the loop
  never came back into the function). The 7.9 k/s return-resumes left follow the yields: 7.9 k / 186 = ~42 unwound frames resumed
  per yield (top pairs after: 0x2e67e4 -> 0x2e6944 -> 0x2ed824 -> 0x2e67e4, 1.0-1.2 k/s each).
- **Returns.** A callee that returns normally (`pc == fall`) stays on the direct path (ps2CgCall / ps2DspCall: budget decrement,
  table call, one compare); verified by reading the code and by the counts: no scheduler dispatch in either build is a normal return.
  Return-resumes only follow yields and tail jumps.
- **What changed in the generated code** (details in patches/README.md, 0023): tail jumps set `ctx->pc = target | 1` and return; the
  caller loops (`ps2DspAfterCallSlow`, and the scheduler for the outermost frame) instead of unwinding; `jr $ra` back into a
  self-recursive function's own return labels is a `goto`; 56 212 `entry_*` blocks nested inside real functions and 5 695 chained
  standalone blocks are folded into their functions (82 652 -> 20 745 functions/files, `killzone.exe` 102.2 -> 74.1 MB,
  compile CPU 6 864 -> 3 543 s); the function table maps every old block address to the owning function, whose resume switch has
  a case for it; the switch is skipped when a call enters at the function start.
- **fps and EE ms/frame** (concurrent pairs of two build dirs made from the same runtime sources: `dsp0` = the generated code of
  `generated/` before this patch, `dsp2` = after; `PS2X_SCHED_STATS=3` in both, EE ms/frame = (1000 ms - pacing sleep) / fps as in
  perf_metric.py; the machine was quieter than for the counts, 70-75 fps):

  | pair | fps before / after | EE ms/frame before / after | change |
  |---|---|---|---|
  | 1 | 73.6 / 73.8 | 10.38 / 10.06 | -0.32 |
  | 2 | 75.1 / 73.8 | 9.85 / 9.56 | -0.29 |
  | 3 | 71.6 / 71.8 | 10.83 / 10.24 | -0.59 |
  | 4 | 69.8 / 70.0 | 11.45 / 11.14 | -0.31 |

  Second set, three builds run at once for 4 rounds (before = `dsp0`, final = `dsp2`, no folding = `dsp3`, `PS2X_CODEGEN_FOLD=0`;
  all three relinked from the same runtime sources right before):

  | round | fps before / final / no folding | EE ms/frame before / final / no folding |
  |---|---|---|
  | 1 | 69.3 / 70.3 / 71.2 | 11.01 / 10.68 / 10.61 |
  | 2 | 66.5 / 66.9 / 64.9 | 11.96 / 11.28 / 11.93 |
  | 3 | 65.7 / 67.9 / 66.7 | 12.04 / 11.28 / 11.29 |
  | 4 | 68.8 / 70.7 / 72.1 | 11.81 / 11.23 / 10.92 |

  Median EE ms/frame 11.89 / 11.26 / 11.11 (final -0.63 ms = -5 %). Folding is not measurably faster than the trampolines alone
  (the rounds differ by more than the two builds do, +-0.3 ms); what it removes is the 0.73 M followed tail jumps per second, and it is kept
  on for the code size (-27 % exe), compile time (-48 % CPU) and because a resume can no longer land in a fragment that loops
  through the scheduler. Over all 8 comparisons of the final build with the old code: -0.32 / -0.29 / -0.59 / -0.31 (first set) and
  -0.33 / -0.68 / -0.76 / -0.58 ms (second), median -0.45 ms (-4 %); fps moves by 0-2 (the frame rate steps with the vblank).

  First set: median fps 72.6 -> 72.8 (no change: the frame list is kicked at a vblank, so the frame rate only moves when a frame crosses a
  vblank boundary; docs/findings.md "EE thread frame budget"), EE ms/frame median -0.31 ms, mean -0.38 ms (-3 %). Profile of the EE
  thread without stats (KZ_PROFILE=160,40, one pair of runs): `EeScheduler::run` self 3.6 % -> 0.1 %. What is left in the profile is
  spread thinly (no guest function above ~1.2 %) plus the memory paths outside the RAM window (`ps2CgWr32Slow`, `ps2CgRd32Slow`,
  `ps2CgWr128Slow`, `PS2Runtime::Load32/Store32`, `writeIORegister`, ~5-6 % of the samples; patch 0020 works on those) and `kzvu0Call` /
  `Vu0Execute` / `mVUexecute<0>` (~5 %). 30 % of the EE thread's samples in the earlier profile were the vblank pacing sleep.
  Not used: a first set of pairs against `build\RelWithDebInfo` with `PS2X_SCHED_STATS=1` showed -0.67 ms median; that binary predates
  patches 0020 and 0021 and `=1` costs ~40 ns per dispatch (host clock read + rdtsc), ~4.5 % of the EE thread in the baseline's
  profile (`RtlQueryPerformanceCounter` 2.9 %, `schedStatsReport` 1.5 %) and nothing in the new build, so it flattered the patch.
  `PS2X_SCHED_STATS=3` (pacing sleep only) exists for this reason. Getting under the 8.33 ms vblank period needs the guest code
  itself to get ~20-25 % cheaper (EE 10-11 ms here) and the VIF1 worker to fit as well; dispatch removal is worth 0.3-0.6 ms.
- **Correctness evidence.**
  - `PS2X_CODEGEN_DSP=0 PS2X_CODEGEN_FOLD=0` regenerates the patch-0017 output byte for byte (82 652 files, 0 differences).
  - `tools/scripts/verify_resume_table.py` on the folded tree: 144 623 table entries, the same addresses as before (0 lost, 0 new), 123 881
    of them inside a function that starts elsewhere, every one has a resume `case` in that function, no `goto` without its label.
    (A first version lost 58 code-pointer entries that had been registered under removed blocks; found by diffing the tables.)
  - Headless gameplay runs of the final build (one `=2` run, 7 runs of the pairs, 2 profiled runs): menu -> profile -> level -> difficulty -> character -> mission,
    HUD, weapon, explosions, mission-failed screen at the end, in the same order and looking like the baseline (frames compared side by
    side: work/dsp_c_hist vs work/dsp_b0_hist); no `guest-branch` / `sched-trace` / `[error]` line in any run, `missing tail targets=0`
    in all 23 windows of every run.
  - The `PS2X_CODEGEN_FOLD=0` variant (`dsp3`) passed the same checks in its `=2` run and the 4 rounds above (no error line, no missing tail target).
  - With `KZ_IPU` unset (movies): Guerrilla logo, intro cinematic, menu with the movie background, mission (work/dspChk_ipu).
  - `PS2X_STRESS_YIELD=97` (a checkpoint yield every 97th check, unwinding and resuming the whole stack at every kind of label):
    the final build reaches the main menu in 9 of 9 boots (3 in pairs with the baseline, run to t=100 s: also the campaign submenu
    after the scripted Start press; 6 as six concurrent boots next to 6 baseline boots, run to t=75 s), the baseline in 9 of 9. A tenth boot of the final build, the very
    first stress run (started together with a regen and a movie run), showed the known `[IOP] module arena exhausted` MCSERV.IRX loop
    (276 079 lines, stuck at `sceSifLoadModule`; "Boot flake" above: 1 in 30-40 boots under load, always with several instances
    booting); it is IOP module loading, but 1 of 10 against 0 of 9 is too few runs to rule out a link to this change.

## VIF1 worker hot path (2026-09-30, patch 0022)

The VIF1/VU1/GIF worker needed 9-10 ms per frame (one job per frame) against an 8.33 ms vblank period at 120 Hz. What the game feeds it
per frame, from `PS2X_DMA_STATS=1` (52-58 frames/s under load, 500-550 chains per 10 s): ~4000 XGKICK packets (average 1.2 KB, ~5 MB),
~415 DIRECT (1.75 MB of PATH2), ~6200 UNPACK, ~900 MSCAL, ~80 VU1 code changes (MPG: 4000-5000 a second). UNPACK formats by count: V4-32 45 %,
V2-16 16 %, V3-8 9 %, S-16 and S-8 8 % each, V4-8 7 %, V3-16 and V4-5 5 % each, V4-16 4 %; 15 % are masked, none is in fill mode (WL <= CL),
and STMOD is never written (mode 0 always).

**Method.** `tools/scripts/vif_run.ps1` / `vif_pairs.ps1` / `vif_metric.py` (new): the task's scene with `PS2X_VIF1_STATS=1`, concurrent pairs of
the same binary with env switches, run from a snapshot dir (`mkrun.ps1`; the build can relink meanwhile: the exe is linked with
`/PDBALTPATH:%_PDB%`, set as `-DCMAKE_EXE_LINKER_FLAGS` in the `build\pd` cache, so a run does not hold the build's PDB). Metric: worker ms per
job (= per frame) over t=150..200 s, next to the vif-counter fps. The worker time is what `[vif1-thread]` reports; it includes waits for
the GS thread (ring space, frame throttle). `KZ_VU_STATS=1` was itself expensive (it walked all 2048 program lists on every MSCAL: 12 % of the
worker; now it only reads two counters on the hot path and walks the lists when it prints), so the numbers below are from runs without it
unless stated.

**Where the time went** (worker profile, `KZ_PROFILE=160,40`; JIT frames break the stack walk, so inclusive shares are approximate).
- XGKICK -> GS hand-off: a packet was copied VU memory -> kzvu buffer (`std::vector::insert`) -> arbiter queue (heap vector) -> kzgs ring:
  three copies, an allocation and a free, a sort pass and an atomic notify (`WakeByAddress`) per packet; ~18 % of the worker.
- **microVU "compile" was not compilation.** `mVUcompileJIT<1>` (17 % inclusive, 12.7 % self) is the routine that JIT code calls for every
  JR/JALR. microVU treats an indirect jump target as a new program start, so after each micro-memory change every program entry (an MSCAL or a JR
  target) searches its program list again, comparing every range each candidate was compiled from with the current micro memory. Counted with a
  diagnostic build (memcmp wrapper): 15 M `memcmp` calls per second, 17 bytes on average, ~270 TSC cycles each (cache misses on the candidates'
  16 KB code copies), 3.7-4.1 G TSC cycles per 10 s. That stayed the same from t=150 to t=220 s while the code emitted fell from 4.5 MB to
  0.1-0.4 MB per 10 s. New programs per 10 s, t=130..220: 311, 814, 698, 272, 239, 66, 55, 14, 314, 610: the level-start burst decays
  within ~60 s to 15-60 per 10 s, and new content (t=210-220 here) starts another. Real compilation is a burst cost, the search was the steady
  one. (This corrects the "first-use compile cost" reading under "VU1 recompiles" above; the counts there are right.)
  Two things that did not help: an inline SSE range compare instead of the CRT `memcmp` (same TSC per call, cache-miss bound; worker
  14.0 / 14.5 -> 14.3 / 14.8 ms in two pairs), and treating JR/JALR as part of the same program (`doJumpAsSameProgram`; memcmp calls
  unchanged, worker 14.05 -> 14.64 ms). Both were removed again.
- `vif1UnpackPcsx2` 6 % (the masked UNPACKs went through the per-element loop), `forwardVif1DirectData` 4.6 %, `processVIF1Data` itself ~2 %.

**Changes and measurements.** Same binary, env switches, worker ms per job, concurrent pairs, t=150..200 s; the machine is shared, the first
pair of each series was the quietest.
- Hand-off (`PS2X_GIF_DIRECT`, `KZVU_XGKICK_INPLACE`, `KZGS_NOTIFY_ALWAYS`; plus a cheaper `packetWritesGsEvent` and plain load+store
  counters in `onGifPacket`, no switch): 3 pairs 14.01 / 11.80 / 14.12 -> 12.78 / 10.38 / 12.23 ms (median -13 %), fps 54.9 / 58.7 / 55.6 ->
  57.9 / 59.8 / 57.9. A packet is now copied once (VU memory -> kzgs ring).
- Code-state memo (`KZVU_STATE_MEMO`): first version (512 states, full content compare) 2 pairs 12.19 / 12.85 -> 9.74 / 11.98 ms (-20 %, -7 %).
  Final version (unbounded, 128-bit content key): `memcmp` calls 15 M -> 1.3-2.6 M per 10 s; 85-90 % of the ~50 k code changes per 10 s find
  their table, 3-10 % are new contents, the rest have been compiled into since (the saved table is dropped). Memo cost 0.45 G TSC cycles per
  10 s (1.2 G before the AES hash and the single pass over the 2048 slots).
- Masked UNPACK SSE path (`PS2X_VIF_UNPACK_MASKED_FAST`): 2 pairs (a third one was lost to a rebuild) 8.15 / 9.26 -> 7.98 / 9.03 ms (-2 %).
- **All together** (`PS2X_GIF_DIRECT=0 KZVU_XGKICK_INPLACE=0 KZGS_NOTIFY_ALWAYS=1 KZVU_STATE_MEMO=0 PS2X_VIF_UNPACK_MASKED_FAST=0` against the
  defaults, series `g1`, 3 concurrent pairs): worker 7.82 / 12.86 / 10.07 -> 6.20 / 9.82 / 7.74 ms per frame (-21 %, -24 %, -23 %; median
  10.07 -> 7.74), fps 68.4 / 58.1 / 65.9 -> 70.8 / 59.0 / 67.7 (median 65.9 -> 67.7). Without the stats (`g2`, 2 pairs) fps 67.4 / 72.9 ->
  72.5 / 70.4: not resolvable, the run-to-run spread is +-10 %. One quiet run of the final build (`p3`): 6.29 ms worker per frame, 74.8 fps.

**What limits fps now.** The worker is no longer the limiter (6.2-7.7 ms per frame, 45 % busy at 75 fps). Threads in the `p3` profile (t=160..200,
74.8 fps): EE thread 65 % busy (~8.7 ms per frame); GS thread (PCSX2's hardware renderer: `GSState::GIFPackedRegHandler*`, `GSRendererHW::Draw`, D3D11)
46.7 % busy (~6.2 ms per frame; 57.6 % at 69 fps in another run, ~8.4 ms); worker 45 %. 120 fps needs all three under 8.33 ms. The EE thread
is over it, and the GS thread is close. The worker's remaining time is thin: `mVUcompileJIT<1>` self 9 % of its busy samples (~0.6 ms per frame:
the JR/JALR call and its jump-cache lookups), `Push` 6.6 % (~0.4 ms, the ring memcpy of ~5 MB per frame), the spin-wait between jobs 6 %,
memo 2.7 %, UNPACK/DIRECT ~4 %, and the VU1 code itself as many unresolved JIT addresses of at most ~1.5 % each.

**Correctness evidence.** One 235 s run with `KZVU_XGKICK_CHECK=1 KZVU_STATE_MEMO_VERIFY=1 PS2X_VIF_UNPACK_CHECK=4 PS2X_GIF_SCAN_CHECK=1`
(`chk1`): 23.0 M XGKICK transfers compared with a second `Gif_Tag` walk (packet boundaries and unfinished tail), 0 differences; 9.6 M UNPACKs
run both ways (masked SSE path included; VU1 memory and VIF registers compared), 0 differences; every saved and every restored memo entry
(~500 k code changes) checked with microVU's own range comparison against the content it belongs to, 0 bad (also in an earlier 235 s run);
the fast A+D scan against the plain loop on every packet, 0 differences (and 3 M random packets in a standalone test, 945 k of them with a
SIGNAL/FINISH/LABEL write, 0 differences). GIF ordering: the direct emit only happens when the arbiter queue is empty and the packet is
PATH1/PATH2. There the drain comparator puts it first among anything submitted before the next drain (a PATH3 IMAGE never sorts ahead of PATH1, and a
PATH2 is drained at its own submit), so the emitted order is the old one; with a PATH3 packet queued (job start) everything queues as before. kzvu
tests: 1776 checks (575 + the new memo test), 0 failures, also with `KZVU_STATE_MEMO=0` and `KZVU_STATE_MEMO_VERIFY=1`; kzvu0_test 509 checks.
Frames of the final build (`ipu1` with `KZ_IPU` unset, `v1_b1`, `chk1`): intro movie, menus with the movie backgrounds, loading, level, weapon,
HUD, explosions, death screen, as in the old build. A byte comparison of the GIF stream between old and new switches (`KZ_GS_HASH=1`, 130 s)
is not possible: the game's timing makes almost every frame differ between any two runs (308 of 15598 frame hashes equal at the same frame
number, 597 of ~14.8 k distinct hashes shared).
Runs note: both runs of one pair and one run of another died when a build re-staged `build\pd\resources` under them (their run dirs used a junction
to it); `mkrun.ps1` copies it now.

## clang-cl build (2026-09-30)

Opt-in second toolchain: LLVM 23.1.2 `clang-cl` (`D:\LLVM\bin`) with `lld-link`, same sources, same options. MSVC stays the default (`tools\scripts\build.bat`).
`tools\scripts\build_clang.ps1 [-Name clang] [-Cmake '<extra cmake args>']` configures and builds `build\<Name>`; it reuses `build\RelWithDebInfo`'s FFmpeg prefix,
shader cache and FetchContent sources, so nothing is downloaded and nothing is written to C:.

**What had to change** (no behaviour change, no `generated/` or `ext/PS2Recomp` edit, so there is no `patches/ps2recomp/0024-*.patch`):
- `CMakeLists.txt`: a `KZ_CLANG_CL` branch of the RelWithDebInfo flags: `/Zi /O2 /Oi /Gy /Gw /GS- /clang:-march=x86-64-v3 /clang:-ffp-contract=off /DNDEBUG`
  (`/O2` is `-O3` in clang-cl; no `/Ob3`, no `/Qspectre-`, no `/arch:AVX2` because `-march=x86-64-v3` covers it). **Never `/fp:fast`.** `-ffp-contract=off` is
  explicit because clang-cl defaults to `-ffp-contract=on`, which would fuse `a*b+c` into FMA (x86-64-v3 has it) and change PS2 float results. The kz* libraries
  keep their own `/fp:contract /arch:AVX2` (PCSX2 code, as under MSVC). Checked in the object code: 0 `vfmadd*` in 8 sampled generated unity objects
  (hundreds of scalar `vmulss/vaddss/vsubss` in them); every compile line of the build carries `-ffp-contract=off`.
- `ext/kzvu/shim/unity/KzvuMicroVU.cpp`: `memoHash` uses `_mm_aesenc_si128`; MSVC accepts it anywhere, clang needs the target feature, so the function has
  `__attribute__((target("aes")))` under `__clang__`. That was the **only** compile error in the whole tree (runtime, IOP, kzgs, kzvu, kzipu, kzspu2, src/, 20 743
  generated files, PCH + unity as configured). The `(__m128i)(x)` casts in the generated code and macros compile as they are; the MSVC-only guards (`if(MSVC)`)
  are true for clang-cl. Warnings only: `-Wdeprecated-declarations` (fopen/strerror), the same as MSVC's C4996.
- Aliasing: **`-fno-strict-aliasing` is not needed on the command line, because clang-cl already implies it.** It passes `-relaxed-aliasing` and `-fwrapv` by default (MSVC
  compatibility), so the typed `*(uint32_t*)(rdram+a)` / `*(uint64_t*)` guest memory accesses of `ps2_cg.h` (a 32-bit store followed by a 64-bit load of the same guest address
  would be a TBAA violation) keep MSVC's semantics. Strict aliasing was not tried; it would need `may_alias` access types first.
- Opt-in extras in `CMakeLists.txt`: `-DKZ_CLANG_LTO=thin|full` (also pass `-DCMAKE_AR=<llvm>/bin/llvm-lib.exe`, MSVC's `lib.exe` does not index bitcode),
  `-DKZ_CLANG_PGO_GEN=ON` (instrumented; `src/kz_main.cpp` calls `__llvm_profile_write_file()` before its `std::_Exit`, which skips the runtime's atexit writer; set
  `LLVM_PROFILE_FILE=<dir>\kz_%p.profraw`, then `llvm-profdata merge -output=kz.profdata *.profraw`), `-DKZ_CLANG_PGO_USE=<file.profdata>`.
- `tools/scripts/vif_run.ps1` bug found on the way: it set `KZ_PROFILE` to the empty string when not profiling, and an empty variable still counts as set (kz_main.cpp
  `getenv("KZ_PROFILE")`), so every such run had the 20 s sampling profiler running from t=0. It now removes the variable. That profiler is what crashed twice (below).
  `perf_run.ps1` has the same line and was left alone.

**Build cost.** Clang, all 532 objects (generated code in 163 unity batches, PCH), `-j12`: 100 s wall, 1574 CPU-s (MSVC: "compile CPU 3 543 s" for the generated code alone,
~15 min, see "Guest control flow without the scheduler"). The thin-LTO and PGO+LTO builds take the same wall time (99 / 97 s), the code generation moves into the link. Two builds
of the same tree have the same size to the byte (87 265 792). `killzone.exe`: MSVC 74.1 MB, clang 87.3 MB, +LTO 82.9, +PGO 81.2, +PGO+LTO 79.6.

**Correctness** (all runs headless, `KZ_FPS=120`, scripted input as in the perf runs):
- Boot, menus, intro movie (`KZ_IPU` unset, 150 s, frames every 5 s: Sony/Guerrilla logos, the FMV; `f_ipu_clang`, `f_ipu_msvc`, `f2_ipu_pgolto`), level, weapon,
  HUD, explosions, "mission objective" text, death and FAILED screen: the same scenes as the MSVC build, in every build variant (frame montages; not byte comparable, the timing
  differs between any two runs).
- `PS2X_STRESS_YIELD=97` boots to the menu (clang 120 s, PGO+LTO 100 s) with `KZ_CRASH_TRACE=1`, no crash.
- `KZ_VU0_VERIFY=8 PS2X_LAZY_TIMERS=verify`, 235 s gameplay: clang 5.11 M VU0 calls, PGO 5.33 M, LTO 5.72 M, PGO+LTO 5.45 M checked, **0 mismatches** each; timer shadow 80-93 k
  comparisons, 0 mismatches, 214 M early returns checked each. (Both paths of the VU0 check are clang-compiled: this shows they agree, not that they equal MSVC's results.)
- No `missing` / `exception` / `no function` line in the stderr of any of the runs listed here (`unhandled import cdvdman:78` is in the MSVC log too).
- Tests built with clang-cl in `build\clang_tests` (`-DKZVU_BUILD_TEST=ON -DKZGS_BUILD_TEST=ON -DKZIPU_BUILD_TEST=ON -DKZSPU2_BUILD_TEST=ON`): kzvu_test 1776 checks, 0 failures (as in patch 0022);
  kzvu0_test 509 checks, 0 failures (103 captures, JIT != in-game 0, JIT != PCSX2 interpreter 0); kzgs_test PASS (0 failures); kzipu_test PASS (min 55.6 dB PSNR); kzspu2_test PASS.
- **A crash, but not in the game:** 2 of 44 runs with the sampling profiler on (the `vif_run.ps1` bug above) died with 0xC0000005 after 10-35 s. The one with `KZ_CRASH_TRACE=1` died in
  the `kz_profiler.cpp` thread inside `RtlVirtualUnwind` (unwinding another thread's stack), the other had no trace. 0 crashes in the ~50 runs without the profiler (20 x 30 s boots, ~30 x 235 s,
  verify and stress runs). Whether an MSVC build dies the same way under this profiler was not measured. The profiler (`KZ_PROFILE`) is a diagnostic, not part of the game.

**Speed.** `PS2X_SCHED_STATS=3`, the task's input script, `KZ_IPU=off`, `KZ_FPS=120`, `run_headless.ps1`, 235 s, fps = (vif counter at t=200 - at t=150) / 50, EE ms/frame =
(1000 - pacing sleep) / fps as in `perf_metric.py`, no other killzone.exe running. The machine's speed drifts a lot between hours (the MSVC build measured 7.4-8.4 ms/frame in the first
set and 5.4-5.8 ms in the second), so only runs interleaved in one series are comparable:

| series | build | fps (runs) | EE ms/frame (runs) | median fps | median EE ms |
|---|---|---|---|---|---|
| A: 3 alternating pairs | MSVC | 73.4 / 76.4 / 78.4 | 8.38 / 7.60 / 7.42 | 76.4 | 7.60 |
| | clang | 77.1 / 73.6 / 73.3 | 6.54 / 7.05 / 7.11 | 73.6 | 7.05 |
| B: 4 rounds x 5 builds, order rotated each round | MSVC | 82.8 / 78.0 / 77.7 / 77.7 | 5.41 / 5.82 / 5.80 / 5.78 | 77.8 | 5.79 |
| | clang | 79.2 / 79.4 / 77.7 / 77.3 | 5.06 / 5.03 / 5.09 / 5.08 | 78.5 | 5.07 |
| | clang + ThinLTO | 80.6 / 82.1 / 79.0 / 82.8 | 4.90 / 4.74 / 5.09 / 4.67 | 81.3 | 4.82 |
| | clang + PGO | 86.0 / 80.3 / 80.1 / 80.0 | 4.22 / 4.52 / 4.67 / 4.58 | 80.2 | 4.55 |
| | clang + PGO + ThinLTO | 80.3 / 77.5 / 77.8 / 77.7 | 4.53 / 4.68 / 4.72 / 4.61 | 77.8 | 4.64 |

Per-round difference to the MSVC run of the same round, EE ms/frame: clang -0.35 / -0.79 / -0.71 / -0.70 (median -0.71 ms, -12 %); ThinLTO -0.51 / -1.08 / -0.71 / -1.11 (-0.90, -16 %);
PGO -1.19 / -1.30 / -1.13 / -1.20 (-1.20, -21 %); PGO+LTO -0.88 / -1.14 / -1.08 / -1.17 (-1.11, -19 %). The PGO profile was trained on this same scenario (one 250 s run of the
instrumented build), so its number is the best case for this scene.
- **fps did not move measurably** (medians 77.8 MSVC; 78.5 / 81.3 / 80.2 / 77.8 for the four clang builds; the spread inside one build is 3-6 fps): at 5-6 ms per frame the EE thread is
  no longer the limit, the pacing sleep is 55-64 % of the second. The VIF1 worker (`PS2X_VIF1_STATS=1`, `vif_run.ps1`, 3 rounds, worker ms per frame): MSVC 5.53 / 5.51 / 5.46,
  clang 5.23 / 5.30 / 5.47, PGO 5.35 / 5.45 / 5.27 (medians 5.51 / 5.30 / 5.35, -3 %): it is mostly microVU-generated code, which no host compiler changes. fps in those runs: 77.1 / 79.9 / 78.8 MSVC,
  78.0 / 82.9 / 79.8 clang, 82.4 / 78.2 / 82.2 PGO. So the toolchain shaves the EE part (guest code plus runtime), which any later register-locals/fiber work builds on, but
  120 fps also needs the worker, the GS thread and the frame-list hand-off ("What limits fps now" above) under 8.33 ms.

## Frame pipeline timeline (2026-09-30, patch 0025)

Question: gameplay at `KZ_FPS=120` ran ~80 fps although every stage fits in one 8.33 ms vblank (EE ~5.5 ms, VIF1/VU1/GIF worker ~4.5 ms,
GS thread ~3.3 ms), i.e. frames took ~1.5 vblanks. Answer: nothing in the EE -> D1 -> worker -> GS hand-off serialised them. The EE
thread's *estimated cycle clock* did (details and the fix below): 81.8 -> 118.1 fps (medians of 3 interleaved runs each).

**Recorder.** `KZ_TIMELINE=<file.csv>` (+ `KZ_TL_START`/`KZ_TL_END`, default 160..200 s of run time) writes one CSV row per event, QPC ns
clock (`include/runtime/ps2_timeline.h`); `tools/scripts/tl_analyze.py <csv> [--frames N [K] | --dump T0_MS D_MS]` makes the tables below.
The event list is in patches/README.md (0025). Runs: the task scene (`KZ_FPS=120 KZ_IPU=off`, scripted input), 235 s, build `build\tl`
(MSVC, RelWithDebInfo), snapshot with `mkrun.ps1`; `tl_run.ps1`, `tl_ab.ps1`, `tl_boots.ps1` drive them. The recorder costs nothing
measurable (81.4 fps in the first baseline run with it on, 79.0-80.8 fps in the A/B runs without it).

**What the game does per frame** (from the generated code, confirmed by the hooks): the EE builds a frame list; `FUN_001759c0` spins until
D1_CHCR.STR is clear (`FUN_00175948(0, 0x10009000, 0x100)`) and then sets the byte `0x55A795` (previous kick finished, the next may start);
the vblank handler `FUN_00152018` -> `FUN_00151fc8` kicks only when that byte is set *and* the next buffer's state byte
(`0x55A6E8 + (cur^1)`) is 2 (list finished); the kick `FUN_00150090` clears the byte, flips the buffer index and stores CHCR=0x145 (when D1 is
still busy it calls an object method with a message first, then kicks anyway); `FUN_001759f8` waits, with a timeout, until the byte is set.
So a frame list can only be kicked at a vblank.

**Baseline timeline** (`work/tl2.csv`, window t=160..200 s, 81.4 fps by the vif counter, 82.1 D1 kicks/s; 4800 vblanks = 120.0/s):

| candidate | measured | verdict |
|---|---|---|
| D1 completion reaches the EE late | kick -> worker job start 0.01 ms (median); job 4.49 ms (p90 5.48, max 11.5); kick -> completion posted 4.54 ms; posted -> applied on the EE 0.01 ms (max 0.27); no busy read of D1/D2 CHCR at all (0 `CHCR_BUSY` events), so the game's D1-idle spin never waits; completion later than one vblank after its kick in 3 of the 1432 frames that took >= 2 vblanks | not it |
| game waits for the previous frame's DMA | the line above: `FUN_001759c0` finds STR clear on its first read | not it |
| the kick only happens at a vblank | kick check at 4800 vblanks, (flag, next-buffer state): (1,2) 3208 = kick, **(1,0) 1554 = D1 free but the frame list not finished**, (0,*) 38 | the symptom: the EE's list is late for 32 % of the vblanks, see below |
| kzgs `maxQueuedFrames=2` | frames queued after each push: always 1; frame-throttle waits 0; ring-full waits 0 | not it |
| barriers | 2757 waits, 421 ms = 1.05 % of the window, all "VIF register write" in the vblank handler waiting for the 0.005 ms GS-half job | not it |
| present-skip logic | 4710 of 4800 vblanks present (the game flips DISPFB every vblank, so the registers differ); `GSvsync` 0.13 ms | not it |
| worker / GS capacity | worker busy 36.8 % (DMA jobs), GS thread busy 39.4 % = 3.3 ms per vblank | fits |
| EE thread | asleep in `processDueDeadlines` (pacing) 55.2 % of the window; idle waits (no runnable guest thread, i.e. blocked on a semaphore / event flag) 0.0 % | see below |

**Per-frame breakdown** (kick to kick; the game kicks once per frame, 0.38 ms after its vblank; "EE awake" = frame length minus the EE's
sleeps; ms):

| class | frames | length | EE awake | EE pacing sleep | D1 done after kick | worker job |
|---|---|---|---|---|---|---|
| 1 vblank | 1850 | 8.42 | 3.78 | 4.64 | 4.28 | 4.27 |
| 2 vblanks | 1409 | 16.66 | **7.40** | **9.26** | 4.77 | 4.76 |
| 3+ vblanks | 23 | 41.01 | 22.32 | 18.69 | 11.21 | 5.03 |

In 1292 of the 1432 frames that took two or more vblanks the EE's awake time was under one vblank (8.33 ms). Twelve consecutive frames from
t=19999 ms of the window: vblanks per frame 2 2 2 1 2 2 (19: a screenshot stall, below) 2 2 2 2 2 2, length 16-18 ms, EE awake 6.8-9.4 ms, D1 done 4.4-5.3 ms after the kick.

**Critical path** (one 2-vblank frame, `work/tl2.csv` frame 1002; ms after its kick; clock = the EE cycle clock relative to the kick):
```
0.000  D1_KICK (its vblank was 0.4 ms ago); the worker starts the job 0.005 ms later
3.473  EE goes to sleep, clock = +2 457 360 cycles: one vblank's cycles (2.46 M) are used up after 3.5 ms of host time
5.475  worker job done, completion posted; the EE is woken by the event, applies it, sleeps on
7.500  EE wakes at the host deadline of V1: VBLANK; kick check (flag 1, next buffer state 0): list not finished, no kick
7.650  another sleep, 0.9 ms (the VBlankEnd event, 0.5 ms of guest time after the vblank)
9.370  FUN_001bff10 entered, clock = +3 376 462 cycles
12.310 EE sleeps again, clock = V1 + 2.46 M: the second vblank's cycles are used up
16.444 VBLANK V2; kick check (1, 2); D1_KICK at 16.793
```
The worker (4.8 ms), its completion and the GS thread were done long before V1. The list was late because the EE thread's *guest cycle clock*
reached each vblank's cycle deadline early and the scheduler put the thread to sleep until the host deadline (`EeScheduler::processDueDeadlines`:
a scheduled event is due only when both its cycle deadline and its host deadline have passed). The cycles are an estimate charged per call /
back edge (`ps2_cg.h`) and run 2-2.4x faster than wall time here (one vblank's 2.46 M cycles in 3.5-4.2 ms of EE time). A frame whose estimated
work is over one vblank period's cycles (this one: 3.4 M; half the frames) takes two vblanks in the guest's time, however little host time it
needed, and the EE sleeps for the rest (55 % of the window). In this frame the EE was awake for ~4.5 ms up to the limiter; without the sleeps the list
would have been ready before V1 and kicked there. At 60 Hz the budget is 4.9 M cycles per vblank and every frame fits. So the critical path of
a frame is the EE's estimated cycles rounded up to whole vblanks, not a wait on D1, the worker, kzgs or the GS. This corrects "The guest cycle
estimate is not the limiter" in "EE thread frame budget": that test (a temporary `PS2X_EE_CYCLE_SCALE=0.5`) ran when the EE was the busiest
stage (~100 % busy), where halving the charge could not show.

**Fix: `PS2X_EE_CYCLE_SCALE`** (default `auto`, `=1` = old behaviour). `EeScheduler::checkpointDue` multiplies the cycles it charges by a
factor; `auto` = vblank period / 60 Hz period, at most 1 (0.5 at 120 Hz, 0.417 at 144, 0.25 at 240, 1.0 at <= 60 Hz, where nothing changes). The
game then gets the same estimated cycles per vblank as at its native 60 Hz (timers, IOP time and the iterations of guest spin loops per vblank are
what they are at 60 Hz), like the frame-timer and fade corrections in `kz_timing`; it is the EE cycle rate setting of PCSX2. The order of DMA, VIF,
GIF and GS accesses is decided by the guest program and the worker's FIFO, not by the clock, so the guest sees the same DMA/VIF/GS state; only
the amount of guest code that runs between two vblanks changes. The wall-clock floor of the clock stays (`accountCycles`).

**Measured.** fps = (vif counter at t=200 - at t=150) / 50, `tl_ab.ps1`: one instance at a time, order rotated per round, machine otherwise idle (CPU 8-12 %).

| variant | runs (fps) | median |
|---|---|---|
| `PS2X_EE_CYCLE_SCALE=1` (old), final binary (`r_tl3`) | 82.1 / 81.8 / 81.2 | **81.8** |
| default (auto = 0.5), final binary | 117.4 / 118.1 / 118.8 | **118.1** |
| `=1` (old), first set (`r_tl2`, before removing an always-on kzgs trace hook) | 79.0 / 80.8 / 79.8 | 79.8 |
| default (auto = 0.5), first set | 119.8 / 119.2 / 117.8 | 119.2 |
| 0.75, 0.6 (one run each, `r_tl2`) | 105.6, 116.6 | |

(Earlier old-clock runs of the same scene: 79.9 / 81.2 / 75.1 and 77.3 / 77.6 / 78.2, so the old clock spreads 75-82.) `KZ_FPS=240`, scale 0.25, one
run: 178.7 fps. Timeline with the fix (`work/pm_tl.csv`, 117.7 kicks/s): 4680 of 4707 frames take one vblank (the others: 6 with two kicks in
one vblank, 16 with two vblanks, 5 longer), kick check (1,0) 17 times (1554 before), EE awake 5.09 ms per frame (p90 7.2; over one vblank in 11
frames = 0.2 %), EE pacing sleep 40 %, D1 job 3.8 ms, GS thread 38.6 % busy (3.2 ms per vblank); consecutive frames are 7.3-9.3 ms long, one
vblank each. The two timelines are different runs of a non-deterministic scene (in the montages the fixed run reached the death screen earlier); the A/B table is
the comparison, the timelines explain it.

**Correctness.** 12 boots of 50 s, 3 at a time (`tl_boots.ps1`): all exit 0, `dma=` advances in every 10 s heartbeat (e.g. 1 -> 1438 -> 2930 ->
4289 -> 5610; the boots, frames, WAV and verify runs are on the build before the last trace-hook change, and 12 more boots on the final binary gave the same); the same at `KZ_FPS=60` (no scale applied: the log has no `[sched] EE cycle clock scale` line and the code path is the old
one), 144 (0.417) and 240 (0.25), 2 boots each; 2 boots of 100 s with `PS2X_STRESS_YIELD=97 KZ_CRASH_TRACE=1`; the only warning line is the
known `unhandled import cdvdman:78`. `KZ_VU0_VERIFY=8 PS2X_LAZY_TIMERS=verify`, 235 s gameplay (118 fps): 6.7 M VU0 calls checked, 101 753 timer
comparisons and 423 M early returns, 0 mismatches. Frames of the default vs the old clock (montages of every 10 s with `KZ_IPU=off`, every 5 s
with `KZ_IPU` unset): Sony / Guerrilla logos, the intro movie with its shots at the same timestamps, the menu video backgrounds, profile /
level / difficulty / character screens, the loading screen (further along at the same time with the faster clock), level, weapon, HUD, explosions
and smoke, death screen: the same, no flicker, missing geometry or new artifacts. SPU2 WAV over 130 s with `KZ_IPU` unset: 130.01 s (old) vs
130.00 s, per-10 s RMS and peaks equal (e.g. -15.5 / -15.3 dBFS, peaks 23 726 / 22 977 in both), same music (correlation 0.95 at a constant 5 ms
offset; the movie segment matches at a 383 ms start offset, correlation 0.97), `kz_audio` reports 1.000x real time. No `TIMED OUT` lines. The
game's simulation speed is unchanged: the vsync counter runs at 120.0/s in both and the frame timer counts vblank ticks; the scripted menu /
tutorial steps reach the same scenes at the same times. What the scale can change is guest code with a fixed-iteration timeout that waits for
something on host time (CD, IOP): it has less host time at 0.5. None was seen; loading screens, IOP RPC waits and the movie start passed in every run.

**What limits fps now.** At 120 Hz nothing in the pipeline: EE awake 5.1 ms, worker 3.8-4.5 ms and GS thread 3.2 ms per vblank average against
8.33 ms. The ~2 % missing to 120 are (a) the headless screenshot every 10 s (`KZ_SHOT_INTERVAL`): `saveShot` runs on the worker thread inside
`runVsync` and stalls the pipeline for 130-175 ms (four such jobs in every 40 s window, t=160/170/180/190 s; ~0.6 s = 1.5 %; it is in the old
numbers too), and (b) heavy frames, where the EE has the least headroom (awake p90 7.2 ms; 0.2 % of the frames are over one vblank). At 240 Hz
(4.17 ms vblank) the stages no longer fit: 2-vblank frames have EE awake 6.4 ms and a 5.0 ms worker job (178.7 fps). Further gains at higher rates
are the EE's guest code (clang-cl PGO took ~1.2 ms per frame off it, see "clang-cl build") and the VU1 side of the worker, not the hand-off.

## Play-test fixes: mouse yaw, button order, profile name, menu keys (2026-09-30)
User report on the packaged build: mouse look only moved vertically and felt smoothed, C fired, no way to type the profile
name, no keyboard input in menus. All four reproduced headless and fixed.
- **Mouse yaw.** `FUN_0021c550` (ApplyLook) adds the yaw delta to player+0x148, but in normal walking (entity flag +0x31)
  the body heading is rebuilt from the look *rate* and +0x148 is reset to the lag/sway offset at the end of the call, so
  a yaw added to f13 is lost. Measured: a scripted 90-degree mouse turn (`mx=1800`) left the view unchanged while `rx=1`
  turned it. Mouse yaw now goes in as a rate on the controller (ctrl+0xA8 and its copy ctrl+0x3C): rate = yaw /
  (maxYaw * dt). maxYaw is measured from every ApplyLook with a non-zero rate (f13 / (rate * dt)); 2.793 rad/s unzoomed.
  dt is the frame timer at the controller update (*(0x559178)+0x64 ticks * +0x50 s/tick), which equalled ApplyLook's
  f12 in all 1468 logged frames (1-tick, 2-tick and clamped 0.108-0.133 s hitch frames). Result: `mx=1800/-1800/900` ->
  applied 1.5705 / -1.5708 / 0.7854 rad, the view turns 90 / back / 45 degrees in the captures. Pitch stays on f14.
- **Smoothing.** ApplyLook runs a camera lag (player+0x2A8/0x2AC: the view trails the aim by up to 10 degrees and eases
  back) and idle sway (+0x170/0x174), both gated by byte player+0x16C. With keyboard/mouse the byte is cleared for the
  duration of the call (view follows the mouse 1:1); pad play keeps both.
- **C fired / Mouse1 crouched.** Runtime `fillPadStatus` wrote the DS2 pressure bytes 16..19 as L1, L2, R1, R2; the real
  order is L1, R1, L2, R2. Killzone reads every button from the pressure bytes (`FUN_001b33b8`: byte pad+0x34+id), so R1
  and L2 were swapped for keyboard *and* gamepad. `KZ_PAD_LOG=1` (hooks 0x1B33B8): scripted R1/L2/R2/L1/triangle read as
  ids 10/9/11/8/4 before, R1 -> 9 (fire) after. Runtime patch 0026. Controller map in RAM (settings+0xEC): walk -101,
  strafe 100, pitch 102, turn -103, allowlook -1, sprint 14, fire 9, secondary 11, action 6, switch 5, grenade 8,
  special 7, reload 4, stance 10, zoom mode 15, zoom 105.
- **Profile name.** The on-screen keyboard (`FUN_001b0ee0`, state obj+0x21C: 1 open, 2 confirm, 4 cancel) is preceded
  every frame by `FUN_001b0df0`, the game's own USB keyboard reader (HID 0x28 enter -> state 2, 0x29 escape -> 4, 0x2A
  backspace -> `FUN_001b18d8`, else `FUN_001b17a0(obj, ch)` add char); no USB keyboard is emulated. That function is
  hooked: while state == 1, typed keys (letters with Shift/Caps, digits, space, - _ .) go to the game's add-char /
  backspace as tail calls (a yield inside resumes in guest code), Enter/Escape set the state. Keys used for typing do
  not reach the pad (held keys are marked consumed until released). Checked on an empty card (`KZ_MC_ROOT`): No profiles
  -> Create -> Edit -> "Drako" typed -> Create profile -> saved (BASCUS-97402655FB312 written) -> level, difficulty,
  character -> gameplay.
- **Menus.** The front end navigates with the D-pad only (left stick `ly=1` did not move the Create Profile selection;
  D-pad down did). Arrow keys and WASD now also press the D-pad (bindings Version=2; older [Bindings] sections get the
  additions appended). No D-pad id is read during gameplay (`KZ_PAD_LOG`, each direction held 2 s).
- Test hooks: `KZ_MC_ROOT=<dir>` memory card folder; `KZ_INPUT_SCRIPT` items `t:type=text`, `t:textenter|textback|
  textcancel`, `b<t>:...` (dropped once the name is entered) and `v<t>:...` (seconds after the name is entered).

## Display edge artifacts and doubled menus (2026-09-30, play-test report)

Report: "a pixelly line along the top and left edge" and "menus look doubled up and a bit bugged", 16:9, auto upscale.
Both were reproduced with headless runs (hidden window of the monitor's size, `KZ_WINDOW_SIZE=2560x1440`) and traced
with `kzgs_replay` on recorded GS traces. The existing `gs_*.png` captures read the merged output at internal
resolution, which is not the presented image. `KZ_SHOT_PRESENT=1` now also writes `gp_*.png`: the frame scaled and
aspect-corrected to the window (`kzgsReadback(.., presentW, presentH)`). Both file names carry the GS frame number.
Other automation switches: `KZ_UPSCALE=n`, `KZ_RENDERER=d3d11|d3d12|vulkan`, `KZ_CROP=l,t,r,b`, `KZ_VS_EXPAND=0|1`,
`KZGS_GS_OPTS=Name=0|1,..`; `run_headless.ps1 -ExeDir -EnvSet "A=1|B=2"`.

### 1. Dotted line on the top and left edge

- **Reproduced.** Live run, 2560x1440 window (auto upscale 4x, FXAA on), mission-failed screen, `gs_049_t250s.png`
  (2048x1792): columns x=0 and x=1 differ from the background colour in 1791 and 1788 of 1792 rows (column 2 in 104),
  and rows 7..9 are garbage across the width; rows 0..6 are the black band. The garbage is multicoloured 1 px dots.
  In gameplay the same strips are a brighter 1-px rim instead of noise (the stale content happens to be similar).
  Crops: `work/fix_proof/edge_FAILED_screen_BEFORE_garbage_2560x1440.png`.
  Same scene, same frame number (f36532, t=304 s) in two live runs, old settings (`KZ_CROP=0,0,0,0 KZ_VS_EXPAND=1`) and
  the fix, presented 2560x1440: pixels differing from the background colour in the top 14 rows: before
  `2100 x6, 2060, 2097, 134, 0 ...` (rows 0..5 are the black band, 6..8 the dotted line), after 0 in all 14 rows; left 14
  columns before `995 994 977 65 0 ...`, after 0. Crops: `work/fix_proof/edge_FAILED_screen_top_before_after_2560x1440.png`,
  `work/fix_proof/edge_FAILED_screen_left_before_after_2560x1440.png`.
- **Geometry.** PCSX2's display circuit (`kzgs_replay --pcrtc`): `displayRect (0,2,512,450)` (the game programs DY=52), so
  the merged 512x448 output has 2 black lines on top, and game frame row 0 / column 0 sit at merged line 2 / column 0.
  At 4x the bad strip is 2 px wide (x 0..1) and 2 lines high (merged rows 8..9): half a native pixel.
- **Root cause (measured).** `halfPixelOffset=4 (Native)`, the Killzone GameIndex setting kzgs applies. Replay of a
  gameplay frame at 4x, first rows after the black band, mean level: Native `39 39 31 31 31`, Off/Normal/SpecialAggressive
  `31 31 31 31 32`; `nativeScaling` 0..4 changes nothing; at 1x there is no rim at all. So Native leaves the top/left half
  native pixel of an upscaled frame holding whatever the target held before (stale VRAM); which pass does not cover it was
  not traced to a draw. The other hpo values are not a fix: post effects (glow) move by half a pixel, which is why the
  GameIndex entry exists.
- **Fix.** Presentation crop, which is PCSX2's own `Crop` option and does not touch rendering: 4 columns each side and
  3 top / 4 bottom lines (`toKzgs`). Top 3 = the 2 black lines + game row 0; left 4 covers column 0. The total is 8 x 7,
  i.e. the 512:448 aspect, so the picture keeps its shape; a smaller crop such as 1,3,0,0 changes the aspect slightly
  and PCSX2 then letterboxes 3 px bars on a 16:9 window (seen in the replay), which would have been a new line.
  Measured on the presented image (replay, FXAA, 16:9): before the first rows are `0 0 0 0 0 0 21 37 33 31` (2560x1440,
  2x) or `0 x9 15` (3840x2160, 4x) and the left columns `63 63 63 64`/`62 63 63 63`; after `31 31 31 ...` and
  `64 64 64 ...` from pixel 0, at 2x/3x/4x (1440p) and 4x/5x/6x (2160p), no bars.
  Live before/after (gameplay): `work/fix_proof/edge_gameplay_top_left_before_after_2560x1440.png`,
  `work/fix_proof/edge_gameplay_top_left_after_3840x2160.png`.
- Cost: 1.6 % of each dimension (2 of the 7 lines were the black band); a CRT's overscan hides far more, the HUD boxes lose a few pixels.

### 2. Doubled / bugged menus

- **Reproduced.** Front-end menus (Game > Campaign/Battlefields, profile list, level/character select), live capture at 4x:
  frames where the text is doubled 1-2 lines apart, the picture is dimmer and white dashes appear, next to clean frames.
  The same frames appear at 1x, so it is not an upscaling effect. Replaying the recorded menu trace
  (`KZ_GS_TRACE`, 6000 frames) through PCSX2's software renderer gives the correct picture and is deterministic
  (0 of 41 frames differ between two runs).
- **Root cause (measured): PCSX2's D3D11 renderer on NVIDIA with vertex-shader sprite expansion.** Same trace, frames 4400-4440,
  compared with the software renderer, RTX 3080: D3D11 3 to 7 wrong frames in each of 4 runs (the wrong window moves between
  runs); D3D12 and Vulkan 0 of 41 in 2+2 runs; D3D11 on the Intel UHD 770 0 of 41. The menu's glow composite is drawn as
  SPRITE strips, the primitive type the vertex-shader expansion handles (a plausible link, not shown draw by draw). With it off, D3D11 had 0 wrong frames
  in 8 paced runs (2/4/8/16 ms between vsyncs, 16 frames each) and 3 of 3 at 20 ms, while the default D3D11 was wrong in
  7 of 8 and 3 of 3. Other switches did not help (`DisableFramebufferFetch`, `UserHacks_DisablePartialInvalidation`,
  `UserHacks_DisableRenderFixes`, `UserHacks_DisableDepthSupport`); `UserHacks_DisableSafeFeatures` did but is broader.
  At 4x with FXAA (the shipped settings) D3D11 with VS expand was wrong in 1 of 3 runs and differed from D3D12 by a mean
  1.3 (per channel, 0..255) even in the "clean" runs; with it off it equals D3D12 exactly (0.0 in 3 of 3).
  The failure only shows while the GS thread has idle time between frames (a replay that keeps it saturated, or
  captures taken every frame from frame 1, did not show it), which is why it is intermittent in play.
  Proof montage (rows: D3D11 default, D3D11 fixed, D3D12; columns: 3 frames of the same trace, 4x FXAA):
  `work/fix_proof/menu_d3d11_default_vs_fixed_vs_d3d12_4x.png`.
- **Not shown / caveats.** The mechanism inside the NVIDIA driver is not proven. Every readback changes GS timing, so a
  live capture cannot see the bug directly: after the readback was changed to a download texture (below) six live runs (1x/2x/4x D3D11, 4x D3D12, 1x D3D12,
  1x Vulkan, about 50 captures each, compared with the software renderer on the matching trace frame) showed no wrong frame, and the wrong-frame windows last only a few frames at menu
  transitions. The evidence is the paced GS trace replay, which uses the same code and GPU as the game.
  Also measured: `GSSaveSnapshotToMemory` (the old readback) allocates and recycles a pooled render target and made
  ~25 % of later live frames wrong even with VS expand off (13 of 46 captures), so the earlier screenshots were partly
  measuring their own capture. `kzgsReadback` now copies the output through a download texture (no pooled target).
- **Fix.** `toKzgs`: D3D11 renders with `vertexShaderExpand = false`. No measurable cost (300 gameplay frames at 4x replay in
  ~1.0 s with and without). D3D12/Vulkan keep it.
- Checked and unrelated: PCRTC anti-blur is active (both circuits read the same lines, `fbRect (0,0,512,448)` and
  `(0,0,512,447)`), turning deinterlacing off gives a bit-identical bad frame, FXAA is not the cause (wrong frames also without it), and the
  present-skip logic in `kz_gs.cpp` is not involved (the replay calls `kzgsVsync` for every recorded vsync and still shows it).

## Mouse yaw, second pass: the real turn path, hold to aim, sharper defaults (2026-09-30)
Player report: horizontal mouse inverted and faster than vertical; wanted hold-to-aim; dislikes FXAA/blur.
- **The earlier yaw model was wrong.** The body turn is in the player update `FUN_0021a8c8`:
  `heading += turn * T * dt`, turn = control state +0x18 (`ctrl+0x3C`; stick right is *negative*, and it ramps to about
  2x while the stick is held), `T = ctl[0x190] / ctl[0x44C]` (the controller's vtable +0x188 at 0x45E4F0; 2.0944 rad/s
  unzoomed, `ctl+0x44C` = zoom factor, 2 with the first rifle's scope). Heading angle = `ctl+0x78`, aim mode =
  `ctl+0x454` (0 normal, 1 zoomed, 2 mounted). The previous patch scaled by the *look* speed (vtable +0x190, 2.793) and
  one frame's dt, and used the stick-left sign.
- **Every turn value acts twice** (measured with `KZ_AIM_LOG=2`, heading logged at the controller update and at
  ApplyLook): once in the player update of frame k and once more in frame k+1 before that frame's player update, from
  a latched copy. One frame of turn changes the heading by `turn * T * (dt[k] + dt[k+1])`: impulses 67.5 / 3.75 /
  -11.25 (unzoomed) and 33.75 (zoomed) gave 4.7124 / 0.19635 / -0.98175 / 0.58905 rad, all equal to the formula.
  (So a held stick turns at 2 * T * turn; whether the PS2 original does the same has not been checked against PCSX2.)
- **Mouse yaw now:** a debt of heading still owed; each frame it is charged with the second step of the previous
  command (this frame's dt) and half of the rest is commanded. Totals are exact and independent of frame-time changes;
  the motion is a two-frame average. Zoomed, yaw and pitch are divided by the zoom factor. Verified: `mx=1800` ->
  -1.5708 rad (right, like stick right), `mx=-600` -> +0.5236, `my=-300` -> pitch +0.2618; zoomed `mx=600` -> -0.2618,
  `my=-300` -> +0.1309.
- **Hold to aim** (`[Input] AimMode=Hold`, default): key/mouse bindings to R3 are not sent to the pad in gameplay; R3
  is pulsed whenever the key state differs from the game's zoom state (`ctl+0x454 == 1`). Verified with the scripted
  `zoomkey` event: mode 0 -> 1 while held, back to 0 on release. Gamepad R3 stays a toggle.
- **D-pad in gameplay** is the scope zoom axis (controller map zoom = 105 = D-pad up/down, read through the axis
  path, which `KZ_PAD_LOG` did not log). The WASD/arrow D-pad presses added for menus are now menu-only
  (`MenuUp` ... targets, bindings Version=3).
- **Sharper defaults:** FXAA off (existing ini files are switched once), final scaling uses PCSX2's sharp bilinear
  (`SharpScaling`), optional CAS (`Sharpen`, applied at present time, not visible in headless captures: untested).
  Presented 2560x1440 capture, stddev of the Laplacian over the weapon/HUD region: 8.35 (FXAA + smooth) -> 10.44.
- Fixed (see "Quit to the front end: white screen" below): with movies on, quitting a mission to the front end showed a white screen.

## Post-process passes, draw distance, LOD, SMAA (2026-10-01)
Player taste: no FXAA, no blur; SMAA / MSAA, normal textures, specular, shadows and render distance welcome. Everything
below was found from a GS trace of first-mission gameplay (`work/kzfix/failed.trace`, `kzgs_replay --dump-draws F:N
--dump-skip K --dump-rt` writes PCSX2's per-draw dump with the render target after every draw) plus the reflection data
(property names, defaults and offsets are registered in the ELF) and Ghidra.

### 1. The game's post-processing (draw sequence of one gameplay frame, frame buffers FBP 0x0 / 0xE00, scratch = Z memory 0x1C00)
`FUN_001EF640` (post-process render, obj = PostProcess; preset fields copied to obj+0x240..0x280) draws, in order:
| draws (trace f38) | what | GS signature | game code |
|---|---|---|---|
| 27837-27853 (17) | **glow**: the frame's alpha channel (glow mask, read as PSMT8H) -> glow palette CLUT (`Effects/Glow/glow-palette.act`, loaded by `FUN_001BD5A0`) -> 256x224 in Z memory -> 15 blur/downsample taps -> 128x112 -> added back over the frame (A=0,B=2,C=2,D=1,FIX=128) | FRAME 0x1C00 reading TBP 0x0/0x1C00, TW=TH=9 | `FUN_0015DFC0` -> `FUN_0015E028` (taps with `FUN_0015F4D8`) |
| 27854-27971 (118) | **colour grade**: luma of the frame (CT16 -> CT32 -> P8 CLUT shuffle, 111 draws of 64x32 tiles to re-swizzle) built into an 8-bit index, then one 512x448 draw through the 256-entry palette made from the preset's shadow/midtone/highlight colours (CBP 0x3952), blended with the preset Contrast/Brightness/MixFactor | FRAME 0x1C00..0x29E0 then FRAME 0 reading 0x1C00 as PSMT8H | `FUN_00167050` (luma prep, params at +0x240) and `FUN_00166A28` (palette) |
| 27972 (1) | **motion blur**: previous frame buffer (FBP 0xE00, the other buffer, `FUN_00150BE8(ctx,1)`) blended over the current one with FIX = strength*127.5 (preset "Motion blur" Strength 0.1 -> FIX 12 here) | A=0,B=1,C=2,D=1 FIX=12, TBP 0xE00 | `FUN_0015F768(f12 = strength)` (obj+0x280); also called with 0.02 by `FUN_001A8D30` |
| 27973 | **film grain**: noise texture strips (alpha = Noise Strength*255); already switchable by the existing noise filter option (byte 0x55DF6C) | TBP 0x395E, 128x128 | tail of `FUN_001EF640` |
Other blur: `FUN_001A9308` (called every frame from `FUN_001A8D30`) draws the camera "lens" overlay (rain on lens): eight
half-resolution ping-pong blur taps (`FUN_0015F198`) plus the overlay texture, only when the camera has a lens texture
(+0x140). PostProcessPreset resource (reflection class at 0x545462, object size 0x38, type pointer 0x523A20 at +0): Contrast
+0x10, Brightness +0x14, MixFactor +0x18, Shadow +0x1C / Midtone +0x20 / Highlight +0x24 colours, Motion blur Strength
+0x28, Noise Strength +0x2C, Grain size +0x30, Texture +0x34; 19 instances in a gameplay RAM dump (motion blur 0.1-0.5,
noise 0.025-0.3). There is no depth-of-field pass: none of the draws reads depth, and no DoF property exists in the
reflection data.
- **Hooks** (`src/kz_post.cpp`, runtime function replacement, no regen): `0x15DFC0` glow, `0x15F768` frame blend, `0x1A9308`
  lens blur, each returns at once when its option is off. The colour grade, film grain and heat vision (`0x15E028` called
  directly with the heat palette) are not touched.
- Config: `[Graphics] MotionBlur=0|1`, `Glow=0|1`, `LensBlur=0|1` (all default 0 = off); env `KZ_MOTION_BLUR`, `KZ_GLOW`,
  `KZ_LENS_BLUR`, `KZ_POST_LOG=1` (call counts).
- **Measured** (first mission, `KZ_FPS=120`, 2560x1440 window, 4x internal, frames per second = vif counter over t=150..200 s, `work/pp/fps.ps1`):
  original look (all three passes on) 117.3, passes off 117.3. The game is EE/CPU bound there (about 118 fps at 120 Hz), so skipping the
  17 + 1 GS draws is not visible in the frame rate; it removes 15 blur taps and one full-screen blend per frame from the GS.
  GS trace of the same moment, two frames, passes on / off (`work/pp/orig.trace`, `nopost.trace`, replayed with
  `kzgs_replay --dump-draws 18:3000`, `work/pp/sumdraws.py`): glow taps 15 -> 0, frame-blend draw (A0 B1 C2 D1 FIX 12 into FBP 0xE00) 1 -> 0, colour
  grade tiles 112 -> 112 and final grade draws 2 -> 2 (kept).
  Pixel effect of each pass in that trace (`rt1` render target after the draw): motion blur changes 91 % of the pixels (mean 0.8/255, max 22) in this almost
  static scene; the glow add changes 0 pixels here (the glow mask in the alpha channel is empty in the tutorial scene), so its look is not demonstrated;
  it only matters around emissive surfaces. `work/pp/motionblur_before_after.png` (before | after | difference x10).
- **Not done / not found:** no depth-of-field and no bloom other than the glow pass exist. The heat-vision filter reuses `0x15E028` and was not touched.

### 2. Draw distance and LOD (first-mission zones: far plane = fog end = 35..70 units)
- **RenderZone** (reflection class at 0x552C70, vtable 0x531500): "Fade distance", "Render: Far plane" (default 4096) +0x140, fog enabled +0x144,
  fog start +0x148, fog end +0x14C, start/end density +0x150/+0x154, colour +0x158. RenderZoneManager (vtable 0x531448) instance 0x7C4540 in the
  gameplay dump: four zones with far = fog end = 65, 70, 50, 35 and fog start -10/-5, so the original view distance is 35-70 world units.
  `FUN_00339C38(manager, camera, out)` picks the zone around the camera (blending two at a boundary) and fills `out`: out[0] far plane
  (min of far and fog end when fog is on), byte out+4 fog enabled, out[2] fog start, out[3] fog end, out[4]/[5] densities, out[6] colour; returns 1 when a zone was found.
- **Render distance correction (2026-10-20):** the original wrapper on `0x339C38` tried to read outputs after calling
  a recompiled guest function. That assumption is invalid: the generated owner tail-dispatches a continuation and
  returns to the wrapper before the output is finalized. Its only newly instrumented call reported zeroes, so the
  older screenshot and frame-rate attribution is withdrawn as acceptance evidence. The current implementation hooks
  the finalized consumers instead: `0x1C1A2C` scales the camera far plane and `0x151180`, gated to the three
  RenderZone fog call sites, scales fog start/end. A 2x first-mission run measured actual values
  `70 -> 140` and `-10..70 -> -20..140`; both signatures are locked in the generated manifest.
- **LOD:** the LOD selectors divide the distance by `ctx+0x1AC` (`FUN_003F4E70` static meshes, `FUN_003F4F10` skinned, `FUN_003EFAD8` multi-mesh, the grass and ~35
  other readers). `FUN_001C2168(f12 = fov, a0 = game object)` sets it to `obj[0x1C] * tan(refFov/2) / tan(fov/2)` (refFov 75 at 0x559540, obj = `*(0x559178)`, obj[0x1C] = 1.0
  in the dump). Both the entry and `0x1C21B0` continuation wrappers were rejected because generated tail dispatch made
  their temporary/readback assumptions false. The final hook scales f12 at setter continuation `0x151E64`, gated to
  caller return `0x1C21D4`, immediately before it stores render-context `+0x1AC`.
  Other per-class distances exist as data: MeshResource "Maximum visible distance" (+0x1C, default -1 = none), Grass "Max View Distance"/"Max Animate Distance"
  (default 20), LodStaticMeshResource/LodSkinnedMeshResource "LOD Info" tables. The 4x hook logs the actual
  `1.436 -> 5.742` setter transformation independently of RenderZone controls. The earlier frame-rate and image
  deltas remain preliminary, not range-qualification evidence.
- Config: `[Graphics] RenderDistance=1..2` (default 1 = original), `LodScale=1` (disabled); env
  `KZ_RENDER_DISTANCE`, `KZ_LOD_SCALE`. The hands-on Escape retest froze at LOD 4x even with render distance 1x, so
  product configuration now clamps LOD to 1x and does not install its experimental hook.

### 3. Shadows (identified, not changed)
Materials carry "Cast Static Shadows / Cast Dynamic Shadows / Receives Dynamic Shadows" (MeshResource +0x?, light classes), shader variants PROJECTIVESHADOW
and STENCILSHADOW (material type enum, `FUN_00337298`/`FUN_00337538`), baked lightmaps (LIGHTMAPCOLOR/INTENSITY, "Lightmap Density Factor"), and dynamic shadows are
TextureProjector/Projector objects (reflection classes 0x55304C/0x552F20: texture, matrix, near/far distance, blend mode) limited by LightingManager "Max Lights" and
"Max Projectors" (`FUN_00331658`). The first-mission trace has no shadow-casting object in view (every draw targets the frame buffer or the post scratch area), so the
projector texture size and distance could not be measured and nothing was changed. Not feasible to claim more without a trace of a scene with characters.

### 4. SMAA 1x (done, D3D11)
- PCSX2's HW renderer has no SMAA. `GSRenderer::Merge` runs `GSDevice::FXAA()` at internal resolution (before the final scale), which ends in the virtual
  `DoFXAA(src, dst)`; GSDevice11 is final and private. `ext/kzgs/src/kz_smaa11.cpp`: when SMAA is on, GSOptions::FXAA is set and the live device gets a
  copy of its vtable with the DoFXAA slot replaced (slot index decoded from the vcall thunk of `&GSDevice::DoFXAA`, `kz_smaa_vt.cpp`). The replacement runs the unmodified
  SMAA.hlsl (HIGH preset, luma edges) as three raw D3D11 passes (edges -> blend weights with the Area/Search textures -> neighbourhood blending) and saves/restores the
  pipeline state around them. If it fails, the frame is copied through (no FXAA). ext/pcsx2 is untouched. Files: MIT SMAA from the KoAR-Modernized project
  (`ext/kzgs/shaders`, `ext/kzgs/third_party/smaa`, licence text included).
- Replay evidence (`failed.trace` frame 40, 1x internal, `work/pp/smaa_1x.png`): 3.8 % of the pixels change, all on edges (tree trunks, tank outline, crosshair arc smoothed, textures untouched).
  Live evidence (rifle at 2560x1440, `work/pp/smaa_live_rifle.png`). Frame rate 117.4 vs 117.3 without.
- `[Graphics] SMAA=1` (default on), env `KZ_SMAA`, `kzgs_replay --smaa`. D3D12 and Vulkan ignore it (no FXAA either: the flag is only set for D3D11).
- **MSAA:** confirmed not practical: PCSX2's GSOptions has no MSAA setting, every render target is a plain texture that later draws sample (render-to-texture feedback,
  the post chain above reads the frame as a texture), and the D3D11 device creates only single-sample targets. The sharp anti-aliasing available is internal resolution (Upscale) plus SMAA.

### 5. Specular maps and new material effects
- Texture replacement is compiled into kzgs (PCSX2 `GSTextureReplacements`) and is now switchable: `[Graphics] TextureReplacement=1` loads files from `textures/SCUS-97402/replacements`
  next to the exe (names are PCSX2 texture hashes); `KZ_TEX_DUMP=1` dumps every texture the game draws to `textures/SCUS-97402/dumps`. See the measurement below.
- New lighting terms (specular, normal maps) are not feasible at the GS level: the game's shading is baked into VU1 vertex colours and lightmaps; the GS only sees finished
  textured triangles, with no normals or light vectors, so a specular term would need new VU1 programs and new vertex data in every model. Not attempted.
- **Texture replacement measured:** PCSX2 creates `<exe>/textures/SCUS-97402/dumps` one level at a time, so kzgs now creates `<exe>/textures` itself (before this
  nothing was ever dumped). With `KZ_TEX_DUMP=1` the game dumped 19 textures by the title menu (`kzgs_replay --dump-textures`: 80 in the 40-frame gameplay trace, names `<tex0 hash>-<clut hash>-<size/bits>.png`).
  Replacing one dump (the 1024x128 plank atlas `a934f5fca6f05cb9-58a3ccaa8b269230-00001e93.png`, tinted magenta, copy in `work/pp/texrep/`) and replaying
  with `--replace-textures` changed 1.9 % of the frame (x 0..134, y 225..407 of 512x448: the plank on the left, `work/pp/texture_replacement_test.png`), so replacement
  works through kzgs; a texture pack (higher resolution or with baked specular detail) is therefore possible. No pack exists; making one is art work.
  Config `[Graphics] TextureReplacement=1` (default on, harmless when the folder is empty), env `KZ_TEX_REPLACE`, `KZ_TEX_DUMP`; replay `--dump-textures`, `--replace-textures`.

### Defaults chosen
SMAA on (verified, no cost), motion blur / glow / lens blur off, texture replacement on, render distance 1x and LOD
1x. Elevated distance/LOD costs and supported campaign ranges remain under GFX-03 qualification.

## Quit to the front end: white screen (2026-10-01)
**Symptom.** Movies on (`KZ_IPU` unset): start a mission, pause, Quit, confirm. The front end comes back without its background movie; in the player's runs the whole
screen is white, with only OPTIONS / GAME / EXTRAS drawn (in other runs the menu is drawn over the frozen pause screen). `KZ_IPU=off` is fine (black background).

**It is not the movie path.** Measured with `PS2X_MPEG_TRACE`-style logging in the HLE and `KZ_IPU_TRACE` (debug code, not kept):
- The menu movie is opened again after the level (`FUN_002ded00` call #4, args identical to the boot-time menu movie #2), `sceMpegCreate`/`sceMpegReset` run, `sceMpegGetPicture`
  delivers frames from the first call (frame counter, `queued`, mean colour of the decoded RGBA 39..66, the same dark green as before the level) and loops. kzipu is idle at the
  open (`busy=0`, FIFOs empty, no DMA active, `pending=0`), no kzipu DMA write ever touched game memory outside its buffers, and `g_eofPlayer`/`g_eofReads` are not involved.
- The frame is uploaded to GS memory every frame (GS trace: 32 strips of 16x448 CT32 into DBP 0x1C00, pixel data green-grey, not white). What differs is the draw of the background
  quad: before the level (and at boot) ALPHA_1 = 0x80000080A8, TEX0.TCC = 1 (opaque), after the level ALPHA_1 = 0x44 (blend with the vertex alpha), TCC = 0 and vertex alpha 0:
  the quad is invisible, nothing clears the frame, and the additive light bars accumulate to white.
- All draws of the front end after the level go through `FUN_001527F0` (render state setter) with the engine's default 2D material alpha, `*(u8*)0x55DF6C` (word 13 of the render
  state copied from `0x55DF38`), at 0 instead of 0xFF. A census of called guest functions in both states showed the same code paths in both; only this value differs.
- A page-protect watchpoint on the word (host call stack symbolised from the PDB) named the writer: `kzPatchesApply` <- `onVsync`.

**Root cause.** `src/kz_patches.cpp`, the "disable noise filter in gameplay" pnach: it clears the byte at `0x55DF6C` every vsync while the gate halfword `0x57BA88` is 4 (in a level,
including its pause menu). That byte is not only the film-grain overlay's alpha: the front end draws its background movie quad (and the pause menu panels and highlight bars)
with the same default material alpha. The game writes it once at start-up (0xFF) and never again, so after the patch cleared it, the menu inherited 0.
Gate/byte log without the patch: gate 0 in the menus, 5344 for a few ticks while loading, 4 in the level, byte 0xFF all along. With the patch: byte 0x00 from tick 17450 and never restored.
`KZ_NO_PATCHES=1` shows the menu movie correctly after a Quit (that is how the cause was first confirmed).

**Fix.** `kzPatchesApply` remembers the byte before the first override of a level session and writes it back on the first vsync where the gate is no longer 4 (or when the option is
switched back on). Gameplay is unchanged (noise filter still off).
- Evidence (`work/proof_quit_to_menu.png`: before white / before default / after; `work/proof_ram165.bin` and `work/proof_ram215.bin`, EE RAM at the end of two runs): at 165 s (in the
  level) byte 0x00, gate 4; at 215 s (front end) byte 0xFF, gate 0. After the fix the front end shows the looping background movie with the light bars at every sampled second from
  184 s on (`work/proof_after`). Boot with movies on: Sony / Guerrilla logos, intro cinematic, KILLZONE title, menu with the movie (`work/proof_boot_grid.png`); mission entered and played
  as before; `KZ_IPU=off`: black front end with its panels and bars (`work/proof_off3_grid.png`; the quit script needs one extra Cross at 183 s there because the confirmation dialog
  comes up later than the 179 s press).
- Not changed: during the level the pause menu is still drawn with the cleared byte, so its panels and highlight bars are missing (flat grey) while the noise filter is off. Only a
  noise-filter-specific patch (not the shared material alpha) or a pause detector would fix that.
- The IPU/HLE code (`src/kz_ipu.cpp`, `ext/PS2Recomp` libmpeg HLE) needed no change.

## Film grain switch moved to the post-process object (2026-10-01)
The grain strips at the end of `FUN_001EF640` are drawn only when the PostProcess object's noise strength (`obj+0x284`,
from the active preset) is above 2/255. With `NoiseFilter=0` a hook on `0x1EF640` zeroes it on entry (`src/kz_post.cpp`,
env `KZ_GRAIN`). The pnach byte `0x55DF6C` (default 2D material alpha) is no longer written, so the pause menu keeps its
panels, highlight bar and dimmed background, and the front end is unaffected. Checked with movies on: grain off vs on
(crop `work/grain_cmp.png`), pause menu with panels, Quit -> menu with its movie (`work/grain_quit_grid.png`).

## Mouse yaw: why the two-frame average stays (2026-10-01)
Tried "command = motion / (T * dt) - previous command" so the frame's rendered heading change equals the frame's mouse
motion (rendered heading = heading at ApplyLook + this command's first step). The first frame was exact (30 deg in,
29.99 deg out), but the leftover command cannot be removed cleanly once the mouse stops, because every command acts in
two frames: cancelling it needs an equal and opposite command for ever (the heading is then wrong between the two
steps inside every frame, by up to the last flick), halving it gives a one-frame bounce of half the last step, and
running it down by 20 % per frame left a single 30 deg impulse at 37.1 deg with varying frame times. The committed rule
(half the debt per frame) reaches the target in two frames with no overshoot and exact totals; its cost is a two-frame
box average of the motion (about 4 ms at 120 fps). It can only become immediate if the double application itself is
removed. Other findings from the same hands-on report: the player's ini had MotionBlur=1 and NoiseFilter=1 (the old
launcher labels were unclear), which smears view changes; the device path was verified (WASAPI, 48 kHz, callback
consuming 48000 frames/s at volume 0) and the SPU2 output is silent only during the boot movies.

## Speed parity with the original, input latency (2026-10-01)

Player report at the monitor's refresh rate: "everything feels delayed, molasses: animations, movements, shooting, swapping" and
"mouse input still feels like smoothing or buffering". Question: is the port's game logic slower or laggier than the original, or is
the original just heavy? Answer, measured against the original game in PCSX2: **every dt-based system runs at the original's speed**
(walk, strafe, sprint, reload, crouch; game time = wall time within 0.15 % at 30, 60, 120, 144 and 240 Hz), two pieces of view code
that count *frames* were not frame-rate independent and are fixed, fire rate / switch / crouch timers quantise to frames (the port is
equal or faster, never slower), and the port's input-to-present latency is 15-22 ms at 120 Hz (the original's 30 fps pipeline is about
70-100 ms by construction). Nothing measured makes the port slower than the original; the heaviness is the game's (reload 3.3 s, weapon
switch 1.2-1.7 s, crouch 0.77 s, a stick that needs a second to reach full turn speed).

### Method
- **Reference run.** `tools/scripts/parity_pcsx2.py` runs a copy of PCSX2 (`tools/pcsx2_par`, git-ignored, PINE slot 28111) on a
  private invisible Windows desktop (`pcsx2_hidden.py`: CreateDesktop + CreateProcess with lpDesktop, so it cannot take the focus or
  show a window), renderer Null, `[SPU2/Output] Backend=Null, OutputMuted=true` written into its ini before every launch (the early
  launches of the session ran without it and played audio; fixed after the coordinator's report). Savestate 9
  (`tools/pcsx2/sstates/... .09.p2s`, first mission, standing in the trench) is loaded over PINE; input goes through
  `pcsx2_pad_hook2.pnach`: the scePadRead call at 0x416D98 jumps to a cave at 0xFF000 that, when the word at 0xFFF00 is non-zero, copies
  the 18 bytes at 0xFFF10 over pad reply bytes 2..19 (buttons active-low, sticks, pressure bytes), so any button/stick combination can
  be written over PINE. Game memory is sampled over PINE in batched reads at about 1 kHz. The player dies about 14 s after a load (enemy
  fire), so each scenario is one load and at most 13 s.
- **Port.** `KZ_PARITY_LOG=<file>` (`src/kz_parity.cpp`, called from `kzTimingOnVsync`) writes one CSV row per guest vblank: wall time,
  vsync counter, frame timer (`*(0x559178)` +0x68 now, +0x64 elapsed, +0x50 seconds per tick), the player entity and its character
  controller (`player+0x31C`: position +0x50, heading +0x78), and any words named in `KZ_PARITY_WORDS` (`p:` player, `c:` controller,
  `w:` the rifle, `g:` game object, `a:` absolute, `>` follows a pointer). The player and the rifle are found by a RAM sweep of 128 K words
  per vblank (0.15 ms, never long enough to disturb the vblank; the heap layout differs from PCSX2's, so addresses cannot be copied).
  `KZ_PARITY_START` delays logging, `KZ_PARITY_DUMP=t1,t2` dumps RAM. `parity_pcsx2.py --words` takes the same names and writes the same CSV;
  the pad script (`KZ_INPUT_SCRIPT` syntax, byte 0 / 255 for a full stick) is the same for both.
- **Game time.** `parity_analyze.py`: a frame boundary is a change of the frame timer's `now` tick, the simulation step is the tick
  difference times seconds per tick (capped at the game's 0.1333 s clamp). The timer's `elapsed` field cannot be used: the limiter
  stores `now` before `elapsed`, so a sampler reads a stale value for 15 % of the frames. All times below are game seconds.
  Game seconds per wall second at 30/60/120/144/240 Hz, four 10 s windows each: 1.0000 / 1.0000 / 1.0009 / 1.0001 / 0.9999 (worst window
  1.0014); vsync counter / (R * wall) 1.0000 everywhere. (The 10 s headless screenshot stalls the pipeline 130-175 ms and costs ~0.04 s of game
  time through the clamp: `KZ_SHOT_INTERVAL` is 400 in these runs.)
- Scenarios (port script offset 162/176 s after boot, PCSX2 from the load; the trench walls end each move after 1.5-2.5 s, the geometry is
  the same in both): walk back 2.5 s, strafe left 2 s, strafe right 2 s, turn right / left 1 s at full stick, turn right 2 s at half stick;
  a 0.55 s turn, then forward + L3 (sprint); fire 2 s, reload, weapon switch (5 presses), crouch hold. The player cannot walk forward from
  the spawn (a berm), so walking is measured backwards and sprint after a turn.

### Results (`tools/scripts/parity_compare.py AC`; the port columns are with the fixes below)
| quantity (units / rad / game s) | PCSX2 | port@30 | @60 | @120 | @144 | @240 |
|---|---|---|---|---|---|---|
| walk back speed | 3.581 | 3.581 | 3.581 | 3.582 | 3.581 | 3.581 |
| strafe left / right speed | 4.860 / 4.747 | 4.860 / 4.747 | 4.860 / 4.747 | 4.860 / 4.747 | 4.860 / 4.747 | 4.860 / 4.750 |
| sprint peak speed (0.1 s bins) | 7.2 | 7.2 | 7.2 | 7.2 | 7.2 | 7.2 |
| stick turn right, full: rad/s at 0.3 / 0.6 / 0.9 s | 4.13 / 5.24 / 7.02 | 4.13 / 5.24 / 7.02 | 4.15 / 5.34 / 7.11 | 4.12 / 5.16 / 6.92 | 4.11 / 5.09 / 6.85 | 4.12 / 5.15 / 6.92 |
| stick turn left, full | 4.23 / 5.37 / 7.19 | 4.23 / 5.37 / 7.19 | 4.21 / 5.27 / 7.09 | 4.22 / 5.32 / 7.14 | 4.21 / 5.28 / 7.11 | 4.21 / 5.23 / 7.05 |
| stick turn, half stick, rad/s (0.3 ... 1.5 s) / angle in 1.9 s | 0.499 flat / 0.949 | same | same | same | same | same |
| fire interval (s) / shots per second | 0.167 / 6.00 | 0.167 / 6.00 | 0.167 / 6.00 | 0.164 / 6.11 | 0.167 / 6.00 | 0.156 / 6.40 |
| reload: magazine out -> in | 1.835 | 1.833 | 1.833 | 1.850 | 1.833 | 1.833 |
| reload: weapon state "reloading" | 3.303 | 3.300 | 3.300 | 3.300 | 3.285 | 3.292 |
| crouch: request -> stance changed | 0.767 | 0.733 | 0.767 | 0.758 | 0.750 | 0.763 |
| weapon switch flag, slot 1->2 / 2->1 | 1.235, 1.201 / 1.668, 1.635 | 1.200 / 1.667 | 1.150 / 1.600 | 1.150 / - | 1.146 / 1.583 | 1.275 / 1.763 |
| button -> first reaction: switch / reload (+-0.05) | 0.267 / 0.534 | 0.200 / 0.433 | 0.217 / 0.467 | 0.208 / 0.458 | 0.208 / 0.451 | 0.225 / 0.479 |

The port at 30 Hz (`KZ_FPS=30`) reproduces PCSX2 bit for bit (turn 4.131 / 5.242 / 7.021 vs 4.131 / 5.241 / 7.019, half stick 0.949 in both), so
the whole chain (timer patches, pad path, controller code) is the original's and every difference at higher rates is a frame-rate effect inside
the game code. Walk, strafe and sprint speeds are identical to three digits. Grenade throw could not be measured: in this state L1 and R2
change nothing (rifle slots 1 and 2 are empty, the 7/29-round object stays inactive); reported as unverified.

### Discrepancies
1. **Stick turn acceleration counts frames (fixed in `src/kz_timefix.cpp`).** A held stick turns at 2T = 4.19 rad/s at first (every turn
   value acts twice, in its frame and the next: the original does that too, 4.13 rad/s in PCSX2) and speeds up to about 7.4 rad/s over a second.
   `FUN_0023e8c8` adds |turn| * turnSpeed * pi/180 per frame to a counter (`ctrl+0xBC`, reset when the stick is released) and starts a 1 -> 2x
   smoothstep ramp once it passes 15 degrees. Per frame, not per second: full stick starts the ramp after 8 frames (0.27 s at 30 fps), half stick
   after about 60 frames (2.0 s at 30 fps: never inside a 2 s hold). At 120 Hz (game frames of 8-17 ms) it started after 0.1 s / 0.5 s. Before the fix
   (`KZ_TIMEFIX=0`, port / PCSX2): full-stick turn rate at 0.6 s 1.08-1.14, angle after 0.9 s 1.05-1.10; half stick at 1.5 s 1.40-1.67 (it is flat in the
   original), angle after 1.9 s 1.20-1.30. The step is now scaled by dt * 30: full stick is within 0.970-1.019 of PCSX2 at 60-240 Hz and half stick is
   exactly 0.499 flat (ratio 1.000), identical at 30 Hz.
2. **Camera lag is an Euler step (fixed, gamepad play).** `FUN_0021C550` (ApplyLook) keeps the view trailing the aim (`player+0x2A8` yaw / `+0x2AC` pitch,
   enabled by byte `+0x16C`; kz_aim clears it for keyboard/mouse) as lag = (lag - step) * 10 * (0.1 - dt), exact for one dt only. At 0.5 rad/s the original
   trails 0.0316 s and settles with tau 0.0822 s; the port (KZ_AIM=off to keep the lag) trailed 0.0381-0.0391 s (+21..24 %) and settled with tau
   0.0888-0.0915 s (+8..11 %) at 60/120/144 Hz. The stored lag is now multiplied after the call by exp(-dt / 0.075) / (10 * (0.1 - dt)): trail 0.0311 / 0.0311 /
   0.0320 s (-2..+1 %), tau 0.075 s (-9 %). (A first-order filter cannot match both the trail and the settling of the original's discrete step: 0.075 balances them.)
3. **Countdown timers quantise to frames (not changed).** The rifle sets a countdown (`weapon+0x138`) to 0.1538 s at each shot, subtracts dt every frame and
   fires on the first frame where it is <= 0 (`+0x11C` holds the time of the last shot). The designed rate is 6.5/s; the original's 5 frames of 33.4 ms make it
   0.1668 s (6.0/s). Port: 60 Hz 10 frames = 0.1667; 120 / 144 Hz frames are 1-2 vblanks, 0.164-0.167; 240 Hz frames are 1-3 vblanks (4-12 ms) and the interval averages
   0.156 s (6.4/s, +7 %). Weapon switch and crouch are a countdown plus a few one-frame state changes, which cost 33 ms apiece at 30 fps: the switch flag is 0.04-0.09 s
   *shorter* in the port at 60-144 Hz (1.14-1.17 vs 1.20-1.24 s; 1.58-1.60 vs 1.64-1.67 s; at 240 Hz one run was 0.04-0.10 s longer, its frames are irregular),
   the button-to-first-reaction 0.05-0.08 s shorter. Left as is: matching the original
   would mean re-quantising every countdown to 30 fps.
4. **Equal:** walk/strafe/sprint speeds and their acceleration, reload (animation driven), crouch transition 0.767 s, the frame-time clamp (0.1333 s of game time
   at every rate), the EE cycle clock scale (no effect on game time, see above).
5. Not changed: the idle-sway low-pass (`sway * 0.97 + noise * 0.03` per frame in ApplyLook) is per frame too, but it filters a 0.33 rad/s noise and only
   shifts the phase of the idle sway; the pitch auto-centre and the stick slew limit (4 units/s) are time based.

### Input latency
- **Mouse path.** `SDL_PollEvent` runs on the main thread about every millisecond (`SDL_Delay(1)` loop in `kz_main.cpp`); `KZ_PUMP_PROBE=1` pushes an event
  every 2 ms and measures its wait: median 0.11-0.51 ms, p90 0.6-0.8 ms, p99 1.2-1.6 ms, max 1.9-3.8 ms (hidden window, game running). `kzInputOnEvent` adds
  each motion to an accumulator at once; the aim patch takes it once per game frame at the controller update, so a mouse count waits at most one game frame
  (one or two vblanks, 8-17 ms at 120 Hz) and is never carried over a frame. `kz_aim.cpp` then spreads each yaw command over two frames because every
  turn value acts twice (as in the original); pitch is added at once. Not touched (owner is on it).
- **Where frames queue.** The game kicks a frame list only in a vblank handler and only when the previous kick finished, so at most one list is in flight in
  front of the VIF1 worker, as on the PS2. Behind it, `kzgsVsync` counts frames pushed and not yet finished by the GS thread and blocks the producer (the VIF1 worker,
  and through it the EE: its D1 transfer does not complete) once more than `maxQueuedFrames` are in flight. That limit was 2 (PCSX2's MTGS default too), i.e. two finished
  frames could wait behind the one being drawn.
- **Measured** (`KZ_TIMELINE` + `tools/scripts/tl_latency.py`; input = the frame limiter's exit before the frame's D1 kick, present = end of the GS-thread frame of the first vsync
  pushed after the list was done; host ms; other builds ran on the host at the same time, so absolute numbers vary by a few ms):

  | case | queue limit | input -> present, median / p90 | frames queued after each push (1 = none waiting) |
  |---|---|---|---|
  | 120 Hz, VSync off (default), idle host | 2 | 22.1 / 31.4 (2.7 vblanks) | 1: 1798, 2: 18, 3: 6 |
  | same | 1 | 20.1 / 31.1 | 1: 1900, 2: 22 |
  | 240 Hz, same, two paired runs | 2 / 1 | 15.3, 15.5 / 16.2, 15.0 (3.7-3.9 vblanks of 4.17 ms) | mostly 1, up to 3 / 2 |
  | 120 Hz, blocking present emulated (`KZGS_FAKE_FIFO_HZ=120`) | 2 | **31.7 / 49.3** (3.8 vblanks) | 1: 1718, 2: 769, 3: 448 |
  | same | 1 | **18.8 / 42.5** (2.3) | 1: 2849, 2: 108 |
  | 120 Hz, GPU-bound (3840x2160 window, 8x upscale) | 2 | 38.6 / 66.7 | 1: 739, 2: 679, 3: 717 |
  | same | 1 | 30.6 / 51.9 | 1: 1135, 2: 1008 |

  Breakdown at 120 Hz VSync off: limiter exit -> kick 5-10 ms (the list is built in ~5 ms, then waits for the next vblank), kick -> worker done 4.4-6.4 ms, done -> present 1.6-9 ms.
  Throughput is the same with limit 1 and 2 (1758 / 1871 kicks in 28 s at 120 Hz; 1807 / 1766 and 1757 / 1753 at 240 Hz under 4-way host load). With an idle GS thread the queue is
  one deep either way; the depth appears when the GS thread is slow or its present blocks, which is the VSync-on case.
- **Fix:** `KzgsConfig::maxQueuedFrames` default 1: at most one frame waits behind the one the GS thread is on (`KZ_GS_QUEUE=n` overrides for tests). Back-pressure on the EE does
  exist: with the blocking present the producer (limit 1) was held back 49 % of the time and the game still ran at 117.7 of 120 fps; the same case with limit 2 and the
  timeline window set too early showed the EE in a spin wait while loading, so the throttle does reach the EE.
- The original at 30 fps: list built in a 33 ms frame, kicked at the next vblank (up to 33 ms later), drawn during the next frame and displayed at its end, i.e. 2-3 frames = 70-100 ms before
  the monitor; derived from the frame structure, not measured (PCSX2 has no host present to time). The PCSX2 reference shows the same one-frame quantisation of input: first heading change
  0.033 s after the pad write.

### Not verified
Real display latency with VSync on (a hidden window's Present does not block; emulated only with `KZGS_FAKE_FIFO_HZ`, DXGI's own queue behind it is not measured), grenade throw and
the secondary weapon (not available in the savestate), other weapons, the first-person animations beyond the state timers listed, enemy/AI timing, the hold-to-aim zoom time,
the mouse path end to end with a physical mouse.

### Files, switches
`src/kz_parity.*` (`KZ_PARITY_LOG/START/WORDS/DUMP`), `src/kz_timefix.*` (`KZ_TIMEFIX=0` off, `KZ_TIMEFIX_LAG=0` keeps the camera lag, `KZ_TIMEFIX_LOG`), `tools/scripts/parity_pcsx2.py`,
`pcsx2_hidden.py`, `pcsx2_pad_hook2.pnach`, `parity_analyze.py`, `parity_compare.py <AC|ACB>`, `parity_events.py`, `tl_latency.py`; `KZGS_STATS=1`, `KZGS_FAKE_FIFO_HZ`, `KZ_GS_QUEUE`,
`KZ_VSYNC`, `KZ_PUMP_PROBE=1`. Raw logs (work/, git-ignored): `AC_*.csv` (fixes) and `ACB_*.csv` (`KZ_TIMEFIX=0`) movement and turns, `B_*.csv` weapons and sprint, `S_*.csv` switches,
`LAG*_*.csv` camera lag, `parity/ref_*.csv` PCSX2, `lat*.csv` timelines.

## FMV audio: the boot movies were silent (2026-10-01, patch 0027)
**Symptom.** `KZ_AUDIO_WAV`: RMS 0 / peak 0 from t=0 until the intro cinematic ended (~80 s), then normal menu music and gameplay sound.

**Where it was lost (measured).** The movie player is `FUN_002df9d0`/`FUN_002df080` (EE, `DAT_0055b3d8` = player object). Audio is the PSS private stream 0xBD sub-stream
0xFF 0xA1 (SPU2 ADPCM, stereo, 48 kHz; the first 0x28 bytes are an `SShd`/`SSbd` header, interleave 0x20, five language channels `..A10000-4`). The game registers
`FUN_002e07a0` as the audio stream callback (copies the payload into a ring at +0x108, size 0x8000, fill +0x114, sets +0x140 and returns 0 when full), and
`FUN_002e2528` (consumer, once per movie update) deinterleaves 0x400-byte halves with the SPR DMA, `sceSifSetDma`s them to an IOP buffer and uploads them into a 2x0x400
ring per channel in SPU2 RAM with `sceSdRemote(0x80D0 VoiceTrans)`; it polls NAX (`0x8060`), starts the voices with `0x8050`/`0x8030` and sets volumes with `0x8010`.
That is libsd's EE client (`FUN_003d6ae8` = `sceSdRemoteInit`, `FUN_003d6ba8` = `sceSdRemote`, sid 0x80000701, client at 0x58CAC0), not the PSOUND_R RPC the rest of
the game uses.
1. `FUN_003d6ae8/ba8` are bound to the runtime stubs `sceSdRemoteInit/sceSdRemote` (`generated/FUN_003d6ba8_0x3d6ba8.cpp`), which only keep a model and never send an RPC.
   RPC tracing (debug, not kept): zero calls to sid 0x80000701, no SPU2 DMA, `voices 0/0`, although the audio callback ran and the ring was full.
   (SDRDRV.IRX is loaded twice, ids 9 and 13; the second load is the movie system's one-time init.) Fix: real RPC (Audio.cpp). Result: sound, but wrong (below).
2. With the RPCs working the DMA'd data was corrupt: callbacks were queued and run later (`changed_bytes` ~4000 of 4077 in half of them: the game had reused its input
   buffer), and their result was ignored (a full ring let the demux go on; the game's blocking feeder then drops its whole ring: fill 0x7D49 -> 0xFF2 every ~1.8 s, i.e. 0.55 s
   of audio skipped each time). Fix: synchronous callbacks with the result honoured (MPEG.cpp). Verified with dumps (debug, not kept): the accepted PSS payloads
   deinterleaved per the header equal every uploaded half, 1114 rounds / 41.6 s with 0 skips or duplicates.
3. At the host's high vblank rate the HLE paced the movie one picture per 2 vsync ticks: 30 frames per 92-121 ticks at 120 Hz instead of 120 (1.3x too fast on a loaded
   machine, up to 2x on a fast one), so audio arrived faster than the SPU consumes it and fed cause 2's overflow. Fix: tick scale by vblank period (MPEG.cpp). After: 30 frames
   per 121-129 ticks at 120 Hz (ideal 120.1), no refusals, ring fill 0x1200-0x3B87 of 0x8000.
Menu background movies have no audio stream (`AddStrCallback` for video only, no 0xBD packets); they are silent by design.

**Playback check.** WAV (voice 0 = left, gain 0x2CCC/0x7FFF) vs the uploaded halves decoded in Python: contiguous with one constant offset. The only anomalies are voice
underruns when the EE thread stalls: 28 of 1125 upload pairs later than 2 half periods (max gap 176 ms vs 37.3 nominal) on a machine at ~50 % load with other agents'
builds; each repeats <= 75 ms of the SPU2 ring. The consumer polls once per movie update, so a loaded EE thread shows up as repeats.

**WAV levels, `ipu_test.ps1`-style run (movies on, `KZ_FPS=120`, start/cross script, 5 s windows, RMS dBFS / peak), before = `PS2X_SDR_REMOTE=0`:**

| t (s) | before RMS / peak | after RMS / peak |
|---|---|---|
| 0-25 | silent | silent (logos) |
| 25-30 | silent | -35.6 / 0.088 |
| 30-35 | silent | -28.6 / 0.214 |
| 35-40 | silent | -24.4 / 0.318 |
| 40-45 | silent | -18.5 / 0.622 |
| 45-50 | silent | -18.1 / 0.633 |
| 50-55 | silent | -17.5 / 0.764 |
| 55-60 | silent | -17.4 / 0.657 |
| 60-65 | silent | -16.2 / 0.674 |
| 65-70 | silent | -16.4 / 0.657 |
| 70-75 | silent | -15.9 / 0.687 |
| 75-80 | silent | -15.8 / 0.702 |
| 80-85 | -113 / 0.0001 | -28.4 / 0.588 (end of the movie) |
| 85-90 (menu music) | -19.6 / 0.657 | -19.8 / 0.657 |

Menu music 88-135 s, before vs after: correlation 0.993 / 0.925 / 0.9993 / 0.9993, RMS ratio 1.000 (the same constant offset as in earlier findings). Gameplay 150-240 s: RMS
within ~1 dB, correlation low as always (scripted gameplay is not deterministic). `KZ_IPU=off` (100 s): per-10 s RMS equal to 0.1 dB before/after (movie path unused).
`KZ_FPS=60`: intro audio 25-80 s at -37.6...-16.8 dBFS, menu music after. No `TIMED OUT` lines.

**A/V sync.** The movie's first picture and first audio block both have PTS 7386; audio started 0.1-0.2 s before the first picture and led by 0.27 s after 7 s of media on a
loaded machine (video 0.95-1.0x real time there). Neither clock is slaved to the other (the game drives audio by SPU NAX, the HLE video by vsync ticks).

**Not verified.** The runtime unit test "sceMpegDemuxPssRing dispatches ..." (ps2xTest, not built here: `PS2X_BUILD_TEST` is off): the asynchronous path it uses is unchanged and is
taken whenever the scheduler is not executing guest code. A second movie with audio (created around 142 s at mission start) was created in every run but never started its audio
before the level began; it runs the same code. In-level cutscenes were not exercised. `tools/ghidra_scripts/KzRefs.java` lists the functions that reference a data address (used to
find the player's state accessors).


## Menu jitter and blur (2026-10-01, play-test report)
Report: "the loading screen and many menus have vertical jitter", "menus and other spots look blurry" (2560x1440, borderless, D3D11, auto upscale,
VSync off). Gameplay not affected. Everything below was measured headless with a hidden 2560x1440 window (the port's own window path), movies on.

### Tools added
- `KZ_SHOT_BURST=N` (+ `KZ_SHOT_BURST_FROM/TO` seconds, `KZ_SHOT_BURST_PRESENT=1|2`): at every shot time the GS thread copies the next N *presented* frames
  into memory without stalling the game (`gb_<burst>_<seq>_fld<field>_sm<scanmask>_fb...png`, `gb_<burst>.csv` = one line per guest vblank: field, presented or
  skipped as unchanged, display registers, wall time; `*.tc.txt` = PCSX2 texture-cache targets). `=2` hooks the swap chain's `Present` (`ext/kzgs/src/kz_present11.cpp`)
  and saves back buffer 0, i.e. the exact final pixels (Bilinear Sharp, crop, CAS). The old per-second `gs_*.png`/`gp_*.png` readbacks stall the GS thread and
  `GSSaveSnapshotToMemory` recycles pool targets; neither can show per-frame behaviour. `KZGS_PRESENT_LOG=file` logs GS vsync completion times.
- `kzgs_replay --window --backbuffer --skip-idle --single-circuit --sharp --bilinear --cas --filter`; `tools/scripts/burst_analyze.py` (shift / text-detail analysis).

### 1. What is NOT the cause: interlace, field parity, present-skip
- Display registers in every scene of every run (boot logos, cinematics, front end, loading screen, pause menu, gameplay; 1,480 logged presented vblanks): only two states,
  PMODE=0x8063 (both circuits, MMOD=1, ALP=0x80), SMODE2 INT=1 FFMD=0, DISPFB1.DBY=0 / DISPFB2.DBY=1 (circuit 2 one line lower, one line shorter: the game's own
  flicker filter), the two frame buffers FBP 0 and 0x70 alternating. Field alternates every vblank and is not used: PCSX2's `Merge` calls `Interlace(..., mode)` with mode -1
  in Automatic for these registers, i.e. no deinterlacing. Replay of menu frames 4500..4503: Automatic == Off == Weave to 0.02 mean abs diff and 0.00 shift;
  `game_deinterlacing` (the only way to reach the MAD path) was false in 1,020 of 1,020 live presented frames.
  For comparison the modes that would act: Bob TFF/BFF shift the image **+-1 line each field** (frame-to-frame -1.00 +1.00 -1.00, exactly a one-line vertical shake);
  Blend lowers text detail 23 % (Laplacian variance 12506 -> 9618).
- Vertical shift between consecutive presented frames of static content (LK estimate, mean abs diff < 0.1): 342 pairs at 60 Hz, 538 at 120 Hz, 293 at 144 Hz (1x internal),
  171 at 120 Hz on the real back buffer (4x, 1440p): max 0.000 px each (1 native line = 3.2-5 px). So the picture does not move.
- Present-skip: a static screen presents nothing after the first frame (all following vblanks skipped), so it cannot change.
  VS expand is off for D3D11 in the port (the earlier fix); in replays it is deterministic (expand on: doubled text 6 of 6 runs, Laplacian variance 95 vs 158; off: 6 of 6 clean).

### 2. The cause: the second read circuit (Killzone's flicker filter) renders wrong frames (measured)
First-pass mistake: the static-pair statistic above ignores pairs with mean abs diff >= 0.1, which is exactly where this bug lives; periodic mad spikes of equal size in menu
bursts (3.5, 3.5, 0, 0, 0, 3.4 ...) were first read as video. A fixed text region of the live front-end menu ("Campaign", back buffer, 4x) shows it:
- Presented frames alternate between clean text (Laplacian variance ~180-190) and frames where the text and the highlight bar are a **50 % blend of two game frames**
  (text doubled 1-3 native lines apart, variance ~120). Runs of 1-8 bad frames between clean ones, no relation to the field. Picture:
  `work/fix_proof/jitter_menu_before_top_after_bottom_2560x1440.png` (top row: two consecutive presented frames, clean then blended; bottom: after the fix).
- Bad frames among comparable front-end frames before the fix: 20 of 72 (3/7/10 per burst of 24; 120 Hz, final run), 30 of 48 (120 Hz control), 11 of 24 (60 Hz), 15+ of 24 (144 Hz).
- Mechanism (partly shown): both circuits are enabled and read the same buffer; PCSX2's anti-blur (default on) realigns circuit 2 (framebufferRect y 0..448 and 0..447; verified:
  merged output == frame buffer rows exactly, diff 0.000; with anti-blur off the text detail is 16 % lower), but `Merge` still does one `GetOutput` render-target lookup per circuit
  with different sizes (448 vs 447 rows). The two Killzone frame buffers are contiguous (FBP 0 and 0x70 = TBP 0 and 0xE00, 3584 blocks each); the bad frame looks like the second lookup
  returning the other buffer / an older copy. Not shown: why a replay of the same GIF stream (including `--skip-idle`, `--sleep`, hidden window, back-buffer capture, 3 paced runs) is clean while the live GS thread
  produces bad frames; the trigger is timing in the live GS thread (same family as the NVIDIA D3D11 timing-dependent frames in "Doubled / bugged menus").
- **Fix (src/kz_gs.cpp `snapshotPrivRegs`):** when both circuits are enabled, read the same FB/DBX and circuit 2 is at most one line lower and one line shorter than circuit 1 (Killzone's filter),
  circuit 2 is set equal to circuit 1. The merge then has one source and no second lookup. This is the same picture the anti-blur path gives: replay of a gameplay frame (failed.trace f40),
  a front-end frame (menu.trace f4500) and a movie-menu frame (f1935), 4x, back buffer: mean abs diff 0.0000, max 0 (`--single-circuit` vs default). `KZ_FLICKER_FILTER=1` passes the registers unchanged.
- Result, live, 4x, 2560x1440 back buffer, same scene (front-end "Campaign" menu): 120 Hz 20 of 72 bad before, **0 of 72 after**; 120 Hz control pair 30 of 48 -> 0 of 48; 60 Hz 11 of 24 -> 0 of 24; 144 Hz 15+ of 24 -> 0 of 24.
- Also set: `interlaceMode = Off` in `toKzgs` (`KZ_INTERLACE=n` restores a PCSX2 mode). Bit-identical to Automatic for this game, but removes the field from the output by construction.

### 3. Blur
- No flicker-filter blur: anti-blur gives frame-buffer rows exactly (above). The wrong blended frames (above) were a real source of soft/doubled menu text; gone.
- Text detail (Laplacian variance of "Battlefields", front-end menu frame 4500, presented 2560x1440, VS expand off): shipped 4x/PS2 filter/SMAA 158; SMAA off 156 (SMAA does not soften text);
  1x 1233, 2x 747, 3x 284 (GS bilinear magnification of the 512-px text atlas softens more the higher the internal scale; nearest filtering 1318 but blocky); half-pixel offset / native scaling: no effect (158-165).
  Visual comparison favoured the shipped 4x look (smooth strokes) over the pixel-blocky 1x/nearest, so no change.
- **SMAA does soften textures** (the launcher text "without blurring textures" is wrong at the HIGH preset). Gameplay frame (failed.trace f40, 4x): whole-frame Laplacian variance 84 -> 54 with SMAA, 37 % of
  pixels change by more than 4/255. Against an 8x supersampled reference: detail kept 73 % (HIGH, threshold 0.1) / 81 % (0.15) / 86 % (0.2) / 98 % (0.3); error to the reference is not reduced at any internal
  scale (2x -0.5..-0.9 %, 3x-5x +0..+7 % worse with SMAA, texture error +0.4..+2.3 %). Now `[Graphics] SmaaThreshold` (0.05..0.5, default **0.2**, env `KZ_SMAA_THRESHOLD`, `kzgsSetSmaaThreshold`).
- **CAS sharpening works** (the launcher says "Not verified yet"): on the real back buffer Laplacian variance of "Campaign" 180 -> 280 (30 %) -> 291-305 (50 %), gameplay frame 52 -> 84 (30) -> 109 (50) -> 128 (80),
  no clipping, no visible halos in crops. Default `Sharpen=30` (existing ini files with Sharpen=0 and no SmaaThreshold key take it once). Frame-time cost not measured.
- Inherent: the game's UI/loading art is 512x448 (the loading-screen panels are visibly low-resolution textures), the pause menu background is blurred by the game, front-end glow/bloom smears text edges
  ("OPTIONS" in menu.trace f4500), and the menu movie is MPEG-compressed. None of that can be fixed without replacement art (texture replacement exists).

### Not verified
- The real visible window / monitor (hidden-window back-buffer captures only), tearing with VSync off, the player's exact GPU driver state.
- The exact trigger of the live-only blended frames (see above); the fix removes the code path rather than the trigger.
- CAS frame-time cost; gameplay was verified by replay equality (0.0000 diff) rather than a fresh live gameplay burst after the fix.
- Not edited (owned by others): the launcher's SMAA/Sharpening descriptions (`src/kz_launcher.cpp`).

## VU-01 portable interpreter isolation (2026-10-17)

### Existing portable path

- PS2Recomp already contains a cycle-accurate `VU1Interpreter` used by the runtime for both VU1 and VU0. The game
  selects it with `KZ_VU=builtin` and `KZ_VU0=interp`.
- When both built-in modes are selected, `kzVuInstall` returns before `kzvuInit`; no PCSX2 microVU object, x86
  emitter, or JIT code cache is compiled into the portable conformance target or initialized by that runtime path.
- The previous `ext/kzvu` build could not serve as an ARM64 gate: its CMake file deliberately requires MSVC x64,
  defines x86/SSE/AVX flags, and always compiles microVU even when `KzvuConfig::useJit` is false.

### Isolation and corpus

- `KZ_PORTABLE_VU_ONLY=ON` is now an early top-level CMake path. It configures `ext/kzvu/portable` without the
  Windows preflight, private generated tree, SDL, renderers, or any PCSX2 microVU source.
- The `kzvu_interpreter_core` target compiles only PS2Recomp's three VU interpreter translation units. Apple Silicon
  uses the already locked sse2neon revision; `ext/PS2Recomp` remains clean at
  `cc1fc7d87930b48d20abd52974d1be77368eb60a`.
- `kzvu_interpreter_conformance` performs 28 deterministic checks across arithmetic, four-cycle FMAC flag
  visibility, dependent-pipeline stalls, branch delay slots, masked LQ/SQ behavior, wrapping XGKICK packets,
  generation-driven decoded-code invalidation, bounded execution/resume, E- and T-bit stops, reset, and reuse.
- The standalone test glue now provides a real default-constructed `GS` object rather than binding a `GS&` to raw
  byte storage. XGKICK still routes through the lightweight `PS2Memory` capture, so no renderer is linked.

### Evidence and remaining gate

- Fresh portable-only configure/build/test on Windows: pass; only seven compile/link steps, all inside the
  interpreter target and conformance executable.
- MSVC `kzvu_interpreter_conformance`: 28/28 checks passed.
- clang-cl `kzvu_interpreter_conformance`: 28/28 checks passed.
- Existing MSVC microVU/PCSX2/PS2Recomp differential test: passed.
- Complete ISO-free suites after adding the gate: MSVC 10/10 and clang-cl 10/10.
- Strict dependency verification: 28 passed, 0 warnings, 0 failures. Generated inventory and resume-table checks:
  pass. `git diff --check`: pass.
- **Not yet verified:** native AppleClang ARM64 configure, compile, and execution. VU-01 remains in progress, and
  VU-02 must not begin, until `cmake --preset macos-arm64`, `cmake --build --preset macos-arm64`, and the prescribed
  `ctest --preset macos-arm64 ...` command pass on Apple Silicon.

## MAC-01 renderer-free host smoke implementation (2026-10-17)

### Darwin host boundary

- `host/src/core_macos.mm` replaces the generic unsupported backend on Apple hosts. It implements executable-path
  discovery, UTF-8 paths, environment mutation, monotonic timing through the shared core, pthread naming, ARM64/NEON
  feature reporting, and the ARM64 FPCR flush-to-zero mode used for guest execution.
- Anonymous mappings use `mmap`/`mprotect`; instruction publication uses `sys_icache_invalidate`. The backend rejects
  writable-and-executable mappings rather than weakening W^X. The host component test now publishes and executes a
  two-instruction AArch64 function after a read-write to read-execute transition.
- Cocoa metrics are isolated in Objective-C++ and accept only the opaque `NativeWindow::cocoa` handle. They require
  the main thread and report backing-pixel dimensions plus the `NSWindow` backing scale. Crash reporting, stack
  sampling, and statistical profiling remain explicit unsupported errors on macOS.

### Isolated build and smoke corpus

- `KZ_MACOS_PHASE0_ONLY=ON` configures only `kz_host_core`, the two SDL host smokes, and the portable VU interpreter.
  It does not inspect the private generated tree or configure game code, PCSX2 microVU, or a renderer.
- SDL is built from the locked `SDL3-3.4.16.tar.gz` source archive
  (`sha256 7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68`), not an unpinned package-manager
  installation. Strict dependency verification now covers both the Windows development archive and this source tree.
- `kz_host_window_smoke` creates a hidden high-density SDL window, verifies the platform-native handle and drawable
  metrics, delivers a registered user event through the SDL queue, and tears the window down.
- `kz_host_audio_smoke` opens the default playback device, validates its negotiated format, resumes the stream,
  observes and services the callback with silence, pauses it, and destroys the stream. Windows CTest uses SDL's dummy
  audio driver for hermetic automation; macOS intentionally exercises the default CoreAudio path.

### Evidence and remaining gate

- Focused clang-cl host suite: 3/3 passed.
- Complete ISO-free suites: MSVC 12/12 and clang-cl 12/12.
- Strict dependency verification: 31 passed, 0 warnings, 0 failures. Generated inventory and resume-table checks:
  pass. `git diff --check`: pass.
- **Not yet verified:** configuration, compilation, Cocoa main-thread behavior, the CoreAudio callback, AArch64 code
  publication, and teardown under native AppleClang on Apple Silicon. MAC-01 remains in progress and GS-01 remains
  blocked until the prescribed native host-smoke command passes.

## Boot dispatch restoration and renderer isolation (2026-10-17)

### Why a known-working upstream still failed locally

- The failure was not evidence that upstream PS2Recomp could not execute Killzone. The local private input corpus had
  diverged from the executable control-flow surface expected by the recompiler.
- The previous normalization reconstructed discontiguous Ghidra ranges from the exported start plus body size. That
  omitted executable continuations, partial final MIPS instructions, terminal branch delay slots, shared entries, and
  callbacks that Ghidra did not publish as function starts.
- PS2Recomp uses the low bit of `ctx->pc` as `PS2_TAIL_MARK`. Allowing an odd normalized end can therefore decode to
  the preceding aligned address and create a self-dispatch loop. Both starts and ends must remain four-byte aligned.
- Schema-3 normalization now clamps discontiguous bodies, rounds partial instructions to complete MIPS words, and
  includes terminal branch/call/return delay slots. The final map has 35,352 records: 4,269 clamped, 154
  instruction-aligned, and 903 delay-slot-extended.
- Raw Ghidra maximum bounds are not a safe substitute. Thousands of discontiguous bodies span unrelated code or data;
  the schema-3 transformation preserves only instruction-level evidence around the exported body.

### Fixed-point dispatch closure

- Regeneration now treats both emitted tail targets and generated resume cases as executable evidence and closes them
  to a fixed point. Generated resume PCs are not restricted to the ELF section named `.text`.
- The first pass found 2,277 emitted tails and 7,419 resume targets. The second pass found 15 additional resume
  targets. Closure converged with 9,681 unique generated entries after overlap.
- The merged configuration has 4 explicit entries, 24,953 discovered entries, 42 locked hooks, and 9,681 generated
  closure entries, for 34,666 configured entries.
- The generated tree has 31,998 files, 31,996 translation units, 31,995 generated functions, 142 generated stubs,
  132,656 dispatch entries, 100,661 dispatchable resume cases, and 20,528 resume owners. PS2Recomp reports 101,715
  resumable entry points across 20,955 owner functions.
- Registration is global and first-registration-wins. Overlapping wrappers can contain the same resume PC, so the
  correct invariant is that every distinct generated resume PC is present somewhere in the global dispatch table.
  Requiring every overlapping owner to receive the same address incorrectly reported 3,098 failures.
- The original resume blocker at `0x004E9224` already had a valid generated switch case but lacked a global dispatch
  entry. It is now registered to its generated owner, and the manifest verifier enforces the global invariant.

### Callback and SIF boundaries

- Materialized callbacks require narrow structural rules. Publishing every prologue, return, or in-text constant would
  hide corrupted targets and admit embedded VU/data words.
- Discovery now accepts materialized leaf returns, materialized stack-decrement prologues, and materialized targets at
  an unambiguous post-return boundary (`jr $ra`, its mandatory delay slot, optional known padding, then the target).
  Only 15 of 252 otherwise-unclassified materialized in-text values satisfy the post-return rule.
- The stack-prologue rule recovered `0x002B4288`, `0x004888E8`, and callback `0x004BBF50`. The latter is materialized
  at `0x003440D4..0x003440E0`, passed to a registration routine, and begins in a Ghidra range gap.
- The post-return rule recovered the FilePS2 command handler at `0x0014B6A0`. Guest code materializes it at
  `0x0014B804..0x0014B80C` and passes it to `sceSifAddCmdHandler` at `0x0014B810`.
- `0x002B3158` has legitimate dual ownership: it is both the `sceSifInitRpc` entry and the delay-slot instruction of
  the preceding return. It is an explicit configured entry and is rebound authoritatively after generated-table
  initialization so first-registration-wins cannot leave a generated owner in the runtime hook slot.
- The fixed hook manifest now contains 42 unique signatures. `sceSifInitRpc` uses a 20-byte signature because its
  first 16 bytes also occur at `0x002B66F8`.

### FilePS2 completion and ISO mounting

- When both `--elf` and `--iso` are supplied, the boot path now extracts and mounts the disc files at `cdrom0:` rather
  than leaving the explicit ELF's directory as the disc root. `PFILE_R.IRX` and the later boot modules are therefore
  available to the guest while the ISO still backs raw sector reads.
- The IOP completes FilePS2 RPC SID `0x66662012`, function `0x320`, and posts command ID 0 to the EE handler at
  `0x0014B6A0`.
- Threaded IOP delivery can conceal a missing EE callback. Posting reports success when it queues the command, and the
  later EE-side drain discards the boolean returned by `dispatchSifCommand`. A missing handler therefore left the
  guest completion counter unchanged without emitting the usual missing-target diagnostic.
- With the callback dispatchable, boot advances through FilePS2 completion and all subsequent observed IOP module
  loads. The current probes produce no missing-target diagnostic.

### D3D11/D3D12 split

- A 15-second timeline recorded 862 DMA1 kicks, 861 completed VIF/GIF jobs, and 845 GS presents. Longer D3D11 probes
  reached DMA 3,699 and VIF 3,696, with active PATH1 traffic, thousands of VU1 XGKICK packets, and increasing
  primitive counts.
- GS state matches known-good Killzone behavior: `PMODE=0x8063`, `SMODE2=1`, and framebuffer bases alternate between
  `0` and `0x70`. Guest execution and frame production are no longer stalled.
- On an NVIDIA GeForce RTX 5070 with driver `32.0.16.1742`, D3D11 captures remain exactly all-zero pixels through
  90 seconds despite that healthy guest and GS state.
- The same executable, ELF, ISO, and guest state render correctly through D3D12. A persisted-config probe, with no
  `KZ_RENDERER` override, selected D3D12 and captured advancing SCEA/legal intro frames at 4, 8, and 12 seconds.
- The local MSVC configuration now persists `Renderer=d3d12`. D3D11 remains selectable; the launcher tells users to
  try D3D12 when D3D11 presents a black frame. This is a working configuration and backend isolation, not a claim
  that the D3D11 defect has been repaired.
- `dev9: unknown dev9 hardware` and unhandled `cdvdman:78` remain nonfatal; neither prevents D3D12 rendering.

### D3D11 re-verification (2026-10-20)

The historical all-zero result no longer reproduces on the same RTX 5070 and driver `32.0.16.1742` with the current
pinned renderer stack. The hardware-adapter D3D11 golden passed at 1x and 2x with zero pixel mismatches. A 35-second
isolated-card chapter 1 run exited 0 and produced nonblack captures from 10 seconds onward (35.0-99.1% nonblack).
Four captures taken from the actual D3D11 swap-chain back buffer were also nonblack (55.5-71.4%). Finally, the
original no-input 90-second scenario exited 0; its eight captures were distinct, and frames from 20 through 80
seconds contained rendered output with nonblack coverage up to 94.4%.

No renderer behavior was changed to force this result. The PCSX2 checkout did move from
`646df006a4688063e56c0826e7ea4b8bb0c112c4` at the failed measurement to
`d3e6982a3e36647d642bcd805d3ca5b442d0975c`, but the exact clearing change was not isolated. WIN-01 is closed as no
longer reproducible on the supported pinned stack; the earlier finding remains above as historical evidence.

### Late black screen: shadowed runtime stubs

- Early D3D12 acceptance did not cover the complete boot path. A 60-second capture later showed valid SCEA, legal,
  and Guerrilla frames through about 20 seconds, followed by exactly black frames from 25 seconds onward while
  DMA/VIF/GS counters continued to advance. This was a second failure, independent of the D3D11 presentation issue.
- The first concrete stall was a physical `MCSERV.IRX` RPC (`SID 0x80000400`, function `0x1`) entering MCMAN and
  timing out twice in `WaitSema(6)`. The BIOS-less IOP does not emulate the SIO2 memory-card transfers that physical
  MCMAN expects, but this was not the final root cause.
- Routing only `MCSERV.IRX` to the runtime HLE removed that semaphore timeout. Its original initialization reply
  advertised MCSERV/MCMAN versions `0x0205`/`0x0206`, below Killzone's minimum `0x020A`/`0x020E`; matching the retail
  modules' `0x0210`/`0x0226` let the guest issue `GetInfo`. The screen still remained black in both cases.
- The merged configuration classifies `sceMcInit@0x002BE788`, `sceMcGetInfo@0x002BF180`, and
  `sceMcFormat@0x002BF458` as runtime stubs, but overlapping generated functions had already claimed those table
  slots. `sceMcSync@0x002BF060` was a real generated host stub. Boot therefore mixed guest MCSERV RPC state with the
  host stub's independent memory-card state and could never complete the intended synchronization sequence.
- Authoritative post-registration bindings for all three shadowed memory-card entries restore one coherent host
  implementation. The successful path performs three `GetInfo`/`Sync` sequences and one `GetDir`/`Sync` sequence.
  With those bindings in place, physical MCSERV remains loaded but receives no boot-critical memory-card calls, so
  the selective HLE loader and version changes were removed.

### Movie black screen: asynchronous stub return contract

- The same ownership audit found generated owners at `sceMpegGetPicture@0x003CD750` and
  `sceMpegClearRefBuff@0x003CD9F8`, despite both addresses being configured runtime stubs. The other observed
  libmpeg entries were generated as host-stub wrappers correctly.
- Rebinding the two addresses directly to raw runtime handlers was not sufficient. A generated stub first assigns
  `ctx->pc = $ra` and then calls its host handler. The direct binding helper can infer the fallthrough only for a
  synchronous return; `sceMpegGetPicture` can suspend in an external MPEG wait and resume later, so the saved context
  must already contain `$ra`.
- Diagnostic runs proved FFmpeg was decoding and serving `512x448` frames while the guest remained black: without
  the wrapper contract, resumed calls retained `pc=0x003CD750` and re-entered the handler instead of continuing at
  the caller. Killzone-specific wrappers now set `pc` to `$ra` before invoking both MPEG handlers.
- After that correction, captured D3D12 frames show the decoded opening movie continuously from 35 through at least
  70 seconds. Scripted input reaches the rendered main menu by 75-80 seconds with IPU enabled. The minimized final
  probe keeps physical `MCSERV.IRX`, reports successful host memory-card operations, shows no MCSERV semaphore
  timeout, renders the movie, and reaches the menu.
- This explains why upstream could be known-working while this build failed: runtime classification was correct,
  but first-registration-wins let overlapping local generated owners shadow authoritative host boundaries. The fix
  is local address ownership, not a change to the upstream memory-card service or decoder.

### Final Windows evidence

- A second regeneration converged to 31,998 files with **0 changed/new and 0 removed**.
- Resume-table verification: 132,656 dispatch entries, 100,661 resume cases, 31,995 generated targets; pass.
- Generated inventory and schema-3 provenance lock: pass, including all 42 unique fixed hook signatures.
- Strict offline dependency and PS2Recomp patch reconstruction: 31 passed, 0 warnings, 0 failures.
- Final MSVC ISO-free suite: 12/12 passed.
- Final clang-cl ISO-free suite: 12/12 passed.
- Persisted D3D12 12-second visual probe: exit code 0 and three nonblack, advancing captures.
- Complete D3D12 movie probe: nonblack decoded FMV frames at 35, 40, 45, and 50 seconds.
- Complete minimized boot probe: physical MCSERV loaded, host memory-card operations completed without a semaphore
  timeout, opening movies rendered, and the main menu was visible at 80 seconds.

## MPEG playback cadence and queue starvation (2026-10-17)

### Symptom and exclusions

- The opening cinematic, animated menu background, and especially the idle attract reel visibly paused and advanced
  in bursts. The active configuration used D3D12, VSync off, an unlimited launcher frame cap, and a 59 Hz display, so
  this was separate from the previously fixed high-refresh movie-clock scaling.
- A 90-frame D3D12 burst during the opening cinematic changed image only 31 times, about 20.9 changes/s. Most changes
  were two vblanks apart, as a 29.97 fps movie requires, but repeated four-vblank stalls and occasional six-to-eight
  vblank stalls produced the visible choppiness.
- The MPEG sequence header reports frame-rate code 4 and the decoded timestamps advance by 3,003 ticks on a 90 kHz
  clock: the source is 29.97 fps, not an intentionally low-rate asset.
- Disabling synchronous movie-audio callbacks produced 30 changes in the same 90-frame window (20.22/s). Temporarily
  removing HLE picture-presentation pacing produced 29 (19.55/s). Audio-ring backpressure and over-throttling were
  therefore not the principal cause; decoded pictures were unavailable when the game asked for them.

### Root cause

- A detailed intro trace recorded 296 elementary-stream feed events producing 321 decoded pictures. The queue after
  a feed was normally one or two pictures and never exceeded four.
- Across 318 served pictures, the normal two-vblank gap occurred 209 times, but there were 71 four-vblank, 21
  six-vblank, nine eight-vblank, and five ten-vblank gaps. The runtime entered its empty-queue wait 496 times across
  316 unique vblank ticks even though almost every timestamped picture had the correct 3,003-tick PTS delta.
- `sceMpegGetPicture` invoked `sceMpegCbNodata` only after the decoded queue reached zero. Killzone has no separate
  `sceCdSt` producer and normally demuxes only one or two pictures in that callback. The decoder consequently ran dry
  before each asynchronous file read and demux refill completed.

### Low-water prefetch

- For self-fed movies, `sceMpegGetPicture` now invokes the registered nodata callback before returning the current
  picture when four or fewer decoded pictures remain. At most two proactive callbacks run before one picture is
  presented, which preserves refill throughput without holding the guest away from movie-audio maintenance for an
  unbounded callback chain.
- Proactive refill is disabled for `sceCdSt`-produced streams and after CD EOF, MPEG sequence end, stream end, or
  decoder failure. The existing no-progress state is consumed on callback re-entry, so an asynchronous read that has
  not completed presents the available picture rather than recursively invoking the callback.
- The first experiment reached an eight-picture queue, held four to six pictures through normal playback, reduced
  waits to 19 events on 13 unique ticks, and produced 499 ideal two-vblank gaps among the first 511 measured picture
  gaps. Its 90-frame visual burst changed 42 times instead of 31.
- The cleaned MSVC build, with all temporary timing and unthrottled-presentation diagnostics removed, measured:
  - opening cinematic: 49 image changes over 1.6169 s, or 30.3 changes/s;
  - animated main-menu background: 43 over 1.4664 s, or 29.32 changes/s;
  - idle attract reel: 44 over 1.4724 s, or 29.88 changes/s. Of its measured change gaps, 42 were the ideal two
    vblanks and one was four vblanks.
- A 380-second timeline entered the attract reel by 160 seconds, showed advancing reel frames through 240 seconds,
  returned to the menu by 260 seconds, and exited normally.
- The standalone PS2Recomp test target compiles the change, and the new scheduler regression,
  `sceMpegGetPicture caps proactive refills before presenting a picture`, passes. The broader upstream test
  executable still reports unrelated existing failures and terminates with an access violation after the MPEG tests;
  it is not represented as a full-suite pass.
- Final integration checks pass: strict dependency reconstruction reports 31 passed, 0 warnings, and 0 failures;
  both MSVC and clang-cl ISO-free suites pass 12/12; the generated inventory retains 42 locked hook signatures; and
  the resume table retains 132,656 dispatch entries, 100,661 resume cases, and 31,995 generated targets.

## FMV audio continuity under scheduler stalls (2026-10-17)

### Two separate failure layers

- A muted real SDL/WASAPI capture of the original four-picture-prefetch build inserted 5,536 zero frames during the
  27-80 second opening-movie window: 115.3 ms of host-device underrun. `kz_audio.cpp` had a nominal 40 ms target, but
  used it only as the destination of a high-water skip; playback began immediately with no actual reserve.
- The raw pre-device SPU2 stream also stuttered. Comparing stereo PCM against itself at a 3,584-frame lag found 67
  exact-equality intervals, 1.433062 seconds total, with a 116.812 ms maximum. Equality at exactly one complete guest
  movie ring proves that SPU2 replayed stale ADPCM rather than the host device merely repeating or dropping samples.

The state-2 movie uploader transfers `0x400` ADPCM bytes per channel at a time. One bank decodes to 1,792 frames
(37.33 ms at 48 kHz); the original two-bank `0x800` ring therefore wraps every 3,584 frames (74.67 ms). A normal
29.97 fps picture update is 33.37 ms apart, leaving little tolerance for a delayed movie update.

Low-overhead refill instrumentation recorded 1,434 nodata callbacks across 1,405 presented-picture attempts:
715 callbacks appended PSS data, 719 made no progress, 632 audio packets were accepted, and zero audio packets were
refused as full. Long repeats aligned with movie-picture call gaps of 16-21 fields (267-350 ms). The remaining problem
was therefore missed guest uploads during scheduling/refill stalls, not MPEG audio-ring backpressure.

### Rejected experiments

All repeat counts below use the same 27-80 second window, exact stereo equality at the active full-ring lag, and a
minimum run of 256 frames.

| Experiment | Intervals | Total stale audio | Maximum | Result |
|---|---:|---:|---:|---|
| no MPEG low-water prefetch | 211 | 11.116 s | 330.67 ms | much worse |
| four-picture prefetch baseline | 67 | 1.433062 s | 116.812 ms | reference |
| seven-picture prefetch | 76 | 13.188667 s | 843.958 ms | severe regression |
| one proactive callback per picture | 106 | 2.311021 s | 110.896 ms | insufficient demux throughput |
| `PS2X_WALLCLOCK=0` | 81 | 1.846125 s | 105.750 ms | wall-clock floor is not the root cause |
| no periodic screenshots | 92 | 1.820500 s | 240.542 ms | readback is not the root cause |
| nonblocking compatible picture return | 58 | 1.576271 s | 174.250 ms | no continuity improvement |
| `KZ_AUDIO_PACE=0` | 89 | 3.080 s | 844.35 ms | pacing is protective |

Calling `FUN_002df080` or `FUN_002df790` from a host-scheduled context corrupted or stalled the stream and was removed.
Larger rings alone also trade fewer misses for much longer repeats. With unlimited refill recursion, four, eight, and
sixteen banks still produced full-ring repeats. Even after bounding refill recursion, an eight-bank ring produced
five and four 298.7 ms-ring intervals in paired runs (0.743500/0.744833 seconds total, 254.146/252.854 ms maximum).

### Accepted guest fix

- Twelve locked instruction patches in `config/killzone.toml` expand the stereo SPU2 allocation from `0x1000` to
  `0x2000` bytes and each channel ring from `0x800` to `0x1000`. Allocation size, second-channel base, channel stride,
  modulo shifts, wrap bias, available-space calculation, and ADPCM loop-end placement all change together. The
  original instruction words and hashes are locked in `config/generated-manifest.json`; private generated files are
  not published.
- Proactive MPEG refill is capped at two callbacks per presented picture. The counter belongs to each MPEG playback
  object and resets after a picture is consumed. This keeps the four-picture low-water benefit while bounding one
  recursive guest refill burst. The focused PS2Recomp regression forces continuous PSS progress and verifies that
  exactly two callbacks run before presentation.
- Three low-overhead four-bank/capped captures contained **zero** old 3,584-frame repeats. Representative new
  7,168-frame results were 11 intervals / 0.783479 s / 101.375 ms, 7 / 0.503042 s / 102.188 ms, and
  12 / 0.878833 s / 106.083 ms. The larger ring cannot manufacture missing source audio during an exceptional
  scheduler stall, but it removes the frequent 74.67 ms wrap/replay that caused the original pronounced stutter.
- Video cadence remains correct: a 90-presented-field D3D12 intro burst changed image 44 times; 42 of 43 change gaps
  were the ideal two fields and one was four fields.

### Accepted host fix

The SDL callback now waits until the SPU2 output ring contains the current device request plus a real 40 ms reserve
before consuming its first frame. High-water recovery retains the same reserve after the current request. It does not
re-arm after a later deficit, because that experiment converted three 10 ms underruns into 130 ms of deliberate
rebuffer silence.

On the same 480-frame WASAPI device, paired muted 90-second runs reduced movie-window underruns from 5,536 frames
(115.3 ms) to 1,288 and 3,283 frames (26.8 and 68.4 ms), reductions of 77% and 41% despite host-timing variance.
An 80 ms reserve was rejected after two runs developed multi-second emulator stalls and catch-up bursts; the
conservative 40 ms policy is the validated default.

The user accepted the final D3D12 build subjectively: "Audio seems fine for now." Together with the objective
captures, this closes the current FMV-audio issue. Rare 149.3 ms-ring misses remain possible when the whole movie
update is delayed for several hundred milliseconds, so a future regression report should include the exact scene
and timestamp before this policy is changed.

### Integration status

PS2Recomp revision `6e4bc1827a65d45be92d10f9eca97c04f062f4b8` contains the two-callback budget and
focused regression; `patches/ps2recomp/0029-mpeg-refill-budget.patch` and the 61-path canonical diff reconstruct
that tree. Parent implementation commit `4ed9b95b877ad62fa58bd1d87dc88e4251b929ab` contains the coordinated guest,
runtime, host-output, and dependency-lock changes.

Final integration validation passed strict dependency reconstruction (31 checks), generated inventory, resume-table
verification, the complete MSVC ISO-free suite (12/12), and the complete clang-cl ISO-free suite (12/12). The broad
standalone PS2Recomp test executable still reaches its pre-existing `sceMpegDemuxPssRing` failure and later access
violation; only the focused MPEG regression was represented as passing at that revision. WIN-03 resolves this later
below.

## PS2Recomp standalone suite restoration (2026-10-20)

The prior broad-suite failure combined one stale MPEG fixture with an independent null-runtime crash. The PCM stream
callback was registered with libmpeg type 2 but fed a private-stream header that could not match the verified
`BD FF A0 00 00` key. After that assertion, the first `sceSdRemote` regression called `sceSdRemoteInit` without a
`PS2Runtime`; the new SDRDRV probe dereferenced that null runtime before reaching the fallback model.

Correcting the PCM header and guarding the optional runtime removed both symptoms. The now-completing suite exposed
seven VIF regressions plus stale JALR and SIF expectations:

- the PCSX2-derived UNPACK path treated zero `STCYCL.CL` as zero source vectors instead of 256;
- two PATH2 fixtures placed raw image bytes outside a subsequent `DIRECT`, contradicting the corrected parser;
- the unresolved-JALR test still expected broad caller fallback entries even though calls intentionally publish only
  their return PC;
- the SIF test sampled threaded IOP cycles synchronously instead of testing persistent IOP state.

Zero `CL` now follows hardware semantics, and the fixtures assert the current contracts. The minimal harness also
supports `PS2X_TEST_FILTER=<substring>` for focused reproduction. Focused audio (2/2), VIF (29/29), JALR (2/2) and
SifInitRpc (1/1) runs pass. The complete standalone executable now exits 0 with 439/439 tests passing.

## Versioned measurement harness (2026-10-20)

TEST-01 extends the existing screenshot and burst paths without renaming their artifacts. Every run with
`KZ_SHOT_DIR` writes `capture.jsonl` schema 1. Its run record contains only protected-content-safe state: the
guest-vsync clock/rate, source ID, renderer/backend identity, adapter, effective graphics settings and effective
testing/card mode. It deliberately excludes the ISO, ELF, memory-card and output-directory paths.

Each saved internal, presented or swap-chain image gets a frame record with capture point, dimensions, present
sequence and `frame_id == guest_vsync`. Burst metadata resolves kzgs' sequence to the host present sequence through
the vblank log; the existing burst CSV now also carries `guest_vsync`. The guest pad provider records transitions of
default fire action R1 as `player_fire_input`, tied to guest-vsync and present sequence. This is a deterministic input
event for measurement alignment, not yet a claim that a projectile was emitted; WPN-01 can add an engine-confirmed
shot event after discovering that path.

`verify_capture_metadata.py` validates records and referenced images, rejects absolute paths, generates summaries
containing hashes/counts/dimensions/configuration only, and aligns repeated runs by nearest same-type event within one
guest second. Two 38-second D3D12 chapter-1 runs exited 0 with seven captures each. Four fire transitions aligned
within two guest-vsyncs; a middle scripted pulse occurred while one run was not polling the pad and is reported as
unmatched rather than silently discarded. A presented-output burst run produced four monotonic 1280x896 frame
records. The contract self-test and product self-test pass.

## D3D12 effective post-process parity (2026-10-20)

The selected policy is explicit capability reporting, not an implicit fallback: SMAA remains available only on
D3D11. D3D12/Vulkan gray the launcher control, preserve its persisted request for a later D3D11 switch, and direct
users to explicit FXAA or higher internal resolution. D3D12 with the accepted baseline reports
`aa=none, cas=30, scaling=sharp_bilinear, crop=4,3,4,4 (SMAA requested but unavailable)` in both stdout and
`capture.jsonl`.

To measure the final chain, D3D12 burst capture now copies the active swap-chain render target inside a compiler-safe
`EndPresent` vtable hook, after scaling/CAS draws and before transition/Present. The clang-cl virtual-call thunk form
resolves to slot 19. The copy uses PCSX2's own D3D12 download texture, restores the resource state and does not touch
the protected game inputs. Offline replay gained the same post controls plus exact back-buffer capture.

A single 120-frame active-gameplay GS trace drove all arms. Four 1600x900 frames per arm produced:

- requested SMAA on D3D12: 4/4 byte-identical to baseline (disabled is truly baseline-identical);
- FXAA: 8.611% pixels changed, mean absolute RGB 0.0985, max 36;
- CAS 30 versus off: 19.243% changed, mean 0.1576, max 16;
- sharp versus smooth bilinear: 32.816% changed, mean 0.2750, max 25;
- crop `4,3,4,4` versus uncropped: 65.555% changed, mean 2.4556, max 120.

The D3D12 hardware integration test also passes an 800x600 swap-chain resize, zero-size minimize followed by 640x448
restore, injected device-loss recovery through PCSX2's normal reopen path, GIF ring stress, packet continuation and a
D3D12-to-D3D11 renderer restart. All recovered outputs are nonblack and have the requested dimensions.

## Internal render scale versus physical output (2026-10-20)

No configuration migration or quality-default change was needed. The previously ambiguous behavior is now an
explicit contract:

- `Upscale=0` selects `ceil(output_height / 448)`, clamped to 1x-8x, at renderer start or when settings are applied;
- `Upscale=1..8` is a fixed integer GS render scale, independent of physical output resize;
- resize changes the physical swap-chain output only; zero-size minimize retains the last valid dimensions;
- aspect/crop, CAS and sharp/smooth filtering remain presentation operations, with the declared diagnostic order
  `gs_merge -> aa -> sharp_prescale_if_upscaling -> crop_aspect -> cas -> present_filter`;
- `KZ_UPSCALE` is the explicit automation override for controlled sub-output experiments.

The host logs the scale decision immediately, then `kzgsGetResolutionInfo` publishes exact dimensions after a
completed frame. Exact dimensions cannot be predicted as `640 * scale`: the synthetic kzgs scene uses 640x448, while
Killzone's active display measured 512x448. `capture.jsonl` records both the pre-frame 448-line selection contract and
the completed-frame dimensions, and the verifier checks that the exact render height and physical output agree with
that contract.

Four eight-second D3D12 product runs with legacy `Upscale=0`, isolated cards and no path-bearing summary data passed:

| Physical output | Scale | Exact internal render | Captures | Exit |
|---|---:|---:|---:|---:|
| 1280x720 | 2x | 1024x896 | 3 | 0 |
| 1920x1080 | 3x | 1536x1344 | 3 | 0 |
| 2560x1440 | 4x | 2048x1792 | 4 | 0 |
| 3840x2160 | 5x | 2560x2240 | 3 | 0 |

An independent hardware synthetic run held the internal scene at exactly 1280x896 (fixed 2x) while changing the
swap chain through all four output sizes. Every final-back-buffer capture was nonblack and exact-sized. The test then
restored an ordinary decorated window and passed minimize/restore, device-loss recovery and D3D12-to-D3D11 restart,
showing that output history and renderer recreation do not silently rewrite the fixed internal scale.

## GFX-03 control qualification (2026-10-20 to 2026-10-21)

The inventory covers two guest controls (`RenderDistance`, `LodScale`) and three renderer controls (anisotropy,
texture replacement and CAS). Default `RenderDistance=1`/`LodScale=1` installs no detail hooks. Elevated controls
install independently: a 2x render-distance arm produced far/fog calls and no LOD calls; a 4x LOD arm produced LOD
calls and no far/fog calls. The first LOD wrapper was not effective: it restored `obj+0x1C` before generated
continuation `0x1C21B0` consumed it. The final hook is the call-site-gated setter continuation at `0x151E64`; at 4x it
changes the actual selector input from 1.436 to 5.742. Environment overrides reject nonnumeric/non-finite values;
`KZ_RENDER_DISTANCE` clamps to the supported 1..2 range and product `KZ_LOD_SCALE` remains fixed at 1.

The human Escape reproducer found a critical interaction: render distance 2x plus LOD 4x froze about one second into
level load with buzzing audio. The standard isolated run reproduced a guest stall at `pc=0x1A` with DMA/VIF fixed at
2097/874 while host vblank advanced; 2/3 additional combined runs froze, and a 1.5x/4x arm reached an invalid
`0x80001828` guest indirect target. Baseline and render 2x alone advanced through Escape. Although final LOD 4x
advanced in the headless gate and logged the expected `1.436 -> 5.742` mutation, the second hands-on test froze again
with view distance 1x. Headless success is not accepted over that interactive failure: `LodScale` is now constrained
to 1x in config/environment resolution and shown unavailable in the launcher. An existing config with an elevated
value installs no LOD hook; the isolated Escape validation advanced and exited 0.
The final hands-on safe build showed LOD unavailable at 1x, exposed the mipmap selector, and loaded/played Escape
normally. Audio cutouts still occurred in heavier scenes and remain AUDIO-01 rather than a graphics acceptance issue.

The first exact-clock scene matrix used one cloned card, identical input, six captures at guest-vsync
1800/2160/2520/2880/3240/3600 and six matching fire transitions in each arm. All three arms exited 0. It is not valid
visual acceptance: a same-arm baseline repeat changed 80.377% of pixels with mean absolute RGB 13.2611. Against the
first baseline, 2x render distance changed 91.828% / 12.5122 and 4x LOD changed 89.304% / 19.4528. Exact guest clocks
do not remove front-end, loading and tutorial progression variance, so no image delta below or near that repeat
variance is attributed to a control.

Gameplay-relative automation now provides a narrower live measurement clock. `KZ_INPUT_SCRIPT=g<t>:...` schedules
input from the first validated local-player controller update, and `KZ_SHOT_GUEST_FROM=g<n>` schedules captures `n`
guest vblanks from the same origin. Capture metadata records `gameplay_offset_vsync`, which image comparison prefers
when both arms provide it. At offset 150, a cloned-card same-arm repeat fell to 56.513% changed / 0.9629 mean absolute
RGB, much lower than the boot-clock repeat but still not pixel deterministic. A paused six-frame repeat measured
23.189% / 0.2070 mean; most of that is low-amplitude pause-filter noise.

The replacement LOD investigation is deliberately narrower than the withdrawn shared-context mutation, but its first
iteration was still too broad. Gating point-to-AABB helper `0x2C8178` only by helper return `0x3F4F84` admitted both
outer callers of the blended selector. The 90,000-call / 39,626-change (44.03%) result is therefore withdrawn: its
slots were filled by the `+0x2C` static-resource path (`0x3EF81C`), not character resources.

The corrected diagnostic accepts only the `+0x30` skinned-resource paths: simple selector return `0x3F4EA0` with
saved outer return `0x3FF3FC`, and blended selector return `0x3F4F84` with saved outer return `0x3FF4F0`. At 1x it
executes the original guest helper; above 1x it divides squared distance by scale squared. Static resources,
RenderZone state and scratchpad `renderContext+0x1AC` remain untouched. `KZ_CHARACTER_LOD_RESOURCE_LOG=<offset>`
records runtime resource bounds and thresholds, while `KZ_CHARACTER_LOD_PROBE_CENTER=x,y,z,radius` limits mutation
to one world-space group.

At gameplay offset 180 the visible corpse resolves to three simple skinned parts with center approximately
`(-461.80, 113.65, 34.96)` and authored threshold 3.0. Unzoomed metric `4.424` selects level 1; rifle zoom changes the
same parts to metric `2.072`, selecting level 0 without any probe. A radius-1 unzoomed 4x arm changes the target to
metric `1.106` and changed 495 of the first 10,000 skinned selections; the 90-second run exited normally.

The targeted visual result is still not acceptable. At offset 180, corpse/tight-ROI same-arm means were
3.3385/3.7678. The targeted probe measured 3.7330/4.3641 against one baseline and 6.5207/7.4372 against the other.
Although both cross-run comparisons are higher, the margin against one repeat is small. More importantly, a
same-process 180..300 targeted window produced a corpse activation-edge mean of 4.4227 while adjacent baseline
temporal change was 8.8006; the deactivation edge was 3.4884 versus 2.9091 post-window temporal change. No clean,
reversible mesh delta appears above scene motion. The diagnostic remains useful for attribution, but character LOD
is a no-go for this milestone: no launcher setting replaces `LodScale`, the product clamp stays 1x, and hands-on or
campaign/high-water probe gates are not justified.

Render-distance qualification now has a repeatable campaign gate. `tools/scripts/gfx03_campaign_gate.ps1` clones the
testing card for each baseline/render-2x arm, requires the selected chapter, uniform frames, no missing target or host
exception, the original 1x path in baseline and measured far/fog calls at 2x. `run_headless.ps1` samples the child
process every 250 ms and writes peak working-set, paged-memory and virtual-memory values to `process.json`; summaries
contain no retail paths or image content.

The first representative matrix ran chapters 1, 5 and 10 for 90 seconds per arm. All 6/6 runs exited 0 and produced
17 `2048x1792` captures. First 2x measurements were:

| Chapter | Far plane | Fog | Peak working set baseline / 2x |
|---:|---|---|---:|
| 1 | `70 -> 140` | `-10..70 -> -20..140` | 631.8 / 659.4 MiB |
| 5 | `120 -> 240` | `10..120 -> 20..240` | 531.3 / 526.2 MiB |
| 10 | `150 -> 300` | `-10..150 -> -20..300` | 547.8 / 500.8 MiB |

Working-set deltas are +4.37%, -0.96% and -8.59%; paged and virtual peaks likewise show no monotonic increase.
DMA/VIF counters continued advancing through every run. This qualifies the mechanism and three representative
checkpoints. Human review accepted the fog at 2x and made it the supported maximum: larger values reveal enemy spawn
activity and low-polygon background geometry that the authored fog is intended to hide. `RenderDistance`, the
`KZ_RENDER_DISTANCE` override and the launcher therefore clamp to `1..2`; higher values report the clamp. Chapters
2-4 and 7-9 subsequently passed both baseline and 2x arms. Chapter 2 needed a 90-second 2x retry before it reached the
far/fog consumers. First transformed values across those chapters were `150/10..150 -> 300/20..300`,
`110/0..110 -> 220/0..220`, `45/-5..45 -> 90/-10..90`, `45/0..45 -> 90/0..90`,
`120/20..120 -> 240/40..240`, and `60/10..60 -> 120/20..120`.

Chapter 6 is not valid memory-comparison evidence. Fixed-wall 90-second arms can pass and measure
`50/-10..50 -> 100/-20..100`, but baseline and 2x reach active rendering at different times. A new automation limit
counts presented frames after the local-player origin and requires 30 seconds of actual rendering; it passes a
chapter 5 smoke but exposed chapter 6's nondeterminism. Baseline retries reached missing targets `0x08414615` from
`0x2E67DC`, `0x0` from `0x1C8544`, and `0x20000` from `0x3B743C`, each followed by fixed DMA/VIF counters. One 2x
retry exited with host access violation `0xC0000005` after reporting malformed render heights down to 16-24 pixels.
Neither arm completed the aligned duration, so the prior 426.7/808.0 MiB comparison is withdrawn and no failure is
attributed to render distance. These runs exercise direct chapter selection, which is now diagnostic-only.

A focused baseline-only run (`gfx03_c06_allocator_diag1`) enabled the retained STAB-01 heap hooks with page watches
disabled. At guest vsync 5242, cleanup owner `0x008044C0` passed `0x00FA0CA0` and then `0x0001000A` to `free` from
return `0x2E819C`. Neither is a heap chunk: the surrounding words are a resource-record sequence containing
`0x500`, `0x1000A`, `0x00FA0CA0`, `0x2820`, and subsequent numbered records. The calls came through the known
`0x2E7B38 -> 0x2E7440 -> 0x2E8170` cleanup chain and were immediately followed by target `0x52F110` from virtual
call `0x2E67DC`. A saved caller link slot identified `0x01F56E94`, but a second exact-word run
(`gfx03_c06_linkwatch_diag2`) used a different allocation layout; it instead stalled at `0x2E6620`/return
`0x2E68F0` with DMA/GIF/VIF fixed at `2753/390/798`.

These baseline arms install no render-distance hooks. They localize Chapter 6 to the same cleanup/list and allocator
investigation boundary as the historical STAB-01 work, but do not prove that Chapter 6 and Chapter 11 share an
identical first writer. The result is retained as diagnostic evidence and is not a supported-path acceptance gate.

Deterministic GS replay isolates renderer-only controls. On the first active-gameplay trace:

- anisotropy 0x versus 16x is 4/4 byte-identical;
- texture replacement off versus on with an empty replacement directory is 4/4 byte-identical;
- CAS 30 remains independently measured by GFX-01 (19.243% changed versus off).

That trace has 130 `TEX1` writes but no mip-enabled state (`MXL=0`, `MMIN=0`), so a mipmap option cannot change it.
The docking trace has 148 mip-enabled states (`MXL=3`, K from -2.8125 to 0). Dense capture of all frames 83-107 found
PS2 mipmapping versus base-only identical in 24/25 frames; frame 86 changed 2.87% of pixels (mean absolute RGB 0.03,
max 19), for an aggregate 0.115% changed / 0.0010 mean. Config schema 2 therefore exposes two measured modes:
`Mipmapping=ps2` (default) and `Mipmapping=highest` (disables `Mipmap`, `HWMipmap` and trilinear selection so level
zero is sampled). Adjustable transition bias is deferred because PCSX2 has no corresponding public GS option.

The frame-86 mip delta is spatially coherent rather than diffuse replay noise. All 26,345 changed pixels are within
`x=378..648`, `y=316..576`, covering the distant bulkhead behind the squad; base-only restores its rivet and panel
pattern. Characters occlude the delta and the HUD is unchanged. Changed pixels average 0.879 RGB levels with a 3.0
95th percentile and maximum 19, so the mode is a narrow texture-detail option rather than a global sharpening claim.

PCSX2 enables anisotropy only for triangle primitives which do not use its shader-emulated sampler and do not use
manual trilinear LOD. Paletted textures, render-target textures, complex clamp/depth cases, sprites and manual-mipmap
draws bypass it. The launcher now describes that eligibility instead of promising every ground/wall texture will
change. The docking trace supplies the missing deterministic positive case. Its 0x arm is byte-identical to the prior
25-frame PS2-mip replay; 16x changes five frames, with aggregate 0.306% changed / 0.0030 mean / maximum 30. Frame 86
changes 69,724 pixels (7.599%) across the oblique floor, ceiling, bulkhead and foreground geometry; frames 87-90
change another 138 pixels each.

A four-way replay also checks the control interaction. At 16x, base-only mip mode changes five frames (aggregate
0.211%, maximum 20). With base-only selected, 0x versus 16x anisotropy changes five frames (0.403%, maximum 30).
Relative to PS2 mipmapping at 0x, the combined base-only/16x arm changes five frames (0.518%, maximum 30). The shader
paths interact on 21,594 frame-86 pixels, as expected when base-only changes sampler eligibility, but neither control
masks the other or affects unrelated frames.

Texture clarity is therefore qualified with deliberately narrow claims: anisotropy improves eligible angled
surfaces, and highest-detail mip mode forces level zero only where the game authored mipmapping. Character LOD is
rejected for this milestone unless offline mesh evidence justifies reopening it. GFX-03 is complete. Supported map
access uses exact `Shooterman` and native campaign selection; the direct-start Chapter 6 stress gate is retired.

## GFX-03 reinvestigation: per-consumer scene detail (2026-10-21/22)

A follow-up session re-examined the withdrawn shared-context LOD mechanism to see whether scaling render-context
`+0x1AC` (`0x70003A00+0x1AC`, reached through global `0x559118`) individually by consumer, instead of as one shared
value, could reopen elevated model detail safely. It does not change the product decision (`LodScale` stays fixed
at 1x), but it replaces the earlier correlational evidence with a disassembly-level account of the mechanism and a
cleaner reproduction of the unsafe interaction, plus new diagnostic tooling kept out of the product build path.

### The shared value has over twenty consumers

`ctx+0x1AC` is not a LOD setting; it is read by every per-class scene-distance gate in the executable:

| Consumer | Code | Effect |
|---|---|---|
| LodStaticMeshResource simple / blended | `0x3EF700` / `0x3EF7A0` call selectors `0x3F4E70` / `0x3F4F10` | metric = AABB distance / ctx, compared against the resource's `+0x2C` "LOD Info" table (count, -, threshold pointer) |
| LodStaticMeshResource instanced | `0x3EFAD8` (vtable slot `0x53AC38`) | same `+0x2C` table; per-placement distance computed inline |
| LodSkinnedMeshResource simple / blended | `0x3FF3D0` / `0x3FF470` | same selectors against the `+0x30` table |
| MeshResource "Maximum visible distance" (`+0x1C`, -1 = unlimited) | cull `0x3F1574` (AABB helper return `0x3F1650`), compound objects `0x246EC8` | visible while distance < maximum * ctx |
| Grass "Max View Distance" (`+0x134`) | walker `0x3EE6F8` (cell cull `0x3EE874`, instance cull `0x3EEBB8`) | cull radius = maximum * ctx |
| scope zoom | `0x225F30` | calls setter `0x151E60` with `ctx * zoom` |
| ~15 further per-class gates | e.g. `0x1D8F54`, `0x253314`, `0x25FFE0`, `0x27C814`, `0x2822F0`, `0x28F39C`, `0x313734`, `0x3BC2A8` | `distance * ctx` accessors on other entity classes; not individually classified |

All selector and cull paths reach the same AABB-distance helper (`0x2C8178`), so a call-site-gated hook on that
helper, plus hooks on the three selector entry points for threshold-table patching, can scale each class
independently. A new diagnostic module, `src/kz_detail.{h,cpp}` (not wired into any product config key; every hook
is gated by a `KZ_DETAIL_*` environment variable and installs nothing when none is set), implements this: it
patches a resource's own "LOD Info" threshold table once per table (covering its simple, blended and instanced
paths together), scales the visible-cull helper call-site, and patches a grass object's own view-distance field.
Telemetry (`KZ_DETAIL_LOG[=<vblank-period>]`) reports per-class selection-level histograms before/after scaling,
LOD stream-request counts, streamer state, a guest-heap walk, DMA packet high-water, malloc-exhaustion events, grass
budget saturation and the minimum observed main-thread stack pointer.

### LOD streaming makes elevated selection safe by itself

LOD meshes are streamed, not always-resident. After selecting a level, a resource searches for the nearest
*resident* level (`0x3FF318` skinned, `0x3EF648` static; residency check `0x189C08` reads a node flag bit) and draws
that instead. When the selected level differs from the resident one, the returned level's dependency tree is
stamped with the streamer clock through `0x189CB0` -> `0x186240` -> `0x186220` (`node+8 = streamer+0x1C`). The
streamer (`new(0x478)`, constructor `0x186FC0`, global `0x559170`, state machine `0x18A0A0`) loads stamped nodes in
batches of at most 1 MB (`+0x8C`) or 100 entries (`+0xA0`) per tick. Because only resident levels are ever drawn,
elevated LOD selection alone degrades gracefully to the best already-loaded level rather than requesting missing
data synchronously; this is consistent with per-class LOD scaling (static, instanced, skinned) at up to 8x each
passing a 45-75 s scripted-gameplay gate independently, in chapter 5 (`5.Fields1`), with normal streamer activity
and no abnormal memory growth.

### Grass is budget-bound, not distance-bound

The grass draw (`0x3EE6F8`/`0x3EF0xx`) allocates `min(instances, 1500)` 80-byte records on the *main-thread stack*
every frame, and the walker itself stops at 1500 (`grass+0x158`, constant `0x5DC` at `0x3EE75C`/`0x3EF0DC`/
`0x3EF21C`). Fields1's grass object has an authored `Max View Distance` of 50; stock play already peaks at
1385-1500 instances per frame and routinely saturates the cap. Doubling the view distance via the diagnostic hook
hits the 1500 cap continuously (thousands of times per minute) without a crash or measurable memory growth: grass
density is bounded by its own per-frame budget regardless of view distance, so there is no reason to believe a
grass-distance product setting would be unsafe on its own.

### Main-thread stack headroom is unaffected

`SetupThread` places the main stack at `0x100000-0x140000` (256 KiB). Baseline gameplay bottoms out around
`sp=0x1209F0` (~130 KiB headroom free) in the `KZ_DETAIL_LOG` minimum-stack-pointer telemetry. Every elevated-detail
arm, including every one that crashed under the render-distance interaction below, measured the same
`0x1209F0-0x120950` floor. Stack exhaustion is not the failure mechanism for any case tested.

### Render distance 2x plus any elevated LOD class remains unsafe, and the mechanism is not yet isolated

Each control passed alone: per-class static/instanced/skinned/grass scaling up to 8x, the shared-context diagnostic
at 4x, and `RenderDistance=2` alone (4/4 clean runs, each exiting cleanly at the scripted gameplay limit). Combining
`RenderDistance=2` with *any* tested elevation of LOD selection distance reproduced the original withdrawn
mechanism's failure signature: guest `missing-target` indirect calls to addresses like `0x0`, `0x1B`, or
`0x6F636952` (ASCII "Rico" — a reused/stale vtable-like slot), or indefinite stalls inside `malloc` (`0x172928`) or
a block-copy routine (`0x2E6620`). Combinations tried and their outcome over this session's small samples:

| Arm (with `RenderDistance=2`) | Result |
|---|---|
| shared-context `LodScale=4` (the original withdrawn mechanism) | 1/1 failed |
| per-class static+instanced LOD `=4` | 2/2 failed |
| per-class skinned LOD `=4` | 1/2 failed (the other reached the gameplay limit cleanly) |
| per-class static+instanced+skinned LOD `=4`, with `KZ_DETAIL_NOTOUCH=1` suppressing LOD stream requests from any resource whose table this module scaled | 1/1 failed |
| `PS2X_IOP_THREAD=0` (synchronous IOP) with per-class LOD `=4` | inconclusive; batch did not complete before the session ended |

The sample sizes are too small, and skinned-only's single pass is too thin, to name one LOD class as uniquely
responsible; the safest reading is that render distance 2x combined with elevated LOD selection distance of *any*
kind is unsafe, matching the original 2026-10-20/21 finding, now reproduced with cleaner per-class tooling instead
of the single shared value.

Ruled out as the mechanism:

- **Stack exhaustion** — see above; unaffected in failing runs.
- **Heap exhaustion** — the fixed arena (`0x591410-0x1FFFFF0`, initialized by `0x173430`, hard end at
  `0x2000000-0x10`) holds ~24.1 MiB used, 1.3-2.0 MiB of free bins and an 874-994 KiB top chunk in both passing and
  failing arms; malloc's own exhaustion path (purge callback `*(gp-0x7E98)`, fatal callback `*(gp-0x7E9C)`, both the
  no-op stub `0x30A180` in ordinary gameplay) never fires in either arm, and both stay within roughly 1.3 MiB of
  exhaustion at the level-load peak regardless of pass/fail.
- **DMA packet-buffer overflow** — the two 768 KiB double-buffered packet areas (`0x5DAA00`/`0x69AB00`, size
  tables at `0x5134A8`/`0x5134B0`) peak at 276-287 KiB used in passing runs and 372-407 KiB in failing runs; a
  *passing* shared-context-LOD-4x-only run separately reached 425 KiB. No overflow (cursor past the buffer end) was
  ever observed by the per-frame `KZ_DETAIL_LOG` packet-high-water probe.
- **Threaded-IOP latency specifically** — the combination also failed with `PS2X_IOP_THREAD=0` (synchronous IOP),
  so a race specific to the threaded-IOP path is not the sole explanation, though a race elsewhere in the
  offload machinery was not excluded.
- **Late offloaded nowait-RPC result copies writing into a freed/reused receive buffer** — an in-flight receive-
  buffer registry (experimental PS2Recomp patch, below) recorded zero guest frees of a buffer with a pending
  offloaded RPC result in any failing run.
- **LOD-triggered stream requests specifically** — suppressing `0x189CB0` stream-residency calls from any resource
  whose threshold table this module had patched (`KZ_DETAIL_NOTOUCH=1`) did not prevent the failure.

What was positively observed in the corrupted state: guest code calls through live objects at addresses that were
reused since a previous allocation (e.g. `0x1009CC0` lies inside a different, newer `0x530190`-tagged object than
whatever last owned it), and a per-frame heap walk (`KZ_DETAIL_HEAPCHECK`) caught a `rpcCopyToRdram` write at
`0xE1B950+0x70` landing eight bytes past its enclosing chunk, directly into the next chunk's size/inuse header.
This is the same symptom class, and in one run the same cleanup chain (`0x2E7B38 -> 0x2E7440 -> 0x2E8170`, frees
from `0x2E819C`) and the same LOD-streamer object address (`0x01281020`) as the retired WIN-06 direct-start
investigation (`docs/findings.md`, "WIN-06: write watch names the realloc wrap" and "renderer guards expose the
remaining guest corruption"). Whether render distance 2x plus elevated LOD reaches the *same* underlying corruption
as WIN-06, or only produces a similar-looking downstream effect, is not established; the WIN-06 investigation itself
could not pin a single first-fault signature across its own 7-10 ten-run batches after extensive instrumentation
(see `docs/findings.md` "WIN-06" sections). Further narrowing was not completed in this session.

### Texture mip-distance bias is feasible but not implemented

The Texture resource's `PS2Mip1Distance` (`+0x10`) and `PS2MipMapL` (`+0x14`) are copied to the GS texture object
(`+0x98`/`+0x9C`). `0x15AB90` builds the GS `TEX1` register with `MXL = levels - 1` and
`K = 16 * (1 - 2^L * log2(Mip1Distance * scratchpad[0x3BAC]))`, clamped to `-0x800..0x7FF`; scaling `Mip1Distance`
by a factor `s` is therefore exactly `K -= 16 * 2^L * log2(s)`. PCSX2 only honours K/L (manual trilinear LOD) when
hardware mipmapping is enabled, and that path disables anisotropic filtering, so `Mipmapping=highest` with 16x
anisotropy (the existing, qualified combination) stays the sharper option. A K-bias "texture detail distance" mode
is feasible at the kzgs level but was not implemented or qualified this session. Separately,
`TextureManager`'s "Maximum texture size" (`+0x28`) defaults to 2048, above the GS's working limit, so textures are
not downscaled on load regardless of this setting.

### Diagnostic-only tooling added, kept out of the product path

- `src/kz_detail.{h,cpp}`: per-class detail scaling, LOD streamer/heap/packet/stack telemetry, a same-process A/B
  toggle (`KZ_DETAIL_TOGGLE=<n>`, active only in odd `n`-vblank gameplay windows), and an opt-in per-frame heap
  walk with free history (`KZ_DETAIL_HEAPCHECK=<path>`, writes a CSV of the host-write/free ring at shutdown).
  Three new manifest-validated debug hooks support it: `LodStreamRequest` (`0x00189CB0`), `GrassRender`
  (`0x003EE6F8`), `LodStaticInstanced` (`0x003EFAD8`), `LodSelectSimple` (`0x003F4E70`), `LodSelectBlended`
  (`0x003F4F10`), `AllocFailHandler` (`0x0030A180`) and `PacketFrameBegin` (`0x0014FD90`). None is reachable without
  its guarding environment variable; the module installs no hook and changes no behaviour by default.
- `patches/ps2recomp/experimental/gfx03-host-write-rpc-diagnostics.diff` (not applied; see `patches/README.md`) —
  adds a null-by-default host-write observer hook (`ps2TraceGuestRangeWrite`, previously a no-op per `LEARNINGS.md`)
  and an in-flight offloaded-RPC receive-buffer registry/query (`ps2RpcFindInFlightReceive`) to the locked
  PS2Recomp checkout, both gated behind `PS2X_GUEST_RANGE_WRITE_OBSERVER` so `kz_detail.cpp` degrades cleanly when
  the patch is not applied. Used only to produce the heap-corruption evidence above; not part of the accepted
  submodule revision.

### Disposition

No product or launcher change results from this reinvestigation. `RenderDistance` stays clamped to `1..2` and
`LodScale` stays fixed at `1.0`; the earlier GFX-03 acceptance in `WORKDONE.md` is unaffected. The per-class
mechanism and diagnostic tooling are retained for a future session that wants to pursue elevated per-class LOD
(static/instanced/skinned/grass) as a narrower product feature than the withdrawn shared value — on the evidence
above, static/instanced/skinned/grass alone each look safe, but the render-distance-2x interaction needs a found
root cause, not just a wider no-go net, before any of them can ship together with elevated render distance.

## GFX-04 temporal-input feasibility: no-go (2026-10-21)

The current renderer boundary cannot provide a correct input contract for modern temporal reconstruction.

### Color and composition

PCSX2's `GSDevice::Merge` combines the active display circuits and makes that merged texture current; interlace,
FXAA and other enabled post steps can replace it again before `kzgsReadback`. Standard color and the D3D12 swap chain
are `DXGI_FORMAT_R8G8B8A8_UNORM`, with no declared scene-linear, HDR or sRGB resource contract. Live captures already
contain weapon, HUD, tutorial overlays and the game's post effects. This is useful composed SDR color, not a
pre-HUD scene-color input.

### Depth target inventory

`KzgsBurstFrame.tcInfo` now records both display circuits and every PCSX2 color/depth cache target. A diagnostic D3D12
chapter-1 run produced two bounded consecutive samples:

- Loading frames 1319-1338: all 20 had one depth target at `TBP0=0x01C00`, `PSM=0x31` (`PSMZ24`), texture
  `1024x896`, valid rectangle `512x384`. The displayed color framebuffer was `0x00000`.
- 3D gameplay frames 1796-1815: all 20 had **zero** depth targets. Three color targets occupied `0x00000`,
  `0x00E00` and `0x01C00`, while the displayed framebuffer alternated between `0x00000` and `0x00070`.

A sparse sequence independently showed `0x01C00` changing from depth during loading to color during gameplay. The
cache therefore has no persistent, display-correlated gameplay depth surface or convention that can be handed to a
temporal API. The direct-start option was used only to reach the diagnostic scene quickly; no release acceptance
depends on it.

### Camera, jitter and motion

The GS vertex stream contains `ST`, `RGBAQ`, already transformed screen-space `XYZ`, `UV` and fog. View, projection
and object transforms have already been consumed by guest VU/EE code. kzgs has no object identity, previous transform
or motion-vector stream. A renderer-wide subpixel offset would jitter HUD, sprites, particles and post-process draws
along with world geometry. Reconstructing per-object motion would require game-specific engine hooks or speculative
draw matching; optical flow is not an engine motion-vector contract.

### UI, reactive content and resets

HUD, tutorial overlays, weapon, particles and transparency are composited into the same color targets without a
separate UI or reactive mask. The host can reset history on resize, renderer/config restart and device loss, but the
GS interface has no reliable semantic event for camera cuts, teleports, pause, save/load or gameplay transitions.

### Decision and fallback

GFX-04 is a technical **no-go** for DLSS/FSR/XeSS-style temporal reconstruction under the present architecture.
Required inputs cannot be made correct without the broad game-specific draw/engine guessing prohibited by the stop
condition. DLSS-01 is therefore stopped before SDK or licensing work. The native fallback is the validated spatial
D3D12 path: integer internal scaling, sharp presentation, CAS, anisotropy, measured mip selection and bounded view
distance. No temporal setting is exposed in the launcher.

## GFX-05 display follow-up (2026-10-21)

Killzone aspect handling has two layers. `kzPatchesApply` sets the game object's native widescreen word only for
`Aspect=16:9`, changing engine FOV and HUD layout. kzgs separately applies the selected presentation aspect when the
merged GS image is fitted to the physical output.

The deterministic replay tool now accepts `--aspect 4:3|16:9|stretch`. Final-backbuffer replay of docking frame 86
with the validated texture/presentation settings produced:

| Output | Mode | Result |
|---|---|---|
| `1920x1080` | 4:3 | centered 4:3 picture with pillarboxes; 60.3% nonblack |
| `1920x1080` | 16:9 | fills output; 80.4% nonblack |
| `1920x1080` | stretch | byte-identical to 16:9 because the output is 16:9 |
| `3440x1440` | 4:3 | centered 4:3 picture; 44.9% nonblack |
| `3440x1440` | 16:9 | centered 16:9 picture with pillarboxes; 59.8% nonblack |
| `3440x1440` | stretch | fills output; 80.4% nonblack and 96.938% of pixels differ from 16:9 |

Matched live arms are required for engine geometry because presentation replay cannot change the game flag captured
in a trace. Chapter-1 4:3 and 16:9 arms at gameplay offset 301 both exited 0. The 4:3 arm has narrower horizontal FOV,
original HUD placement and pillarboxes; the 16:9 arm expands FOV and moves HUD/tutorial elements to the widescreen
edges. Matched front-end captures likewise pass in both layouts.

The recorded FMV frame fills 16:9 and is pillarboxed in 4:3; 16:9 and stretch are byte-identical on a 16:9 output.
The current renderer remains untagged `R8G8B8A8_UNORM` composed SDR, so there is no scene-linear basis for an HDR
claim. Native ultrawide is also not claimed: 16:9 correctly pillarboxes ultrawide output, while Stretch is an explicit
distortion mode and is labeled as such in the launcher.

Automated scope verification was not accepted: Chapter 1 suppressed zoom during its tutorial, and a Chapter-5
direct-start arm reproduced the retired corruption path in one aspect arm. That run was excluded rather than
attributed to aspect mode.

Hands-on acceptance then closed GFX-05 through the supported native path. With Test memory card enabled and exact
`Shooterman`, every campaign map and difficulty was available through the game's menus. On the ultrawide monitor,
16:9 pillarboxed correctly, 4:3 appeared correct, Stretch filled with the expected distortion, and gameplay,
scope/zoom and weapon switching remained normal. The Validated enhancements preset was used with VSync on. VSync off
still produced egregious tearing, so VSync-on remains the accepted baseline and is now the default/preset value.

## Testing options (2026-10-17)

Launcher Testing tab (`[Testing]` in `killzone.ini`, all off by default; nothing is hooked while off): unlock all
campaign levels (WIN-02), start in a chapter, and a separate test memory card (WIN-05). Code:
`src/kz_testing.cpp`. Hook addresses are in the GEN-01 manifest (`SwitchToMenu`, `SetLevelFile`,
`ProfileMaxDifficulty`, `ProfileLevelUnlocked`, `ProfileAnimaticUnlocked`; `MovieOpen` was already there).

### Campaign unlock state

- The front end is script-driven. PlayerProfile script methods are registered at 0x1D3558.. as member-function
  pointers (name string followed by an 8-byte {delta, index -1, pfn}): `GetMaxDifficulty` 0x1D7118,
  `IsLevelUnlocked` 0x1D7190, `IsAnimaticUnlocked` 0x1D7258.
- `IsLevelUnlocked(profile, globalLevel)`: index = clamp(level-1, 0, 47); level 1 is always unlocked; otherwise true when
  the profile data's per-level record `+0x2D4[index-1]` is > 0. `GetMaxDifficulty` returns `+0x214[index]`.
- All three first compare the profile name with **"Shooterman"** (string 0x543C3C, referenced nowhere else): true, 2 and
  true. **"Shooterman2"** (0x543C47) unlocks levels 1-5 only. A profile named Shooterman is the developers' cheat;
  verified: the level list then opens on the last chapter and chapter 11 loads and plays.
- Level select asks for the first sub-level of each of the 11 chapters: global levels 1, 5, 9, 12, 16, 20, 23, 26, 29,
  31, 33. Difficulties are Easy 0, Normal 1, Hard 2.
- The option replaces the three queries with the Shooterman answers; the profile is never written. Four unlock-on runs
  left the memory-card files with identical SHA-256 hashes; an ordinary fresh profile (`Tester`) reached chapter 11.

### Front-end flow

- `main` (0x140138) opens menu sets with `OpenMenu(id, ...)` (0x18F7A8: 0 Intro, 1 FrontEnd, 2 Online, 3 Status,
  4 EndGame, 5 TestMenu, 6/7 offline multiplayer, 8 Pause), Intro first, then the game loop 0x1402B0 opens FrontEnd.
  The byte at 0x560110 also skips the Intro set; it is filled from `cdrom0:SYSTEM.CNF` (disc variant), so it was left
  alone.
- Screens are switched by `SwitchToMenu(menu, const char *name, push)` (0x1A9738). Intro: LanguageOutro, Logos (about
  23 s, buttons do not skip it), MemoryCardTest, PlayGameIntro (cinematic `source:Movies/Intro/Intro_NTSC`, about 58 s).
  FrontEnd campaign path: MainMenu, ScreenGame, ScreenSelectProfile (or ScreenCreateProfile on an empty card),
  MapSelect, ScreenDifficulty, ScreenCharacterSelect.
- `Game settings` (`*(*(0x559178)+0x74)`) setters called by the scripts: `SetModeScript("Story")`, `SetGameMode(0)`,
  `SetGameType(0)`, `SetNrOfPlayers(1)`, `SetPlayerJoystick(0,0)`, `SetLevelFile("11.OTS")` (0x1B89A8, plain C string
  "<chapter>.<name>", e.g. `1.Trenches`, `3.Station`), `SetGameScript("11.OTS.MissionScript")`,
  `SetGameDifficulty(n)`, `SetPlayerCharacter(0, "Templar")`. Fields: level file +0x0C, abort +0x1A8, status +0x1AC,
  difficulty +0x1B0, end-game option +0x1B4, game type +0x1B8, mode +0x1BC, players +0x220 (stride 0x1E0).
- The menu object also has `EnableCheats` (0x433780, sets menu+0x260) and the profile option table lists God Mode, Hit
  Info, Victory Status, Lock/Spread Circle and Show Coordinates. Not investigated further.
- MapSelect is a one-item carousel that opens on the highest unlocked chapter; D-pad Up moves one chapter back (ten Ups
  from chapter 11 chose `1.Trenches`).
- `KZ_MC_ROOT` is the card folder itself (the default is `<ELF folder>/mc0`), not its parent.

### Implementation choices

- The original intro shortcut kept the Intro script running but pointed its "Logos" switch at "MemoryCardTest"
  (found within 16 KB of "Logos"; 0x3F0 bytes before it) and failed the cinematic open. It reached the main menu at
  5.4-7.2 s instead of about 87 s, but skipped required game initialization and was removed on 2026-10-20.
- Skipping the whole Intro set (`OpenMenu(0)` returns at once) also reached the menu, but skips MemoryCardTest; an
  empty card then goes straight to Create Profile without the "No profiles found" dialog. Rejected (see below).
- Start in a chapter drives the menus with pad pulses keyed on `SwitchToMenu` (settle 1.2 s, 150 ms press, 450 ms gap)
  and checks the chapter with `SetLevelFile`. On an empty explicitly isolated card, Create Profile types
  `[Testing] DefaultProfile` (exact `Shooterman` by default, `KZ_DEFAULT_PROFILE` for automation) through the on-screen
  keyboard, then answers Yes to save. Normal-card automation retains the legacy `Tester` name; populated cards only
  take the existing first profile-screen entry and are never renamed or overwritten. It turns on the unlock (the
  carousel count assumes chapter 11 is the start) but now waits for the full Intro menu set before automating the front
  end. Explicit `KZ_UNLOCK_ALL=0` suppresses that convenience hook during Start in so a populated `Shooterman` card can
  exercise the native profile queries. ISO-free self-tests cover selection, exact typing, overrides, invalid values,
  hook suppression and populated-card no-type behavior.
- PROFILE-01 BYOD acceptance (2026-10-20): an empty isolated card entered Create Profile at guest time 16.1 s, typed
  exact `Shooterman`, saved three files totaling 60,304 bytes with one exact ASCII-name occurrence, reached
  `1.Trenches` on Normal, exited 0 and produced 11 captures. A populated-card rerun with `KZ_UNLOCK_ALL=0` logged native
  profile queries, installed no unlock hooks, reached `11.OTS` on requested difficulty 2, exited 0 and produced 11
  captures. The populated test-card aggregate SHA-256 was unchanged at
  `f221f592efe92aaf9a95d3b297beeb5985700d33d6bb9f231d4874cd39cf2cf0`; the normal card remained an empty tree with
  SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`. The local card and captures remain ignored
  under `work/`.
- A research call logger that wrapped about 30 front-end functions made the game thread jump to 0 during the chapter
  11 load (2 of 2 runs); the shipped hooks either replace a whole leaf query or only inspect arguments before calling
  the original.

### Failure evidence: chapter 11 load after shortened startup (WIN-06)

Chapter 11 (`11.OTS`) loads, win-clang-rel, headless, 2048x1792 frames:

| Intro | Runs | Bad | Failures |
|---|---:|---:|---|
| played (baseline: `fe_nolog5`, `t_A`, `t_base1`-`4`) | 6 | 0 | - |
| whole Intro set skipped (`t_p3`, `t_C2`, `t_B2`) | 3 | 2 | corrupt frame then 0xC0000005; corrupt HUD digits |
| logos + cinematic skipped (`t_V1`-`3`) | 3 | 1 | 0xC0000374 heap corruption |
| cinematic only skipped, `KZ_SKIP_INTRO_MASK=2` (`t_M2_1`-`3`) | 3 | 1 | 0xC0000005 |
| logos + cinematic, menus started 80 s after boot (`t_D1`, `t_D2`) | 2 | 1 | 0xC0000005 |

- Failures are in the host process (access violation, heap corruption), during or right after the chapter 11 loading
  screen. Chapter 1 loaded cleanly after a skip (two runs). Delaying the level start does not help, so it is not the
  time since boot; the common factor is that the intro cinematic was never played.
- Skipping only the logos (`KZ_SKIP_INTRO_MASK=1`) left the cinematic playing past 160 s in 3 of 3 runs (normally it
  ends at about 84 s), so the Logos screen also sets state that PlayGameIntro uses.
- Later sections trace the downstream corruption and reject MPEG/IPU decoding as the cause. The decisive distinction
  is that the supported path completes startup; the movie decoder itself is not the faulty component.

## WIN-06: timer-alarm list, interrupt masking and movie A/B (2026-10-20)

**Disposition (2026-10-21): retired diagnostic work, not a release blocker.** The gate below was created to qualify
the direct-start/skip-level path. The project now uses exact `Shooterman`, whose native profile queries unlock every
map, followed by the game's own campaign menu. Historical allocator and renderer evidence remains valuable, but no
supported feature or release claim depends on this gate. This disposition does not claim the diagnostic path was
fixed.

Gate: `tools/scripts/win06_gate.ps1` (ten sequential headless runs, `KZ_START_LEVEL=11`, so the intro is skipped, each
on a fresh copy of `work/win04_20261019_card`; win-clang-rel, D3D12, 35 s). A run fails on a non-zero exit, a guest
`missing-target`, a host exception, a missing `11.OTS` level file or mixed capture sizes.

### The alarm list that `0x2B70DC` walks

- `0x2B6FB8` is libkernel's **Timer 2** handler (INTC 11; it reads `T2_COUNT`/`T2_MODE` at `0x10001000`/`0x10001010`),
  not Timer 1 as earlier notes said. `0x2B70DC` is its `jalr` to the node's handler (`node+0x28`, gp `+0x2C`,
  argument `+0x30`).
- State block `0x517748`: `+0x00` overflow count (u64), `+0x08` INTC handler id, `+0x0C` sequence, `+0x10` nodes in
  use, `+0x14` free list, `+0x18` active list head (**`0x517760`**, sorted by expiry), `+0x1C` id being handled.
- `InitTimer` (`0x2B6B70`, called from libkernel init `0x2B65D0`) builds a pool of 128 nodes of 0x40 bytes at
  **`0x56CB40`-`0x56EB40`**. Node: `+0` next, `+4` prev, `+8` tag, `+0xC` flags (bit 0 counting, bit 1 handler set),
  `+0x10` start, `+0x18` base, `+0x20` schedule, `+0x28` handler. Ids are `node << 4 | tag`; `node = (id >> 10) << 6`.
- API: `AllocTimerCounter` `0x2B72B8` (`0x2B7328` with interrupts off), `StartTimerCounter` `0x2B7380`/`0x2B7430`,
  `SetTimerHandler` `0x2B74B0`/`0x2B75A8`, list insert `0x2B6EF8`, remove `0x2B6F80`, reprogram `0x2B6D68`.
  `SetTimerAlarm` `0x2B7758` (and `iSetTimerAlarm` `0x2B7888`) adds a second pool of 64 records of 0x10 bytes at
  `0x56EB40` (free head `0x56EF40`) and the trampoline `0x2B76F8`. The only caller of `SetTimerAlarm` is libkernel's
  `DelayThread` (`0x2B7E38`: CreateSema, SetTimerAlarm with an iSignalSema callback, WaitSema, DeleteSema).
- Consequence: the head value `0x5000` seen in `win06_timerhead_06` is **not a node at all** (outside the pool,
  unaligned to it). The head is only written by insert/remove, so the list memory or a node link was overwritten by
  a stray write. The other signatures point the same way: a jump to `0x7005728` with DMA-tag-like words at
  `0x13F800` (main stack, `win06_dilog_01`), an indirect call from `0x3A40A4` to `0x123610` (inside the main stack
  range, `win06_ipuoff_09`), and host access violations (`0xC0000005`) at 20-30 s.

### Interrupt delivery inside `di`/`ei`

- `EeScheduler::run()` injects pending invocations (interrupts, SIF and stream callbacks) and switches threads
  without checking the running context's Status.EIE, so a handler can run inside a guest `di`/`ei` section; the
  alarm-list edits rely on such sections. `PS2X_DI_GATE=log` (experimental runtime change) reported 2 deliveries in
  the one failing run of three and none in the two passing runs; both were the frame-pipeline VIF1 handler
  `0x14E0C8`, once at the `ei` of the interrupt-off wrapper `0x2B63B0` and once during `SyncDCache` (`0x2B1320`,
  cache operations are no-ops here). Neither touches the alarm list.
- With the gate enforced (`PS2X_DI_GATE` default in that build): **8/10** (`win06_digate_*`; runs 8 and 9 host
  `0xC0000005`). Not the cause; kept as `patches/ps2recomp/experimental/ee-di-interrupt-gate.diff`.

### Movies are not required

- `KZ_IPU=off` makes every movie open fail (the path the intro skip takes), so no MPEG/IPU HLE state, decoder or
  stream callback is ever created. Result: **8/10** (`win06_ipuoff_*`; run 8 host `0xC0000005` at about 22 s, run 9
  the `0x3A40A4` indirect call). The baseline with movies on was 8/10 (`win06_mpgfix_*`).
- So "the intro cinematic was never played" does not mean "a movie code path misbehaves": with every movie absent the
  load still fails. What playing the cinematic contributes is something it leaves behind or the time it occupies
  (heap and VRAM layout, IOP/SPU2 state) that moves or masks a stray write during the chapter 11 load.
- Checked and ruled out on the way: the libmpeg HLE's callback scratch and private packet copies come from the
  runtime arena `0x80000`-`0xB0000` (`ps2SetRuntimeArena`), not the game heap. The game's video stream callback
  `0x2E0628` copies each packet into the player's ring (`*(0x55B3D8)`, `+0xF4` base, `+0xF8` size, `+0xFC` write)
  and runs asynchronously in the HLE, which could write through a stale player; it cannot explain the movies-off
  failures. `sceMpegDelete` is guest code (not HLE-bound).

### Next

- Superseded by "WIN-06: write watch names the realloc wrap (2026-10-20)" below.
- Find the writer, not more signatures: a host write watch on the alarm pool and list head (`0x517748`-`0x517780`,
  `0x56CB40`-`0x56EF50`) that logs any write from a pc outside libkernel's timer code (`0x2B6B50`-`0x2B7F00`), with
  the guest pc and the DMA/VIF/SIF/IPU channel when the write comes from a host-side transfer. Generated stores go
  through `FAST_WRITE*`, so a page-protection watch (as used for the quit-to-front-end white screen) is the
  cheapest option that needs no regeneration.
- Then compare heap state at the `11.OTS` level-file call with and without the cinematic (`KZ_DUMP_RAM`).

## WIN-06: write watch names the realloc wrap (2026-10-20)

Tool: `KZ_WIN06_WATCH=1` (`src/kz_win06_watch.cpp`, host-only, commit `6363163` plus the hooks described below). The
host pages over `0x517748`-`0x517780` and `0x56CB40`-`0x56EF50` are read-only; a write fault saves the CONTEXT and a
snapshot of the page, unprotects it and single-steps the store (trap flag), then re-protects and diffs. Each changed
word is attributed from the PDB-symbolised host stack; generated functions carry the guest address in their names and
the guest pc in their source comments, so the log gives the exact guest instruction. Cost: about 13,000 faults per
35 s run (neighbouring data, mostly at boot) and about 500 watched changes, no visible slowdown. The same flag hooks
`free` `0x172DD0`, `realloc` `0x173038`, the C realloc wrapper `0x173660` and the upload call `0x15A268` to validate
their arguments (dlmalloc-style chunk header at ptr-8, size at ptr-4, heap descriptor `0x55EC20`, bounds
`+0x448`/`+0x44C`).

### Result

| Batch | Result | What the failing runs showed |
|---|---:|---|
| `win06_watch` (watch only) | 6/10 | run 10: host-class writes to `0x517748`.. by `ps2CgWr128Slow` <- `FUN_00173038` (guest pc `0x1731B4`-`0x1731C4`) |
| `win06_watch2` (+ registers) | 5/10 | run 08: same loop, destination `0x02517740` (host `r9`); run 07: packet builder `0x159F7C`-`0x15A100` |
| `win06_watch3` (+ heap/upload hooks) | 8/10 | runs 02, 07: `realloc` called with `0x00000008` / `0x0D3A0C15` from ra `0x173694` |
| `win06_watch4` (+ header checks) | 6/10 | host GS crashes and one corrupt vtable call; no realloc case in this batch |
| `win06_iopsync` (`PS2X_IOP_THREAD=0`, no watch) | 4/10 | GS palette crash (4), missing frame size (1), missing-target (1) |

- In passing runs only libkernel's timer code (`0x2B6B70` InitTimer, `0x2B6FB8` handler, list insert/remove) writes
  the watched words. No host-side DMA, SIF, VIF or IPU write ever touched them.
- **The writer of the timer block is the game allocator's realloc copy.** `FUN_00173038` (realloc) allocates the new
  block (malloc `0x1727F8` returned a valid `0x01F1B3D0`), then copies `(old chunk size - 4) & ~0x3F` bytes with
  `lq`/`sq` (`0x1731A0`-`0x1731C4`). The old pointer was garbage (`0x00000008`, `0x0D3A0C15`), so the "size" read from
  its fake header is megabytes. The copy runs past `0x02000000`; `ps2CgWr128Slow` masks the address to RAM
  (`Ps2FastWrite128`: `addr & PS2_RAM_MASK`), so the copy continues at `0x0` and overwrites the kernel area, the
  runtime arena, the main stack `0x100000`-`0x140000` and libkernel's data up to `0x56xxxx`. On hardware an EE store
  above 32 MB of physical RAM would be a bus error; here it silently wraps.
- This explains every earlier signature: the alarm head `0x5000` and timer handlers jumping to garbage (the copied
  bytes include MIPS code and strings such as `...dex1;right_index2;right_middle1...`), jumps into the main stack, and the
  headless sampler showing the EE at pc `0x1731A0` ra `0x17314C` at 25-30 s in `win06_timerhead_06`.
- The garbage pointer is the cause to chase, not the copy. `0x173660` is the C realloc wrapper (NULL -> malloc, size
  0 -> free) with 300+ call sites (`0x2C0BE8`.., `0x41027C`-`0x513024`: container growth code), so the caller of the
  wrapper identifies the container. Batch 4 had no realloc case, so the caller is not named yet.
- Other failures are other consumers of corrupt heap data: an indirect call at `0x3B4978` through an object whose
  fields hold `0x123610` (the same value as `win06_ipuoff_09`'s `0x3A40A4` call), and the upload walker `0x15A5E8`
  passing a garbage record (address `0x7B90F5E8`, huge size) to the DMA packet builder `0x159D30`, whose write
  pointer lives in scratchpad (`0x70003D3C`) and ran from `0x5175C0` into the timer block.
- The host GS crash (`GSGetRGBA8AlphaMinMax` <- `GSTextureCache::AttachPaletteToSource`, reading unmapped host
  memory) is the most common failure with or without the watch. It is consistent with a corrupt CLUT/texture
  register from the same corruption, but it is a host crash in its own right: the GS should not read outside its
  buffers whatever the guest sends.
- Threaded IOP ruled out: `PS2X_IOP_THREAD=0` failed 6 of 10.

### Notes on the tool

- Generated functions often return through tail-jump fragments (`ps2DspTail` sets `ctx->pc = target | 1` and the
  call site follows it), so a `replaceFunction` hook cannot read a result after calling the original; only argument
  checks before the call are reliable. A first version that tracked malloc results recorded nothing and reported
  every free as "never allocated".
- dlmalloc small chunks (size < 0x51) do not clear the next chunk's in-use bit when freed, so double frees of small
  blocks are not detected by the header check.

## WIN-06: renderer guards expose the remaining guest corruption (2026-10-20)

Two more unguarded ten-run batches reproduced the same 8/10 rate:

- `win06_watch5`: run 2 returned through a corrupt object to missing target `0x3`; run 4 reached the allocator with
  stale or reused high-heap pointers and rendered a wrong-sized frame. Neither run reached the bad realloc-wrapper
  path. A later range-only batch names that caller below.
- `win06_regs`: both failures were the host GS palette crash. The captured x64 arguments proved that the palette
  pointer was valid but the requested width was `0xFFFFFFC7`: `GetAlphaMinMax(min, max)` had received an inverted
  byte range, and `max - min + 1` wrapped to a roughly 4 GiB scan in release builds. The prior assertion was compiled
  out.

The PCSX2 dependency now returns the cached full-palette alpha range when the requested subrange is inverted or
outside the 16/256-entry palette. This is conservative for alpha-test optimization and prevents an out-of-bounds
read. A second guard skips the depth-alpha optimization when no depth target exists (the other observed host crash
was a null `ds` dereference when frame and depth shared a base pointer). `kzgs_palette_guard` covers valid, inverted
and out-of-range palette queries.

The guards remove the host exceptions but are not the WIN-06 root fix:

| Batch | Result | Remaining failure |
|---|---:|---|
| `win06_gsguard` | 8/10 | run 5: severe texture/CLUT corruption by 25 s and a `1280x896` frame at 30 s; run 6: EE `pc=ra=0x50`, packet-like words at `sp=0x13F810`, and a `1704x1792` frame |
| `win06_guardwatch` | 10/10 | no bad allocator/upload report; the page-protection watch changes timing enough to mask this batch |
| `win06_software` | 9/10 | the software GS still failed during D3D12 output-resource destruction, so the hardware texture cache is not the common origin |
| `win06_callfix` | 9/10 | the PCSX2-parity DMAC CALL-stack-overflow fix did not trigger; run 2 still corrupted the GS local-memory read |
| `win06_vifmap` | 8/10 | one GS crash and the recurring `0x2E67DC` bad callback; both had `unknown=0`, so invalid VIF commands are a later amplifier, not the origin in these runs |
| `win06_recordwatch` | 8/10 | one corrupt frame and one bad realloc/missing-target run; the low-overhead range watch captured the realloc caller and overwrite |

An additional half-open guest range can now be selected without rebuilding:
`KZ_WIN06_WATCH_RANGE=<start>-<end>`; `KZ_WIN06_WATCH_DEFAULT=0` omits the timer ranges to reduce timing impact.
The realloc diagnostics decode a caller's `lw a0, offset(base)` delay slot, retain the pointer-field address and
request size when realloc frees the old block, and print all guest GPRs on a bad argument.

`win06_recordwatch_08` names the `0x00000008` path:

- Caller `0x2C0E9C` grows a four-byte-element vector and loads its old pointer from `[s0+8]`.
- `s0` was the caller's zero-initialized stack object at `0x13FBE0`. Immediately before the bad call its words were
  `{count=4, capacity=3, pointer=8}`; request size was `0x18`.
- The stack chain is `0x2C0E9C` <- `0x2C0EF4` <- `0x2C1184` <- `0x3A6730`.
  `FUN_003A66D0` explicitly clears the three vector words, then passes it to `FUN_002CF430`, a scratchpad-backed
  query, before iterating and appending the returned values.
- The bad realloc's copy begins wrapping above 32 MiB at vsync 1466 and writes the watched callback record at
  `0x8D1AC8` from unmasked destinations `0x028D1AC0`, `0x048D1AC0`, and later aliases. Thus the recurring
  `0x2E67DC` callback target is downstream of this realloc in that run, not the original corrupt field.

A debug hook at the vector grow return (`0x2C0EA4`) checks `v0` before it is stored at `[s0+8]`.
`win06_vectorgrow2` (30 runs, shortened to 28 s) failed 2/30. Run 26 captured the first bad state before the store:
the vector was `{count=1, capacity=0, pointer=0}`, the locked heap bounds were still the valid
`0x00591410-0x01FFFFF0`, and the allocator returned `v0=8`. Within the next eleven vblanks, many independent stack
vectors also received `8`, then the bad realloc wrapped and destroyed the timer state. The source is therefore
allocator metadata/control flow before the vector store, not `FUN_002CF430` overwriting an earlier valid vector
pointer.

A heap-descriptor-only page watch retained the last 64 writes, but page protection perturbed the timing enough to
mask the bad result. A temporary direct trace in the otherwise unhooked generated allocator then reached guest
`pc=0x172D8C` with `t1=0` and returned `8`; the temporary generated-source edit was removed and the normal generated
tree rebuilt. This confirms the allocator result without treating a watched 10/10 batch as acceptance evidence.

## WIN-06: startup shortcut and broader runtime A/B results (2026-10-20)

The allocator trace above identified how an invalid result becomes broad low-RAM corruption, but did not find an
emulation correction. Startup timing was initially correlated with failures, but a measured full-intro run disproved
it as the common cause:

| Startup path | Result | Evidence |
|---|---:|---|
| Logos retained, native Cross after 2 seconds (`win06_native_accept`) | 8/10 | same missing targets and visible corruption |
| Logos retained, native Cross after 30 seconds, D3D12 | failed on run 1 | `2048x1792` and `1280x896` frames plus a missing target |
| Full Intro menu set and movie (`win06_fullintro_timing_01`) | failed | `MainMenu` at 218.3 s, `11.OTS` at 225.7 s, missing target by 240 s |

The shortcut therefore remains useful for reproducing the fault but is not its root cause. Additional A/B arms also
retained failures:

| Arm | Result |
|---|---:|
| locals with classic checkpoints | 8/10 |
| synchronous/legacy VIF, GIF, scratchpad, dispatch, timer and RPC paths | failed 2/4 before the arm was stopped |
| audited PS2Recomp `cc1fc7d` | failed on run 1 |
| patch 0025 frame pipeline reversed | failed on run 5 |
| closure-complete `PS2X_CODEGEN_DSP=0` | failed on run 1 with a wrong-size frame |
| unmapped high-memory stores routed to the runtime null path | 8/10 |

The stronger ownership diagnostics captured two earlier states than pointer `8`:

- A four-byte vector at stack object `0x13FBE0` grew in place with valid `{count, capacity}` transitions
  `{7,6}`, `{10,9}`, `{14,13}`, `{18,17}`. Realloc then encountered an already-corrupt adjacent chunk size; the
  vector itself and its recorded previous allocation were consistent.
- Another run reached cleanup `0x2E7B38 -> 0x2E7440 -> 0x2E8170` with list node `0x01F101C0` already shaped like a
  free chunk before its first observed call to `free`. The later allocator return, wrapped copy, timer overwrite and
  renderer crashes are downstream of this stale ownership transition.

Follow-up batches resolved a cross-owner stale pointer:

- In `win06_block1_01`, cleanup owner `0x007E1EC0` freed node `0x01F0FF00` at vsync 1655 through
  `0x2E7B38 -> 0x2E7440 -> 0x2E8170`.
- Live parent `0x01281020` still held that allocation in its embedded vector at `parent+0x58`, pointer field
  `parent+0x5C = 0x0128107C`.
- At vsync 1662, resize call `0x4299F4` requested 72 bytes by reallocating the freed pointer. The same stale field was
  reused again at vsync 1671 and 1678. The call site's pointer-field neighborhood and the previous-free stack agree;
  this is not an allocator-header heuristic.
- In a separate successful watched run (`win06_listhead2_01`), a stack-local vector requested its first 12-byte
  allocation and received `0x01051C30`, freed ten vsyncs earlier through the generic delete wrapper. Its chunk header
  had already been overwritten with live float data. The run completed, proving that a pass/fail-only gate can miss
  allocator corruption.

Measured after the PROFILE-01 changes, the first uninstrumented batch was 10/10, but an independent repeat was 7/10:
one 35-second hang, one host access violation and one indirect-call missing target. Watched `win06_watch5` and
`win06_watch6` batches were each 4/5; later watched batches reached 10/10, confirming that page protection perturbs
timing and cannot close the task.

The watch now retains allocator bounds, cleanup owner/list head, delete-wrapper caller and bounded list-chain
provenance. A whole-RAM reference scan was tested and removed because it performed 22,003 scans in five runs and
materially perturbed timing. Debug hook `Vector36ResizeReturn` at `0x4299FC` (unique 128-byte executable signature)
now associates resize results with owner fields, checks for a live owner at cleanup and reports the last owner on a
bad realloc. Its first three ten-run batches were all 7/10; they reproduced GS faults, missing targets and mixed-size
corruption, but not the exact earlier cross-owner alias. The third batch emitted no allocator/owner marker before its
three failures, so the hook remains a targeted ownership diagnostic rather than a success oracle. At the time, this
left WIN-06 open at the lifecycle transition that leaves the alias live; the later 2026-10-21 scope disposition
retired the diagnostic instead of adding allocator guards, ignored frees or generic field clearing.

One final bounded investigation tested whether the cleanup-list and 36-byte-vector allocations overlap before the
later bad realloc. A manifest-validated return hook at `0x2E7238` records cleanup-list node allocations and compares
them with the existing `0x4299FC` resize-owner history. `win06_listoverlap1` ran with page watches disabled and was
8/10: one missing-target/mixed-frame run and one host access violation. Neither failure nor any passing run emitted
an overlap or linked-free marker. The corrupt run instead caught a first-allocation failure: `malloc(0x600)` returned
`0x01F101A0`, whose header was live non-allocator data, while heap descriptor word `0x0055EC6C` changed from
`0x01F10198` to `0x00000600` during the call. A follow-up single-word hardware-watch arm, `win06_heapword1`, was
7/10 and reproduced the same shape with `malloc(0x400)` returning `0x01F10020` before the allocator collapsed to
`0x408` and then `0x8`; it yielded no usable writer event.

These arms show that the earlier cross-owner alias is real but not a universal first-fault signature. The common
frontier remains allocator candidate/bin corruption before the invalid return. By project decision on 2026-10-21,
the direct-start investigation is retired rather than fixed. Its hooks and logs remain available for diagnostics,
but it is not an M4 or release requirement.

## Windows gameplay baseline re-verification (2026-10-19)

The scripted portion of WIN-04 used `win-clang-rel`, D3D12 and an isolated empty memory card. The front-end autopilot
created and saved `Tester`, selected `1.Trenches` on Normal and reached first-mission gameplay. The guest reported a
120 Hz vblank/frame rate; the host exited 0 at the 120-second headless limit with no crash signature.

The run produced 11 captures at 10-second intervals through 110 seconds. All 11 SHA-256 hashes were distinct, sampled
nonblack coverage ranged from 81% to 99%, and the later frames visibly contain first-person gameplay, HUD, world
geometry and animation. This proves the scripted gameplay portion only. WIN-04 remains open for a hands-on run with
testing options disabled that covers save/load, quit to the front end, high-refresh behavior and mouse aim.

### Hands-on closure (2026-10-20)

Two hands-on D3D12 runs with every testing option off completed BASE-01. Normal profile selection, first-mission
gameplay, controller input, game timing and mouse aim passed. Quit to menu followed by Continue/load resumed saved
progress. Closing the window completed promptly with exit code 0 and no crash or hang.

The first run used the recorded `VSync=0` configuration and showed screen tearing. A focused A/B with `VSync=1`
eliminated tearing; the human owner selected VSync-on as the comparison baseline. Two observations remain tracked,
not hidden in the sign-off: short-distance LOD pop-in is aggressive at original `RenderDistance=1`/`LodScale=1`
(GFX-03), and action-heavy first-mission firefights repeatedly truncate gunfire/effects and dialogue by roughly
100 ms on speakers (AUDIO-01).

### Display and audio re-verification (2026-10-21)

The GFX-05 hands-on matrix passed with VSync on, Validated enhancements, Test memory card and exact `Shooterman`.
VSync off still tears badly. Hope gameplay had no observed audio issue, while Escape firefights continued to truncate
effects/dialogue; AUDIO-01 should use Escape as the positive arm and Hope as a negative comparison.

## AUDIO-01 callback-boundary instrumentation (2026-10-21)

`KZ_AUDIO_WAV` already records the raw 48 kHz stereo mix before the SPU2 output ring. `KZ_AUDIO_DIAG` now records a
schema-1 CSV row for every SDL request: host time and callback gap, the corresponding raw-WAV frame, SPU2 producer
drops, queue depth before/after, high-water skips, reads, startup priming and underrun silence, SDL input/output queue
depth, submission failures and diagnostic-record loss. The audio callback only appends to a bounded SPSC memory
queue; a background thread writes and flushes the CSV so disk I/O cannot stall the real-time callback. The
`summarize_audio_diag.py` validator rejects malformed, non-monotonic or incomplete callback sequences and reports a
protected-content-safe JSON summary for the whole trace or a host-time window.

A 12.37-second muted SDL/WASAPI smoke using the configured disc image exercised the measurement path:
1,239 contiguous callbacks, maximum callback gap 10.066 ms, zero diagnostic-record loss, producer overflow, underrun
silence, SDL queue-query failure or stream-submit failure. One existing high-water event at host time 3.398358 s saw
7,231 SPU2 frames queued and discarded 4,831 frames before ending at the 1,920-frame target. That is 100.65 ms of
audio, close to the reported Escape truncation duration. Similar one-time 4,838/5,302-frame skips exist in earlier
intro-device logs, so this is a concrete lead rather than proof of the combat defect.

### Paired hands-on localization

The paired speaker runs used VSync on, the isolated test card and exact `Shooterman`. Escape reproduced many cuts
across background audio and combat chatter; Hope reproduced a few across background, scripted dialogue and combat
chatter, so Hope is lower severity rather than a clean negative arm.

| Run | Host span | Underrun silence | High-water skip | Loss rows |
|---|---:|---:|---:|---:|
| Escape baseline | 184.04 s | 317,863 frames / 6.62 s | 254,851 / 5.31 s | 1,104 |
| Hope comparison | 220.45 s | 161,858 / 3.37 s | 166,077 / 3.46 s | 466 |

Both callbacks remained near 10 ms and recorded zero SPU2 producer overflow, SDL queue-query failure, stream-submit
failure or diagnostic-record loss. Loss therefore begins between SPU2 production and callback delivery: the queue
empties while the IOP worker falls behind, then catch-up production crosses high water and the old path discards back
to its reserve. A focused audible enemy-wave cut coincided with published-cycle backlog growing from 181.21 ms to
954.27 ms while the callback stayed timely.

### Mitigation and rejected experiments

The retained host mitigation raises the initial/reserve target from 40 to 120 ms and high water from 150 to 320 ms.
Diagnostic overrides are `KZ_AUDIO_BUFFER_MS=10..400` and `KZ_AUDIO_HIGH_MS=<reserve+10>..480`. The hands-on candidate
had no noticeable firing/reload delay and reduced many cuts to a few, but one large combat dropout remained; AUDIO-01
is therefore not complete.

- Synchronous IOP reduced one short probe but regressed the long 120 ms gate to 63,765 underrun frames, 30,595 skipped
  frames and 4,182 producer drops. Rejected.
- Highest Windows priority for the IOP/SPU2 worker reduced automated backlog but locked interactive Escape. Rejected.
- Accounting offloaded RPC execution against future published IOP budget held IOP lead near 6 ms instead of growing
  to 9.8 seconds. It improved the clock model but a hands-on run still ended after a large dropout.
- RPC cycle credit plus above-normal worker priority and a 160 ms reserve left only 800 underrun frames and zero skips
  in a 117-second automated run. Its hands-on run was canceled, so it is not product code. The exact candidate is
  preserved at `patches/ps2recomp/experimental/audio-iop-cycle-credit-priority.diff`; the locked submodule is clean.

First-pass captures should use `KZ_AUDIO_DIAG` without `KZ_AUDIO_WAV`: the callback ledger writes on a background
thread, while the raw WAV tap necessarily writes from the producer thread and can perturb the boundary being
measured. Add the WAV only for a targeted producer-side comparison.
