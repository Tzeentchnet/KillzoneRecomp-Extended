#pragma once

#include <cstdint>

struct R5900Context;
class PS2Runtime;

// Address-bound handlers must set the guest return PC so scheduler re-entry after a yield cannot repeat the call.
void kzSifLoadModuleAt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
void kzSifStopModuleAt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
void kzSceSifSearchModuleByName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
void kzSceSifUnloadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
void kzLgkbmInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
