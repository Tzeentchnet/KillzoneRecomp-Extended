#pragma once

// Minimal ISO9660 reader: enough to validate the user's disc image and read files from its root/IOP directories.

#include "kz_region.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct KzIsoFile
{
    uint32_t lba = 0;
    uint32_t size = 0;
};

class KzIso
{
public:
    bool open(const std::filesystem::path &path, std::string *error = nullptr);
    // Path like "SYSTEM.CNF" or "IOP/PADMAN.IRX" (case-insensitive, ";1" version suffix optional).
    std::optional<KzIsoFile> find(const std::string &path) const;
    bool read(const KzIsoFile &file, std::vector<uint8_t> &out) const;

private:
    std::filesystem::path m_path;
    uint32_t m_rootLba = 0;
    uint32_t m_rootSize = 0;
};

// Result of checking a supported Killzone disc: SCUS-97402 (XOR CRC CAAEC49C), SCES-52893 (69FE9BB6), or
// SCES-52004 (6624A78C), chosen by SYSTEM.CNF's BOOT2 line (kz_region.h).
struct KzDiscCheck
{
    bool ok = false;
    KzRegion region = KzRegion::Us; // valid when ok
    std::string message;            // human-readable status for the launcher
};
KzDiscCheck kzCheckDisc(const std::filesystem::path &iso);

// The version of a boot ELF given directly (--elf), by file name and XOR CRC. Returns false for other executables
// (region stays US then; message in *note).
bool kzDetectElfRegion(const std::filesystem::path &elf, KzRegion &region, std::string *note);

// Extracts the small boot files (SYSTEM.CNF, the boot ELF, IOP/*.IRX, IOP/IOPRP300.IMG) from the disc image into `dir`
// (skipping files that already exist with the right size). The large data files stay in the image and are read by sector.
// Returns the path of the boot ELF, or empty on failure (message in *error).
std::filesystem::path kzPrepareDiscFiles(const std::filesystem::path &iso, const std::filesystem::path &dir, std::string *error);
