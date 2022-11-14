#version 330 core
out vec4 FragColor;

in vec3 fColor;

uniform float transparency;

void main()
{
    FragColor = vec4(fColor, transparency);
}