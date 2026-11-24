#version 460
#extension GL_EXT_nonuniform_qualifier : require

struct GpuMaterialData {
    vec4 albedoColor;
    vec4 emissiveColor;
    vec4 pbrParams;
    vec4 extraParams;
    ivec4 texIndices0;
    ivec4 texIndices1;
};

layout(set = 0, binding = 0) uniform sampler2D uTextures[];
layout(set = 0, binding = 1) readonly buffer MaterialBuffer {
    GpuMaterialData materials[];
} uMaterials;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vWorldTangent;
layout(location = 4) in vec3 vWorldBitangent;
layout(location = 5) in vec4 vCurrClip;
layout(location = 6) in vec4 vPrevClip;
layout(location = 7) flat in uint vMaterialIndex;
layout(location = 8) flat in uint vSelected;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormalRough;
layout(location = 2) out vec4 outMetalEmissive;
layout(location = 3) out vec2 outMotion;

void main() {
    GpuMaterialData mat = uMaterials.materials[vMaterialIndex];

    vec4 albedo = mat.albedoColor;
    if (mat.texIndices0.x >= 0) {
        albedo *= texture(uTextures[nonuniformEXT(mat.texIndices0.x)], vUV);
    }

    if (mat.texIndices1.z >= 0) {
        albedo.a *= texture(uTextures[nonuniformEXT(mat.texIndices1.z)], vUV).r;
    }

    if (mat.extraParams.y > 0.5 && albedo.a < mat.extraParams.x) {
        discard;
    }

    vec3 N = normalize(vWorldNormal);
    if (mat.texIndices0.y >= 0 && length(vWorldTangent) > 0.001 && length(vWorldBitangent) > 0.001) {
        vec3 mapN = texture(uTextures[nonuniformEXT(mat.texIndices0.y)], vUV).xyz * 2.0 - 1.0;
        mapN.xy *= mat.pbrParams.w;
        mat3 TBN = mat3(normalize(vWorldTangent), normalize(vWorldBitangent), N);
        N = normalize(TBN * mapN);
    }
    if (!gl_FrontFacing && mat.extraParams.w > 0.5) {
        N = -N;
    }

    float roughness = mat.pbrParams.x;
    if (mat.texIndices0.z >= 0) {
        roughness *= texture(uTextures[nonuniformEXT(mat.texIndices0.z)], vUV).g;
    }
    if (mat.extraParams.z > 0.5) {
        roughness = 1.0 - roughness;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    float metallic = mat.pbrParams.y;
    if (mat.texIndices0.w >= 0) {
        metallic *= texture(uTextures[nonuniformEXT(mat.texIndices0.w)], vUV).b;
    }
    metallic = clamp(metallic, 0.0, 1.0);

    float ao = mat.pbrParams.z;
    if (mat.texIndices1.x >= 0) {
        ao *= texture(uTextures[nonuniformEXT(mat.texIndices1.x)], vUV).r;
    }

    vec3 emissive = mat.emissiveColor.rgb * mat.emissiveColor.a;
    if (mat.texIndices1.y >= 0) {
        emissive *= texture(uTextures[nonuniformEXT(mat.texIndices1.y)], vUV).rgb;
    }
    float emissiveLuma = dot(emissive, vec3(0.2126, 0.7152, 0.0722));
    // Boost albedo with emissive tint if emissive material has low albedo
    vec3 finalAlbedo = clamp(albedo.rgb + emissive * 0.25, vec3(0.0), vec3(1.0));

    vec2 currNDC = vCurrClip.xy / max(abs(vCurrClip.w), 1e-5);
    vec2 prevNDC = vPrevClip.xy / max(abs(vPrevClip.w), 1e-5);
    vec2 motion = (currNDC - prevNDC) * 0.5;

    outAlbedo = vec4(finalAlbedo, albedo.a);
    outNormalRough = vec4(N, roughness);
    outMetalEmissive = vec4(metallic, emissiveLuma, float(vSelected), ao);
    outMotion = motion;
}
