# Multiplayer restoration TODO

- **Milestone:** MP0 - Reach a deterministic online-client idle/menu state
- **Last updated:** 2026-10-23
- **Status document:** [`README.md`](README.md)

This queue is separate from the campaign enhancement queue in [`../../TODO.md`](../../TODO.md). A task is complete
only when its acceptance evidence is reproducible from authorized private inputs and does not introduce campaign
regressions.

Status values: `not started` | `in progress` | `blocked` | `complete`.

## Summary

| Order | ID | Task | Status | Depends on |
|---:|---|---|---|---|
| 1 | MP-00 | Separate and safeguard the online guest | complete | - |
| 2 | MP-01 | Reconstruct staged SNR2 modules | complete | MP-00 |
| 3 | MP-02 | Execute loader, BLGFX, DNASMC, and ONLINE startup | complete | MP-01 |
| 4 | MP-03 | Make callback iteration incremental | complete | MP-02 |
| 5 | MP-10 | Explain the wait at guest PC `0x007651A8` | in progress | MP-03 |
| 6 | MP-11 | Reach a deterministic online idle/menu state | blocked | MP-10 |
| 7 | MP-20 | Recover and execute NETWORK | blocked | MP-11 |
| 8 | MP-30 | Replace or bypass retired DNAS behavior | blocked | MP-20 |
| 9 | MP-40 | Bridge the PS2 network APIs to native sockets | blocked | MP-20 |
| 10 | MP-50 | Connect one client to local Clank | blocked | MP-30, MP-40 |
| 11 | MP-60 | Complete a two-client LAN match | blocked | MP-50 |
| 12 | MP-70 | Add user-facing configuration and qualification | blocked | MP-60 |

## Completed foundation

### MP-00 - Separate and safeguard the online guest

- [x] Add `KZ_ONLINE_BUILD`.
- [x] Produce `killzone-online.exe` separately from `killzone.exe`.
- [x] Select `LOADER.ELF` and reject campaign/online guest mismatches.
- [x] Extract online files from the authorized disc without committing them.
- [x] Disable campaign-only fixed-address hooks and automation for the online guest.
- [x] Keep online compatibility timing at 60 Hz.

**Acceptance evidence:** campaign and online builds select different guests; online startup does not install campaign
address patches.

### MP-01 - Reconstruct staged SNR2 modules

- [x] Parse and validate loader-linked SNR2 metadata.
- [x] Decode packed and named relocation streams.
- [x] Resolve loader and cross-module symbols.
- [x] Apply loader-linked relocations before PS2Recomp analysis.
- [x] Trim REL containers to their resident images.
- [x] Emit indexed exports as recompiler entry points.
- [x] Build synthetic executable segments for BLGFX, DNASMC, and ONLINE.
- [x] Add focused synthetic tests.

**Acceptance evidence:** nine SNR2 tests pass; staged bundles report their unresolved imports explicitly; generated
retail derivatives remain under ignored `work/`.

### MP-02 - Execute online startup

- [x] Bypass the BIOS-less LOADFILE initialization deadlock with existing SIF module tracking.
- [x] Execute the BLGFX constructor.
- [x] Execute the DNASMC constructor.
- [x] Execute all 503 ONLINE constructors.
- [x] Enter ONLINE's indexed client export at `0x0035EFC0`.
- [x] Bind verified ONLINE CDVD SDK functions to mounted-ISO handlers.
- [x] Load and start `PFILE_R.IRX`.
- [x] Advance through the currently observed resource/content callbacks.

**Acceptance evidence:** the smoke log contains `FilePS2_driver done RegisterLibraryEntries` and
`FileStreamer activated!`; PS2Recomp retains zero decode failures and does not add ONLINE-specific unhandled
instructions.

### MP-03 - Make callback iteration incremental

