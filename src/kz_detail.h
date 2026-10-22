#pragma once

// Per-consumer scene-detail distances (docs/findings.md "GFX-03 reinvestigation").
//
// The game multiplies one render-context value (+0x1AC) into every detail distance: static, instanced and skinned
// LOD selection, the per-mesh "Maximum visible distance" cull, the grass view distance and about fifteen other
// per-class gates. Scaling that shared value raises all of them at once; this module scales each consumer
// independently instead.
//
// [Graphics] StaticLodScale/SkinnedLodScale/VisibleCullScale/GrassViewScale (1 = original, qualified only up to
// 2x and only in isolation from RenderDistance > 1 - that combination corrupts guest memory; kzDetailInstall forces
// all four back to 1 whenever RenderDistance > 1) drive the product path with no env var needed. KZ_DETAIL_* env
// vars remain for diagnosis, override the resolved config values, and are not subject to that safety clamp - do
// not combine them with an elevated RenderDistance outside controlled investigation.

#include <cstdint>

class PS2Runtime;
struct KzConfig;

enum class KzDetailClass : uint8_t
{
    StaticLod,
    SkinnedLod,
    VisibleCull,
    Grass,
    InstancedLod, // LodStaticMeshResource instanced draws (0x3EFAD8); same tables as StaticLod
    Count
};

struct KzDetailScales
{
    float value[static_cast<int>(KzDetailClass::Count)] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float &operator[](KzDetailClass c) { return value[static_cast<int>(c)]; }
    float operator[](KzDetailClass c) const { return value[static_cast<int>(c)]; }
};

void kzDetailInstall(PS2Runtime &runtime);
// The config-driven base before any KZ_DETAIL_* diagnostic override: forces every scale back to 1 whenever
// config.renderDistance > 1 (docs/findings.md "GFX-03 reinvestigation" - that combination corrupts guest memory).
KzDetailScales kzDetailResolveConfigScales(const KzConfig &config, bool reportDiagnostics);
// Diagnostic only: with KZ_DETAIL_HEAPCHECK=<path>, writes the recent host-write and free history to <path>.
void kzDetailShutdown();
int kzDetailSelfTest();
