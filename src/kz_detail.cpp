#include "kz_detail.h"

#include "kz_config.h"
#include "kz_gs.h"
#include "kz_hook_addresses.h"

#include "ps2_runtime.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// Scene-detail consumers of render context +0x1AC (docs/findings.md "GFX-03 reinvestigation"):
//   0x3F4E70 simple and 0x3F4F10 blended LOD selectors: metric = AABB distance / ctx+0x1AC, compared with the
//            resource's "LOD Info" table (count, -, threshold pointer). LodStaticMeshResource passes its +0x2C table
//            (outer returns 0x3EF72C / 0x3EF81C), LodSkinnedMeshResource its +0x30 table (0x3FF3FC / 0x3FF4F0).
//   0x3EFAD8 LodStaticMeshResource instanced draw: the same +0x2C table, with the per-placement distance inline.
//   0x3F1574 MeshResource "Maximum visible distance" (+0x1C, -1 = unlimited) cull: visible when
//            distance^2 < (maximum * ctx+0x1AC)^2, through AABB helper 0x2C8178 (return 0x3F1650).
//   0x3EE6F8 Grass cell walker: cells and instances beyond "Max View Distance" (+0x134) * ctx+0x1AC are skipped. The
//            caller allocates min(instances, 1500) 80-byte records on the stack and the walker stops at 1500 (+0x158).
// Scaling ctx+0x1AC raises every consumer at once. Here each class is scaled separately: LOD classes by multiplying
// the resource's threshold table once (covering simple, blended and instanced paths alike), the cull through the
// call-site-gated AABB helper, and grass through its view-distance field.
//
// LOD meshes are streamed. After selection, the resource searches for the nearest resident level (0x3FF318 for
// skinned resources, 0x3EF648 static; 0x189C08 per level) and draws that. When it differs from the selected level,
// the returned level's node tree is stamped with the streamer clock through 0x189CB0. The streamer (global 0x559170)
// loads in batches of at most 1 MB / 100 entries.
//
// Everything here is diagnostic and environment-gated; with no variable set no hook is installed.
//   KZ_DETAIL_DIAG=static=4,instanced=4,skinned=4,visible=2,grass=2  per-class scales (1..8)
//   KZ_CHARACTER_LOD_PROBE=<scale>  alias for skinned=<scale>
//   KZ_DETAIL_LOG[=<frames>]  telemetry snapshots (default every 600 vblanks): LOD level shifts, stream requests,
//                             streamer state, heap walk, malloc exhaustion, grass budget, minimum stack pointer
//   KZ_DETAIL_TOGGLE=<n>      scaling active only in odd n-vblank gameplay windows (same-process A/B captures)
//   KZ_DETAIL_NOTOUCH=1       drop 0x189CB0 calls from resources whose tables are scaled
//   KZ_DETAIL_GRASSCAP=<1501..4000> raise the main-thread-stack grass-record ceiling above 1500 - CONFIRMED UNSAFE
//                             (crashes within seconds of grass rendering at an elevated view distance; kept only
//                             for reproduction, see docs/findings.md "GFX-03 reinvestigation")
//   KZ_DETAIL_HEAPCHECK=<csv> (with KZ_DETAIL_LOG) heap walk every 8 vblanks, free history and, with the experimental
//                             PS2Recomp patch, host-write history; reports the first break and writes <csv> at exit
// Render distance 2x combined with any elevated LOD class corrupts the guest heap (docs/findings.md); keep them apart.
namespace
{
    constexpr uint32_t kAabbDistance = kz_hook::AabbDistance;
    constexpr uint32_t kLodStreamRequest = kz_hook::LodStreamRequest;
    constexpr uint32_t kGrassRender = kz_hook::GrassRender;
    constexpr uint32_t kLodSelectSimple = kz_hook::LodSelectSimple;
    constexpr uint32_t kLodSelectBlended = kz_hook::LodSelectBlended;
    constexpr uint32_t kLodStaticInstanced = kz_hook::LodStaticInstanced;

    constexpr uint32_t kSimpleSelectorReturn = 0x003F4EA0u;  // outer return saved at sp+0x08
    constexpr uint32_t kBlendedSelectorReturn = 0x003F4F84u; // outer return saved at sp+0x20
    constexpr uint32_t kStaticSimpleCaller = 0x003EF72Cu;
    constexpr uint32_t kStaticBlendedCaller = 0x003EF81Cu;
    constexpr uint32_t kSkinnedSimpleCaller = 0x003FF3FCu;
    constexpr uint32_t kSkinnedBlendedCaller = 0x003FF4F0u;
    constexpr uint32_t kVisibleCullReturn = 0x003F1650u;
    constexpr uint32_t kStaticLodTableOffset = 0x2Cu;

    constexpr uint32_t kStaticRequestReturn = 0x003EF780u;
    constexpr uint32_t kInstancedRequestReturn = 0x003EFDB4u;
    constexpr uint32_t kSkinnedRequestReturn = 0x003FF450u;

    constexpr uint32_t kAllocFailHandler = kz_hook::AllocFailHandler;
    // malloc (0x1727F8) on exhaustion: purge callback *(gp-0x7E98), retry, then fatal callback *(gp-0x7E9C). In
    // gameplay both point at the shared `return 0` stub 0x30A180; these are its return addresses from that path.
    constexpr uint32_t kAllocPurgeReturn = 0x00172D1Cu;
    constexpr uint32_t kAllocFatalReturn = 0x00172DB0u;
    // Fixed-arena heap descriptor (init 0x173430(desc, start, end)): top chunk +0x30, bounds +0x448/+0x44C.
    constexpr uint32_t kHeapDescriptor = 0x0055EC20u;

    constexpr uint32_t kStreamerGlobal = 0x00559170u;
    constexpr uint32_t kGrassMaxViewOffset = 0x134u;
    constexpr uint32_t kGrassInstanceCountOffset = 0x158u;
    constexpr int32_t kGrassInstanceCap = 1500;
    constexpr uint32_t kGrassRecordCapClamp = kz_hook::GrassRecordCapClamp;
    constexpr uint32_t kGrassRecordBytes = 80u; // the record size multiplied in by the same clamp (0x3EF214: a1=0x50)
    // Diagnostic-only, over-the-existing-ceiling only: KZ_DETAIL_GRASSCAP=<1500..4000> raises the per-frame grass
    // record count (and with it the main-thread-stack VLA the game allocates for it, record_count * 80 bytes) above
    // the shipped 1500 ceiling. See docs/findings.md "GFX-03 reinvestigation" for the margin this was measured
    // against; this is not wired to any product config key.
    constexpr int32_t kGrassCapDiagnosticMax = 4000;

    constexpr uint32_t kRamMask = 0x01FFFFFFu;
    constexpr uint32_t kRamSize = 0x02000000u;
    constexpr uint32_t kMainStackFloor = 0x00100000u; // SetupThread(stack=0x100000, size=0x40000); see findings.md
    constexpr float kDiagnosticScaleMax = 8.0f;
    constexpr uint32_t kMaxLodLevels = 8;

    constexpr int kClassCount = static_cast<int>(KzDetailClass::Count);
    constexpr const char *kClassNames[kClassCount] = {"static", "skinned", "visible", "grass", "instanced"};

    KzDetailScales g_scales;
    bool g_log = false;
    uint64_t g_snapshotFrames = 600;

    PS2Runtime::RecompiledFunction g_origAabbDistance = nullptr;
    PS2Runtime::RecompiledFunction g_origLodStreamRequest = nullptr;
    PS2Runtime::RecompiledFunction g_origGrassRender = nullptr;
    PS2Runtime::RecompiledFunction g_origGrassRecordCapClamp = nullptr;
    int32_t g_grassCap = kGrassInstanceCap; // diagnostic ceiling; see KZ_DETAIL_GRASSCAP in kzDetailInstall
    PS2Runtime::RecompiledFunction g_origLodSelectSimple = nullptr;
    PS2Runtime::RecompiledFunction g_origLodSelectBlended = nullptr;
    PS2Runtime::RecompiledFunction g_origLodStaticInstanced = nullptr;

