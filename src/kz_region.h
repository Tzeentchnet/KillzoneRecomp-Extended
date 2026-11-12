#pragma once

// One exe runs the US disc (SCUS-97402) and either supported PAL executable: SCES-52893 (En/Ru/El) or SCES-52004
// (En/Fr/De/Es/It/Nl). The version is chosen once, from the disc (SYSTEM.CNF BOOT2 + boot ELF CRC, kz_iso.cpp) or from
// the --elf file, before the ELF is loaded, and never changes afterwards. Guest addresses that differ between the
// executables are in kz_addrs.h.

#include <cstdint>
#include <string>

enum class KzRegion : uint8_t
{
    Us,
    Pal,
    Eu,
};

KzRegion kzRegion();
inline bool kzIsPal() { return kzRegion() != KzRegion::Us; }

const char *kzRegionTitle(KzRegion r);      // "Killzone (USA)" / "Killzone (Europe)"
const char *kzRegionSerial(KzRegion r);     // "SCUS-97402" / "SCES-52893" / "SCES-52004"
const char *kzRegionBootElf(KzRegion r);    // "SCUS_974.02" / "SCES_528.93" / "SCES_520.04"
const char *kzRegionBootFolder(KzRegion r); // isolated boot files and memory cards next to the settings file
uint32_t kzRegionBootElfXorCrc(KzRegion r); // PCSX2-style XOR CRC of the boot ELF

// Each PAL executable needs its matching generated code linked in; US is always present.
bool kzRegionSupported(KzRegion r);

// Sets the version and guest address table (kz_addrs.h). For PAL it also fills the live function table with the
// matching generated code (kz_pal_table.h). Call once, before the PS2Runtime is created and before anything writes a
// table entry (kzIpuBindRuntime, loadELF's overrides, the hooks). Returns false with a message when that is not possible.
bool kzSelectRegion(KzRegion r, std::string *error);