- [x] Generate through a disposable staging directory.
- [x] Preserve timestamps for unchanged generated files.
- [x] Localize direct guest-call declarations instead of including the monolithic function header everywhere.
- [x] Use stable unity groups keyed by guest-address prefix.
- [x] Keep registration in its own group.
- [x] Prove an unchanged regeneration/build performs zero compiles and zero links.
- [x] Prove one callback addition rebuilds only its address group, registration, and the final executable.

**Acceptance evidence:** callback `0x007C18E8` changed four generated files and caused two compilation steps plus one
link; the following no-op build did no work.

## MP0 - Finish online-client bootstrap

### MP-10 - Explain the wait at `0x007651A8`

**Status:** in progress

- [ ] Identify the owning function and exact loop or wait condition.
- [ ] Determine whether it waits for a callback, IOP event, resource state, timer, or omitted function-map target.
- [ ] Capture the controlling object fields and producer path.
- [ ] Confirm whether headless execution changes the condition.
- [ ] Fix the underlying missing behavior; do not patch the PC forward or force a success-shaped result.

**Acceptance:**

- The wait condition has a documented producer/consumer explanation.
- A 30-second headless run either advances deterministically or remains in a proven legitimate idle loop.
- No guest exception, missing target, fabricated RPC success, or new analyzer regression occurs.

**Stop conditions:**

- If the wait depends on NETWORK or DNAS, stop runtime callback expansion and begin MP-20 or MP-30 with evidence.
- If the address lies in data misclassified as code, correct the private function map rather than synthesizing an
  unbounded block.

### MP-11 - Reach a deterministic online idle/menu state

**Status:** blocked by MP-10

- [ ] Continue adding only runtime-proven indirect targets and precise omitted return slots.
- [ ] Record each target's source PC, return PC, owning subsystem, and map provenance.
- [ ] Stop broad Ghidra-map ingestion; it marks large ONLINE data regions executable.
- [ ] Identify the first stable online UI or service-wait state.
- [ ] Promote the private map-building recipe into a deterministic, protected-content-safe tracked driver if its
  inputs and outputs can remain BYOD-only.

**Acceptance:**

- Two repeated smoke runs reach the same state without a missing recompiled target.
- All ONLINE constructors and the indexed export still execute.
- `PFILE_R.IRX` still activates.
- The analyzer retains zero decode failures and no additional unhandled instructions.

## MP1 - Execute NETWORK with complete import closure

### MP-20 - Recover and execute NETWORK

**Status:** blocked by MP-11

- [ ] Locate the loader allocation/relocation sequence for `NETWORK.REL`.
- [ ] Recover its deterministic guest base from runtime evidence.
- [ ] Add NETWORK to the synthetic ELF at its exact resident extent.
- [ ] Resolve ONLINE's 31 staged NETWORK imports.
- [ ] Resolve loader imports `fcntl`, `link`, and `times`.
- [ ] Remove `--allow-unresolved`.
- [ ] Compile and execute NETWORK constructor/export entry points.
- [ ] Inventory the relocated network and DNAS-facing API addresses.

**Acceptance:**

- The four-module bundle reports zero unresolved loader or module imports.
- No module segments overlap.
- NETWORK startup executes without a missing target or guest exception.
- A symbol/address inventory identifies socket, DNS, configuration, and DNAS boundaries.

**Stop conditions:**

- Do not guess a base merely to eliminate overlap.
- Do not replace unresolved imports with generic success stubs.

## MP2 - Local authentication and native networking

### MP-30 - Replace or bypass retired DNAS behavior

**Status:** blocked by MP-20

- [ ] Trace the DNAS state machine, inputs, outputs, callbacks, and error handling.
- [ ] Identify the minimum result the Killzone client requires before network login.
- [ ] Implement a narrow local compatibility path or bypass at the verified boundary.
- [ ] Keep failure states explicit and diagnosable.
- [ ] Ensure the client never attempts to contact retired production DNAS infrastructure.

**Acceptance:**

- The client passes the authentication gate deterministically with no external DNAS service.
- Invalid local configuration produces a visible/logged failure instead of silent success.
- No unrelated ONLINE state is skipped.

