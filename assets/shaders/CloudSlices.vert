#version 300 es
precision highp float;

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoords;

uniform mat4 u_View;
uniform mat4 u_Projection;
uniform vec3 u_CameraPos;

uniform float u_CloudBaseHeight;
uniform float u_CloudLayerThickness;
uniform int   u_TotalSlices;
uniform float u_CloudScale;
uniform vec2  u_WindOffset;
uniform float u_DomeRadius;

out vec3  v_VolumeUV;
out vec3  v_WorldPos;
out float v_SliceFactor;
out vec3  v_ViewDir;

void main()
{
    // Factor normalizado de capa Z (0.0 en la base inferior, 1.0 en la cúspide)
    float sliceFactor = float(gl_InstanceID) / max(float(u_TotalSlices - 1), 1.0);

    // Mapeo hemisférico: transformar la grilla plana en un domo curvado hacia el horizonte
    vec2 p = a_Position.xz;
    float distNorm = clamp(length(p), 0.0, 1.0);
    float domeProfile = 1.0 - distNorm * distNorm;

    // Altura relativa a la cámara: u_CloudBaseHeight en el cenit, descendiendo suavemente hacia el horizonte
    float baseH = u_CloudBaseHeight * (0.25 + 0.75 * domeProfile);
    float layerH = u_CloudLayerThickness * (0.4 + 0.6 * domeProfile);
    float localY = baseH + sliceFactor * layerH;

    // Posición horizontal radial en metros
    vec2 localXZ = (distNorm > 0.0001 ? normalize(p) * (distNorm * u_DomeRadius) : vec2(0.0));
    vec3 relPos = vec3(localXZ.x, localY, localXZ.y);

    // Mapeo UV horizontal continuo con desplazamiento de viento y paralaje por capa
    vec2 worldXZ = localXZ + u_CameraPos.xz;
    vec2 uv = worldXZ * u_CloudScale;
    v_VolumeUV = vec3(uv + u_WindOffset * (0.85 + 0.25 * sliceFactor), sliceFactor);
    v_WorldPos = relPos + u_CameraPos;
    v_SliceFactor = sliceFactor;
    v_ViewDir = normalize(relPos);

    // Transformar a espacio de vista usando la rotación de la cámara (sin traslación para máxima estabilidad en móvil)
    vec3 viewPos = mat3(u_View) * relPos;
    gl_Position = u_Projection * vec4(viewPos, 1.0);
}
