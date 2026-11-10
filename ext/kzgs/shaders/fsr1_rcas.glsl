// SPDX-License-Identifier: GPL-3.0+
#version 450
#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"
#define FSR_RCAS_F 1

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

AF4 FsrRcasLoadF(ASU2 p)
{
	return texelFetch(source_image, clamp(p, ivec2(0), ivec2(pc.output_size) - ivec2(1)), 0);
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "ffx_fsr1.h"

void main()
{
	uvec2 pixel = gl_GlobalInvocationID.xy;
	if (any(greaterThanEqual(pixel, pc.output_size)))
		return;
	vec3 color;
	FsrRcasF(color.r, color.g, color.b, pixel, pc.con0);
	imageStore(destination_image, ivec2(pixel), vec4(color, 1.0));
}
