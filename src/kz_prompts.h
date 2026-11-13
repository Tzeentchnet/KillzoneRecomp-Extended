#pragma once

// Device-aware button prompts. Keyboard/mouse pictures are automatic; controller-family glyphs are opt-in.

class PS2Runtime;

void kzPromptsInstall(PS2Runtime &runtime);
int kzPromptsSelfTest();
