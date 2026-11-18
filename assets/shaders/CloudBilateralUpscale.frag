#version 330 core

in vec2 v_UV;

uniform sampler2D u_LowResClouds;    // Textura de nubes a 1/4 resolución
uniform sampler2D u_SceneDepth;      // Buffer de profundidad full-res
uniform vec2      u_LowResTexelSize; // 1.0 / (width / 4, height / 4)
uniform float     u_DepthSensitivity;
uniform float     u_NearPlane;
uniform float     u_FarPlane;
uniform mat4      u_InvProjection;
uniform mat4      u_InvView;
uniform vec3      u_CameraPos;
uniform float     u_CloudBaseHeight;

out vec4 FragColor;

float LinearizeDepth(float d)
{
    float zNdc = d * 2.0 - 1.0;
    return (2.0 * u_NearPlane * u_FarPlane) / (u_FarPlane + u_NearPlane - zNdc * (u_FarPlane - u_NearPlane));
}

void main()
{
    float centerDepthRaw = texture(u_SceneDepth, v_UV).r;

    // Si es cielo de fondo (sin geometría opaca), muestreo directo suave
    if (centerDepthRaw >= 0.99999) {
        FragColor = texture(u_LowResClouds, v_UV);
        return;
    }

    vec4 clip = vec4(v_UV * 2.0 - 1.0, centerDepthRaw * 2.0 - 1.0, 1.0);
    vec4 vPos = u_InvProjection * clip;
    vPos /= max(abs(vPos.w), 1e-5);
    vec3 wPos = (u_InvView * vPos).xyz;

    // Si la geometría y la cámara están por debajo de la base de las nubes, descartar
    if (u_CameraPos.y < u_CloudBaseHeight && wPos.y < u_CloudBaseHeight) {
        FragColor = vec4(0.0);
        return;
    }

    // Si el píxel es un objeto opaco del mundo y el raymarch no detectó nubes frente a él, descartar
    vec4 centerCloud = texture(u_LowResClouds, v_UV);
    if (centerCloud.a <= 0.0001) {
        FragColor = vec4(0.0);
        return;
    }

    float centerDepthLinear = LinearizeDepth(centerDepthRaw);
    vec4 accumColor = vec4(0.0);
    float totalWeight = 0.0;

    // Filtro bilateral 3x3 respetando discontinuidades de profundidad
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(float(x), float(y)) * u_LowResTexelSize;
            vec2 sampleUV = v_UV + offset;

            vec4 cloudSample = texture(u_LowResClouds, sampleUV);
            float sampleDepthRaw = texture(u_SceneDepth, sampleUV).r;

            // Si el píxel vecino es cielo pero el píxel actual es un objeto en primer plano, no sangrar nubes
            if (sampleDepthRaw >= 0.9999) {
                continue;
            }

            float sampleDepthLinear = LinearizeDepth(sampleDepthRaw);
            float spatialDist2 = float(x * x + y * y);
            float spatialWeight = exp(-spatialDist2 * 0.5);
            float depthDiff = abs(centerDepthLinear - sampleDepthLinear);
            float depthWeight = exp(-depthDiff * u_DepthSensitivity);

            float weight = spatialWeight * depthWeight;
            accumColor += cloudSample * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 1e-4) {
        FragColor = accumColor / totalWeight;
    } else {
        FragColor = vec4(0.0);
    }
}
