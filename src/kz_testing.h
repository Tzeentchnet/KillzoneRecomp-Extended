#pragma once

// Opt-in testing options from the launcher's Testing tab: in-memory campaign unlocks, intro skip, an isolated memory
// card, and an autopilot that presses through the front end into a chosen chapter. On an empty isolated card the
// autopilot creates KzConfig::defaultProfile through the game's own UI. Nothing is hooked while the options are off.

class PS2Runtime;

void kzTestingInstall(PS2Runtime &runtime, bool isolatedMemoryCard); // after loadELF and memory-card root selection
void kzTestingPoll(); // host loop, next to kzInputPoll(): drives the start-in-level autopilot
int kzTestingSelfTest();
