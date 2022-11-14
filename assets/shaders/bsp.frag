#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform sampler2D lightmapTexture;

void main()
{
    vec3 txtColor = texture(lightmapTexture, TexCoord).rgb;

    float gamma = 2.2;
    vec3 color = pow(txtColor, vec3(1.0/gamma));
    FragColor = vec4(txtColor, 1.0);
}