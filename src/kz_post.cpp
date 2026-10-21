#include "kz_post.h"

#include "kz_config.h"
#include "kz_gs.h"
#include "kz_hook_addresses.h"

#include "ps2_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Game functions (SCUS-97402), found from a GS trace of first-mission gameplay and the PostProcessPreset reflection data
// (docs/findings.md "Post-process passes"). All three are called with the render context and draw directly into the
// frame's GIF packet:
//   0x15DFC0 glow pass: the glow mask in the frame's alpha channel goes through the glow palette, is blurred down to
//            128x112 in the Z-buffer memory and added back to the frame (additive). Called from the post-process render
//            0x1EF640 only. (The heat-vision palette variant is a direct call of 0x15E028, which is left alone.)
//   0x15F768 frame blend (f12 = alpha): blends the *previous* frame buffer over the current one with alpha f12*255, the
//            "motion blur" (PostProcessPreset "Motion blur" Strength -> PostProcess object +0x280). Also called by
//            0x1A8D30 with 0.02 after a screen-capture overlay.
//   0x1A9308 lens blur: only when the camera has a lens texture (+0x140); eight half-resolution ping-pong blur
//            passes plus the overlay.
// View distance (docs/findings.md "Draw distance and LOD"):
//   0x339C38 RenderZoneManager blend (a0 manager, a1 camera, a2 out): picks the zone around the camera (blending two
//            at a boundary) and writes out[0] far plane (min of far plane and fog end when fog is on), out+4 byte fog
//            enabled, out[2] fog start, out[3] fog end, out[4]/[5] densities, out[6] colour. Returns 1 when a zone
//            was found. Recompiled function wrappers cannot inspect those results after a guest return. The hooks
//            therefore scale the finalized consumers: far plane at 0x1C1A2C and fog start/end at 0x151180, gated to
//            the three RenderZone call sites.
//   0x1C2168 computes the camera/FOV LOD scale and calls setter 0x151E60. The setter's 0x151E64 continuation stores
//            f12 to render context +0x1AC, the divisor used by static, skinned and multi-mesh LOD selectors
//            (0x3F4E70, 0x3F4F10, 0x3EFAD8 ...). The hook scales f12 only for return address 0x1C21D4.
// The colour grade (0x167050 + the palette composite) and the film grain (the noise texture strips at the end of
// 0x1EF640) are not touched.
namespace
{
    constexpr uint32_t kGlowPass = kz_hook::GlowPass;
    constexpr uint32_t kFrameBlend = kz_hook::FrameBlend;
    constexpr uint32_t kLensBlur = kz_hook::LensBlur;
    constexpr uint32_t kPostRender = kz_hook::PostRender; // post-process render (a0 = PostProcess object)
    constexpr uint32_t kNoiseStrengthOffset = 0x284u;  // film grain alpha; the pass is skipped below 2/255
    constexpr uint32_t kFarPlaneApply = kz_hook::FarPlaneApply;
    constexpr uint32_t kFogParams = kz_hook::FogParams;
    constexpr uint32_t kLodScaleApply = kz_hook::LodScaleApply;
    constexpr uint32_t kAabbDistance = kz_hook::AabbDistance;
    constexpr uint32_t kSimpleLodDistanceReturn = 0x003F4EA0u;
    constexpr uint32_t kBlendedLodDistanceReturn = 0x003F4F84u;
    constexpr uint32_t kSkinnedSimpleCallerReturn = 0x003FF3FCu;
    constexpr uint32_t kSkinnedBlendedCallerReturn = 0x003FF4F0u;
    constexpr uint32_t kRamMask = 0x01FFFFFFu;
    constexpr uint32_t kRamSize = 0x02000000u;
    constexpr float kRenderDistanceMin = 1.0f;
    constexpr float kRenderDistanceMax = 16.0f;
    constexpr float kLodScaleMin = 1.0f;
    constexpr float kLodScaleMax = 1.0f;

    bool g_motionBlur = false;
    bool g_glow = false;
    bool g_lensBlur = false;
    bool g_grain = false;
    bool g_log = false;

