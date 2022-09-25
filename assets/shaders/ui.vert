#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 model;

void main()
{
	TexCoord = aTexCoord.xy;
	gl_Position = model * vec4(aPos.xy, 0.0, 1.0);
}