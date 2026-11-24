#version 460

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

struct GpuInstanceData {
    mat4 model;
    mat4 prevModel;
    uint materialIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 prevViewProj;
    mat4 invViewProj;
    vec4 cameraPos;
} uCamera;

layout(set = 1, binding = 1) readonly buffer InstanceBuffer {
    GpuInstanceData instances[];
} uInstances;

layout(push_constant) uniform PushData {
    uint instanceIndex;
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vWorldTangent;
layout(location = 4) out vec3 vWorldBitangent;
layout(location = 5) out vec4 vCurrClip;
layout(location = 6) out vec4 vPrevClip;
layout(location = 7) flat out uint vMaterialIndex;
layout(location = 8) flat out uint vSelected;

void main() {
    GpuInstanceData inst = uInstances.instances[pc.instanceIndex];
    vec4 worldPos = inst.model * vec4(aPos, 1.0);
    vec4 prevWorldPos = inst.prevModel * vec4(aPos, 1.0);

    mat3 normalMat = transpose(inverse(mat3(inst.model)));
    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(normalMat * aNormal);
    vWorldTangent = normalize(mat3(inst.model) * aTangent);
    vWorldBitangent = normalize(mat3(inst.model) * aBitangent);
    vUV = aUV;

    vCurrClip = uCamera.viewProj * worldPos;
    vPrevClip = uCamera.prevViewProj * prevWorldPos;
    vMaterialIndex = inst.materialIndex;
    vSelected = inst.pad0;

    gl_Position = vCurrClip;
}