    PS2Runtime::RecompiledFunction g_origGlow = nullptr;
    PS2Runtime::RecompiledFunction g_origFrameBlend = nullptr;
    PS2Runtime::RecompiledFunction g_origLensBlur = nullptr;
    PS2Runtime::RecompiledFunction g_origPostRender = nullptr;
    PS2Runtime::RecompiledFunction g_origFarPlaneApply = nullptr;
    PS2Runtime::RecompiledFunction g_origFogParams = nullptr;
    PS2Runtime::RecompiledFunction g_origLodScaleApply = nullptr;
    PS2Runtime::RecompiledFunction g_origAabbDistance = nullptr;
    float g_renderDistance = 1.0f;
    float g_lodScale = 1.0f;
    float g_characterLodProbeScale = 1.0f;
    uint64_t g_characterLodProbeFrom = 0;
    uint64_t g_characterLodProbeTo = 0;
    uint64_t g_characterLodResourceLogAt = 0;
    bool g_characterLodTargetEnabled = false;
    std::array<float, 3> g_characterLodTargetCenter{};
    float g_characterLodTargetRadius = 0.0f;
    std::atomic<int> g_farPlaneCalls{0}, g_fogCalls{0}, g_lodCalls{0};
    std::atomic<int> g_invalidZoneValues{0}, g_invalidLodValues{0};
    int g_characterLodCalls = 0;
    int g_characterLodChanges = 0;
    int g_invalidCharacterLodValues = 0;
    std::array<int, 4> g_characterLodBaselineLevels{};
    std::array<int, 4> g_characterLodProbeLevels{};
    std::array<uint32_t, 16> g_loggedCharacterLodTables{};
    size_t g_loggedCharacterLodTableCount = 0;
    std::array<uint32_t, 32> g_loggedCharacterLodResources{};
    size_t g_loggedCharacterLodResourceCount = 0;

    std::atomic<int> g_glowCalls{0}, g_blendCalls{0}, g_lensCalls{0};
    std::atomic<int> g_glowSkipped{0}, g_blendSkipped{0}, g_lensSkipped{0};

    void logCall(const char *name, std::atomic<int> &calls, std::atomic<int> &skipped, bool skip)
    {
        const int n = ++calls;
        if (skip)
            ++skipped;
        if (g_log && (n == 1 || n % 600 == 0))
            std::printf("[kz-post] %s: %d calls, %d skipped\n", name, n, skipped.load());
    }

    // A call enters at the function's own address; a resume at an inner label (after a yield) has another pc and must
    // run the original.
    bool entering(const R5900Context *ctx, uint32_t addr) { return ctx->pc == addr; }