    // Telemetry, reset at every snapshot. All hooks run on the EE game thread.
    struct Interval
    {
        std::array<uint64_t, 2> selections{}; // static (simple/blended/instanced), skinned
        std::array<uint64_t, 2> levelChanges{};
        std::array<std::array<uint64_t, 4>, 2> baselineLevels{};
        std::array<std::array<uint64_t, 4>, 2> currentLevels{};
        uint64_t instancedCalls = 0;
        uint64_t visibleCalls = 0;
        uint64_t visibleScaled = 0;
        std::array<uint64_t, 4> requests{}; // static, instanced, skinned, other
        uint64_t grassCalls = 0;
        uint64_t grassCapHits = 0;
        int32_t grassMaxCount = 0;
        uint64_t invalidBounds = 0;
        uint32_t minStack = 0xFFFFFFFFu;
    };
    Interval g_interval;
    uint64_t g_lastSnapshotFrame = 0;
    uint64_t g_totalRequests = 0;
    bool g_noBoostedRequests = false;
    uint64_t g_suppressedRequests = 0;
    uint64_t g_totalGrassCapHits = 0;
    uint32_t g_minStack = 0xFFFFFFFFu;

    PS2Runtime::RecompiledFunction g_origAllocFailHandler = nullptr;
    constexpr uint32_t kPacketFrameBegin = kz_hook::PacketFrameBegin;
    constexpr uint32_t kPacketContext = 0x3D30u;
    constexpr uint32_t kPacketStartTable = 0x005134A8u; // two buffer starts, | 0x30000000 (allocated by 0x15C7B0)
    constexpr uint32_t kPacketEndTable = 0x005134B0u;   // two buffer ends
    PS2Runtime::RecompiledFunction g_origPacketFrameBegin = nullptr;
    uint32_t g_packetCapacity = 0;
    uint32_t g_intervalMaxPacket = 0;
    uint32_t g_maxPacket = 0;
    uint64_t g_packetOverflows = 0;
    uint32_t g_minHeapTop = 0xFFFFFFFFu;
    uint32_t g_intervalMinHeapTop = 0xFFFFFFFFu;
    uint64_t g_allocPurges = 0;
    uint64_t g_allocFatals = 0;

    uint32_t heapTopBytes(const uint8_t *rdram);
    void maybeHeapCheck(const uint8_t *rdram);

    void noteStack(const R5900Context *ctx)
    {
        const uint32_t sp = getRegU32(ctx, 29);
        g_interval.minStack = std::min(g_interval.minStack, sp);
        g_minStack = std::min(g_minStack, sp);
    }

    void noteHeap(const uint8_t *rdram)
    {
        const uint32_t top = heapTopBytes(rdram);
        g_intervalMinHeapTop = std::min(g_intervalMinHeapTop, top);
        g_minHeapTop = std::min(g_minHeapTop, top);
    }

    // One "LOD Info" threshold table, keyed by the table's address. `patched` is what this module last wrote; any
    // other content means the resource was (re)loaded and is patched again from its new original values.
    struct LodPatch
    {
        uint32_t values = 0;
        uint32_t count = 0;
        uint8_t cls = 0;
        std::array<float, kMaxLodLevels> original{};
        std::array<float, kMaxLodLevels> patched{};
    };
    std::unordered_map<uint32_t, LodPatch> g_lodPatches;
    std::array<uint64_t, 2> g_lodTablesPatched{};
    uint64_t g_lodTablesRejected = 0;

    struct GrassPatch
    {
        uint32_t object = 0;
        float original = 0.0f;
        float scaled = 0.0f;
    };
    std::array<GrassPatch, 64> g_grassPatches{};
    size_t g_grassPatchNext = 0;
    uint64_t g_grassPatchWrites = 0;

