#version 330 core

layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;

uniform mat4 u_InvProjection;
uniform mat4 u_InvView;

out vec2 v_UV;
out vec3 v_WorldRay;

void main()
{
    v_UV = a_UV;

    // Reconstrucción del rayo en espacio de mundo en los vértices del quad
    vec4 clipPos = vec4(a_Pos, 1.0, 1.0);
    vec4 viewRay = u_InvProjection * clipPos;
    v_WorldRay = mat3(u_InvView) * (viewRay.xyz / max(abs(viewRay.w), 0.0001));

    gl_Position = vec4(a_Pos, 0.0, 1.0);
}
