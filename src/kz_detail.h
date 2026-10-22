#pragma once

// Per-consumer scene-detail distances (docs/findings.md "GFX-03 reinvestigation").
//
// The game multiplies one render-context value (+0x1AC) into every detail distance: static, instanced and skinned
// LOD selection, the per-mesh "Maximum visible distance" cull, the grass view distance and about fifteen other
// per-class gates. Scaling that shared value raises all of them at once. This module instead scales individual
// consumers for diagnosis and records the streamer, heap and stack telemetry used to qualify them. It installs no
// hook unless a KZ_DETAIL_* variable is set.

#include <cstdint>

class PS2Runtime;

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
// Diagnostic only: with KZ_DETAIL_HEAPCHECK=<path>, writes the recent host-write and free history to <path>.
void kzDetailShutdown();
int kzDetailSelfTest();