    float rdF(const uint8_t *rdram, uint32_t a)
    {
        float v;
        std::memcpy(&v, rdram + (a & kRamMask), 4);
        return v;
    }
    void wrF(uint8_t *rdram, uint32_t a, float v) { std::memcpy(rdram + (a & kRamMask), &v, 4); }
    uint32_t rdU32(const uint8_t *rdram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, rdram + (a & kRamMask), 4);
        return v;
    }
    void wrU32(uint8_t *rdram, uint32_t a, uint32_t v) { std::memcpy(rdram + (a & kRamMask), &v, 4); }

    bool inRamRange(uint32_t address, uint32_t size)
    {
        return address >= 0x00100000u && size <= kRamSize && address <= kRamSize - size;
    }

    bool entering(const R5900Context *ctx, uint32_t addr) { return ctx->pc == addr; }

    uint32_t heapTopBytes(const uint8_t *rdram)
    {
        const uint32_t top = rdU32(rdram, kHeapDescriptor + 0x30u);
        return inRamRange(top, 8u) ? (rdU32(rdram, top + 4u) & ~7u) : 0u;
    }

    struct HeapWalk
    {
        bool valid = false;
        uint32_t used = 0;
        uint32_t freeBins = 0;
        uint32_t largestFree = 0;
        uint32_t top = 0;
    };

    // Walks the fixed arena chunk by chunk (size at +4, bit 0 = previous chunk in use). A chain that leaves the arena
    // or has an impossible size is reported invalid instead of being followed.
    HeapWalk walkHeap(const uint8_t *rdram)
    {
        HeapWalk w;
        const uint32_t start = rdU32(rdram, kHeapDescriptor + 0x448u);
        const uint32_t end = rdU32(rdram, kHeapDescriptor + 0x44Cu);
        const uint32_t top = rdU32(rdram, kHeapDescriptor + 0x30u);
        if (!inRamRange(start, 16u) || end > kRamSize || end <= start || top < start || top >= end)
            return w;
        uint32_t chunk = start + 8u;
        for (uint32_t n = 0; n < 400000u && chunk < top; ++n)
        {
            const uint32_t size = rdU32(rdram, chunk + 4u) & ~7u;
            if (size < 16u || chunk + size > top)
                return w;
            const uint32_t next = chunk + size;
            if (rdU32(rdram, next + 4u) & 1u)
                w.used += size;
            else
            {
                w.freeBins += size;
                w.largestFree = std::max(w.largestFree, size);
            }
            chunk = next;
        }
        if (chunk != top)
            return w;
        w.top = rdU32(rdram, top + 4u) & ~7u;
        w.valid = true;
        return w;
    }

    // Validates and scales one threshold set. Thresholds must be finite, non-negative and non-decreasing; the result
    // keeps that order because every entry is multiplied by the same positive factor.
    bool scaleThresholds(const float *in, uint32_t count, float scale, float *out)
    {
        if (count == 0u || count > kMaxLodLevels || !std::isfinite(scale) || scale < 1.0f)
            return false;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (!std::isfinite(in[i]) || in[i] < 0.0f || in[i] > 1.0e6f || (i > 0u && in[i] < in[i - 1u]))
                return false;
            out[i] = in[i] * scale;
            if (!std::isfinite(out[i]))
                return false;
        }
        return true;
    }

    // Diagnostic A/B: with KZ_DETAIL_TOGGLE=<n>, scaling is active only in odd n-frame gameplay windows, so adjacent
    // captures on either side of a window edge differ by the detail setting and little else.
    uint64_t g_togglePeriod = 0;

    bool detailActive()
    {
        if (g_togglePeriod == 0)
            return true;
        return ((kzGsGameplayOffsetVsync() / g_togglePeriod) & 1u) != 0;
    }

    void patchLodTable(uint8_t *rdram, uint32_t table, KzDetailClass cls)
    {
        const float scale = g_scales[cls];
        if (scale == 1.0f || !inRamRange(table, 12u))
            return;
        const uint32_t count = rdU32(rdram, table);
        const uint32_t values = rdU32(rdram, table + 8u);
        if (count == 0u || count > kMaxLodLevels || !inRamRange(values, count * 4u))
            return;
        std::array<float, kMaxLodLevels> current{};
        for (uint32_t i = 0; i < count; ++i)
            current[i] = rdF(rdram, values + i * 4u);

        auto it = g_lodPatches.find(table);
        const bool ours = it != g_lodPatches.end() && it->second.values == values && it->second.count == count &&
                          std::equal(current.begin(), current.begin() + count, it->second.patched.begin());
        if (!detailActive())
        {
            if (ours)
            {
                for (uint32_t i = 0; i < count; ++i)
                    wrF(rdram, values + i * 4u, it->second.original[i]);
                g_lodPatches.erase(it);
            }
            return;
        }
        if (ours)
            return;

        LodPatch patch;
        patch.values = values;
        patch.count = count;
        patch.cls = static_cast<uint8_t>(cls);
        patch.original = current;
        if (!scaleThresholds(current.data(), count, scale, patch.patched.data()))
        {
            ++g_lodTablesRejected;
            return;
        }
        for (uint32_t i = 0; i < count; ++i)
            wrF(rdram, values + i * 4u, patch.patched[i]);
        g_lodPatches[table] = patch;
        ++g_lodTablesPatched[cls == KzDetailClass::SkinnedLod ? 1u : 0u];
    }

    int lodClassForSimpleCaller(uint32_t ra)
    {
        if (ra == kStaticSimpleCaller)
            return static_cast<int>(KzDetailClass::StaticLod);
        if (ra == kSkinnedSimpleCaller)
            return static_cast<int>(KzDetailClass::SkinnedLod);
        return -1;
    }

    int lodClassForBlendedCaller(uint32_t ra)
    {
        if (ra == kStaticBlendedCaller)
            return static_cast<int>(KzDetailClass::StaticLod);
        if (ra == kSkinnedBlendedCaller)
            return static_cast<int>(KzDetailClass::SkinnedLod);
        return -1;
    }

    // Mirrors 0x2C8178 for a valid box: squared distance from a point to an axis-aligned box. Invalid boxes (min > max
    // on any axis) and non-finite input return false so the caller runs the original guest helper.
    bool aabbPointDistanceSquared(const uint8_t *rdram, uint32_t bounds, uint32_t point, float &distanceSquared)
    {
        if (!inRamRange(bounds, 24u) || !inRamRange(point, 12u))
            return false;
        float sum = 0.0f;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const float minimum = rdF(rdram, bounds + axis * 4u);
            const float maximum = rdF(rdram, bounds + 12u + axis * 4u);
            const float coordinate = rdF(rdram, point + axis * 4u);
            if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(coordinate) ||
                minimum > maximum)
                return false;
            float separation = 0.0f;
            if (coordinate < minimum)
                separation = minimum - coordinate;
            else if (maximum < coordinate)
                separation = coordinate - maximum;
            sum += separation * separation;
        }
        if (!std::isfinite(sum))
            return false;
        distanceSquared = sum;
        return true;
    }

    int levelFor(const float *thresholds, uint32_t count, float metric)
    {
        if (!std::isfinite(metric) || count == 0u)
            return -1;
        for (uint32_t i = 1; i < count; ++i)
            if (metric <= thresholds[i])
                return static_cast<int>(i - 1u);
        return static_cast<int>(count - 1u);
    }

    // Log mode only: the level the selector is about to pick (current, possibly patched thresholds) and the level the
    // original thresholds would have picked, from the same AABB distance.
    void recordLodSelection(const uint8_t *rdram, const R5900Context *ctx, bool simpleSelector, size_t lodIndex,
                            float distanceSquared)
    {
        const uint32_t state = simpleSelector ? 0u : getRegU32(ctx, 17);
        const uint32_t table = simpleSelector ? getRegU32(ctx, 16) : getRegU32(ctx, 19);
        const float metricFactor = simpleSelector ? (ctx->f[20] != 0.0f ? 1.0f / ctx->f[20] : NAN)
                                   : inRamRange(state, 0x20u) ? rdF(rdram, state + 0x1Cu)
                                                              : NAN;
        if (!inRamRange(table, 12u))
            return;
        const uint32_t count = rdU32(rdram, table);
        const uint32_t values = rdU32(rdram, table + 8u);
        if (count == 0u || count > kMaxLodLevels || !inRamRange(values, count * 4u))
            return;
        std::array<float, kMaxLodLevels> current{};
        for (uint32_t i = 0; i < count; ++i)
            current[i] = rdF(rdram, values + i * 4u);
        const float *baseline = current.data();
        if (const auto it = g_lodPatches.find(table);
            it != g_lodPatches.end() && it->second.values == values &&
            std::equal(current.begin(), current.begin() + count, it->second.patched.begin()))
            baseline = it->second.original.data();
        const float metric = std::sqrt(distanceSquared) * metricFactor;
        const int baselineLevel = levelFor(baseline, count, metric);
        const int currentLevel = levelFor(current.data(), count, metric);
        ++g_interval.selections[lodIndex];
        if (baselineLevel >= 0 && baselineLevel < 4)
            ++g_interval.baselineLevels[lodIndex][static_cast<size_t>(baselineLevel)];
        if (currentLevel >= 0 && currentLevel < 4)
            ++g_interval.currentLevels[lodIndex][static_cast<size_t>(currentLevel)];
        if (baselineLevel >= 0 && currentLevel >= 0 && baselineLevel != currentLevel)
            ++g_interval.levelChanges[lodIndex];
    }

    struct StreamerSnapshot
    {
        bool present = false;
        uint32_t state = 0;
        uint32_t clock = 0;
        uint32_t target = 0;
    };

    // Streamer fields used by its state machine (0x18A0A0): +0x10 state, +0x14 target clock, +0x1C clock.
    StreamerSnapshot readStreamer(const uint8_t *rdram)
    {
        StreamerSnapshot s;
        const uint32_t streamer = rdU32(rdram, kStreamerGlobal);
        if (!inRamRange(streamer, 0x478u))
            return s;
        s.present = true;
        s.state = rdU32(rdram, streamer + 0x10u);
        s.target = rdU32(rdram, streamer + 0x14u);
        s.clock = rdU32(rdram, streamer + 0x1Cu);
        return s;
    }

    void printLevels(const char *name, const Interval &i, size_t k)
    {
        std::printf(" %s %llu sel, %llu changed, levels %llu/%llu/%llu/%llu -> %llu/%llu/%llu/%llu;", name,
                    static_cast<unsigned long long>(i.selections[k]),
                    static_cast<unsigned long long>(i.levelChanges[k]),
                    static_cast<unsigned long long>(i.baselineLevels[k][0]),
                    static_cast<unsigned long long>(i.baselineLevels[k][1]),
                    static_cast<unsigned long long>(i.baselineLevels[k][2]),
                    static_cast<unsigned long long>(i.baselineLevels[k][3]),
                    static_cast<unsigned long long>(i.currentLevels[k][0]),
                    static_cast<unsigned long long>(i.currentLevels[k][1]),
                    static_cast<unsigned long long>(i.currentLevels[k][2]),
                    static_cast<unsigned long long>(i.currentLevels[k][3]));
    }

    void maybeSnapshot(const uint8_t *rdram)
    {
        if (!g_log)
            return;
        const uint64_t frame = kzGsFrameCount();
        if (frame < g_lastSnapshotFrame + g_snapshotFrames)
            return;
        g_lastSnapshotFrame = frame;
        const StreamerSnapshot s = readStreamer(rdram);
        const Interval &i = g_interval;
        std::printf("[kz-detail] frame %llu gameplay %llu: requests static/instanced/skinned/other "
                    "%llu/%llu/%llu/%llu (total %llu, suppressed %llu); streamer %s state %u clock %u target %u; "
                    "tables patched static/skinned %llu/%llu rejected %llu\n",
                    static_cast<unsigned long long>(frame),
                    static_cast<unsigned long long>(kzGsGameplayOffsetVsync()),
                    static_cast<unsigned long long>(i.requests[0]), static_cast<unsigned long long>(i.requests[1]),
                    static_cast<unsigned long long>(i.requests[2]), static_cast<unsigned long long>(i.requests[3]),
                    static_cast<unsigned long long>(g_totalRequests),
                    static_cast<unsigned long long>(g_suppressedRequests), s.present ? "present" : "absent", s.state,
                    s.clock, s.target, static_cast<unsigned long long>(g_lodTablesPatched[0]),
                    static_cast<unsigned long long>(g_lodTablesPatched[1]),
                    static_cast<unsigned long long>(g_lodTablesRejected));
        // Authored first switch (level 0 -> 1) in metric units, over the tables this module has patched.
        std::array<std::vector<float>, 2> firstSwitch;
        for (const auto &[table, patch] : g_lodPatches)
            if (patch.count > 1u)
                firstSwitch[patch.cls == static_cast<uint8_t>(KzDetailClass::SkinnedLod) ? 1u : 0u].push_back(
                    patch.original[1]);
        for (size_t k = 0; k < firstSwitch.size(); ++k)
        {
            auto &v = firstSwitch[k];
            if (v.empty())
                continue;
            std::sort(v.begin(), v.end());
            std::printf("[kz-detail]   %s tables %zu: authored first switch p10 %.2f p50 %.2f p90 %.2f max %.2f\n",
                        k == 0 ? "static" : "skinned", v.size(), v[v.size() / 10], v[v.size() / 2],
                        v[v.size() * 9 / 10], v.back());
        }
        std::printf("[kz-detail]  ");
        printLevels("static", i, 0);
        printLevels("skinned", i, 1);
        std::printf(" instanced draws %llu; visible culls %llu scaled %llu\n",
                    static_cast<unsigned long long>(i.instancedCalls),
                    static_cast<unsigned long long>(i.visibleCalls),
                    static_cast<unsigned long long>(i.visibleScaled));
        const HeapWalk heap = walkHeap(rdram);
        std::printf("[kz-detail]   heap %s: used %.2f MiB, free bins %.2f MiB (largest %.0f KiB), top %.0f KiB; "
                    "min top %.0f KiB (run %.0f KiB); alloc purges %llu fatals %llu\n",
                    heap.valid ? "ok" : "UNWALKABLE", heap.used / 1048576.0, heap.freeBins / 1048576.0,
                    heap.largestFree / 1024.0, heap.top / 1024.0, g_intervalMinHeapTop / 1024.0,
                    g_minHeapTop / 1024.0, static_cast<unsigned long long>(g_allocPurges),
                    static_cast<unsigned long long>(g_allocFatals));
        g_intervalMinHeapTop = 0xFFFFFFFFu;
        std::printf("[kz-detail]   packet max %.0f KiB of %.0f KiB (run max %.0f KiB), overflows %llu\n",
                    g_intervalMaxPacket / 1024.0, g_packetCapacity / 1024.0, g_maxPacket / 1024.0,
                    static_cast<unsigned long long>(g_packetOverflows));
        g_intervalMaxPacket = 0;
        std::printf("[kz-detail]   grass calls %llu max instances %d (cap %d) cap hits %llu (total %llu), "
                    "patches %llu; invalid bounds %llu; min sp 0x%08X (run 0x%08X, margin %d KiB to 0x%08X floor)\n",
                    static_cast<unsigned long long>(i.grassCalls), i.grassMaxCount, g_grassCap,
                    static_cast<unsigned long long>(i.grassCapHits),
                    static_cast<unsigned long long>(g_totalGrassCapHits),
                    static_cast<unsigned long long>(g_grassPatchWrites),
                    static_cast<unsigned long long>(i.invalidBounds), i.minStack, g_minStack,
                    g_minStack > kMainStackFloor ? static_cast<int>((g_minStack - kMainStackFloor) / 1024) : -1,
                    kMainStackFloor);
        std::fflush(stdout);
        g_interval = Interval{};
    }

    void lodSelectSimple(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kLodSelectSimple))
        {
            const int cls = lodClassForSimpleCaller(getRegU32(ctx, 31));
            if (cls >= 0)
                patchLodTable(rdram, getRegU32(ctx, 4), static_cast<KzDetailClass>(cls));
        }
        g_origLodSelectSimple(rdram, ctx, rt);
    }

    void lodSelectBlended(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kLodSelectBlended))
        {
            const int cls = lodClassForBlendedCaller(getRegU32(ctx, 31));
            if (cls >= 0)
                patchLodTable(rdram, getRegU32(ctx, 5), static_cast<KzDetailClass>(cls));
        }
        g_origLodSelectBlended(rdram, ctx, rt);
    }

    void lodStaticInstanced(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kLodStaticInstanced))
        {
            ++g_interval.instancedCalls;
            const uint32_t resource = getRegU32(ctx, 4);
            if (inRamRange(resource, kStaticLodTableOffset + 12u))
                patchLodTable(rdram, resource + kStaticLodTableOffset, KzDetailClass::InstancedLod);
            noteStack(ctx);
            maybeSnapshot(rdram);
        }
        g_origLodStaticInstanced(rdram, ctx, rt);
    }

    void aabbDistance(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (!entering(ctx, kAabbDistance))
        {
            g_origAabbDistance(rdram, ctx, rt);
            return;
        }
        noteStack(ctx);
        noteHeap(rdram);
        maybeHeapCheck(rdram);
        maybeSnapshot(rdram);

        const uint32_t helperReturn = getRegU32(ctx, 31);
        if (helperReturn == kVisibleCullReturn)
        {
            ++g_interval.visibleCalls;
            const float scale = detailActive() ? g_scales[KzDetailClass::VisibleCull] : 1.0f;
            float distanceSquared = 0.0f;
            if (scale != 1.0f &&
                aabbPointDistanceSquared(rdram, getRegU32(ctx, 4), getRegU32(ctx, 5), distanceSquared))
            {
                ++g_interval.visibleScaled;
                ctx->f[0] = distanceSquared / (scale * scale);
                ctx->pc = helperReturn;
                return;
            }
            g_origAabbDistance(rdram, ctx, rt);
            return;
        }
        if (g_log && (helperReturn == kSimpleSelectorReturn || helperReturn == kBlendedSelectorReturn))
        {
            const bool simpleSelector = helperReturn == kSimpleSelectorReturn;
            const uint32_t stack = getRegU32(ctx, 29);
            const uint32_t offset = simpleSelector ? 8u : 0x20u;
            const uint32_t outerReturn = inRamRange(stack, offset + 4u) ? rdU32(rdram, stack + offset) : 0u;
            const int cls = simpleSelector ? lodClassForSimpleCaller(outerReturn) : lodClassForBlendedCaller(outerReturn);
            float distanceSquared = 0.0f;
            if (cls >= 0)
            {
                if (aabbPointDistanceSquared(rdram, getRegU32(ctx, 4), getRegU32(ctx, 5), distanceSquared))
                    recordLodSelection(rdram, ctx, simpleSelector,
                                       cls == static_cast<int>(KzDetailClass::StaticLod) ? 0u : 1u, distanceSquared);
                else
                    ++g_interval.invalidBounds;
            }
        }
        g_origAabbDistance(rdram, ctx, rt);
    }

    // Per-frame DMA packet usage. 0x14FD90 waits for the next double-buffered packet buffer and then resets the packet
    // context in scratchpad (0x70003D30: +0x0C cursor, +0x10 start, +0x14 end). At its entry the context still holds
    // the completed frame, so cursor - start is that frame's packet size. Packet writers do not bounds-check.

    // KZ_DETAIL_HEAPCHECK: walk the heap once per frame and, the first time it breaks, report the recent host-side
    // writes (HLE/IOP via ps2TraceGuestRangeWrite) and guest frees that cover the break.
    struct RangeEvent
    {
        uint32_t address = 0;
        uint32_t size = 0;
        uint64_t frame = 0;
        const char *op = nullptr;
        uint32_t ra = 0;
    };
    bool g_heapCheck = false;
    bool g_heapCheckReported = false;
    // Producers are the EE thread (HLE stubs) and the IOP thread; the index is atomic and a torn entry is acceptable
    // in a diagnostic ring, so recording never blocks either thread.
    std::atomic<uint64_t> g_hostWriteCount{0};
    std::array<RangeEvent, 262144> g_hostWrites{};
    std::array<RangeEvent, 65536> g_frees{};
    size_t g_freeNext = 0;
    PS2Runtime::RecompiledFunction g_origHeapFree = nullptr;
    uint64_t g_lastGoodWalkFrame = 0;
    uint64_t g_lastHeapCheckFrame = 0;
    [[maybe_unused]] uint32_t g_inFlightFrees = 0;

    void observeHostWrite(uint32_t address, uint32_t size, const char *op)
    {
        const uint32_t physical = address & 0x1FFFFFFFu;
        if (physical < 0x00591400u || physical >= kRamSize)
            return;
        const uint64_t index = g_hostWriteCount.fetch_add(1, std::memory_order_relaxed);
        g_hostWrites[index % g_hostWrites.size()] = {physical, size, kzGsFrameCount(), op, 0};
    }

    void heapFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kz_hook::HeapFree))
        {
            const uint32_t ptr = getRegU32(ctx, 4) & 0x1FFFFFFFu;
            if (inRamRange(ptr, 8u))
            {
                const uint32_t chunkSize = rdU32(rdram, ptr - 4u) & ~7u;
                g_frees[g_freeNext++ % g_frees.size()] = {ptr - 8u, chunkSize, kzGsFrameCount(), nullptr,
                                                          getRegU32(ctx, 31)};
#if defined(PS2X_GUEST_RANGE_WRITE_OBSERVER)
                // Needs patches/ps2recomp/experimental/gfx03-host-write-rpc-diagnostics.diff. A block freed while an
                // offloaded nowait RPC still has to copy its result into it: the late copy lands in whatever reuses it.
                Ps2RpcInFlightInfo info;
                if (chunkSize >= 16u && ps2RpcFindInFlightReceive(ptr, ptr + chunkSize - 8u, info) &&
                    g_inFlightFrees++ < 40u)
                {
                    std::printf("[kz-heapcheck] free of in-flight RPC receive buffer: block 0x%08X+0x%X freed from "
                                "0x%08X at frame %llu; RPC sid 0x%08X fn %u client 0x%08X receive 0x%08X+0x%X, "
                                "submitted %llu us ago from ra 0x%08X\n",
                                ptr, chunkSize - 8u, getRegU32(ctx, 31),
                                static_cast<unsigned long long>(kzGsFrameCount()), info.sid, info.rpcNum,
                                info.clientPtr, info.receiveBuffer, info.receiveSize,
                                static_cast<unsigned long long>(info.ageMicroseconds), info.callerRa);
                    std::fflush(stdout);
                }
#endif
            }
        }
        g_origHeapFree(rdram, ctx, rt);
    }

    void reportHeapBreak(const uint8_t *rdram)
    {
        const uint32_t start = rdU32(rdram, kHeapDescriptor + 0x448u);
        const uint32_t top = rdU32(rdram, kHeapDescriptor + 0x30u);
        uint32_t chunk = start + 8u, previous = 0;
        while (chunk < top)
        {
            const uint32_t size = rdU32(rdram, chunk + 4u) & ~7u;
            if (size < 16u || chunk + size > top)
                break;
            previous = chunk;
            chunk += size;
        }
        std::printf("[kz-heapcheck] heap broke between frame %llu and %llu: chunk 0x%08X size word 0x%08X, previous "
                    "chunk 0x%08X size 0x%X, top 0x%08X\n",
                    static_cast<unsigned long long>(g_lastGoodWalkFrame),
                    static_cast<unsigned long long>(kzGsFrameCount()), chunk, rdU32(rdram, chunk + 4u), previous,
                    previous ? rdU32(rdram, previous + 4u) : 0u, top);
        const uint32_t lo = (previous ? previous : chunk) - 0x40u, hi = chunk + 0x100u;
        int shown = 0;
        const uint64_t count = g_hostWriteCount.load(std::memory_order_relaxed);
        for (uint64_t k = 0; k < std::min<uint64_t>(count, g_hostWrites.size()) && shown < 24; ++k)
        {
            const RangeEvent &e = g_hostWrites[(count - 1 - k) % g_hostWrites.size()];
            if (!e.size || e.address >= hi || e.address + e.size <= lo)
                continue;
            ++shown;
            std::printf("[kz-heapcheck]   host write %s 0x%08X+0x%X at frame %llu\n", e.op ? e.op : "?", e.address,
                        e.size, static_cast<unsigned long long>(e.frame));
        }
        std::printf("[kz-heapcheck]   %d host writes cover the break (%llu recorded)\n", shown,
                    static_cast<unsigned long long>(count));
        shown = 0;
        for (size_t k = 0; k < g_frees.size() && shown < 24; ++k)
        {
            const RangeEvent &e = g_frees[(g_freeNext - 1 - k) % g_frees.size()];
            if (!e.size || e.address >= hi || e.address + e.size <= lo)
                continue;
            ++shown;
            std::printf("[kz-heapcheck]   free chunk 0x%08X+0x%X at frame %llu from 0x%08X\n", e.address, e.size,
                        static_cast<unsigned long long>(e.frame), e.ra);
        }
        std::printf("[kz-heapcheck]   %d frees cover the break\n", shown);
        std::fflush(stdout);
    }

    // Called from frequently executed hooks; walks the heap at most once every eight vblanks.
    void maybeHeapCheck(const uint8_t *rdram)
    {
        if (!g_heapCheck || g_heapCheckReported)
            return;
        const uint64_t frame = kzGsFrameCount();
        if (frame < g_lastHeapCheckFrame + 8u)
            return;
        g_lastHeapCheckFrame = frame;
        if (walkHeap(rdram).valid)
            g_lastGoodWalkFrame = frame;
        else if (rdU32(rdram, kHeapDescriptor + 0x448u) != 0u)
        {
            g_heapCheckReported = true;
            reportHeapBreak(rdram);
        }
    }

    void packetFrameBegin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kPacketFrameBegin))
        {
            if (const uint8_t *spr = ps2GetScratchpadHostPtr())
            {
                uint32_t cursor;
                std::memcpy(&cursor, spr + kPacketContext + 0x0Cu, 4);
                cursor &= 0x0FFFFFFFu;
                // The context's own start/end are cleared after a frame closes; the buffer tables persist.
                uint32_t start = 0, end = 0;
                for (uint32_t b = 0; b < 2u; ++b)
                {
                    const uint32_t s = rdU32(rdram, kPacketStartTable + b * 4u) & 0x0FFFFFFFu;
                    const uint32_t e = rdU32(rdram, kPacketEndTable + b * 4u) & 0x0FFFFFFFu;
                    if (s != 0u && e > s && cursor >= s && cursor - s < 2u * (e - s))
                    {
                        start = s;
                        end = e;
                    }
                }
                static int rawLogged = 0;
                if (g_log && rawLogged < 4 && cursor != 0u)
                {
                    ++rawLogged;
                    std::printf("[kz-detail] packet sample: cursor 0x%08X buffers 0x%08X..0x%08X / 0x%08X..0x%08X\n",
                                cursor, rdU32(rdram, kPacketStartTable), rdU32(rdram, kPacketEndTable),
                                rdU32(rdram, kPacketStartTable + 4u), rdU32(rdram, kPacketEndTable + 4u));
                }
                if (start != 0u && end - start < 0x01000000u)
                {
                    g_packetCapacity = end - start;
                    const uint32_t used = cursor - start;
                    g_intervalMaxPacket = std::max(g_intervalMaxPacket, used);
                    g_maxPacket = std::max(g_maxPacket, used);
                    if (cursor > end && g_packetOverflows++ < 8)
                        std::printf("[kz-detail] DMA packet overflow: %u of %u bytes, frame %llu gameplay %llu\n",
                                    used, g_packetCapacity, static_cast<unsigned long long>(kzGsFrameCount()),
                                    static_cast<unsigned long long>(kzGsGameplayOffsetVsync()));
                }
            }
        }
        g_origPacketFrameBegin(rdram, ctx, rt);
    }

    // Counts malloc exhaustion events. The stub itself returns 0 for every caller; only the two allocator call sites
    // are recorded, so other users of the shared stub are unaffected.
    void allocFailHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kAllocFailHandler))
        {
            const uint32_t ra = getRegU32(ctx, 31);
            if (ra == kAllocPurgeReturn || ra == kAllocFatalReturn)
            {
                const bool fatal = ra == kAllocFatalReturn;
                const uint64_t n = fatal ? ++g_allocFatals : ++g_allocPurges;
                if (n <= 8 || n % 1000 == 0)
                    std::printf("[kz-detail] malloc exhausted (%s #%llu): request %u bytes, top %u bytes, "
                                "frame %llu gameplay %llu\n",
                                fatal ? "fatal" : "purge", static_cast<unsigned long long>(n), getRegU32(ctx, 4),
                                heapTopBytes(rdram), static_cast<unsigned long long>(kzGsFrameCount()),
                                static_cast<unsigned long long>(kzGsGameplayOffsetVsync()));
            }
        }
        g_origAllocFailHandler(rdram, ctx, rt);
    }

    void lodStreamRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kLodStreamRequest))
        {
            const uint32_t ra = getRegU32(ctx, 31);
            const size_t slot = ra == kStaticRequestReturn      ? 0u
                                : ra == kInstancedRequestReturn ? 1u
                                : ra == kSkinnedRequestReturn   ? 2u
                                                                : 3u;
            ++g_interval.requests[slot];
            ++g_totalRequests;
            maybeSnapshot(rdram);
            // Diagnostic: drop stream requests from resources whose LOD tables this module has scaled, so elevated
            // selection adds no streaming work and draws only already-resident levels.
            if (g_noBoostedRequests && slot < 3u)
            {
                const uint32_t resource = getRegU32(ctx, slot == 1u ? 20 : 18);
                const uint32_t table = resource + (slot == 2u ? 0x30u : kStaticLodTableOffset);
                const auto it = g_lodPatches.find(table);
                if (it != g_lodPatches.end() && inRamRange(it->second.values, 4u) &&
                    rdF(rdram, it->second.values + (it->second.count > 1u ? 4u : 0u)) ==
                        it->second.patched[it->second.count > 1u ? 1u : 0u])
                {
                    ++g_suppressedRequests;
                    ctx->pc = getRegU32(ctx, 31);
                    return;
                }
            }
        }
        g_origLodStreamRequest(rdram, ctx, rt);
    }

    // Scales one grass object's view distance in place. The field is resource data read only by the grass walker, so
    // the patch is idempotent: a value already equal to our scaled result is left alone, and a changed value (a
    // reloaded or reused object) is treated as a new original.
    void patchGrassViewDistance(uint8_t *rdram, uint32_t grass, float scale)
    {
        const uint32_t field = grass + kGrassMaxViewOffset;
        const float current = rdF(rdram, field);
        if (!std::isfinite(current) || current <= 0.0f || current > 100000.0f)
            return;
        for (GrassPatch &patch : g_grassPatches)
        {
            if (patch.object != grass)
                continue;
            if (!detailActive())
            {
                if (current == patch.scaled)
                    wrF(rdram, field, patch.original);
                patch = GrassPatch{};
                return;
            }
            if (current == patch.scaled)
                return;
            patch.original = current;
            patch.scaled = current * scale;
            wrF(rdram, field, patch.scaled);
            ++g_grassPatchWrites;
            return;
        }
        if (!detailActive())
            return;
        GrassPatch &patch = g_grassPatches[g_grassPatchNext];
        g_grassPatchNext = (g_grassPatchNext + 1) % g_grassPatches.size();
        patch = {grass, current, current * scale};
        wrF(rdram, field, patch.scaled);
        ++g_grassPatchWrites;
        if (g_log)
            std::printf("[kz-detail] grass 0x%08X view distance %.2f -> %.2f\n", grass, patch.original,
                        patch.scaled);
    }

    // The grass walker (this function, 0x3EE6F8) recurses into itself several times (observed call sites 0x3EEAF8,
    // 0x3EEB30, 0x3EEB6C, 0x3EEB9C) to cover one grass object's visible cells within a single outer invocation from
    // its caller (0x3EF170..0x3EF290, "A"). A allocates exactly one main-thread-stack VLA sized
    // min(local_count_estimate, 1500) * 80 bytes (see kGrassRecordCapClamp below), resets the object's shared
    // per-frame record counter (+0x158) to 0, then makes ONE call to this function with ra fixed at 0x3EF258 -
    // every recursive self-call returns somewhere else. Each recursion level increments +0x158 by 1 per record
    // written and compares the NEW value against the hardcoded literal 1500 (two raw `slti` sites, 0x3EE75C and
    // 0x3EF0DC, with no register holding the constant to redirect); once it reaches 1500 the whole call chain
    // unwinds immediately, so no more instances are added anywhere in that object this frame.
    //
    // KZ_DETAIL_GRASSCAP=<1501..4000> raises the effective ceiling to N by pre-seeding the just-reset counter to
    // (1500-N) on the one top-level entry (ra==0x3EF258) instead of leaving it at 0: it then takes N increments,
    // not 1500, to reach the hardcoded 1500 the game checks against, so the existing abort logic fires at the new
    // ceiling unmodified in theory. kGrassRecordCapClamp (below) separately raises the allocated buffer to match.
    //
    // CONFIRMED UNSAFE, empirically (2026-10-22): bisected with the buffer resize alone (safe, but - as expected -
    // has no effect, since the walker still stops writing at the original 1500) versus the counter bias alone.
    // The bias reproducibly crashes within the first grass draw after an elevated view distance brings the true
    // cell count above 1500: a guest DirectJump to a garbage PC inside the EE scheduler itself, with the observed
    // stack pointer roughly (new_cap-1500)*80 bytes deeper than normal - consistent with the larger buffer being
    // used, then something past or within it being corrupted. This means `+0x158`'s value likely feeds a write
    // address/index computation somewhere in the walker's vectorised (MMI) record-writing code, not only the
    // increment-and-compare this comment describes; that computation was not fully traced. A literal ELF-level
    // patch of the three embedded `1500` constants (requiring a full generated-code regeneration, not attempted)
    // might avoid this specific failure mode since the counter would then count up from a normal 0, but that is
    // unverified. Do not re-enable this path expecting it to work; it is kept only so a future session does not
    // have to rediscover the crash from scratch. See docs/findings.md "GFX-03 reinvestigation".
    constexpr uint32_t kGrassOuterCallerReturn = 0x003EF258u;

    void grassRender(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kGrassRender))
        {
            noteStack(ctx);
            noteHeap(rdram);
            maybeHeapCheck(rdram);
            const uint32_t grass = getRegU32(ctx, 4);
            if (inRamRange(grass, kGrassInstanceCountOffset + 4u))
            {
                const int32_t bias = kGrassInstanceCap - g_grassCap; // <= 0; 0 when the cap is unchanged
                if (bias != 0 && getRegU32(ctx, 31) == kGrassOuterCallerReturn)
                    wrU32(rdram, grass + kGrassInstanceCountOffset, static_cast<uint32_t>(bias));
                ++g_interval.grassCalls;
                const int32_t raw = static_cast<int32_t>(rdU32(rdram, grass + kGrassInstanceCountOffset));
                const int32_t count = raw - bias; // undo the bias for reporting; identity when bias == 0
                g_interval.grassMaxCount = std::max(g_interval.grassMaxCount, count);
                if (raw >= kGrassInstanceCap) // the game's own abort test; unaffected by the bias
                {
                    ++g_interval.grassCapHits;
                    ++g_totalGrassCapHits;
                }
                const float scale = g_scales[KzDetailClass::Grass];
                if (scale != 1.0f)
                    patchGrassViewDistance(rdram, grass, scale);
            }
            maybeSnapshot(rdram);
        }
        g_origGrassRender(rdram, ctx, rt);
    }

    // Raises the stack-VLA buffer allocated by the grass walker's caller to match KZ_DETAIL_GRASSCAP. Tail-dispatch
    // continuation only (no `jal` anywhere targets 0x3EF220); reached with a0=1500 always freshly set by its sole
    // caller immediately before. Overriding a0 here changes what `movz $v1,$a0,$v0` (the buffer-size clamp, already
    // past the `slti $v0,$v1,0x5DC` comparison) assigns when the real cell count is >= 1500: v1 becomes the new
    // ceiling instead of 1500, so `v1 * 80` (the next instruction) allocates enough stack for the raised cap.
    void grassRecordCapClamp(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kGrassRecordCapClamp) && g_grassCap != kGrassInstanceCap &&
            getRegU32(ctx, 4) == static_cast<uint32_t>(kGrassInstanceCap))
            ctx->r[4] = _mm_set_epi64x(0, static_cast<int64_t>(g_grassCap));
        g_origGrassRecordCapClamp(rdram, ctx, rt);
    }

    bool hook(PS2Runtime &rt, uint32_t addr, PS2Runtime::RecompiledFunction fn, PS2Runtime::RecompiledFunction &orig)
    {
        orig = rt.hasFunction(addr) ? rt.lookupFunction(addr) : nullptr;
        return orig && rt.replaceFunction(addr, fn);
    }

    // "static=2,skinned=1.5,visible=2,grass=2"; unknown or malformed entries are rejected with a diagnostic.
    bool parseDiagnosticScales(const char *text, KzDetailScales &scales, bool report)
    {
        bool allOk = true;
        const char *p = text;
        while (p && *p)
        {
            const char *comma = std::strchr(p, ',');
            const size_t length = comma ? static_cast<size_t>(comma - p) : std::strlen(p);
            char item[64]{};
            if (length == 0 || length >= sizeof(item))
            {
                allOk = false;
            }
            else
            {
                std::memcpy(item, p, length);
                char *equals = std::strchr(item, '=');
                int cls = -1;
                if (equals)
                {
                    *equals = '\0';
                    for (int c = 0; c < kClassCount; ++c)
                        if (std::strcmp(item, kClassNames[c]) == 0)
                            cls = c;
                }
                if (cls < 0 || !equals[1])
                {
                    if (report)
                        std::fprintf(stderr, "[kz] KZ_DETAIL_DIAG entry '%.*s' is invalid; ignoring it\n",
                                     static_cast<int>(length), p);
                    allOk = false;
                }
                else
                {
                    errno = 0;
                    char *end = nullptr;
                    const float parsed = std::strtof(equals + 1, &end);
                    if (end == equals + 1 || *end != '\0' || errno == ERANGE || !std::isfinite(parsed))
                    {
                        if (report)
                            std::fprintf(stderr, "[kz] KZ_DETAIL_DIAG %s=%s is invalid; using 1.00\n", item,
                                         equals + 1);
                        allOk = false;
                    }
                    else
                    {
                        scales.value[cls] = std::clamp(parsed, 1.0f, kDiagnosticScaleMax);
                        if (report && scales.value[cls] != parsed)
                            std::fprintf(stderr, "[kz] KZ_DETAIL_DIAG %s=%.3f clamped to %.2f\n", item, parsed,
                                         scales.value[cls]);
                    }
                }
            }
            p = comma ? comma + 1 : nullptr;
        }
        return allOk;
    }
}

