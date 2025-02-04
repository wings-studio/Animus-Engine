#include "../ps_common.h"
// Bloom made by Cherno https://www.youtube.com/channel/UCQ-W1KE9EYfdxhL6S4twUNw

out vec4 FragColor;
in vec2 TexCoord;

const float Epsilon = 1.0e-4;

layout(binding = 1) uniform sampler2D u_Texture;
layout(binding = 2) uniform sampler2D u_BloomTexture;

#include "ub_bloom.h"

vec3 DownsampleBox13(sampler2D tex, float lod, vec2 uv, vec2 texelSize)
{
	// Center
	vec3 A = textureLod(tex, uv, lod).rgb;

	texelSize *= 0.5f; // Sample from center of texels

	// Inner box
	vec3 B = textureLod(tex, uv + texelSize * vec2(-1.0f, -1.0f), lod).rgb;
	vec3 C = textureLod(tex, uv + texelSize * vec2(-1.0f, 1.0f), lod).rgb;
	vec3 D = textureLod(tex, uv + texelSize * vec2(1.0f, 1.0f), lod).rgb;
	vec3 E = textureLod(tex, uv + texelSize * vec2(1.0f, -1.0f), lod).rgb;

	// Outer box
	vec3 F = textureLod(tex, uv + texelSize * vec2(-2.0f, -2.0f), lod).rgb;
	vec3 G = textureLod(tex, uv + texelSize * vec2(-2.0f, 0.0f), lod).rgb;
	vec3 H = textureLod(tex, uv + texelSize * vec2(0.0f, 2.0f), lod).rgb;
	vec3 I = textureLod(tex, uv + texelSize * vec2(2.0f, 2.0f), lod).rgb;
	vec3 J = textureLod(tex, uv + texelSize * vec2(2.0f, 2.0f), lod).rgb;
	vec3 K = textureLod(tex, uv + texelSize * vec2(2.0f, 0.0f), lod).rgb;
	vec3 L = textureLod(tex, uv + texelSize * vec2(-2.0f, -2.0f), lod).rgb;
	vec3 M = textureLod(tex, uv + texelSize * vec2(0.0f, -2.0f), lod).rgb;

	// Weights
	vec3 result = vec3(0.0);
	// Inner box
	result += (B + C + D + E) * 0.5f;
	// Bottom-left box
	result += (F + G + A + M) * 0.125f;
	// Top-left box
	result += (G + H + I + A) * 0.125f;
	// Top-right box
	result += (A + I + J + K) * 0.125f;
	// Bottom-right box
	result += (M + A + K + L) * 0.125f;

	// 4 samples each
	result *= 0.25f;

	return result;
}

// Quadratic color thresholding
// curve = (threshold - knee, knee * 2, 0.25 / knee)
vec4 QuadraticThreshold(vec4 color, float threshold, vec3 curve)
{
	// Maximum pixel brightness
	float brightness = max(max(color.r, color.g), color.b);
	// Quadratic curve
	float rq = clamp(brightness - curve.x, 0.0, curve.y);
	rq = (rq * rq) * curve.z;
	color *= max(rq, brightness - threshold) / max(brightness, Epsilon);
	return color;
}

vec4 Prefilter(vec4 color, vec2 uv)
{
	float clampValue = 20.0f;
	color = min(vec4(clampValue), color);
	color = QuadraticThreshold(color, u_Uniforms.Params.x, u_Uniforms.Params.yzw);
	return color;
}

vec3 UpsampleTent9(sampler2D tex, float lod, vec2 uv, vec2 texelSize, float radius)
{
	vec4 offset = texelSize.xyxy * vec4(1.0f, 1.0f, -1.0f, 0.0f) * radius;

	// Center
	vec3 result = textureLod(tex, uv, lod).rgb * 4.0f;

	result += textureLod(tex, uv - offset.xy, lod).rgb;
	result += textureLod(tex, uv - offset.wy, lod).rgb * 2.0;
	result += textureLod(tex, uv - offset.zy, lod).rgb;

	result += textureLod(tex, uv + offset.zw, lod).rgb * 2.0;
	result += textureLod(tex, uv + offset.xw, lod).rgb * 2.0;

	result += textureLod(tex, uv + offset.zy, lod).rgb;
	result += textureLod(tex, uv + offset.wy, lod).rgb * 2.0;
	result += textureLod(tex, uv + offset.xy, lod).rgb;

	return result * (1.0f / 16.0f);
}

void main()
{
	vec2 texCoords = TexCoord;
	//texCoords += u_Uniforms.HalfTexel;

	vec2 texSize = vec2(textureSize(u_Texture, int(u_Uniforms.LodAndMode.x)));
	vec4 color = vec4(1, 0, 1, 1);
	if (uint(u_Uniforms.LodAndMode.y) == BLOOM_MODE_PREFILTER)
	{
		color.rgb = DownsampleBox13(u_Texture, 0, texCoords, 1.0f / texSize);
		color = Prefilter(color, texCoords);
		color.a = 1.0f;
	}
	else if (uint(u_Uniforms.LodAndMode.y) == BLOOM_MODE_UPSAMPLE_FIRST)
	{
		vec2 bloomTexSize = vec2(textureSize(u_Texture, int(u_Uniforms.LodAndMode.x + 1.0f)));
		float sampleScale = 1.0f;
		vec3 upsampledTexture = UpsampleTent9(u_Texture, u_Uniforms.LodAndMode.x + 1.0f, texCoords, 1.0f / bloomTexSize, sampleScale);

		vec3 existing = textureLod(u_Texture, texCoords, u_Uniforms.LodAndMode.x).rgb;
		color.rgb = existing + upsampledTexture;
	}
	else if (uint(u_Uniforms.LodAndMode.y) == BLOOM_MODE_UPSAMPLE)
	{
		vec2 bloomTexSize = vec2(textureSize(u_BloomTexture, int(u_Uniforms.LodAndMode.x + 1.0f)));
		float sampleScale = 1.0f;
		vec3 upsampledTexture = UpsampleTent9(u_BloomTexture, u_Uniforms.LodAndMode.x + 1.0f, texCoords, 1.0f / bloomTexSize, sampleScale);

		vec3 existing = textureLod(u_Texture, texCoords, u_Uniforms.LodAndMode.x).rgb;
		color.rgb = existing + upsampledTexture;
	}
	else if (uint(u_Uniforms.LodAndMode.y) == BLOOM_MODE_DOWNSAMPLE)
	{
		// Downsample
		color.rgb = DownsampleBox13(u_Texture, u_Uniforms.LodAndMode.x, texCoords, 1.0f / texSize);
	}

	FragColor = color;
}