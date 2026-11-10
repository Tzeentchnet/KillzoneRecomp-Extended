#pragma once

// WIN-06 diagnostic (KZ_WIN06_WATCH=1): write watch on libkernel's Timer 2 alarm state block and node pools. An
// additional half-open guest range can be selected with KZ_WIN06_WATCH_RANGE=<start>-<end>;
// KZ_WIN06_WATCH_DEFAULT=0 omits the timer ranges, and KZ_WIN06_HW_WATCH=<address> uses an exact-word hardware
// write breakpoint on the EE thread. KZ_WIN06_HW_WATCH_AFTER=<vsync> delays arming it, and
// KZ_WIN06_VECTOR_NEXT_WATCH=1 derives the watched word from the stack vector's next chunk. See kz_win06_watch.cpp
// and docs/findings.md "WIN-06". Nothing is installed while KZ_WIN06_WATCH is unset.

class PS2Runtime;

void kzWin06WatchInstall(PS2Runtime &runtime); // after loadELF