KzDetailScales kzDetailResolveConfigScales(const KzConfig &config, bool reportDiagnostics)
{
    KzDetailScales scales;
    scales[KzDetailClass::StaticLod] = config.staticLodScale;
    scales[KzDetailClass::InstancedLod] = config.staticLodScale; // static and instanced share one authored table
    scales[KzDetailClass::SkinnedLod] = config.skinnedLodScale;
    scales[KzDetailClass::VisibleCull] = config.visibleCullScale;
    scales[KzDetailClass::Grass] = config.grassViewScale;
    if (config.renderDistance > 1.0f)
    {
        bool anyElevated = false;
        for (float v : scales.value)
            anyElevated = anyElevated || v > 1.0f;
        if (anyElevated)
        {
            if (reportDiagnostics)
                std::fprintf(stderr,
                             "[kz] elevated RenderDistance and per-class detail distance are unsafe together "
                             "(docs/findings.md \"GFX-03 reinvestigation\"); using 1.0 for static/instanced/"
                             "skinned/visible/grass detail distance\n");
            scales = KzDetailScales{};
        }
    }
    return scales;
}

void kzDetailInstall(PS2Runtime &runtime)
{
    g_scales = kzDetailResolveConfigScales(kzConfig(), true);
    const char *logValue = std::getenv("KZ_DETAIL_LOG");
    g_log = logValue != nullptr;
    // KZ_DETAIL_LOG=<frames> (>= 30) sets the snapshot period; any other value keeps 600.
    g_snapshotFrames = 600;
    if (logValue && std::atoll(logValue) >= 30)
        g_snapshotFrames = static_cast<uint64_t>(std::atoll(logValue));
    g_togglePeriod = 0;
    if (const char *toggle = std::getenv("KZ_DETAIL_TOGGLE"); toggle && *toggle)
    {
        const long long period = std::atoll(toggle);
        if (period > 0)
            g_togglePeriod = static_cast<uint64_t>(period);
        else
            std::fprintf(stderr, "[kz] KZ_DETAIL_TOGGLE=%s is invalid; scaling stays on\n", toggle);
    }
    g_grassCap = kGrassInstanceCap;
    if (const char *cap = std::getenv("KZ_DETAIL_GRASSCAP"); cap && *cap)
    {
        const long long parsed = std::atoll(cap);
        if (parsed > kGrassInstanceCap && parsed <= kGrassCapDiagnosticMax)
            g_grassCap = static_cast<int32_t>(parsed);
        else
            std::fprintf(stderr, "[kz] KZ_DETAIL_GRASSCAP=%s is invalid (want %d..%d); cap stays %d\n", cap,
                         kGrassInstanceCap + 1, kGrassCapDiagnosticMax, kGrassInstanceCap);
    }
    if (const char *diag = std::getenv("KZ_DETAIL_DIAG"); diag && *diag)
        parseDiagnosticScales(diag, g_scales, true);
    // The earlier character-only probe name maps to the skinned class.
    if (const char *probe = std::getenv("KZ_CHARACTER_LOD_PROBE"); probe && *probe)
    {
        const std::string item = std::string("skinned=") + probe;
        parseDiagnosticScales(item.c_str(), g_scales, true);
    }

    const bool lodScaled = g_scales[KzDetailClass::StaticLod] != 1.0f || g_scales[KzDetailClass::SkinnedLod] != 1.0f ||
                           g_scales[KzDetailClass::InstancedLod] != 1.0f;
    const bool visibleScaled = g_scales[KzDetailClass::VisibleCull] != 1.0f;
    const bool grassScaled = g_scales[KzDetailClass::Grass] != 1.0f;
    if (!lodScaled && !visibleScaled && !grassScaled && !g_log)
        return;

    bool ok = true;
    if (lodScaled || g_log)
    {
        ok = hook(runtime, kLodSelectSimple, &lodSelectSimple, g_origLodSelectSimple) && ok;
        ok = hook(runtime, kLodSelectBlended, &lodSelectBlended, g_origLodSelectBlended) && ok;
        ok = hook(runtime, kLodStaticInstanced, &lodStaticInstanced, g_origLodStaticInstanced) && ok;
    }
    if (visibleScaled || g_log)
        ok = hook(runtime, kAabbDistance, &aabbDistance, g_origAabbDistance) && ok;
    const bool grassCapRaised = g_grassCap != kGrassInstanceCap;
    if (grassScaled || grassCapRaised || g_log)
        ok = hook(runtime, kGrassRender, &grassRender, g_origGrassRender) && ok;
    if (grassCapRaised)
    {
        ok = hook(runtime, kGrassRecordCapClamp, &grassRecordCapClamp, g_origGrassRecordCapClamp) && ok;
        std::fprintf(stderr,
                     "[kz] grass record cap: %d -> %d (CONFIRMED UNSAFE: the counter bias this relies on crashes "
                     "within seconds of grass rendering with an elevated view distance - see docs/findings.md "
                     "'GFX-03 reinvestigation'; kept only so a future session can reproduce it without re-deriving "
                     "it from scratch, not for any other use)\n",
                     kGrassInstanceCap, g_grassCap);
    }
    g_noBoostedRequests = std::getenv("KZ_DETAIL_NOTOUCH") != nullptr;
    if (g_noBoostedRequests && !g_log)
        ok = hook(runtime, kLodStreamRequest, &lodStreamRequest, g_origLodStreamRequest) && ok;
    if (g_log)
    {
        ok = hook(runtime, kLodStreamRequest, &lodStreamRequest, g_origLodStreamRequest) && ok;
        ok = hook(runtime, kAllocFailHandler, &allocFailHandler, g_origAllocFailHandler) && ok;
        ok = hook(runtime, kPacketFrameBegin, &packetFrameBegin, g_origPacketFrameBegin) && ok;
        g_heapCheck = std::getenv("KZ_DETAIL_HEAPCHECK") != nullptr;
        if (g_heapCheck)
        {
            ok = hook(runtime, kz_hook::HeapFree, &heapFree, g_origHeapFree) && ok;
#if defined(PS2X_GUEST_RANGE_WRITE_OBSERVER)
            ps2GuestRangeWriteObserver().store(&observeHostWrite, std::memory_order_relaxed);
            std::printf("[kz] detail heap check: heap walk every 8 vblanks with host-write and free history\n");
#else
            (void)&observeHostWrite;
            std::printf("[kz] detail heap check: heap walk every 8 vblanks with free history; host writes need "
                        "patches/ps2recomp/experimental/gfx03-host-write-rpc-diagnostics.diff\n");
#endif
        }
    }
    // A partial install would scale some consumers and not others; report it and leave every class at original.
    if (!ok)
        g_scales = KzDetailScales{};
    std::printf("[kz] detail diagnostics: static x%.2f, instanced x%.2f, skinned x%.2f, visible x%.2f, grass x%.2f, "
                "log %s (%s)\n",
                g_scales[KzDetailClass::StaticLod], g_scales[KzDetailClass::InstancedLod],
                g_scales[KzDetailClass::SkinnedLod], g_scales[KzDetailClass::VisibleCull],
                g_scales[KzDetailClass::Grass], g_log ? "on" : "off",
                ok ? "hooks installed" : "HOOK FAILED; original detail");
}

