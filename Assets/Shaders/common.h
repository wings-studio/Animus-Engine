#pragma once

#if defined(SHADER_ENGINE_SIDE)
#include "Aurora/Core/Vector.hpp"
#include "Aurora/Core/Types.hpp"

using namespace Aurora;

#define uniformbuffer struct

typedef Matrix4 mat4;
typedef Vector4 vec4;
typedef Vector3 vec3;
typedef Vector2 vec2;
typedef glm::mat4x3 mat4x3;
#endif

#if !defined(SHADER_ENGINE_SIDE)
#define uniformbuffer layout(std140) uniform
#endif

uniformbuffer GLOB_Data
{
	vec3 CameraPos;
#if defined(SHADER_ENGINE_SIDE)
	float Padding0;
#endif
	vec3 CameraDir;
#if defined(SHADER_ENGINE_SIDE)
	float Padding1;
#endif
};