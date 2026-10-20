#pragma once

// WIN-06 diagnostic (KZ_WIN06_WATCH=1): write watch on libkernel's Timer 2 alarm state block and node pools. See
// kz_win06_watch.cpp and docs/findings.md "WIN-06". Nothing is installed while the variable is unset.

class PS2Runtime;

void kzWin06WatchInstall(PS2Runtime &runtime); // after loadELF
