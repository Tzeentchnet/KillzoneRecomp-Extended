#pragma once

// Opt-in testing options from the launcher's Testing tab (KzConfig unlockAllLevels, skipIntro, startLevel): campaign
// unlocks and an intro skip, done in memory so saves are never changed, and an autopilot that presses through the front
// end into a chosen chapter. Nothing is hooked while they are off.

class PS2Runtime;

void kzTestingInstall(PS2Runtime &runtime); // after loadELF
void kzTestingPoll();                       // host loop, next to kzInputPoll(): drives the start-in-level autopilot
