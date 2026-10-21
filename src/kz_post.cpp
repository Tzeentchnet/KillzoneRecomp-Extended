#include "kz_post.h"

#include "kz_config.h"
#include "kz_hook_addresses.h"

#include "ps2_runtime.h"

#include <algorithm>
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
//   0x1C2168 LOD scale from the camera's field of view (f12 = fov, a0 = game object): ctx+0x1AC = obj[0x1C] *
//            tan(refFov/2) / tan(fov/2), the divisor of every distance the LOD selectors compare with the LOD tables
//            (static: 0x3F4E70, skinned: 0x3F4F10, multi-mesh: 0x3EFAD8 ...). The hook scales obj[0x1C] around the call.
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
    constexpr uint32_t kLodScale = kz_hook::LodScale;
    constexpr uint32_t kRamMask = 0x01FFFFFFu;
    constexpr uint32_t kRamSize = 0x02000000u;
    constexpr float kRenderDistanceMin = 1.0f;
    constexpr float kRenderDistanceMax = 16.0f;
    constexpr float kLodScaleMin = 0.25f;
    constexpr float kLodScaleMax = 16.0f;

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
    PS2Runtime::RecompiledFunction g_origLodScale = nullptr;
    float g_renderDistance = 1.0f;
    float g_lodScale = 1.0f;
    std::atomic<int> g_farPlaneCalls{0}, g_fogCalls{0}, g_lodCalls{0};
    std::atomic<int> g_invalidZoneValues{0}, g_invalidLodValues{0};

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

    void lodScale(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t base = getRegU32(ctx, 4);
        const bool entry = entering(ctx, kLodScale) &&
                           base >= 0x00100000u && base <= kRamSize - 0x20u;
        const uint32_t obj = base + 0x1Cu;
        float saved = 0.0f;
        if (entry)
        {
            saved = rdF(rdram, obj);
            float scaled = 0.0f;
            if (scaledFinite(saved, g_lodScale, scaled))
            {
                wrF(rdram, obj, scaled);
                const int n = ++g_lodCalls;
                if (g_log && (n == 1 || n % 600 == 0))
                    std::printf("[kz-post] lod scale call %d: base %.3f -> %.3f (x%.2f)\n",
                                n, saved, scaled, g_lodScale);
            }
            else if (g_invalidLodValues.fetch_add(1) == 0)
            {
                std::fprintf(stderr, "[kz] view distance: ignored non-finite LOD scale input\n");
            }
        }
        g_origLodScale(rdram, ctx, rt);
        if (entry)
            wrF(rdram, obj, saved);
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
    return settings;
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
        viewOk = hook(runtime, kLodScale, &lodScale, g_origLodScale) && viewOk;
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
    check(isRenderZoneFogCall(0x001C1C30u) && isRenderZoneFogCall(0x001C1D08u) &&
              isRenderZoneFogCall(0x001EE260u) && !isRenderZoneFogCall(0x00151188u),
          "RenderZone fog call-site filter");
    return failures;
}