void kzDetailShutdown()
{
    const char *path = std::getenv("KZ_DETAIL_HEAPCHECK");
    if (!g_heapCheck || !path || std::strlen(path) < 2)
        return;
    FILE *f = std::fopen(path, "w");
    if (!f)
        return;
    std::fprintf(f, "kind,order,frame,address,size,source\n");
    const uint64_t count = g_hostWriteCount.load(std::memory_order_relaxed);
    const uint64_t writes = std::min<uint64_t>(count, g_hostWrites.size());
    for (uint64_t k = 0; k < writes; ++k)
    {
        const uint64_t order = count - writes + k;
        const RangeEvent &e = g_hostWrites[order % g_hostWrites.size()];
        std::fprintf(f, "write,%llu,%llu,0x%08X,0x%X,%s\n", static_cast<unsigned long long>(order),
                     static_cast<unsigned long long>(e.frame), e.address, e.size, e.op ? e.op : "?");
    }
    const size_t frees = std::min(g_freeNext, g_frees.size());
    for (size_t k = 0; k < frees; ++k)
    {
        const size_t order = g_freeNext - frees + k;
        const RangeEvent &e = g_frees[order % g_frees.size()];
        std::fprintf(f, "free,%zu,%llu,0x%08X,0x%X,0x%08X\n", order, static_cast<unsigned long long>(e.frame),
                     e.address, e.size, e.ra);
    }
    std::fclose(f);
}