### MP-40 - Bridge PS2 network APIs to native sockets

**Status:** blocked by MP-20

- [ ] Map the required `sceNet*`, `sceInet*`, DNS, and network-control calls.
- [ ] Implement TCP and UDP operations using the repository's host boundary.
- [ ] Preserve guest byte order, structure layout, return values, and error codes.
- [ ] Implement blocking/nonblocking behavior, readiness polling, timeout behavior, and shutdown semantics actually
  used by Killzone.
- [ ] Add configurable hostname/address redirection for local services.
- [ ] Add focused host-side tests that do not require retail content.

**Acceptance:**

- Synthetic loopback tests pass for every implemented primitive.
- Guest network calls produce observable native socket traffic.
- Connection refusal, timeout, DNS failure, and disconnect are surfaced correctly.
- No production hostname is contacted unless explicitly configured by the user.

## MP3 - Local Medius service

### MP-50 - Connect one client to local Clank

**Status:** blocked by MP-30 and MP-40

- [ ] Stand up a pinned local Clank instance with Killzone application ID `10724`.
- [ ] Redirect MUIS and universe endpoints to configurable LAN addresses.
- [ ] Capture protocol-safe diagnostics for connection and login stages.
- [ ] Complete one-client login.
- [ ] Enumerate universes/lobbies.
- [ ] Create or join a test game.
- [ ] Document the exact server configuration without committing credentials or private captures.

**Acceptance:**

- A clean local server and one authorized client complete login from a documented configuration.
- The client reaches a lobby/game-list state.
- Repeating the procedure does not depend on public revival infrastructure.

**Stop conditions:**

- Do not treat an open TCP connection as successful login.
- Do not hard-code one developer machine's address or credentials.

## MP4 - Two-client LAN gameplay

### MP-60 - Complete a two-client LAN match

**Status:** blocked by MP-50

- [ ] Give each client isolated config, save data, identity, logs, and local ports.
- [ ] Log in two clients to the same local service.
- [ ] Create a game with client A.
- [ ] Discover and join it with client B.
- [ ] Verify player presence, ready/start flow, map load, and bidirectional game traffic.
- [ ] Play a controlled interval with movement, firing, damage, death/respawn, and scoreboard updates.
- [ ] Verify graceful leave, disconnect, and server shutdown.

**Acceptance:**

- Two clients on the same LAN enter the same match.
- Each client sees the other move and perform at least one gameplay action.
- The match remains stable for at least ten minutes.
- Disconnect and rejoin behavior is documented.
- No external service is required after initial software provisioning.

## MP5 - Productization and regression

### MP-70 - Add user-facing configuration and qualification

**Status:** blocked by MP-60

- [ ] Add launcher/config fields for server address, ports, and diagnostic logging.
- [ ] Fail closed when online-generated code or required retail modules are unavailable.
- [ ] Separate online saves/configuration from campaign test state where required.
- [ ] Add an online smoke target that can run without exposing retail-derived logs.
- [ ] Rerun campaign build, ISO-free tests, and a normal campaign boot.
- [ ] Document firewall requirements and two-client setup.
- [ ] Complete protected-content, credentials, dependency, and package-allowlist review.

**Acceptance:**

- A user with an authorized disc and documented local-server setup can reproduce the two-client result.
- Default campaign behavior and existing ISO-free tests remain unchanged.
- No retail binary, generated game source, credential, or private packet capture enters source control or release
  artifacts.

## Standing rules

- Add only runtime-proven guest targets; never ingest the broad ONLINE Ghidra map wholesale.
- Keep campaign and online guests in separate executables.
- Require complete four-module import closure before calling the reconstruction complete.
- Reuse existing runtime handlers only after verifying ABI and behavior equivalence.
- Never fake RPC, DNAS, or socket success to move the PC forward.
- Keep retail inputs, generated sources, maps, manifests with private paths, and captures under ignored `work/`.
- Update [`README.md`](README.md) after every milestone with measured evidence, not intended behavior.
