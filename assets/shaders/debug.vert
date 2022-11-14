#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 fColor;

uniform mat4 projection;
uniform mat4 view;

void main()
{
	fColor = aColor;
	gl_Position = projection * view * vec4(aPos, 1.0);
}