    void glowPass(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kGlowPass))
        {
            logCall("glow", g_glowCalls, g_glowSkipped, !g_glow);
            if (!g_glow)
            {
                ctx->pc = getRegU32(ctx, 31);
                return;
            }
        }
        g_origGlow(rdram, ctx, rt);
    }

    void frameBlend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kFrameBlend))
        {
            logCall("frame blend", g_blendCalls, g_blendSkipped, !g_motionBlur);
            if (!g_motionBlur)
            {
                ctx->pc = getRegU32(ctx, 31);
                return;
            }
        }
        g_origFrameBlend(rdram, ctx, rt);
    }

    void lensBlur(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kLensBlur))
        {
            logCall("lens blur", g_lensCalls, g_lensSkipped, !g_lensBlur);
            if (!g_lensBlur)
            {
                ctx->pc = getRegU32(ctx, 31);
                return;
            }
        }
        g_origLensBlur(rdram, ctx, rt);
    }

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

    bool inRamRange(uint32_t address, uint32_t size)
    {
        return address >= 0x00100000u && size <= kRamSize && address <= kRamSize - size;
    }

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
            const float squared = separation * separation;
            sum += squared;
        }
        if (!std::isfinite(sum))
            return false;
        distanceSquared = sum;
        return true;
    }

    int selectedLodLevel(const uint8_t *rdram, uint32_t table, float metric, std::array<float, 4> &thresholds,
                         uint32_t &count)
    {
        count = 0;
        if (!std::isfinite(metric) || !inRamRange(table, 12u))
            return -1;
        count = rdU32(rdram, table);
        const uint32_t values = rdU32(rdram, table + 8u);
        if (count == 0u || count > thresholds.size() || !inRamRange(values, count * 4u))
            return -1;
        for (uint32_t i = 0; i < count; ++i)
        {
            thresholds[i] = rdF(rdram, values + i * 4u);
            if (!std::isfinite(thresholds[i]) || (i > 0u && thresholds[i] < thresholds[i - 1u]))
                return -1;
        }
        for (uint32_t i = 1; i < count; ++i)
            if (metric <= thresholds[i])
                return static_cast<int>(i - 1u);
        return static_cast<int>(count - 1u);
    }

    bool firstCharacterLodTable(uint32_t table)
    {
        for (size_t i = 0; i < g_loggedCharacterLodTableCount; ++i)
            if (g_loggedCharacterLodTables[i] == table)
                return false;
        if (g_loggedCharacterLodTableCount == g_loggedCharacterLodTables.size())
            return false;
        g_loggedCharacterLodTables[g_loggedCharacterLodTableCount++] = table;
        return true;
    }

    float scheduledCharacterLodScale(float scale, uint64_t from, uint64_t to, uint64_t offset)
    {
        if ((from > 0 && offset < from) || (to > 0 && offset >= to))
            return 1.0f;
        return scale;
    }

    bool isSkinnedSelectorCall(bool simpleSelector, uint32_t callerReturn)
    {
        return (simpleSelector && callerReturn == kSkinnedSimpleCallerReturn) ||
               (!simpleSelector && callerReturn == kSkinnedBlendedCallerReturn);
    }

    bool withinCharacterLodTarget(
        float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
        const std::array<float, 3> &target, float radius)
    {
        const float dx = (minX + maxX) * 0.5f - target[0];
        const float dy = (minY + maxY) * 0.5f - target[1];
        const float dz = (minZ + maxZ) * 0.5f - target[2];
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    }

    bool matchesCharacterLodTarget(const uint8_t *rdram, uint32_t bounds)
    {
        if (!g_characterLodTargetEnabled)
            return true;
        return withinCharacterLodTarget(
            rdF(rdram, bounds), rdF(rdram, bounds + 4u), rdF(rdram, bounds + 8u),
            rdF(rdram, bounds + 12u), rdF(rdram, bounds + 16u), rdF(rdram, bounds + 20u),
            g_characterLodTargetCenter, g_characterLodTargetRadius);
    }

    bool firstCharacterLodResource(uint32_t table)
    {
        for (size_t i = 0; i < g_loggedCharacterLodResourceCount; ++i)
            if (g_loggedCharacterLodResources[i] == table)
                return false;
        if (g_loggedCharacterLodResourceCount == g_loggedCharacterLodResources.size())
            return false;
        g_loggedCharacterLodResources[g_loggedCharacterLodResourceCount++] = table;
        return true;
    }

    bool guestAsciiString(const uint8_t *rdram, uint32_t address, char (&text)[65])
    {
        if (!inRamRange(address, 1u))
            return false;
        size_t length = 0;
        for (; length + 1 < sizeof(text) && inRamRange(address + static_cast<uint32_t>(length), 1u); ++length)
        {
            const unsigned char c = rdram[(address + static_cast<uint32_t>(length)) & kRamMask];
            if (c == '\0')
            {
                text[length] = '\0';
                return length >= 4;
            }
            if (c < 0x20u || c > 0x7Eu)
                return false;
            text[length] = static_cast<char>(c);
        }
        return false;
    }

    void logCharacterLodResourceStrings(const uint8_t *rdram, uint32_t resource, uint32_t size)
    {
        int logged = 0;
        for (uint32_t offset = 0; offset + 4u <= size && logged < 4; offset += 4u)
        {
            const uint32_t pointer = rdU32(rdram, resource + offset);
            char text[65]{};
            if (guestAsciiString(rdram, pointer, text))
            {
                std::printf(" str+0x%02X='%s'", offset, text);
                ++logged;
            }
            if (!inRamRange(pointer, 32u))
                continue;
            for (uint32_t nested = 0; nested < 32u && logged < 4; nested += 4u)
            {
                const uint32_t nestedPointer = rdU32(rdram, pointer + nested);
                if (guestAsciiString(rdram, nestedPointer, text))
                {
                    std::printf(" str+0x%02X->0x%02X='%s'", offset, nested, text);
                    ++logged;
                }
            }
        }
    }

    void logCharacterLodResource(
        const uint8_t *rdram, uint32_t bounds, uint32_t point, uint32_t state, uint32_t table,
        uint32_t callerReturn, uint64_t gameplayOffset, float metric, int level,
        const std::array<float, 4> &thresholds, uint32_t levelCount)
    {
        constexpr uint32_t tableOffset = 0x30u;
        const uint32_t resource = table >= tableOffset ? table - tableOffset : 0u;
        const float minX = rdF(rdram, bounds), minY = rdF(rdram, bounds + 4u), minZ = rdF(rdram, bounds + 8u);
        const float maxX = rdF(rdram, bounds + 12u), maxY = rdF(rdram, bounds + 16u), maxZ = rdF(rdram, bounds + 20u);
        const float pointX = rdF(rdram, point), pointY = rdF(rdram, point + 4u), pointZ = rdF(rdram, point + 8u);
        std::printf("[kz-lod-resource] slot %zu table 0x%08X resource 0x%08X caller 0x%08X state 0x%08X "
                    "gameplay %llu metric %.3f level %d bounds %.2f,%.2f,%.2f..%.2f,%.2f,%.2f "
                    "point %.2f,%.2f,%.2f thresholds",
                    g_loggedCharacterLodResourceCount - 1u, table, resource, callerReturn, state,
                    static_cast<unsigned long long>(gameplayOffset), metric, level,
                    minX, minY, minZ, maxX, maxY, maxZ, pointX, pointY, pointZ);
        for (uint32_t i = 0; i < levelCount; ++i)
            std::printf(" %s%.3f", i == 0u ? "[" : ",", thresholds[i]);
        std::printf("%s table_words %08X/%08X/%08X",
                    levelCount ? "]" : " []",
                    inRamRange(table, 12u) ? rdU32(rdram, table) : 0u,
                    inRamRange(table, 12u) ? rdU32(rdram, table + 4u) : 0u,
                    inRamRange(table, 12u) ? rdU32(rdram, table + 8u) : 0u);
        if (resource != 0u && inRamRange(resource, tableOffset))
            logCharacterLodResourceStrings(rdram, resource, tableOffset);
        std::printf("\n");
    }

    void aabbDistance(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (!entering(ctx, kAabbDistance))
        {
            g_origAabbDistance(rdram, ctx, rt);
            return;
        }

        const uint32_t distanceReturn = getRegU32(ctx, 31);
        const bool simpleSelector = distanceReturn == kSimpleLodDistanceReturn;
        if (!simpleSelector && distanceReturn != kBlendedLodDistanceReturn)
        {
            g_origAabbDistance(rdram, ctx, rt);
            return;
        }
        const uint32_t stack = getRegU32(ctx, 29);
        const uint32_t savedReturnOffset = simpleSelector ? 8u : 0x20u;
        const uint32_t callerReturn =
            inRamRange(stack, savedReturnOffset + 4u) ? rdU32(rdram, stack + savedReturnOffset) : 0u;
        if (!isSkinnedSelectorCall(simpleSelector, callerReturn))
        {
            g_origAabbDistance(rdram, ctx, rt);
            return;
        }

        float baselineDistanceSquared = 0.0f;
        const uint32_t bounds = getRegU32(ctx, 4);
        const uint32_t point = getRegU32(ctx, 5);
        if (!aabbPointDistanceSquared(rdram, bounds, point, baselineDistanceSquared))
        {
            if (g_invalidCharacterLodValues++ == 0)
                std::fprintf(stderr, "[kz] character LOD probe: invalid selector bounds; preserving original call\n");
            g_origAabbDistance(rdram, ctx, rt);
            return;
        }

        const uint64_t gameplayOffset = kzGsGameplayOffsetVsync();
        float probeScale = scheduledCharacterLodScale(
            g_characterLodProbeScale, g_characterLodProbeFrom, g_characterLodProbeTo, gameplayOffset);
        if (!matchesCharacterLodTarget(rdram, bounds))
            probeScale = 1.0f;
        const float divisor = probeScale * probeScale;
        const float probeDistanceSquared = baselineDistanceSquared / divisor;
        const uint32_t state = simpleSelector ? 0u : getRegU32(ctx, 17);
        const uint32_t table = simpleSelector ? getRegU32(ctx, 16) : getRegU32(ctx, 19);
        const float metricFactor =
            simpleSelector ? (ctx->f[20] != 0.0f ? 1.0f / ctx->f[20] : NAN)
                           : inRamRange(state, 0x20u) ? rdF(rdram, state + 0x1Cu) : NAN;
        const float baselineMetric = std::sqrt(baselineDistanceSquared) * metricFactor;
        const float probeMetric = std::sqrt(probeDistanceSquared) * metricFactor;
        std::array<float, 4> thresholds{};
        uint32_t levelCount = 0;
        const int baselineLevel = selectedLodLevel(rdram, table, baselineMetric, thresholds, levelCount);
        std::array<float, 4> probeThresholds{};
        uint32_t probeLevelCount = 0;
        const int probeLevel = selectedLodLevel(rdram, table, probeMetric, probeThresholds, probeLevelCount);
        ++g_characterLodCalls;
        if (baselineLevel >= 0 && baselineLevel < static_cast<int>(g_characterLodBaselineLevels.size()))
            ++g_characterLodBaselineLevels[static_cast<size_t>(baselineLevel)];
        if (probeLevel >= 0 && probeLevel < static_cast<int>(g_characterLodProbeLevels.size()))
            ++g_characterLodProbeLevels[static_cast<size_t>(probeLevel)];
        if (baselineLevel >= 0 && probeLevel >= 0 && baselineLevel != probeLevel)
            ++g_characterLodChanges;

        const bool firstTable = firstCharacterLodTable(table);
        if (g_characterLodResourceLogAt > 0 && gameplayOffset >= g_characterLodResourceLogAt &&
            firstCharacterLodResource(table))
        {
            logCharacterLodResource(
                rdram, bounds, point, state, table, callerReturn, gameplayOffset, baselineMetric, baselineLevel,
                thresholds, levelCount);
        }
        if (g_log && (firstTable || g_characterLodCalls % 5000 == 0))
        {
            std::printf("[kz-post] character LOD call %d: table 0x%08X, gameplay %llu, x%.2f, "
                        "metric %.3f -> %.3f, level %d -> %d, "
                        "thresholds",
                        g_characterLodCalls, table, static_cast<unsigned long long>(gameplayOffset), probeScale,
                        baselineMetric, probeMetric, baselineLevel, probeLevel);
            for (uint32_t i = 0; i < levelCount; ++i)
                std::printf(" %s%.3f", i == 0u ? "[" : ",", thresholds[i]);
            std::printf("%s; changed %d, levels %d/%d/%d/%d -> %d/%d/%d/%d\n",
                        levelCount ? "]" : " []", g_characterLodChanges,
                        g_characterLodBaselineLevels[0], g_characterLodBaselineLevels[1],
                        g_characterLodBaselineLevels[2], g_characterLodBaselineLevels[3],
                        g_characterLodProbeLevels[0], g_characterLodProbeLevels[1],
                        g_characterLodProbeLevels[2], g_characterLodProbeLevels[3]);
        }

        if (probeScale == 1.0f)
        {
            g_origAabbDistance(rdram, ctx, rt);
            return;
        }
        ctx->f[0] = probeDistanceSquared;
        ctx->pc = getRegU32(ctx, 31);
    }

    bool scaledFinite(float value, float scale, float &result)
    {
        result = value * scale;
        return std::isfinite(value) && std::isfinite(result);
    }

    void farPlaneApply(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t out = getRegU32(ctx, 29);
        if (entering(ctx, kFarPlaneApply) && getRegU32(ctx, 2) == 1u &&
            out >= 0x00100000u && out <= kRamSize - 4u)
        {
            const float farPlane = rdF(rdram, out);
            float scaledFar = 0.0f;
            const bool valid = scaledFinite(farPlane, g_renderDistance, scaledFar);
            const int n = ++g_farPlaneCalls;
            if (g_log && (n == 1 || n % 600 == 0))
                std::printf("[kz-post] far plane call %d: %.1f -> %.1f (x%.2f)\n",
                            n, farPlane, scaledFar, g_renderDistance);
            if (!valid)
            {
                if (g_invalidZoneValues.fetch_add(1) == 0)
                    std::fprintf(stderr, "[kz] view distance: ignored a non-finite far plane\n");
            }
            else
            {
                wrF(rdram, out, scaledFar);
            }
        }
        g_origFarPlaneApply(rdram, ctx, rt);
    }

    bool isRenderZoneFogCall(uint32_t returnAddress)
    {
        return returnAddress == 0x001C1C30u ||
               returnAddress == 0x001C1D08u ||
               returnAddress == 0x001EE260u;
    }

    void fogParams(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (entering(ctx, kFogParams) && getRegU32(ctx, 5) == 0x101u &&
            isRenderZoneFogCall(getRegU32(ctx, 31)))
        {
            const float fogStart = ctx->f[12];
            const float fogEnd = ctx->f[13];
            float scaledStart = 0.0f, scaledEnd = 0.0f;
            const bool valid = scaledFinite(fogStart, g_renderDistance, scaledStart) &&
                               scaledFinite(fogEnd, g_renderDistance, scaledEnd);
            const int n = ++g_fogCalls;
            if (g_log && (n == 1 || n % 600 == 0))
                std::printf("[kz-post] fog call %d: %.1f..%.1f -> %.1f..%.1f (x%.2f)\n",
                            n, fogStart, fogEnd, scaledStart, scaledEnd, g_renderDistance);
            if (!valid)
            {
                if (g_invalidZoneValues.fetch_add(1) == 0)
                    std::fprintf(stderr, "[kz] view distance: ignored non-finite fog parameters\n");
            }
            else
            {
                ctx->f[12] = scaledStart;
                ctx->f[13] = scaledEnd;
            }
        }
        g_origFogParams(rdram, ctx, rt);
    }

    void lodScaleApply(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const bool atEntry = entering(ctx, kLodScaleApply);
        const bool entry = atEntry && getRegU32(ctx, 31) == 0x001C21D4u;
        const float base = ctx->f[12];
        const float previous = ctx->f[0];
        float scaled = base;
        if (entry && scaledFinite(base, g_lodScale, scaled))
        {
            ctx->f[12] = scaled;
        }
        else if (entry && g_invalidLodValues.fetch_add(1) == 0)
            std::fprintf(stderr, "[kz] view distance: ignored non-finite LOD scale input\n");

        g_origLodScaleApply(rdram, ctx, rt);
        if (entry)
        {
            const int n = ++g_lodCalls;
            if (g_log && (n == 1 || n % 600 == 0))
            {
                std::printf("[kz-post] lod scale call %d: previous %.3f, base %.3f -> %.3f (x%.2f)\n",
                            n, previous, base, scaled, g_lodScale);
            }
        }
    }

    // Film grain off: the grain strips at the end of the post-process render are drawn only when the object's noise
    // strength (+0x284, from the active preset) is above 2/255, so it is zeroed on entry.
    void postRender(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == kPostRender && !g_grain)
        {
            const uint32_t obj = getRegU32(ctx, 4);
            if (obj >= 0x00100000u && obj < 0x02000000u)
                wrF(rdram, obj + kNoiseStrengthOffset, 0.0f);
        }
        g_origPostRender(rdram, ctx, rt);
    }

    bool hook(PS2Runtime &rt, uint32_t addr, PS2Runtime::RecompiledFunction fn, PS2Runtime::RecompiledFunction &orig)
    {
        orig = rt.hasFunction(addr) ? rt.lookupFunction(addr) : nullptr;
        return orig && rt.replaceFunction(addr, fn);
    }

    bool envFlag(const char *name, bool value)
    {
        if (const char *v = std::getenv(name); v && *v)
            return std::atoi(v) != 0;
        return value;
    }

    float resolveDetailValue(
        const char *name, const char *value, float fallback, float minimum, float maximum, bool reportDiagnostics)
    {
        if (!value || !*value)
            return std::clamp(fallback, minimum, maximum);
        errno = 0;
        char *end = nullptr;
        const float parsed = std::strtof(value, &end);
        if (end == value || *end != '\0' || errno == ERANGE || !std::isfinite(parsed))
        {
            if (reportDiagnostics)
                std::fprintf(stderr, "[kz] %s=%s is invalid; using %.2f\n", name, value, fallback);
            return std::clamp(fallback, minimum, maximum);
        }
        const float result = std::clamp(parsed, minimum, maximum);
        if (reportDiagnostics && result != parsed)
            std::fprintf(stderr, "[kz] %s=%.3f is outside %.2f..%.2f; clamped to %.2f\n",
                         name, parsed, minimum, maximum, result);
        return result;
    }

    uint64_t resolveOptionalVsync(const char *name, const char *value)
    {
        if (!value || !*value)
            return 0;
        errno = 0;
        char *end = nullptr;
        const unsigned long long parsed = std::strtoull(value, &end, 10);
        if (end == value || *end != '\0' || errno == ERANGE || parsed == 0)
        {
            std::fprintf(stderr, "[kz] %s=%s is invalid; ignoring probe boundary\n", name, value);
            return 0;
        }
        return static_cast<uint64_t>(parsed);
    }

    bool resolveCharacterLodTarget(const char *value)
    {
        if (!value || !*value)
            return false;
        char trailing = '\0';
        std::array<float, 3> center{};
        float radius = 0.0f;
        if (std::sscanf(value, "%f,%f,%f,%f%c", &center[0], &center[1], &center[2], &radius, &trailing) != 4 ||
            !std::isfinite(center[0]) || !std::isfinite(center[1]) || !std::isfinite(center[2]) ||
            !std::isfinite(radius) || radius <= 0.0f)
        {
            std::fprintf(stderr, "[kz] KZ_CHARACTER_LOD_PROBE_CENTER=%s is invalid; ignoring target filter\n", value);
            return false;
        }
        g_characterLodTargetCenter = center;
        g_characterLodTargetRadius = radius;
        return true;
    }

    KzPostDetailSettings enforceDetailSafety(KzPostDetailSettings settings, bool reportDiagnostics)
    {
        if (settings.renderDistance > 1.0f && settings.lodScale > 1.0f)
        {
            if (reportDiagnostics)
                std::fprintf(stderr,
                             "[kz] elevated RenderDistance and LodScale are unsafe together; using LodScale=1.0\n");
            settings.lodScale = 1.0f;
        }
        return settings;
    }
}

