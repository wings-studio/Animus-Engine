#include "../common.h"

uniformbuffer BloomDesc
{
	vec4 Params; // (x) threshold, (y) threshold - knee, (z) knee * 2, (w) 0.25 / knee
	vec2 LodAndMode;
	vec2 HalfTexel;
#if defined(SHADER_COMPUTE) || defined(SHADER_PIXEL)
} u_Uniforms;
#else
};
#endif

#define BLOOM_MODE_PREFILTER      0
#define BLOOM_MODE_DOWNSAMPLE     1
#define BLOOM_MODE_UPSAMPLE_FIRST 2
#define BLOOM_MODE_UPSAMPLE       3