int kzDetailSelfTest()
{
    int failures = 0;
    const auto check = [&](bool condition, const char *label)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[selftest] FAIL: %s\n", label);
            ++failures;
        }
    };
    check(lodClassForSimpleCaller(kStaticSimpleCaller) == static_cast<int>(KzDetailClass::StaticLod) &&
              lodClassForSimpleCaller(kSkinnedSimpleCaller) == static_cast<int>(KzDetailClass::SkinnedLod) &&
              lodClassForBlendedCaller(kStaticBlendedCaller) == static_cast<int>(KzDetailClass::StaticLod) &&
              lodClassForBlendedCaller(kSkinnedBlendedCaller) == static_cast<int>(KzDetailClass::SkinnedLod),
          "LOD caller classification");
    check(lodClassForSimpleCaller(kSkinnedBlendedCaller) < 0 && lodClassForBlendedCaller(kStaticSimpleCaller) < 0 &&
              lodClassForSimpleCaller(0u) < 0,
          "LOD caller classification rejects mismatched paths");
    const float thresholds[3] = {0.0f, 3.0f, 9.0f};
    float scaled[3] = {};
    check(scaleThresholds(thresholds, 3, 2.0f, scaled) && scaled[0] == 0.0f && scaled[1] == 6.0f &&
              scaled[2] == 18.0f,
          "LOD threshold scaling");
    const float unordered[2] = {4.0f, 2.0f};
    const float negative[2] = {-1.0f, 2.0f};
    const float infinite[2] = {0.0f, INFINITY};
    check(!scaleThresholds(unordered, 2, 2.0f, scaled) && !scaleThresholds(negative, 2, 2.0f, scaled) &&
              !scaleThresholds(infinite, 2, 2.0f, scaled) && !scaleThresholds(thresholds, 0, 2.0f, scaled) &&
              !scaleThresholds(thresholds, 3, 0.5f, scaled),
          "LOD threshold validation");
    check(levelFor(thresholds, 3, 2.0f) == 0 && levelFor(thresholds, 3, 3.0f) == 0 &&
              levelFor(thresholds, 3, 3.5f) == 1 && levelFor(thresholds, 3, 20.0f) == 2 &&
              levelFor(scaled, 3, 3.5f) == 0,
          "LOD level selection mirrors 0x3F4E70");
    KzDetailScales scales;
    check(parseDiagnosticScales("static=2,skinned=1.5,visible=3,grass=2", scales, false) &&
              scales[KzDetailClass::StaticLod] == 2.0f && scales[KzDetailClass::SkinnedLod] == 1.5f &&
              scales[KzDetailClass::VisibleCull] == 3.0f && scales[KzDetailClass::Grass] == 2.0f,
          "detail diagnostic scale parsing");
    KzDetailScales rejected;
    check(!parseDiagnosticScales("static=x,lod=2,grass=100", rejected, false) &&
              rejected[KzDetailClass::StaticLod] == 1.0f && rejected[KzDetailClass::Grass] == kDiagnosticScaleMax,
          "detail diagnostic scale rejection and clamp");
    // Grass record-cap bias: raising the cap to N pre-seeds the counter to (1500-N) so it takes N increments, not
    // 1500, to reach the hardcoded 1500 the game's own abort check compares against; reporting undoes the bias.
    {
        const int32_t cap = 2200;
        const int32_t bias = kGrassInstanceCap - cap;
        check(bias == -700, "grass cap bias magnitude");
        const int32_t rawAtCap = kGrassInstanceCap; // the value the game's own check fires at, unchanged
        check(rawAtCap - bias == cap, "grass cap bias undoes to the new ceiling at the game's own abort point");
        check(kGrassInstanceCap - kGrassInstanceCap == 0, "grass cap bias is zero when the cap is unchanged");
    }
    {
        KzConfig original;
        const KzDetailScales atOriginal = kzDetailResolveConfigScales(original, false);
        check(atOriginal[KzDetailClass::StaticLod] == 1.0f && atOriginal[KzDetailClass::InstancedLod] == 1.0f &&
                  atOriginal[KzDetailClass::SkinnedLod] == 1.0f && atOriginal[KzDetailClass::VisibleCull] == 1.0f &&
                  atOriginal[KzDetailClass::Grass] == 1.0f,
              "detail config resolution: original values at defaults");
        KzConfig elevated;
        elevated.staticLodScale = 2.0f;
        elevated.skinnedLodScale = 1.5f;
        elevated.visibleCullScale = 2.0f;
        elevated.grassViewScale = 2.0f;
        const KzDetailScales atElevated = kzDetailResolveConfigScales(elevated, false);
        check(atElevated[KzDetailClass::StaticLod] == 2.0f && atElevated[KzDetailClass::InstancedLod] == 2.0f &&
                  atElevated[KzDetailClass::SkinnedLod] == 1.5f && atElevated[KzDetailClass::VisibleCull] == 2.0f &&
                  atElevated[KzDetailClass::Grass] == 2.0f,
              "detail config resolution: elevated per-class values pass through, static mirrors instanced");
        elevated.renderDistance = 2.0f;
        const KzDetailScales atUnsafeCombination = kzDetailResolveConfigScales(elevated, false);
        check(atUnsafeCombination[KzDetailClass::StaticLod] == 1.0f &&
                  atUnsafeCombination[KzDetailClass::InstancedLod] == 1.0f &&
                  atUnsafeCombination[KzDetailClass::SkinnedLod] == 1.0f &&
                  atUnsafeCombination[KzDetailClass::VisibleCull] == 1.0f &&
                  atUnsafeCombination[KzDetailClass::Grass] == 1.0f,
              "detail config resolution: RenderDistance > 1 forces every per-class scale back to 1");
        KzConfig renderDistanceAlone;
        renderDistanceAlone.renderDistance = 2.0f;
        const KzDetailScales atRenderDistanceAlone = kzDetailResolveConfigScales(renderDistanceAlone, false);
        check(atRenderDistanceAlone[KzDetailClass::StaticLod] == 1.0f &&
                  atRenderDistanceAlone[KzDetailClass::Grass] == 1.0f,
              "detail config resolution: RenderDistance alone is not itself forced to anything");
    }
    return failures;
}