KzPostDetailSettings kzPostResolveDetailSettings(const KzConfig &config, bool reportDiagnostics)
{
    KzPostDetailSettings settings;
    settings.renderDistance =
        resolveDetailValue("KZ_RENDER_DISTANCE", std::getenv("KZ_RENDER_DISTANCE"), config.renderDistance,
                           kRenderDistanceMin, kRenderDistanceMax, reportDiagnostics);
    settings.lodScale =
        resolveDetailValue("KZ_LOD_SCALE", std::getenv("KZ_LOD_SCALE"), config.lodScale,
                           kLodScaleMin, kLodScaleMax, reportDiagnostics);
    return enforceDetailSafety(settings, reportDiagnostics);
}

void kzPostInstall(PS2Runtime &runtime)
{
    const KzConfig &cfg = kzConfig();
    g_motionBlur = envFlag("KZ_MOTION_BLUR", cfg.motionBlur);
    g_glow = envFlag("KZ_GLOW", cfg.glow);
    g_grain = envFlag("KZ_GRAIN", cfg.noiseFilter);
    if (!hook(runtime, kPostRender, &postRender, g_origPostRender))
        std::printf("[kz] film grain switch: hook FAILED\n");
    g_lensBlur = envFlag("KZ_LENS_BLUR", cfg.lensBlur);
    g_log = std::getenv("KZ_POST_LOG") != nullptr;
    g_characterLodProbeScale =
        resolveDetailValue("KZ_CHARACTER_LOD_PROBE", std::getenv("KZ_CHARACTER_LOD_PROBE"), 1.0f, 1.0f, 4.0f, true);
    g_characterLodProbeFrom =
        resolveOptionalVsync("KZ_CHARACTER_LOD_PROBE_FROM", std::getenv("KZ_CHARACTER_LOD_PROBE_FROM"));
    g_characterLodProbeTo =
        resolveOptionalVsync("KZ_CHARACTER_LOD_PROBE_TO", std::getenv("KZ_CHARACTER_LOD_PROBE_TO"));
    g_characterLodResourceLogAt =
        resolveOptionalVsync("KZ_CHARACTER_LOD_RESOURCE_LOG", std::getenv("KZ_CHARACTER_LOD_RESOURCE_LOG"));
    g_characterLodTargetEnabled =
        resolveCharacterLodTarget(std::getenv("KZ_CHARACTER_LOD_PROBE_CENTER"));
    if (g_characterLodProbeTo > 0 &&
        (g_characterLodProbeFrom == 0 || g_characterLodProbeTo <= g_characterLodProbeFrom))
    {
        std::fprintf(stderr, "[kz] character LOD probe: invalid gameplay window; ignoring boundaries\n");
        g_characterLodProbeFrom = 0;
        g_characterLodProbeTo = 0;
    }
    if (g_log || g_characterLodProbeScale != 1.0f || g_characterLodResourceLogAt > 0)
    {
        if (hook(runtime, kAabbDistance, &aabbDistance, g_origAabbDistance))
        {
            std::printf("[kz] character LOD probe: detail distance x%.2f (diagnostic-only)\n",
                        g_characterLodProbeScale);
            if (g_characterLodProbeFrom > 0)
            {
                if (g_characterLodProbeTo > 0)
                    std::printf("[kz] character LOD probe: gameplay window %llu..%llu\n",
                                static_cast<unsigned long long>(g_characterLodProbeFrom),
                                static_cast<unsigned long long>(g_characterLodProbeTo));
                else
                    std::printf("[kz] character LOD probe: gameplay window %llu..end\n",
                                static_cast<unsigned long long>(g_characterLodProbeFrom));
            }
            if (g_characterLodResourceLogAt > 0)
                std::printf("[kz] character LOD resource log: gameplay %llu+\n",
                            static_cast<unsigned long long>(g_characterLodResourceLogAt));
            if (g_characterLodTargetEnabled)
                std::printf("[kz] character LOD probe: center %.2f,%.2f,%.2f radius %.2f\n",
                            g_characterLodTargetCenter[0], g_characterLodTargetCenter[1],
                            g_characterLodTargetCenter[2], g_characterLodTargetRadius);
        }
        else
        {
            std::fprintf(stderr, "[kz] character LOD probe: hook FAILED; using original selection\n");
            g_characterLodProbeScale = 1.0f;
        }
    }
    const KzPostDetailSettings detail = kzPostResolveDetailSettings(cfg, true);
    g_renderDistance = detail.renderDistance;
    g_lodScale = detail.lodScale;
    bool viewOk = true;
    bool viewHooked = false;
    if (g_renderDistance != 1.0f)
    {
        viewHooked = true;
        const bool renderOk = hook(runtime, kFarPlaneApply, &farPlaneApply, g_origFarPlaneApply) &&
                              hook(runtime, kFogParams, &fogParams, g_origFogParams);
        if (!renderOk)
            g_renderDistance = 1.0f;
        viewOk = renderOk && viewOk;
    }
    if (g_lodScale != 1.0f)
    {
        viewHooked = true;
        viewOk = hook(runtime, kLodScaleApply, &lodScaleApply, g_origLodScaleApply) && viewOk;
    }
    std::printf("[kz] view distance: render x%.2f, LOD x%.2f (%s)\n", g_renderDistance, g_lodScale,
                !viewHooked ? "original; no hooks" : viewOk ? "selected hooks installed" : "HOOK FAILED");
    // Nothing to hook when every pass stays on.
    if (g_motionBlur && g_glow && g_lensBlur)
    {
        std::printf("[kz] post-process passes: all on (original look)\n");
        return;
    }
    const bool ok = hook(runtime, kGlowPass, &glowPass, g_origGlow) &&
                    hook(runtime, kFrameBlend, &frameBlend, g_origFrameBlend) &&
                    hook(runtime, kLensBlur, &lensBlur, g_origLensBlur);
    std::printf("[kz] post-process passes: motion blur %s, glow %s, lens blur %s (%s)\n", g_motionBlur ? "on" : "off",
                g_glow ? "on" : "off", g_lensBlur ? "on" : "off", ok ? "hooks installed" : "HOOK FAILED");
}

