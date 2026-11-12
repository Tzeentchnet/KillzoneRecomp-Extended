#pragma once

// The two PAL generated sets do not define their own function tables. Their register_functions.cpp files expose the
// functions below, which clear and fill the live US table. End is one past the last guest address each set fills.
// kz_region.cpp installs the set matching the selected disc.

#include "ps2_runtime.h"

#include <cstdint>

#if defined(KZ_HAVE_PAL)
void kzPalRegisterFunctionTable(PS2Runtime::RecompiledFunction *table, uint32_t base, uint32_t slotCount);
extern const uint32_t kzPalRegisterFunctionTableEnd;
#endif

#if defined(KZ_HAVE_EU)
void kzEuRegisterFunctionTable(PS2Runtime::RecompiledFunction *table, uint32_t base, uint32_t slotCount);
extern const uint32_t kzEuRegisterFunctionTableEnd;
#endif
