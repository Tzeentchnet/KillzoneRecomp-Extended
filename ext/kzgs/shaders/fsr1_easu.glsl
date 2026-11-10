// SPDX-License-Identifier: GPL-3.0+
#version 450
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_EASU_F 1

layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D source_image;
layout(set = 0, binding = 1, rgba8) writeonly uniform image2D destination_image;
layout(push_constant) uniform Constants
{
	uvec4 con0;
	uvec4 con1;
	uvec4 con2;
	uvec4 con3;
	ivec4 source_rect;
	uvec2 output_size;
} pc;

#ifdef FSR1_NATIVE_GATHER_REFERENCE
AF4 FsrEasuRF(AF2 p) { return textureGather(source_image, p, 0); }
AF4 FsrEasuGF(AF2 p) { return textureGather(source_image, p, 1); }
AF4 FsrEasuBF(AF2 p) { return textureGather(source_image, p, 2); }
#else
vec4 gather_channel(vec2 p, int channel)
{
	ivec2 base = ivec2(floor(p * vec2(textureSize(source_image, 0)) - vec2(0.5)));
	ivec2 lo = pc.source_rect.xy;
	ivec2 hi = pc.source_rect.zw - ivec2(1);
	return vec4(
		texelFetch(source_image, clamp(base + ivec2(0, 1), lo, hi), 0)[channel],
		texelFetch(source_image, clamp(base + ivec2(1, 1), lo, hi), 0)[channel],
		texelFetch(source_image, clamp(base + ivec2(1, 0), lo, hi), 0)[channel],
		texelFetch(source_image, clamp(base, lo, hi), 0)[channel]);
}

AF4 FsrEasuRF(AF2 p) { return gather_channel(p, 0); }
AF4 FsrEasuGF(AF2 p) { return gather_channel(p, 1); }
AF4 FsrEasuBF(AF2 p) { return gather_channel(p, 2); }
#endif
#include "ffx_fsr1.h"

void main()
{
	uvec2 pixel = gl_GlobalInvocationID.xy;
	if (any(greaterThanEqual(pixel, pc.output_size)))
		return;
	vec3 color;
	FsrEasuF(color, pixel, pc.con0, pc.con1, pc.con2, pc.con3);
	imageStore(destination_image, ivec2(pixel), vec4(color, 1.0));
}