int kzPostSelfTest()
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
    check(resolveDetailValue("test", nullptr, 1.0f, 1.0f, 16.0f, false) == 1.0f,
          "detail setting default");
    check(resolveDetailValue("test", "2.5", 1.0f, 1.0f, 16.0f, false) == 2.5f,
          "detail setting override");
    check(resolveDetailValue("test", "100", 1.0f, 1.0f, 16.0f, false) == 16.0f,
          "detail setting upper clamp");
    check(resolveDetailValue("test", "-1", 1.0f, 0.25f, 16.0f, false) == 0.25f,
          "detail setting lower clamp");
    check(resolveDetailValue("test", "nan", 1.0f, 1.0f, 16.0f, false) == 1.0f &&
              resolveDetailValue("test", "2oops", 1.0f, 1.0f, 16.0f, false) == 1.0f,
          "invalid detail setting fallback");
    float scaled = 0.0f;
    check(scaledFinite(70.0f, 1.0f, scaled) && scaled == 70.0f,
          "identity detail transform");
    check(scaledFinite(-10.0f, 4.0f, scaled) && scaled == -40.0f,
          "signed fog transform");
    check(!scaledFinite(INFINITY, 4.0f, scaled), "non-finite detail rejection");
    check(scheduledCharacterLodScale(4.0f, 180u, 300u, 179u) == 1.0f &&
              scheduledCharacterLodScale(4.0f, 180u, 300u, 180u) == 4.0f &&
              scheduledCharacterLodScale(4.0f, 180u, 300u, 299u) == 4.0f &&
              scheduledCharacterLodScale(4.0f, 180u, 300u, 300u) == 1.0f,
          "character LOD probe gameplay window");
    check(isSkinnedSelectorCall(true, kSkinnedSimpleCallerReturn) &&
              isSkinnedSelectorCall(false, kSkinnedBlendedCallerReturn) &&
              !isSkinnedSelectorCall(true, 0x003EF72Cu) &&
              !isSkinnedSelectorCall(false, 0x003EF81Cu),
          "character LOD probe caller filter");
    check(withinCharacterLodTarget(-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.1f) &&
              !withinCharacterLodTarget(
                  9.0f, -1.0f, -1.0f, 11.0f, 1.0f, 1.0f, {0.0f, 0.0f, 0.0f}, 1.0f),
          "character LOD probe center filter");
    const KzPostDetailSettings safeDetail = enforceDetailSafety({2.0f, 4.0f}, false);
    check(safeDetail.renderDistance == 2.0f && safeDetail.lodScale == 1.0f,
          "unsafe combined detail controls are rejected");
    check(isRenderZoneFogCall(0x001C1C30u) && isRenderZoneFogCall(0x001C1D08u) &&
              isRenderZoneFogCall(0x001EE260u) && !isRenderZoneFogCall(0x00151188u),
          "RenderZone fog call-site filter");
    return failures;
}
