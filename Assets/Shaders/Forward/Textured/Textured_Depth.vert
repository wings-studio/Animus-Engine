#include "../../vs_common.h"
#include "../../World/instancing.h"

layout(location = 0) in vec3 POSITION;
layout(location = 1) in vec2 TEXCOORD;

out vec2 TexCoord;
out vec3 Normal;
out mat3 TBN;

void main()
{
	vec4 worldPos = INST_TRANSFORM * vec4(POSITION, 1.0);
	gl_Position = ProjectionMatrix * (ViewMatrix * worldPos);
	TexCoord = TEXCOORD;
}