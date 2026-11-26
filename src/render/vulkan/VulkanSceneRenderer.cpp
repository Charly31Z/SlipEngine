#include "SlipEngine/render/vulkan/VulkanSceneRenderer.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"
#include "SlipEngine/render/vulkan/SlipSpirvCompiler.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/render/SlipPostProcess.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipSkyComponent.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>

static_assert(sizeof(Vertex) % sizeof(float) == 0, "Vertex size must be 4-byte float aligned for BDA ray tracing");
static_assert(offsetof(Vertex, Position) == 0, "Vertex::Position offset must be 0");
static_assert(offsetof(Vertex, Normal) == 3 * sizeof(float), "Vertex::Normal offset must be 12");
static_assert(offsetof(Vertex, TexCoords) == 6 * sizeof(float), "Vertex::TexCoords offset must be 24");
static_assert(offsetof(Vertex, Tangent) == 10 * sizeof(float), "Vertex::Tangent offset must be 40");
static_assert(offsetof(Vertex, Bitangent) == 13 * sizeof(float), "Vertex::Bitangent offset must be 52");

namespace {

inline void HashCombineBytes(uint64_t& h, const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= 1099511628211ULL;
    }
}

struct alignas(16) GpuCameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;
    glm::mat4 prevViewProj;
    glm::mat4 invViewProj;
    glm::vec4 cameraPos;
};

struct alignas(16) GpuInstanceData {
    glm::mat4 model;
    glm::mat4 prevModel;
    uint32_t materialIndex = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
};

struct alignas(16) GpuDirLight {
    glm::vec4 directionIntensity{ 0.0f, -1.0f, 0.0f, 1.0f };
    glm::vec4 colorPad{ 1.0f };
};

struct alignas(16) GpuPointLight {
    glm::vec4 positionRange{ 0.0f, 0.0f, 0.0f, 10.0f };
    glm::vec4 colorIntensity{ 1.0f };
};

struct alignas(16) GpuSpotLight {
    glm::vec4 positionRange{ 0.0f, 0.0f, 0.0f, 10.0f };
    glm::vec4 directionCosOuter{ 0.0f, -1.0f, 0.0f, 0.85f };
    glm::vec4 colorIntensityCosIn{ 1.0f };
};

struct alignas(16) GpuSceneLightingUBO {
    glm::mat4 invViewProj{ 1.0f };
    glm::vec4 cameraPos{ 0.0f };
    glm::vec4 skyZenith{ 0.22f, 0.48f, 0.92f, 1.0f };
    glm::vec4 skyHorizon{ 0.72f, 0.82f, 0.95f, 2.5f };
    glm::vec4 skyGround{ 0.25f, 0.22f, 0.20f, 1.0f };
    glm::vec4 sunDirIntensity{ 0.35f, 0.85f, 0.40f, 1.8f };
    glm::vec4 sunColorParams{ 1.0f, 0.96f, 0.88f, 0.045f };
    glm::ivec4 counts{ 0, 0, 0, 180 };
    GpuDirLight dirLights[4];
    GpuPointLight pointLights[32];
    GpuSpotLight spotLights[16];
    glm::vec4 cloudParams0{ 0.0f }; // x=enabled, y=coverage, z=density, w=baseHeight
    glm::vec4 cloudParams1{ 0.0f }; // x=layerThickness, y=scale, z=edgeErosion, w=absorption
    glm::vec4 cloudParams2{ 0.0f }; // xy=windOffset, z=henyeyG, w=powderFactor
    glm::vec4 cloudAmbient{ 0.52f, 0.62f, 0.75f, 0.95f }; // rgb=ambient, w=silverSpread
    glm::mat4 currViewProj{ 1.0f };
    glm::mat4 prevViewProj{ 1.0f };
    glm::vec4 aoParams{ 0.0f, 0.45f, 0.025f, 1.0f }; // x=enabled, y=radius, z=bias, w=intensity
    glm::vec4 aoParams2{ 1.4f, 0.0f, 0.0f, 0.0f };    // x=power
    glm::mat4 view{ 1.0f };
};

struct alignas(16) GpuPostProcessUBO {
    glm::mat4 invViewProj{ 1.0f };
    glm::vec4 cameraPos{ 0.0f };
    glm::vec4 sunDir{ 0.35f, 0.85f, 0.40f, 0.0f };
    glm::vec4 sunColor{ 1.0f, 0.96f, 0.88f, 1.0f };
    glm::vec4 bloomParams{ 0.0f, 0.0f, 1.0f, 0.5f };      // x=enabled, y=intensity, z=threshold, w=softKnee
    glm::vec4 bloomTint{ 1.0f, 1.0f, 1.0f, 0.85f };       // rgb=tint, w=radius
    glm::vec4 lensParams{ 0.0f, 0.0f, 2.5f, 0.35f };      // x=enabled, y=intensity, z=threshold, w=halo
    glm::vec4 colorParams{ 1.0f, 1.0f, 1.0f, 0.0f };      // x=exposureMul, y=contrast, z=saturation, w=temperature
    glm::vec4 lift{ 0.0f, 0.0f, 0.0f, 0.0f };             // rgb, w=tonemapMode
    glm::vec4 gamma{ 1.0f, 1.0f, 1.0f, 0.0f };            // rgb, w=vignetteIntensity
    glm::vec4 gain{ 1.0f, 1.0f, 1.0f, 0.45f };            // rgb, w=vignetteSmoothness
    glm::vec4 colorFilter{ 1.0f, 1.0f, 1.0f, 0.0f };      // rgb, w=blurRadius
    glm::vec4 radialLetterbox{ 0.0f, 0.5f, 0.5f, 0.0f };  // x=radialStrength, y=centerX, z=centerY, w=letterbox
    glm::vec4 fogParams0{ 0.0f, 0.003f, 0.035f, 0.0f };   // x=enabled, y=density, z=heightFalloff, w=fogHeight
    glm::vec4 fogParams1{ 5.0f, 12000.0f, 0.65f, 0.90f }; // x=startDist, y=maxDist, z=sunScattering, w=skyBlend
    glm::vec4 fogColor{ 0.65f, 0.72f, 0.85f, 0.0f };      // rgb, w=pillarbox
    glm::ivec4 flags{ 0, 1, 1, 0 };                       // x=outlineEnabled, y=hasGBuffer, z=fxaaEnabled, w=fxaaQuality
    glm::vec4 exposureAdapt{ 0.0f, 0.18f, 0.05f, 8.0f }; // x=auto, y=key, z=minLum, w=maxLum
    glm::vec4 exposureTiming{ 2.0f, 0.016f, 1.0f, 0.0f }; // x=speed, y=dt, z=compensationMul
};

const char* kFallbackGBufferVert = R"GLSL(#version 460
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;
struct GpuInstanceData { mat4 model; mat4 prevModel; uint materialIndex; uint pad0; uint pad1; uint pad2; };
layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view; mat4 proj; mat4 viewProj; mat4 prevViewProj; mat4 invViewProj; vec4 cameraPos;
} uCamera;
layout(set = 1, binding = 1) readonly buffer InstanceBuffer { GpuInstanceData instances[]; } uInstances;
layout(push_constant) uniform PushData { uint instanceIndex; } pc;
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
)GLSL";

const char* kFallbackGBufferFrag = R"GLSL(#version 460
#extension GL_EXT_nonuniform_qualifier : require
struct GpuMaterialData {
    vec4 albedoColor; vec4 emissiveColor; vec4 pbrParams; vec4 extraParams; ivec4 texIndices0; ivec4 texIndices1;
};
layout(set = 0, binding = 0) uniform sampler2D uTextures[];
layout(set = 0, binding = 1) readonly buffer MaterialBuffer { GpuMaterialData materials[]; } uMaterials;
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
    if (mat.texIndices0.x >= 0) albedo *= texture(uTextures[nonuniformEXT(mat.texIndices0.x)], vUV);
    if (mat.texIndices1.z >= 0) albedo.a *= texture(uTextures[nonuniformEXT(mat.texIndices1.z)], vUV).r;
    if (mat.extraParams.y > 0.5 && albedo.a < mat.extraParams.x) discard;
    vec3 N = normalize(vWorldNormal);
    if (mat.texIndices0.y >= 0 && length(vWorldTangent) > 0.001 && length(vWorldBitangent) > 0.001) {
        vec3 mapN = texture(uTextures[nonuniformEXT(mat.texIndices0.y)], vUV).xyz * 2.0 - 1.0;
        mapN.xy *= mat.pbrParams.w;
        N = normalize(mat3(normalize(vWorldTangent), normalize(vWorldBitangent), N) * mapN);
    }
    if (!gl_FrontFacing && mat.extraParams.w > 0.5) N = -N;
    float roughness = mat.pbrParams.x;
    if (mat.texIndices0.z >= 0) roughness *= texture(uTextures[nonuniformEXT(mat.texIndices0.z)], vUV).g;
    if (mat.extraParams.z > 0.5) roughness = 1.0 - roughness;
    roughness = clamp(roughness, 0.04, 1.0);
    float metallic = mat.pbrParams.y;
    if (mat.texIndices0.w >= 0) metallic *= texture(uTextures[nonuniformEXT(mat.texIndices0.w)], vUV).b;
    metallic = clamp(metallic, 0.0, 1.0);
    float ao = mat.pbrParams.z;
    if (mat.texIndices1.x >= 0) ao *= texture(uTextures[nonuniformEXT(mat.texIndices1.x)], vUV).r;
    vec3 emissive = mat.emissiveColor.rgb * mat.emissiveColor.a;
    if (mat.texIndices1.y >= 0) emissive *= texture(uTextures[nonuniformEXT(mat.texIndices1.y)], vUV).rgb;
    float emissiveLuma = dot(emissive, vec3(0.2126, 0.7152, 0.0722));
    vec3 finalAlbedo = clamp(albedo.rgb + emissive * 0.25, vec3(0.0), vec3(1.0));
    vec2 currNDC = vCurrClip.xy / max(abs(vCurrClip.w), 1e-5);
    vec2 prevNDC = vPrevClip.xy / max(abs(vPrevClip.w), 1e-5);
    outAlbedo = vec4(finalAlbedo, albedo.a);
    outNormalRough = vec4(N, roughness);
    outMetalEmissive = vec4(metallic, emissiveLuma, float(vSelected), ao);
    outMotion = (currNDC - prevNDC) * 0.5;
}
)GLSL";

const char* kFallbackLightingCompA = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
struct GpuDirLight { vec4 directionIntensity; vec4 colorPad; };
struct GpuPointLight { vec4 positionRange; vec4 colorIntensity; };
struct GpuSpotLight { vec4 positionRange; vec4 directionCosOuter; vec4 colorIntensityCosIn; };
layout(set = 0, binding = 0) uniform sampler2D uAlbedoTex;
layout(set = 0, binding = 1) uniform sampler2D uNormalRoughTex;
layout(set = 0, binding = 2) uniform sampler2D uMetalEmissiveTex;
layout(set = 0, binding = 3) uniform sampler2D uDepthTex;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D uHdrOut;
layout(set = 0, binding = 5) uniform SceneLightingUBO {
    mat4 invViewProj; vec4 cameraPos; vec4 skyZenith; vec4 skyHorizon; vec4 skyGround;
    vec4 sunDirIntensity; vec4 sunColorParams; ivec4 counts;
    GpuDirLight dirLights[4]; GpuPointLight pointLights[32]; GpuSpotLight spotLights[16];
    vec4 cloudParams0; vec4 cloudParams1; vec4 cloudParams2; vec4 cloudAmbient;
} uScene;
layout(set = 0, binding = 6) uniform sampler2D uRtgiTex;
layout(set = 0, binding = 7) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
layout(set = 0, binding = 8) uniform sampler2D uRtReflTex;
const float PI = 3.14159265359;
vec3 ReconstructWorldPos(vec2 uv, float depth) {
    vec4 world = uScene.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return world.xyz / max(abs(world.w), 1e-6);
}
float CloudHash31(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
float CloudNoise3D(vec3 p) {
    vec3 i = floor(p); vec3 f = fract(p); f = f * f * (3.0 - 2.0 * f);
    float n000 = CloudHash31(i); float n100 = CloudHash31(i + vec3(1,0,0));
    float n010 = CloudHash31(i + vec3(0,1,0)); float n110 = CloudHash31(i + vec3(1,1,0));
    float n001 = CloudHash31(i + vec3(0,0,1)); float n101 = CloudHash31(i + vec3(1,0,1));
    float n011 = CloudHash31(i + vec3(0,1,1)); float n111 = CloudHash31(i + vec3(1,1,1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y), mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float CloudFbm3D(vec3 p) {
    float v = 0.0; float amp = 0.5;
    for (int i = 0; i < 4; ++i) {
        v += amp * CloudNoise3D(p);
        p = vec3(0.80 * p.x - 0.60 * p.z, 1.15 * p.y + 0.27 * p.x, 0.60 * p.x + 0.80 * p.z) * 2.03 + vec3(13.7, 7.3, 19.1);
        amp *= 0.5;
    }
    return v;
}
float SampleCloudDensity(vec3 pos, float baseH, float thick, float scale, float coverage, float density, float edgeErosion, vec2 windOff) {
    float hFrac = clamp((pos.y - baseH) / thick, 0.0, 1.0);
    float hEnv = smoothstep(0.0, 0.16, hFrac) * smoothstep(1.0, 0.34, hFrac);
    if (hEnv <= 0.001) return 0.0;
    float freq = scale * 6.0; vec3 wind3 = vec3(windOff.x, 0.0, windOff.y) * 6.0;
    vec3 p = vec3(pos.x * freq, hFrac * 1.85, pos.z * freq) + wind3;
    vec3 warp = vec3(CloudNoise3D(p * 0.43 + vec3(1.7, 9.2, 4.1)), CloudNoise3D(p * 0.39 + vec3(8.3, 2.8, 5.7)), CloudNoise3D(p * 0.47 + vec3(3.1, 6.4, 9.5))) - 0.5;
    vec3 q = p + warp * 1.65;
    float fbmMain = CloudFbm3D(q);
    float billow = 1.0 - abs(CloudNoise3D(q * 1.73 + vec3(5.2, 1.3, 8.8)) * 2.0 - 1.0);
    float macroBank = CloudNoise3D(vec3(pos.x * freq * 0.23, 0.37, pos.z * freq * 0.29) + wind3 * 0.35);
    float localCov = clamp(coverage + (macroBank - 0.5) * 0.42, 0.02, 0.98);
    float threshold = 1.0 - localCov;
    float baseCloud = clamp((mix(fbmMain, billow, 0.30) - threshold) / max(1.0 - threshold, 0.02), 0.0, 1.0);
    if (baseCloud <= 0.0005) return 0.0;
    float detailN = CloudFbm3D(q * 3.4 - wind3 * 0.7);
    float eroded = clamp((baseCloud - detailN * edgeErosion * 0.55) / max(1.0 - detailN * edgeErosion * 0.55, 0.05), 0.0, 1.0);
    return pow(eroded, mix(1.0, 1.4, edgeErosion)) * hEnv * (density * 8.5);
}
vec3 EvaluateVolumetricClouds(vec3 ro, vec3 rd, vec3 skyCol, ivec2 pix) {
    if (uScene.cloudParams0.x < 0.5 || rd.y <= 0.012) return skyCol;
    float coverage = clamp(uScene.cloudParams0.y, 0.0, 1.0); float density = max(uScene.cloudParams0.z, 0.0);
    if (coverage <= 0.01 || density <= 0.00005) return skyCol;
    float baseH = uScene.cloudParams0.w; float thick = max(uScene.cloudParams1.x, 5.0);
    float tStart = max((baseH - ro.y) / rd.y, 0.0);
    float tEnd = min(min(max((baseH + thick - ro.y) / rd.y, tStart), tStart + 7500.0), 18000.0);
    if (tEnd <= tStart + 0.1) return skyCol;
    float scale = max(uScene.cloudParams1.y, 0.00005); float edgeErosion = clamp(uScene.cloudParams1.z, 0.0, 1.0);
    float absorption = max(uScene.cloudParams1.w, 0.05); vec2 windOff = uScene.cloudParams2.xy;
    float g = clamp(uScene.cloudParams2.z, -0.85, 0.85); float powderFactor = max(uScene.cloudParams2.w, 0.1);
    float silverSpread = max(uScene.cloudAmbient.w, 0.1);
    vec3 sunDir = normalize(uScene.sunDirIntensity.xyz); float cosTheta = dot(rd, sunDir);
    float hg = (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 0.02), 1.5));
    float phase = mix(0.35, hg * 2.1, 0.55);
    float silver = pow(max(cosTheta, 0.0), 8.0 * silverSpread) * 0.45;
    float ign = fract(52.9829189 * fract(dot(vec2(pix), vec2(0.06711056, 0.00583715))));
    const int STEPS = 32; float dt = (tEnd - tStart) / float(STEPS);
    vec3 accumCol = vec3(0.0); float transmittance = 1.0;
    for (int i = 0; i < STEPS; ++i) {
        vec3 pos = ro + rd * (tStart + (float(i) + ign) * dt);
        float d = SampleCloudDensity(pos, baseH, thick, scale, coverage, density, edgeErosion, windOff);
        if (d > 0.00005) {
            float hFrac = clamp((pos.y - baseH) / thick, 0.0, 1.0);
            float sunStep = clamp(thick * 0.12, 12.0, 90.0);
            float sunTau = (SampleCloudDensity(pos + sunDir * sunStep, baseH, thick, scale, coverage, density, edgeErosion, windOff) +
                            SampleCloudDensity(pos + sunDir * (sunStep * 2.6), baseH, thick, scale, coverage, density, edgeErosion, windOff)) * sunStep * absorption;
            float sunLight = max(exp(-sunTau * 1.1), 0.28 * exp(-sunTau * 0.25));
            float powder = 1.0 - exp(-d * dt * 1.8 * powderFactor);
            vec3 scatter = uScene.cloudAmbient.rgb * mix(0.68, 1.22, hFrac) + uScene.sunColorParams.rgb * uScene.sunDirIntensity.w * sunLight * (phase + silver) * (0.72 + 0.50 * powder);
            float stepT = exp(-d * dt * absorption);
            accumCol += transmittance * scatter * (1.0 - stepT);
            transmittance *= stepT;
            if (transmittance < 0.01) break;
        }
    }
    return mix(skyCol, accumCol + skyCol * transmittance, smoothstep(0.012, 0.085, rd.y) * exp(-tStart * 0.00004));
}
)GLSL";

const char* kFallbackLightingCompB = R"GLSL(vec3 EvaluateSky(vec3 dir, bool includeSunDisc) {
    if (uScene.skyZenith.w < 0.5) return vec3(0.08, 0.10, 0.14);
    float y = dir.y;
    vec3 col = (y >= 0.0)
        ? mix(uScene.skyZenith.rgb, uScene.skyHorizon.rgb, pow(clamp(1.0 - y, 0.0, 1.0), max(uScene.skyHorizon.w, 0.1)))
        : mix(uScene.skyGround.rgb, uScene.skyHorizon.rgb, pow(clamp(1.0 + y, 0.0, 1.0), max(uScene.skyHorizon.w, 0.1)));
    if (includeSunDisc && uScene.skyGround.w > 0.5) {
        vec3 sunDir = normalize(uScene.sunDirIntensity.xyz);
        float cosTheta = max(dot(normalize(dir), sunDir), 0.0);
        float sunRadius = max(uScene.sunColorParams.w, 0.005);
        float disc = smoothstep(cos(sunRadius), cos(sunRadius * 0.5), cosTheta);
        float glow = pow(cosTheta, mix(128.0, 8.0, clamp(float(uScene.counts.w) * 0.001, 0.0, 1.0))) * 0.45;
        col += uScene.sunColorParams.rgb * uScene.sunDirIntensity.w * (disc * 3.0 + glow);
    }
    return col;
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness; float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * d * d, 1e-5);
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0; float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-5);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0); }
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) { return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0); }
vec3 EvaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 rad, vec3 albedo, float rough, float metal) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (DistributionGGX(N, H, rough) * GeometrySmith(N, V, L, rough) * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
    return ((vec3(1.0) - F) * (1.0 - metal) * albedo / PI + spec) * rad * NdotL;
}
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(uHdrOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    vec2 uv = (vec2(pix) + 0.5) / vec2(size);
    float depth = texelFetch(uDepthTex, pix, 0).r;
    if (depth >= 0.99999) {
        vec3 rayDir = normalize(ReconstructWorldPos(uv, 1.0) - uScene.cameraPos.xyz);
        imageStore(uHdrOut, pix, vec4(EvaluateVolumetricClouds(uScene.cameraPos.xyz, rayDir, EvaluateSky(rayDir, true), pix), 1.0));
        return;
    }
    vec4 albedoSamp = texelFetch(uAlbedoTex, pix, 0);
    vec4 normRough = texelFetch(uNormalRoughTex, pix, 0);
    vec4 metalEmis = texelFetch(uMetalEmissiveTex, pix, 0);
    vec3 albedo = pow(max(albedoSamp.rgb, vec3(0.0)), vec3(2.2));
    vec3 N = normalize(normRough.xyz);
    float rough = clamp(normRough.w, 0.04, 1.0);
    float metal = clamp(metalEmis.x, 0.0, 1.0);
    float emis = metalEmis.y;
    float ao = clamp(metalEmis.w, 0.0, 1.0);
    vec3 worldPos = ReconstructWorldPos(uv, depth);
    vec3 V = normalize(uScene.cameraPos.xyz - worldPos);
    bool useRtgi = (uGi.giMode == 1 && uGi.hasValidTLAS != 0);
    vec4 giSamp = useRtgi ? textureLod(uRtgiTex, uv, 0.0) : vec4(0.0, 0.0, 0.0, 1.0);
    if (any(isnan(giSamp)) || any(isinf(giSamp))) giSamp = vec4(0.0, 0.0, 0.0, 1.0);
    float sunVis = (useRtgi && uGi.giRayTracedShadows != 0) ? clamp(giSamp.a, 0.0, 1.0) : 1.0;
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < clamp(uScene.counts.x, 0, 4); ++i) {
        vec3 L = normalize(-uScene.dirLights[i].directionIntensity.xyz);
        float vis = (i == 0) ? sunVis : 1.0;
        Lo += EvaluateBRDF(N, V, L, uScene.dirLights[i].colorPad.rgb * uScene.dirLights[i].directionIntensity.w, albedo, rough, metal) * vis;
    }
    for (int i = 0; i < clamp(uScene.counts.y, 0, 32); ++i) {
        vec3 toL = uScene.pointLights[i].positionRange.xyz - worldPos;
        float dist = length(toL); float range = max(uScene.pointLights[i].positionRange.w, 0.001);
        if (dist < range) {
            float att = clamp(1.0 - dist / range, 0.0, 1.0); att = (att * att) / (1.0 + 0.1 * dist * dist);
            Lo += EvaluateBRDF(N, V, toL / max(dist, 1e-4), uScene.pointLights[i].colorIntensity.rgb * uScene.pointLights[i].colorIntensity.w * att, albedo, rough, metal);
        }
    }
    for (int i = 0; i < clamp(uScene.counts.z, 0, 16); ++i) {
        vec3 toL = uScene.spotLights[i].positionRange.xyz - worldPos;
        float dist = length(toL); float range = max(uScene.spotLights[i].positionRange.w, 0.001);
        if (dist < range) {
            vec3 L = toL / max(dist, 1e-4);
            float theta = dot(-L, normalize(uScene.spotLights[i].directionCosOuter.xyz));
            float cosOut = uScene.spotLights[i].directionCosOuter.w;
            float spot = clamp((theta - cosOut) / max(mix(cosOut, 1.0, 0.35) - cosOut, 0.001), 0.0, 1.0);
            float att = clamp(1.0 - dist / range, 0.0, 1.0); att = (att * att) / (1.0 + 0.1 * dist * dist);
            Lo += EvaluateBRDF(N, V, L, uScene.spotLights[i].colorIntensityCosIn.rgb * uScene.spotLights[i].colorIntensityCosIn.w * att * spot, albedo, rough, metal);
        }
    }
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 Fenv = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, rough);
    vec3 ambient; vec3 specEnv = vec3(0.0);
    if (useRtgi) {
        ambient = max(giSamp.rgb, vec3(0.0)) * albedo * (1.0 - metal * 0.85) * uGi.giIntensity * ao;
        vec3 rtRefl = textureLod(uRtReflTex, uv, 0.0).rgb;
        if (any(isnan(rtRefl)) || any(isinf(rtRefl))) rtRefl = vec3(0.0);
        specEnv = max(rtRefl, vec3(0.0)) * Fenv * max(smoothstep(0.92, 0.10, rough), metal * (1.0 - rough * 0.45)) * ao;
    } else {
        ambient = EvaluateSky(N, false) * albedo * (1.0 - metal * 0.5) * 0.28 * ao;
        specEnv = EvaluateSky(reflect(-V, N), true) * Fenv * (1.0 - rough) * 0.35 * ao;
    }
    imageStore(uHdrOut, pix, vec4(ambient + specEnv + Lo + albedo * emis, 1.0));
}
)GLSL";

const char* kFallbackBloomExtractComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uHdrIn;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D uBloomOut;
layout(set = 0, binding = 2) uniform PostProcessUBO {
    mat4 invViewProj; vec4 cameraPos; vec4 sunDir; vec4 sunColor;
    vec4 bloomParams; vec4 bloomTint; vec4 lensParams; vec4 colorParams;
    vec4 lift; vec4 gamma; vec4 gain; vec4 colorFilter; vec4 radialLetterbox;
    vec4 fogParams0; vec4 fogParams1; vec4 fogColor; ivec4 flags;
} uPost;
vec3 ExtractBright(vec3 hdr, float threshold, float knee) {
    float lum = max(hdr.r, max(hdr.g, hdr.b));
    float soft = clamp(lum - threshold + knee, 0.0, 2.0 * max(knee, 1e-4));
    soft = (soft * soft) / (4.0 * max(knee, 1e-4));
    float contrib = max(soft, lum - threshold) / max(lum, 1e-4);
    return hdr * clamp(contrib, 0.0, 4.0);
}
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uBloomOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    vec2 uv = (vec2(pix) + 0.5) / vec2(size); vec2 t = 0.5 / vec2(size);
    vec3 hdr = max((textureLod(uHdrIn, uv + vec2(-t.x,-t.y), 0.0).rgb + textureLod(uHdrIn, uv + vec2(t.x,-t.y), 0.0).rgb +
                    textureLod(uHdrIn, uv + vec2(-t.x, t.y), 0.0).rgb + textureLod(uHdrIn, uv + vec2(t.x, t.y), 0.0).rgb) * 0.25, vec3(0.0));
    if (any(isnan(hdr)) || any(isinf(hdr))) hdr = vec3(0.0);
    vec3 outCol = vec3(0.0);
    if (uPost.bloomParams.x > 0.5 && uPost.bloomParams.y > 0.001) outCol += ExtractBright(hdr, uPost.bloomParams.z, uPost.bloomParams.w) * uPost.bloomTint.rgb * uPost.bloomParams.y;
    if (uPost.lensParams.x > 0.5 && uPost.lensParams.y > 0.001) outCol += ExtractBright(hdr, uPost.lensParams.z, 0.5) * uPost.lensParams.y * 0.5;
    imageStore(uBloomOut, pix, vec4(clamp(outCol, vec3(0.0), vec3(32.0)), 1.0));
}
)GLSL";

const char* kFallbackBloomBlurComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uBloomIn;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D uBloomOut;
layout(set = 0, binding = 2) uniform PostProcessUBO {
    mat4 invViewProj; vec4 cameraPos; vec4 sunDir; vec4 sunColor;
    vec4 bloomParams; vec4 bloomTint; vec4 lensParams; vec4 colorParams;
    vec4 lift; vec4 gamma; vec4 gain; vec4 colorFilter; vec4 radialLetterbox;
    vec4 fogParams0; vec4 fogParams1; vec4 fogColor; ivec4 flags;
} uPost;
layout(push_constant) uniform BlurPC { ivec2 dir; } pc;
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uBloomOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    float spread = clamp(max(uPost.bloomTint.w, 0.25), 0.25, 2.5) * 1.5;
    vec2 invSize = 1.0 / vec2(size); vec2 uv = (vec2(pix) + 0.5) * invSize; vec2 stepUv = vec2(pc.dir) * invSize * spread;
    const float w[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 res = textureLod(uBloomIn, uv, 0.0).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = stepUv * float(i);
        res += textureLod(uBloomIn, clamp(uv + off, vec2(0.0), vec2(1.0)), 0.0).rgb * w[i];
        res += textureLod(uBloomIn, clamp(uv - off, vec2(0.0), vec2(1.0)), 0.0).rgb * w[i];
    }
    if (any(isnan(res)) || any(isinf(res))) res = vec3(0.0);
    imageStore(uBloomOut, pix, vec4(max(res, vec3(0.0)), 1.0));
}
)GLSL";

const char* kFallbackTonemapComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uHdrIn;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D uLdrOut;
layout(set = 0, binding = 2) uniform PostProcessUBO {
    mat4 invViewProj; vec4 cameraPos; vec4 sunDir; vec4 sunColor;
    vec4 bloomParams; vec4 bloomTint; vec4 lensParams; vec4 colorParams;
    vec4 lift; vec4 gamma; vec4 gain; vec4 colorFilter; vec4 radialLetterbox;
    vec4 fogParams0; vec4 fogParams1; vec4 fogColor; ivec4 flags;
} uPost;
layout(set = 0, binding = 3) uniform sampler2D uMetalEmissiveTex;
layout(set = 0, binding = 4) uniform sampler2D uDepthTex;
layout(set = 0, binding = 5) uniform sampler2D uBloomTex;
vec3 ACESFilm(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
vec3 ReinhardTone(vec3 x) { return clamp(x / (1.0 + x), 0.0, 1.0); }
vec3 FilmicTone(vec3 x) { vec3 v = max(vec3(0.0), x - 0.004); return clamp((v * (6.2 * v + 0.5)) / (v * (6.2 * v + 1.7) + 0.06), 0.0, 1.0); }
vec3 ReconstructWorldPos(vec2 uv, float depth) { vec4 w = uPost.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0); return w.xyz / max(abs(w.w), 1e-6); }
vec3 SampleFilteredHDR(ivec2 pix, vec2 uv, ivec2 size) {
    float radStr = uPost.radialLetterbox.x; float blurRad = uPost.colorFilter.w;
    if (radStr <= 0.001 && blurRad <= 0.01) return texelFetch(uHdrIn, pix, 0).rgb;
    vec3 acc = vec3(0.0); float wSum = 0.0;
    if (radStr > 0.001) {
        vec2 dir = (uPost.radialLetterbox.yz - uv) * clamp(radStr, 0.0, 1.0) * 0.025;
        for (int i = 0; i < 6; ++i) { acc += textureLod(uHdrIn, clamp(uv + dir * float(i), vec2(0.0), vec2(1.0)), 0.0).rgb; wSum += 1.0; }
    }
    if (blurRad > 0.01) {
        vec2 stepUv = (1.0 / vec2(size)) * clamp(blurRad, 0.0, 16.0);
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { acc += textureLod(uHdrIn, clamp(uv + vec2(dx, dy) * stepUv, vec2(0.0), vec2(1.0)), 0.0).rgb; wSum += 1.0; }
    }
    return acc / max(wSum, 1.0);
}
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uLdrOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    vec2 uv = (vec2(pix) + 0.5) / vec2(size);
    vec3 hdr = max(SampleFilteredHDR(pix, uv, size), vec3(0.0));
    if (any(isnan(hdr)) || any(isinf(hdr))) hdr = vec3(0.0);
    if (uPost.fogParams0.x > 0.5 && uPost.flags.y != 0) {
        float depth = texelFetch(uDepthTex, pix, 0).r; float maxDist = max(uPost.fogParams1.y, 1.0);
        vec3 viewDir = normalize(ReconstructWorldPos(uv, 1.0) - uPost.cameraPos.xyz);
        float dist = maxDist; float sampleY = uPost.cameraPos.y + viewDir.y * maxDist;
        if (depth < 0.99999) { vec3 wPos = ReconstructWorldPos(uv, depth); dist = min(length(wPos - uPost.cameraPos.xyz), maxDist); sampleY = wPos.y; }
        float heightFactor = exp(-max(sampleY - uPost.fogParams0.w, 0.0) * max(uPost.fogParams0.z, 0.0));
        float fogAmt = clamp(1.0 - exp(-max(dist - uPost.fogParams1.x, 0.0) * max(uPost.fogParams0.y, 0.0) * heightFactor), 0.0, 1.0);
        if (depth >= 0.99999) fogAmt *= clamp(uPost.fogParams1.w, 0.0, 1.0) * smoothstep(0.35, -0.05, viewDir.y);
        float sunAmt = pow(max(dot(viewDir, normalize(uPost.sunDir.xyz)), 0.0), 8.0) * clamp(uPost.fogParams1.z, 0.0, 2.0);
        hdr = mix(hdr, mix(uPost.fogColor.rgb, uPost.sunColor.rgb, clamp(sunAmt, 0.0, 0.70)), clamp(fogAmt, 0.0, 1.0));
    }
    if ((uPost.bloomParams.x > 0.5 && uPost.bloomParams.y > 0.001) || (uPost.lensParams.x > 0.5 && uPost.lensParams.y > 0.001)) {
        vec3 bloom = max(textureLod(uBloomTex, uv, 0.0).rgb, vec3(0.0));
        if (!any(isnan(bloom)) && !any(isinf(bloom))) hdr += bloom;
    }
    hdr *= max(uPost.colorParams.x, 0.01);
    float temp = clamp(uPost.colorParams.w, -1.0, 1.0);
    hdr.r *= 1.0 + temp * 0.18;
    hdr.b *= 1.0 - temp * 0.18;
    hdr = max(hdr * uPost.gain.rgb + uPost.lift.rgb, vec3(0.0)) * uPost.colorFilter.rgb;
    int tm = int(uPost.lift.w + 0.5);
    vec3 mapped = (tm == 1) ? ReinhardTone(hdr) : ((tm == 2) ? FilmicTone(hdr) : ACESFilm(hdr));
    vec3 ldr = pow(mapped, vec3(1.0 / 2.2) / max(uPost.gamma.rgb, vec3(0.1)));
    ldr = clamp((ldr - vec3(0.5)) * uPost.colorParams.y + vec3(0.5), 0.0, 1.0);
    ldr = clamp(mix(vec3(dot(ldr, vec3(0.2126, 0.7152, 0.0722))), ldr, uPost.colorParams.z), 0.0, 1.0);
    if (uPost.gamma.w > 0.001) {
        float distC = length(uv - vec2(0.5)) * 1.41421356;
        ldr *= mix(1.0 - clamp(uPost.gamma.w, 0.0, 1.0), 1.0, smoothstep(1.05, clamp(0.85 - uPost.gain.w * 0.55, 0.05, 0.85), distC));
    }
    if (uPost.radialLetterbox.w > 0.001 && (uv.y < uPost.radialLetterbox.w || uv.y > 1.0 - uPost.radialLetterbox.w)) ldr = vec3(0.0);
    if (uPost.fogColor.w > 0.001 && (uv.x < uPost.fogColor.w || uv.x > 1.0 - uPost.fogColor.w)) ldr = vec3(0.0);
    if (uPost.flags.x != 0 && uPost.flags.y != 0) {
        if (texelFetch(uMetalEmissiveTex, pix, 0).z <= 0.5) {
            const ivec2 offsets[8] = ivec2[8](ivec2(2,0),ivec2(-2,0),ivec2(0,2),ivec2(0,-2),ivec2(2,2),ivec2(-2,2),ivec2(2,-2),ivec2(-2,-2));
            float edge = 0.0;
            for (int i = 0; i < 8; ++i) { if (texelFetch(uMetalEmissiveTex, clamp(pix + offsets[i], ivec2(0), size - ivec2(1)), 0).z > 0.5) { edge = 1.0; break; } }
            if (edge < 0.5) {
                const ivec2 innerOff[4] = ivec2[4](ivec2(1,0),ivec2(-1,0),ivec2(0,1),ivec2(0,-1));
                for (int i = 0; i < 4; ++i) { if (texelFetch(uMetalEmissiveTex, clamp(pix + innerOff[i], ivec2(0), size - ivec2(1)), 0).z > 0.5) { edge = 1.0; break; } }
            }
            if (edge > 0.5) ldr = mix(ldr, vec3(0.26, 0.59, 0.98), 0.95);
        }
    }
    imageStore(uLdrOut, pix, vec4(clamp(ldr, 0.0, 1.0), 1.0));
}
)GLSL";

std::string LoadShaderSourceWithFallback(const std::string& relPath, const std::string& fallbackSrc) {
    std::string text;
    if (SlipFileSystem::Exists(relPath) && SlipFileSystem::ReadAllText(relPath, text) && !text.empty()) {
        return text;
    }
    return fallbackSrc;
}

VkShaderModule CreateShaderModuleFromGlsl(VkDevice device, const std::string& glsl, SpirvShaderStage stage, const char* debugName) {
    SpirvCompileResult res = SlipSpirvCompiler::CompileGLSL(glsl, stage, debugName, true, "cache/spirv");
    if (!res.success || res.spirv.empty()) {
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = res.spirv.size() * sizeof(uint32_t);
    info.pCode = res.spirv.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &mod) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return mod;
}

} // namespace

VulkanSceneRenderer::~VulkanSceneRenderer() {
    Shutdown();
}

bool VulkanSceneRenderer::Initialize(VulkanContext* context) {
    if (m_Initialized) return true;
    m_Context = context;
    if (!m_Context || !m_Context->IsValid()) return false;

    if (!m_Bindless.Initialize(m_Context)) {
        Shutdown();
        return false;
    }

    if (!m_ASManager.Initialize(m_Context)) {
        Shutdown();
        return false;
    }

    if (!m_RtGI.Initialize(m_Context, m_Bindless.GetDescriptorSetLayout())) {
        Shutdown();
        return false;
    }

    if (!CreatePerFrameResources()) {
        Shutdown();
        return false;
    }

    if (!CreatePipelines()) {
        Shutdown();
        return false;
    }

    m_Initialized = true;
    return true;
}

void VulkanSceneRenderer::InvalidateSceneCache() {
    if (!m_Context || !m_Context->IsValid()) return;
    m_Context->WaitIdle();

    for (auto& kv : m_MeshCache) {
        for (auto& sm : kv.second.submeshes) {
            sm.blas.Destroy(m_Context);
        }
        kv.second.vertexBuffer.Destroy(m_Context);
        kv.second.indexBuffer.Destroy(m_Context);
    }
    m_MeshCache.clear();
    m_PrevModels.clear();
    m_HasPrevViewProj = false;
    m_RtGI.ResetAccumulation();
}

void VulkanSceneRenderer::Shutdown() {
    if (m_Context && m_Context->IsValid()) {
        m_Context->WaitIdle();
        VkDevice device = m_Context->GetDevice();

        for (auto& kv : m_MeshCache) {
            for (auto& sm : kv.second.submeshes) {
                sm.blas.Destroy(m_Context);
            }
            kv.second.vertexBuffer.Destroy(m_Context);
            kv.second.indexBuffer.Destroy(m_Context);
        }
        for (auto& kv : m_PrimitiveCache) {
            for (auto& sm : kv.second.submeshes) {
                sm.blas.Destroy(m_Context);
            }
            kv.second.vertexBuffer.Destroy(m_Context);
            kv.second.indexBuffer.Destroy(m_Context);
        }

        DestroyRenderTargets();
        m_RtGI.Shutdown(m_Context);
        m_ASManager.Shutdown(m_Context);

        if (m_ParticleAdditivePipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_ParticleAdditivePipeline, nullptr); m_ParticleAdditivePipeline = VK_NULL_HANDLE; }
        if (m_ParticleAlphaPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_ParticleAlphaPipeline, nullptr); m_ParticleAlphaPipeline = VK_NULL_HANDLE; }
        if (m_ParticlePipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_ParticlePipeLayout, nullptr); m_ParticlePipeLayout = VK_NULL_HANDLE; }
        m_ParticlePipelinesTried = false;

        for (uint32_t i = 0; i < kFrameSlots; ++i) {
            m_ParticleVertexBuffers[i].Destroy(m_Context);
            m_CameraUBOs[i].Destroy(m_Context);
            m_InstanceSSBOs[i].Destroy(m_Context);
            m_LightingUBOs[i].Destroy(m_Context);
            m_PostProcessUBOs[i].Destroy(m_Context);
        }

        if (m_GBufPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_GBufPipeline, nullptr); m_GBufPipeline = VK_NULL_HANDLE; }
        if (m_GBufPipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_GBufPipeLayout, nullptr); m_GBufPipeLayout = VK_NULL_HANDLE; }
        if (m_LightingPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_LightingPipeline, nullptr); m_LightingPipeline = VK_NULL_HANDLE; }
        if (m_LightingPipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_LightingPipeLayout, nullptr); m_LightingPipeLayout = VK_NULL_HANDLE; }
        if (m_BloomExtractPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_BloomExtractPipeline, nullptr); m_BloomExtractPipeline = VK_NULL_HANDLE; }
        if (m_BloomBlurPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_BloomBlurPipeline, nullptr); m_BloomBlurPipeline = VK_NULL_HANDLE; }
        if (m_BloomPipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_BloomPipeLayout, nullptr); m_BloomPipeLayout = VK_NULL_HANDLE; }
        if (m_TonemapPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_TonemapPipeline, nullptr); m_TonemapPipeline = VK_NULL_HANDLE; }
        if (m_TonemapPipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_TonemapPipeLayout, nullptr); m_TonemapPipeLayout = VK_NULL_HANDLE; }
        if (m_PtDepthPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_PtDepthPipeline, nullptr); m_PtDepthPipeline = VK_NULL_HANDLE; }
        if (m_PtDepthPipeLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_PtDepthPipeLayout, nullptr); m_PtDepthPipeLayout = VK_NULL_HANDLE; }

        if (m_Pool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, m_Pool, nullptr); m_Pool = VK_NULL_HANDLE; }
        if (m_GBufSceneLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_GBufSceneLayout, nullptr); m_GBufSceneLayout = VK_NULL_HANDLE; }
        if (m_LightingLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_LightingLayout, nullptr); m_LightingLayout = VK_NULL_HANDLE; }
        if (m_BloomLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_BloomLayout, nullptr); m_BloomLayout = VK_NULL_HANDLE; }
        if (m_TonemapLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_TonemapLayout, nullptr); m_TonemapLayout = VK_NULL_HANDLE; }
        if (m_PtDepthLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_PtDepthLayout, nullptr); m_PtDepthLayout = VK_NULL_HANDLE; }

        m_Bindless.Shutdown(m_Context);
    }

    m_MeshCache.clear();
    m_PrimitiveCache.clear();
    m_PrevModels.clear();
    m_SelectedEntity = nullptr;
    m_Context = nullptr;
    m_Initialized = false;
}

bool VulkanSceneRenderer::CreatePerFrameResources() {
    VkDevice device = m_Context->GetDevice();

    // 1. Descriptor Pool
    std::array<VkDescriptorPoolSize, 4> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 48 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 24 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 80 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32 }
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 32;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_Pool) != VK_SUCCESS) return false;

    // 2. Layouts
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_GBufSceneLayout) != VK_SUCCESS) return false;
    }

    {
        std::array<VkDescriptorSetLayoutBinding, 9> bindings{};
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[5] = { 5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[6] = { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[7] = { 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[8] = { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_LightingLayout) != VK_SUCCESS) return false;
    }

    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[2] = { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_BloomLayout) != VK_SUCCESS) return false;
    }

    {
        std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
        bindings[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[2] = { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[4] = { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        bindings[5] = { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_TonemapLayout) != VK_SUCCESS) return false;
    }
    {
        VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = 1;
        info.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_PtDepthLayout) != VK_SUCCESS) return false;
    }

    // 3. Allocate sets & buffers
    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        if (!m_CameraUBOs[i].Create(m_Context, sizeof(GpuCameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, false)) return false;
        if (!m_InstanceSSBOs[i].Create(m_Context, sizeof(GpuInstanceData) * kMaxInstances, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, false)) return false;
        if (!m_LightingUBOs[i].Create(m_Context, sizeof(GpuSceneLightingUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, false)) return false;
        if (!m_PostProcessUBOs[i].Create(m_Context, sizeof(GpuPostProcessUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, false)) return false;

        std::array<VkDescriptorSetLayout, 6> layouts = {
            m_GBufSceneLayout, m_LightingLayout, m_TonemapLayout, m_BloomLayout, m_BloomLayout, m_BloomLayout
        };
        std::array<VkDescriptorSet, 6> sets{};
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &allocInfo, sets.data()) != VK_SUCCESS) return false;

        m_GBufSceneSets[i] = sets[0];
        m_LightingSets[i] = sets[1];
        m_TonemapSets[i] = sets[2];
        m_BloomSets[i][0] = sets[3];
        m_BloomSets[i][1] = sets[4];
        m_BloomSets[i][2] = sets[5];
        m_ComputeDescriptorsDirty[i] = true;
        m_BloomDescriptorsDirty[i] = true;
        m_LastTonemapTargets[i] = VK_NULL_HANDLE;
        m_LastLightingGiViews[i] = VK_NULL_HANDLE;
        m_LastLightingReflViews[i] = VK_NULL_HANDLE;
        m_LastTonemapTargetGens[i] = 0;

        VkDescriptorSetAllocateInfo depthAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        depthAlloc.descriptorPool = m_Pool;
        depthAlloc.descriptorSetCount = 1;
        depthAlloc.pSetLayouts = &m_PtDepthLayout;
        if (vkAllocateDescriptorSets(device, &depthAlloc, &m_PtDepthSets[i]) != VK_SUCCESS) return false;

        VkDescriptorBufferInfo camInfo{ m_CameraUBOs[i].GetBuffer(), 0, sizeof(GpuCameraUBO) };
        VkDescriptorBufferInfo instInfo{ m_InstanceSSBOs[i].GetBuffer(), 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo lightInfo{ m_LightingUBOs[i].GetBuffer(), 0, sizeof(GpuSceneLightingUBO) };
        VkDescriptorBufferInfo giInfo{ m_RtGI.GetGiParamsBuffer(i), 0, sizeof(GpuGiParamsUBO) };
        VkDescriptorBufferInfo postInfo{ m_PostProcessUBOs[i].GetBuffer(), 0, sizeof(GpuPostProcessUBO) };

        std::array<VkWriteDescriptorSet, 8> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_GBufSceneSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &camInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_GBufSceneSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &instInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_LightingSets[i];
        writes[2].dstBinding = 5;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].pBufferInfo = &lightInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = m_LightingSets[i];
        writes[3].dstBinding = 7;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].pBufferInfo = &giInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = m_TonemapSets[i];
        writes[4].dstBinding = 2;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[4].pBufferInfo = &postInfo;

        for (int b = 0; b < 3; ++b) {
            writes[5 + b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[5 + b].dstSet = m_BloomSets[i][b];
            writes[5 + b].dstBinding = 2;
            writes[5 + b].descriptorCount = 1;
            writes[5 + b].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[5 + b].pBufferInfo = &postInfo;
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

bool VulkanSceneRenderer::CreatePipelines() {
    VkDevice device = m_Context->GetDevice();

    // 1. G-Buffer Pipeline (Vulkan 1.3 Dynamic Rendering)
    std::string gVertSrc = LoadShaderSourceWithFallback("shaders/vulkan/gbuffer.vert", kFallbackGBufferVert);
    std::string gFragSrc = LoadShaderSourceWithFallback("shaders/vulkan/gbuffer.frag", kFallbackGBufferFrag);
    VkShaderModule gVertMod = CreateShaderModuleFromGlsl(device, gVertSrc, SpirvShaderStage::Vertex, "gbuffer.vert");
    VkShaderModule gFragMod = CreateShaderModuleFromGlsl(device, gFragSrc, SpirvShaderStage::Fragment, "gbuffer.frag");
    if (gVertMod == VK_NULL_HANDLE || gFragMod == VK_NULL_HANDLE) {
        if (gVertMod) vkDestroyShaderModule(device, gVertMod, nullptr);
        if (gFragMod) vkDestroyShaderModule(device, gFragMod, nullptr);
        return false;
    }

    std::array<VkDescriptorSetLayout, 2> gbufSetLayouts = { m_Bindless.GetDescriptorSetLayout(), m_GBufSceneLayout };
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(uint32_t);

    VkPipelineLayoutCreateInfo gPipeLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    gPipeLayoutInfo.setLayoutCount = static_cast<uint32_t>(gbufSetLayouts.size());
    gPipeLayoutInfo.pSetLayouts = gbufSetLayouts.data();
    gPipeLayoutInfo.pushConstantRangeCount = 1;
    gPipeLayoutInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device, &gPipeLayoutInfo, nullptr, &m_GBufPipeLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, gVertMod, nullptr);
        vkDestroyShaderModule(device, gFragMod, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = gVertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = gFragMod;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 5> attrDescs{};
    attrDescs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Position)) };
    attrDescs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Normal)) };
    attrDescs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, TexCoords)) };
    attrDescs[3] = { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Tangent)) };
    attrDescs[4] = { 4, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, Bitangent)) };

    VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAsm{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    std::array<VkPipelineColorBlendAttachmentState, 4> blendAtts{};
    for (auto& b : blendAtts) {
        b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = static_cast<uint32_t>(blendAtts.size());
    blend.pAttachments = blendAtts.data();

    std::array<VkDynamicState, 2> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynInfo.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynInfo.pDynamicStates = dynStates.data();

    std::array<VkFormat, 4> colorFormats = {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16_SFLOAT
    };
    VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    VkGraphicsPipelineCreateInfo gpInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpInfo.pNext = &renderingInfo;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &vertexInput;
    gpInfo.pInputAssemblyState = &inputAsm;
    gpInfo.pViewportState = &viewportState;
    gpInfo.pRasterizationState = &raster;
    gpInfo.pMultisampleState = &msaa;
    gpInfo.pDepthStencilState = &depth;
    gpInfo.pColorBlendState = &blend;
    gpInfo.pDynamicState = &dynInfo;
    gpInfo.layout = m_GBufPipeLayout;

    VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_GBufPipeline);
    vkDestroyShaderModule(device, gVertMod, nullptr);
    vkDestroyShaderModule(device, gFragMod, nullptr);
    if (res != VK_SUCCESS) {
        std::cerr << "[VulkanSceneRenderer] vkCreateGraphicsPipelines fallo (VkResult=" << res << ")" << std::endl;
        return false;
    }

    // 2. Direct Lighting Compute Pipeline
    {
        std::string src = LoadShaderSourceWithFallback("shaders/vulkan/lighting_direct.comp", std::string(kFallbackLightingCompA) + kFallbackLightingCompB);
        VkShaderModule mod = CreateShaderModuleFromGlsl(device, src, SpirvShaderStage::Compute, "lighting_direct.comp");
        if (mod == VK_NULL_HANDLE) {
            src = std::string(kFallbackLightingCompA) + kFallbackLightingCompB;
            mod = CreateShaderModuleFromGlsl(device, src, SpirvShaderStage::Compute, "lighting_direct.comp.fallback");
        }
        if (mod == VK_NULL_HANDLE) return false;

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_LightingLayout;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_LightingPipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = mod;
        cpInfo.stage.pName = "main";
        cpInfo.layout = m_LightingPipeLayout;

        VkResult cRes = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_LightingPipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (cRes != VK_SUCCESS) return false;
    }

    // 2B. Bloom Extract & Separable Blur Compute Pipelines
    {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(int32_t) * 2;

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_BloomLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_BloomPipeLayout) != VK_SUCCESS) {
            return false;
        }

        {
            std::string src = LoadShaderSourceWithFallback("shaders/vulkan/bloom_extract.comp", kFallbackBloomExtractComp);
            VkShaderModule mod = CreateShaderModuleFromGlsl(device, src, SpirvShaderStage::Compute, "bloom_extract.comp");
            if (mod == VK_NULL_HANDLE) return false;

            VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpInfo.stage.module = mod;
            cpInfo.stage.pName = "main";
            cpInfo.layout = m_BloomPipeLayout;
            VkResult cRes = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_BloomExtractPipeline);
            vkDestroyShaderModule(device, mod, nullptr);
            if (cRes != VK_SUCCESS) return false;
        }

        {
            std::string src = LoadShaderSourceWithFallback("shaders/vulkan/bloom_blur.comp", kFallbackBloomBlurComp);
            VkShaderModule mod = CreateShaderModuleFromGlsl(device, src, SpirvShaderStage::Compute, "bloom_blur.comp");
            if (mod == VK_NULL_HANDLE) return false;

            VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpInfo.stage.module = mod;
            cpInfo.stage.pName = "main";
            cpInfo.layout = m_BloomPipeLayout;
            VkResult cRes = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_BloomBlurPipeline);
            vkDestroyShaderModule(device, mod, nullptr);
            if (cRes != VK_SUCCESS) return false;
        }
    }

    // 3. Tone Mapping & Post-Process Composite Pipeline
    {
        std::string src = LoadShaderSourceWithFallback("shaders/vulkan/tonemap.comp", kFallbackTonemapComp);
        VkShaderModule mod = CreateShaderModuleFromGlsl(device, src, SpirvShaderStage::Compute, "tonemap.comp");
        if (mod == VK_NULL_HANDLE) {
            src = kFallbackTonemapComp;
            mod = CreateShaderModuleFromGlsl(device, src, SpirvShaderStage::Compute, "tonemap.comp.fallback");
        }
        if (mod == VK_NULL_HANDLE) return false;

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_TonemapLayout;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_TonemapPipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = mod;
        cpInfo.stage.pName = "main";
        cpInfo.layout = m_TonemapPipeLayout;

        VkResult cRes = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_TonemapPipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (cRes != VK_SUCCESS) return false;
    }

    if (!CreatePtDepthPipeline()) return false;
    return true;
}

bool VulkanSceneRenderer::CreatePtDepthPipeline() {
    VkDevice device = m_Context->GetDevice();
    const char* vertSrc = R"GLSL(#version 460
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
    const char* fragSrc = R"GLSL(#version 460
layout(set = 0, binding = 0) uniform sampler2D uDepth;
void main() {
    float z = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).b;
    gl_FragDepth = clamp(z, 0.0, 1.0);
}
)GLSL";
    VkShaderModule vert = CreateShaderModuleFromGlsl(device, vertSrc, SpirvShaderStage::Vertex, "pt_depth.vert");
    VkShaderModule frag = CreateShaderModuleFromGlsl(device, fragSrc, SpirvShaderStage::Fragment, "pt_depth.frag");
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        if (frag) vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_PtDepthLayout;
    if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_PtDepthPipeLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAsm{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo msaa{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth.depthTestEnable = VK_FALSE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    std::array<VkDynamicState, 2> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynInfo.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynInfo.pDynamicStates = dynStates.data();
    VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo gp{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gp.pNext = &renderingInfo;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vertexInput;
    gp.pInputAssemblyState = &inputAsm;
    gp.pViewportState = &viewportState;
    gp.pRasterizationState = &raster;
    gp.pMultisampleState = &msaa;
    gp.pDepthStencilState = &depth;
    gp.pColorBlendState = &blend;
    gp.pDynamicState = &dynInfo;
    gp.layout = m_PtDepthPipeLayout;
    const bool ok = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_PtDepthPipeline) == VK_SUCCESS;
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    if (!ok) std::cerr << "[VulkanSceneRenderer] No se pudo crear el pipeline de profundidad del path tracing" << std::endl;
    return ok;
}

bool VulkanSceneRenderer::ResolvePathTracingDepth(VkCommandBuffer cmd, uint32_t slot, uint32_t width, uint32_t height) {
    const VulkanTexture& src = m_RtGI.GetPtDepthInst(m_RtGI.GetPtCurrPingPongIndex());
    if (!src.IsValid() || m_PtDepthPipeline == VK_NULL_HANDLE || slot >= kFrameSlots) return false;

    VkImageMemoryBarrier depthBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    depthBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthBarrier.image = src.GetImage();
    depthBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &depthBarrier);

    VkDescriptorImageInfo img{ src.GetSampler(), src.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_PtDepthSets[slot];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &img;
    vkUpdateDescriptorSets(m_Context->GetDevice(), 1, &write, 0, nullptr);

    VulkanTexture::TransitionImageLayout(cmd, m_GBufDepth.GetImage(), m_GBufDepthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_GBufDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkRenderingAttachmentInfo depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAtt.imageView = m_GBufDepth.GetImageView();
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderInfo.renderArea = { { 0, 0 }, { width, height } };
    renderInfo.layerCount = 1;
    renderInfo.pDepthAttachment = &depthAtt;
    vkCmdBeginRendering(cmd, &renderInfo);
    VkViewport vp{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    VkRect2D scissor{ { 0, 0 }, { width, height } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PtDepthPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PtDepthPipeLayout, 0, 1, &m_PtDepthSets[slot], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);

    VulkanTexture::TransitionImageLayout(cmd, m_GBufDepth.GetImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_GBufDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return true;
}

bool VulkanSceneRenderer::EnsureRenderTargets(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;
    if (!m_RtGI.EnsureSize(m_Context, width, height)) return false;
    if (m_TargetsInitialized && m_Width == width && m_Height == height) {
        return true;
    }

    m_Context->WaitIdle();
    DestroyRenderTargets();

    const VkImageUsageFlags gbufUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!m_GBufAlbedo.CreateImage2D(m_Context, width, height, VK_FORMAT_R8G8B8A8_UNORM, gbufUsage, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_GBufNormalRough.CreateImage2D(m_Context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, gbufUsage, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_GBufMetalEmissive.CreateImage2D(m_Context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, gbufUsage, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_GBufMotion.CreateImage2D(m_Context, width, height, VK_FORMAT_R16G16_SFLOAT, gbufUsage, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_GBufDepth.CreateImage2D(m_Context, width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_HdrColor.CreateImage2D(m_Context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;

    const uint32_t bloomW = (std::max)(1u, width / 2u);
    const uint32_t bloomH = (std::max)(1u, height / 2u);
    if (!m_BloomPing.CreateImage2D(m_Context, bloomW, bloomH, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_BloomPong.CreateImage2D(m_Context, bloomW, bloomH, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;

    m_Width = width;
    m_Height = height;
    m_TargetsInitialized = true;
    m_BloomInitialized = false;
    m_GBufDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        m_ComputeDescriptorsDirty[i] = true;
        m_BloomDescriptorsDirty[i] = true;
        m_LastTonemapTargets[i] = VK_NULL_HANDLE;
        m_LastLightingGiViews[i] = VK_NULL_HANDLE;
        m_LastLightingReflViews[i] = VK_NULL_HANDLE;
    }
    return true;
}

void VulkanSceneRenderer::DestroyRenderTargets() {
    m_RtGI.DestroyTargets(m_Context);
    if (m_Context && m_Context->IsValid()) {
        m_GBufAlbedo.Destroy(m_Context);
        m_GBufNormalRough.Destroy(m_Context);
        m_GBufMetalEmissive.Destroy(m_Context);
        m_GBufMotion.Destroy(m_Context);
        m_GBufDepth.Destroy(m_Context);
        m_HdrColor.Destroy(m_Context);
        m_BloomPing.Destroy(m_Context);
        m_BloomPong.Destroy(m_Context);
    }
    m_Width = 0;
    m_Height = 0;
    m_TargetsInitialized = false;
    m_BloomInitialized = false;
    m_GBufDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanSceneRenderer::UpdateComputeDescriptors(uint32_t slot, VkImageView tonemapTargetView, uint64_t targetGeneration) {
    VkDevice device = m_Context->GetDevice();

    if (m_ComputeDescriptorsDirty[slot]) {
        VkDescriptorImageInfo albedoInfo{ m_GBufAlbedo.GetSampler(), m_GBufAlbedo.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo normInfo{ m_GBufNormalRough.GetSampler(), m_GBufNormalRough.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo metalInfo{ m_GBufMetalEmissive.GetSampler(), m_GBufMetalEmissive.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo depthInfo{ m_GBufDepth.GetSampler(), m_GBufDepth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo hdrOutInfo{ VK_NULL_HANDLE, m_HdrColor.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };

        std::array<VkWriteDescriptorSet, 5> writes{};
        for (int b = 0; b < 5; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = m_LightingSets[slot];
            writes[b].dstBinding = static_cast<uint32_t>(b);
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = (b == 4) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        }
        writes[0].pImageInfo = &albedoInfo;
        writes[1].pImageInfo = &normInfo;
        writes[2].pImageInfo = &metalInfo;
        writes[3].pImageInfo = &depthInfo;
        writes[4].pImageInfo = &hdrOutInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        m_ComputeDescriptorsDirty[slot] = false;
    }

    if (m_BloomDescriptorsDirty[slot]) {
        VkDescriptorImageInfo hdrSamp{ m_HdrColor.GetSampler(), m_HdrColor.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo pingImg{ VK_NULL_HANDLE, m_BloomPing.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo pingSamp{ m_BloomPing.GetSampler(), m_BloomPing.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo pongImg{ VK_NULL_HANDLE, m_BloomPong.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo pongSamp{ m_BloomPong.GetSampler(), m_BloomPong.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        std::array<VkWriteDescriptorSet, 6> bWrites{};
        auto fillImgWrite = [](VkWriteDescriptorSet& w, VkDescriptorSet dst, uint32_t bnd, VkDescriptorType type, const VkDescriptorImageInfo* info) {
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = dst;
            w.dstBinding = bnd;
            w.descriptorCount = 1;
            w.descriptorType = type;
            w.pImageInfo = info;
        };
        fillImgWrite(bWrites[0], m_BloomSets[slot][0], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hdrSamp);
        fillImgWrite(bWrites[1], m_BloomSets[slot][0], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &pingImg);
        fillImgWrite(bWrites[2], m_BloomSets[slot][1], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &pingSamp);
        fillImgWrite(bWrites[3], m_BloomSets[slot][1], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &pongImg);
        fillImgWrite(bWrites[4], m_BloomSets[slot][2], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &pongSamp);
        fillImgWrite(bWrites[5], m_BloomSets[slot][2], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &pingImg);

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(bWrites.size()), bWrites.data(), 0, nullptr);
        m_BloomDescriptorsDirty[slot] = false;
    }

    if ((m_LastTonemapTargets[slot] != tonemapTargetView || m_LastTonemapTargetGens[slot] != targetGeneration) &&
        tonemapTargetView != VK_NULL_HANDLE)
    {
        VkDescriptorImageInfo hdrInInfo{ m_HdrColor.GetSampler(), m_HdrColor.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo ldrOutInfo{ VK_NULL_HANDLE, tonemapTargetView, VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo metalInfo{ m_GBufMetalEmissive.GetSampler(), m_GBufMetalEmissive.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo depthInfo{ m_GBufDepth.GetSampler(), m_GBufDepth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo bloomInfo{ m_BloomPing.GetSampler(), m_BloomPing.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        std::array<VkWriteDescriptorSet, 5> writes{};
        auto fillTM = [](VkWriteDescriptorSet& w, VkDescriptorSet dst, uint32_t bnd, VkDescriptorType type, const VkDescriptorImageInfo* info) {
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = dst;
            w.dstBinding = bnd;
            w.descriptorCount = 1;
            w.descriptorType = type;
            w.pImageInfo = info;
        };
        fillTM(writes[0], m_TonemapSets[slot], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hdrInInfo);
        fillTM(writes[1], m_TonemapSets[slot], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ldrOutInfo);
        fillTM(writes[2], m_TonemapSets[slot], 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &metalInfo);
        fillTM(writes[3], m_TonemapSets[slot], 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthInfo);
        fillTM(writes[4], m_TonemapSets[slot], 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &bloomInfo);

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        m_LastTonemapTargets[slot] = tonemapTargetView;
        m_LastTonemapTargetGens[slot] = targetGeneration;
    }
}

const VulkanSceneRenderer::GpuMesh* VulkanSceneRenderer::GetOrUploadMesh(SlipMesh* mesh) {
    if (!mesh) return nullptr;
    auto it = m_MeshCache.find(mesh);
    if (it != m_MeshCache.end()) {
        return &it->second;
    }

    std::vector<Vertex> allVerts;
    std::vector<uint32_t> allIndices;
    std::vector<SubmeshRange> ranges;

    const auto& unifiedVerts = mesh->GetUnifiedVertices();
    const auto& unifiedIndices = mesh->GetUnifiedIndices();

    if (!unifiedVerts.empty() && !unifiedIndices.empty()) {
        allVerts = unifiedVerts;
        allIndices = unifiedIndices;
        for (const auto& sm : mesh->meshes) {
            if (sm.indexCount == 0) continue;
            ranges.push_back({ sm.firstIndex, sm.indexCount, 0, sm.materialIndex });
        }
    } else {
        uint32_t vOffset = 0;
        uint32_t iOffset = 0;
        for (const auto& sm : mesh->meshes) {
            if (sm.vertices.empty() || sm.indices.empty()) continue;
            allVerts.insert(allVerts.end(), sm.vertices.begin(), sm.vertices.end());
            allIndices.insert(allIndices.end(), sm.indices.begin(), sm.indices.end());
            ranges.push_back({ iOffset, static_cast<uint32_t>(sm.indices.size()), static_cast<int32_t>(vOffset), sm.materialIndex });
            vOffset += static_cast<uint32_t>(sm.vertices.size());
            iOffset += static_cast<uint32_t>(sm.indices.size());
        }
    }

    if (allVerts.empty() || allIndices.empty() || ranges.empty()) {
        return nullptr;
    }

    VkBufferUsageFlags extraFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (m_Context->GetCapabilities().supportsRayTracing) {
        extraFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    GpuMesh gpuMesh{};
    if (!gpuMesh.vertexBuffer.CreateDeviceLocalWithData(
            m_Context, allVerts.data(), allVerts.size() * sizeof(Vertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | extraFlags))
    {
        return nullptr;
    }
    if (!gpuMesh.indexBuffer.CreateDeviceLocalWithData(
            m_Context, allIndices.data(), allIndices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | extraFlags))
    {
        gpuMesh.vertexBuffer.Destroy(m_Context);
        return nullptr;
    }
    gpuMesh.vertexCount = static_cast<uint32_t>(allVerts.size());
    gpuMesh.submeshes = std::move(ranges);
    if (m_Context->GetCapabilities().supportsRayQuery) {
        for (auto& sm : gpuMesh.submeshes) {
            m_ASManager.BuildBLAS(
                m_Context,
                gpuMesh.vertexBuffer,
                gpuMesh.vertexCount,
                gpuMesh.indexBuffer,
                sm.firstIndex,
                sm.indexCount,
                sm.vertexOffset,
                sizeof(Vertex),
                static_cast<uint32_t>(offsetof(Vertex, Position)),
                sm.blas
            );
        }
    }

    auto inserted = m_MeshCache.emplace(mesh, std::move(gpuMesh));
    return &inserted.first->second;
}

const VulkanSceneRenderer::GpuMesh* VulkanSceneRenderer::GetOrUploadPrimitive(int primitiveType) {
    auto it = m_PrimitiveCache.find(primitiveType);
    if (it != m_PrimitiveCache.end()) {
        return &it->second;
    }

    std::vector<Vertex> verts;
    std::vector<unsigned int> indices;
    PrimitiveType pType = static_cast<PrimitiveType>(primitiveType);
    switch (pType) {
        case PrimitiveType::Cube:   SlipPrimitive::GenerateCube(1.0f, verts, indices); break;
        case PrimitiveType::Sphere: SlipPrimitive::GenerateSphere(0.5f, 24, 24, verts, indices); break;
        case PrimitiveType::Plane:  SlipPrimitive::GeneratePlane(4.0f, 4.0f, verts, indices); break;
        default:                    SlipPrimitive::GenerateCube(1.0f, verts, indices); break;
    }

    if (verts.empty() || indices.empty()) return nullptr;

    VkBufferUsageFlags extraFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (m_Context->GetCapabilities().supportsRayTracing) {
        extraFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    GpuMesh gpuMesh{};
    if (!gpuMesh.vertexBuffer.CreateDeviceLocalWithData(
            m_Context, verts.data(), verts.size() * sizeof(Vertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | extraFlags))
    {
        return nullptr;
    }
    if (!gpuMesh.indexBuffer.CreateDeviceLocalWithData(
            m_Context, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | extraFlags))
    {
        gpuMesh.vertexBuffer.Destroy(m_Context);
        return nullptr;
    }
    gpuMesh.vertexCount = static_cast<uint32_t>(verts.size());
    SubmeshRange smRange{};
    smRange.firstIndex = 0;
    smRange.indexCount = static_cast<uint32_t>(indices.size());
    smRange.vertexOffset = 0;
    smRange.materialIndex = 0;
    gpuMesh.submeshes.push_back(std::move(smRange));
    if (m_Context->GetCapabilities().supportsRayQuery) {
        auto& sm = gpuMesh.submeshes[0];
        m_ASManager.BuildBLAS(
            m_Context,
            gpuMesh.vertexBuffer,
            gpuMesh.vertexCount,
            gpuMesh.indexBuffer,
            sm.firstIndex,
            sm.indexCount,
            sm.vertexOffset,
            sizeof(Vertex),
            static_cast<uint32_t>(offsetof(Vertex, Position)),
            sm.blas
        );
    }

    auto inserted = m_PrimitiveCache.emplace(primitiveType, std::move(gpuMesh));
    return &inserted.first->second;
}

namespace {
const char* kParticleVert = R"GLSL(#version 460
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aShape;
layout(push_constant) uniform PC { mat4 viewProj; } pc;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vShape;
void main() {
    vUV = aUV; vColor = aColor; vShape = aShape;
    gl_Position = pc.viewProj * vec4(aPos, 1.0);
}
)GLSL";

const char* kParticleFrag = R"GLSL(#version 460
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vShape;
layout(location = 0) out vec4 outColor;
void main() {
    vec2 c = vUV * 2.0 - 1.0;
    float d = length(c);
    int shape = int(vShape + 0.5);
    vec3 rgb = vColor.rgb;
    float a = vColor.a;
    if (shape == 0) {            // Soft circle
        a *= 1.0 - smoothstep(0.0, 1.0, d);
    } else if (shape == 1) {     // Lit sphere (fake shading)
        if (d > 1.0) discard;
        vec3 n = vec3(c.x, -c.y, sqrt(max(1.0 - d * d, 0.0)));
        float l = 0.35 + 0.65 * max(dot(n, normalize(vec3(0.4, 0.6, 0.7))), 0.0);
        rgb *= l;
        a *= 1.0 - smoothstep(0.85, 1.0, d);
    }                            // shape 2: square, full coverage
    if (a <= 0.001) discard;
    outColor = vec4(rgb, a);
}
)GLSL";

struct ParticleGpuVertex {
    glm::vec3 pos;
    glm::vec2 uv;
    glm::vec4 color;
    float shape;
};
} // namespace

bool VulkanSceneRenderer::CreateParticlePipelines() {
    if (m_ParticlePipelinesTried) return m_ParticleAdditivePipeline != VK_NULL_HANDLE && m_ParticleAlphaPipeline != VK_NULL_HANDLE;
    m_ParticlePipelinesTried = true;

    VkDevice device = m_Context->GetDevice();
    VkShaderModule vert = CreateShaderModuleFromGlsl(device, kParticleVert, SpirvShaderStage::Vertex, "particle.vert");
    VkShaderModule frag = CreateShaderModuleFromGlsl(device, kParticleFrag, SpirvShaderStage::Fragment, "particle.frag");
    if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE) {
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        if (frag) vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkPushConstantRange pc{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) };
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_ParticlePipeLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{ 0, sizeof(ParticleGpuVertex), VK_VERTEX_INPUT_RATE_VERTEX };
    std::array<VkVertexInputAttributeDescription, 4> attrs{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(ParticleGpuVertex, pos)) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(ParticleGpuVertex, uv)) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(ParticleGpuVertex, color)) };
    attrs[3] = { 3, 0, VK_FORMAT_R32_SFLOAT, static_cast<uint32_t>(offsetof(ParticleGpuVertex, shape)) };

    VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAsm{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    std::array<VkDynamicState, 2> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynInfo.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynInfo.pDynamicStates = dynStates.data();

    VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    auto build = [&](bool additive, VkPipeline& out) {
        VkPipelineColorBlendAttachmentState att{};
        att.blendEnable = VK_TRUE;
        att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        att.dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.colorBlendOp = VK_BLEND_OP_ADD;
        att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.alphaBlendOp = VK_BLEND_OP_ADD;
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &att;

        VkGraphicsPipelineCreateInfo gp{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gp.pNext = &renderingInfo;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vertexInput;
        gp.pInputAssemblyState = &inputAsm;
        gp.pViewportState = &viewportState;
        gp.pRasterizationState = &raster;
        gp.pMultisampleState = &msaa;
        gp.pDepthStencilState = &depth;
        gp.pColorBlendState = &blend;
        gp.pDynamicState = &dynInfo;
        gp.layout = m_ParticlePipeLayout;
        return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &out) == VK_SUCCESS;
    };

    const bool ok = build(true, m_ParticleAdditivePipeline) && build(false, m_ParticleAlphaPipeline);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    if (!ok) std::cerr << "[VulkanSceneRenderer] No se pudo crear el pipeline de particulas" << std::endl;
    return ok;
}

bool VulkanSceneRenderer::RenderParticles(
    VkCommandBuffer cmd, uint32_t slot, SlipLevel* level, const glm::mat4& view,
    const glm::mat4& viewProj, const glm::vec3& cameraPos, uint32_t width, uint32_t height)
{
    if (!level) return false;

    struct Emitter { ParticleSystemComponent* ps; glm::mat4 world; float distSq; };
    std::vector<Emitter> emitters;
    for (SlipEntity* ent : level->GetEntities()) {
        if (!ent) continue;
        auto ps = ent->GetComponent<ParticleSystemComponent>();
        if (!ps || !ps->IsEnabled() || ps->GetParticles().empty()) continue;
        glm::mat4 world = ent->getMatrix();
        glm::vec3 center = glm::vec3(world[3]);
        emitters.push_back({ ps.get(), world, glm::dot(center - cameraPos, center - cameraPos) });
    }
    if (emitters.empty()) return false;
    std::sort(emitters.begin(), emitters.end(), [](const Emitter& a, const Emitter& b) { return a.distSq > b.distSq; });

    const glm::mat4 invView = glm::inverse(view);
    const glm::vec3 camRight = glm::normalize(glm::vec3(invView[0]));
    const glm::vec3 camUp = glm::normalize(glm::vec3(invView[1]));

    struct Batch { uint32_t first; uint32_t count; bool additive; };
    std::vector<ParticleGpuVertex> verts;
    std::vector<Batch> batches;

    for (const auto& e : emitters) {
        ParticleSystemComponent* ps = e.ps;
        const bool additive = (ps->blendMode == ParticleBlendMode::Additive);
        const bool pointMode = (ps->renderMode == ParticleRenderMode::PointNiagara);
        float shape = 0.0f; // soft circle
        if (pointMode) {
            shape = (ps->pointShape == ParticlePointShape::LitSphere) ? 1.0f
                  : (ps->pointShape == ParticlePointShape::Square) ? 2.0f : 0.0f;
        }
        glm::vec3 emissive(0.0f);
        if (ps->materialMode == ParticleMaterialMode::Emissive) {
            emissive = ps->emissiveColor * ps->emissiveIntensity;
        }

        const uint32_t batchStart = static_cast<uint32_t>(verts.size());
        for (const auto& p : ps->GetParticles()) {
            if (verts.size() + 6 > kMaxParticleVertices) break;
            glm::vec3 wPos = ps->simulationSpaceLocal ? glm::vec3(e.world * glm::vec4(p.position, 1.0f)) : p.position;

            float size = pointMode ? p.size * ps->pointBaseScale : p.size;
            float rad = pointMode ? 0.0f : glm::radians(p.rotation);
            float c = std::cos(rad), s = std::sin(rad);
            float half = size * 0.5f;
            glm::vec3 rAxis = (camRight * c + camUp * s) * half;
            glm::vec3 uAxis = (-camRight * s + camUp * c) * half;

            glm::vec3 rgb = glm::vec3(p.color);
            rgb = rgb + emissive * rgb;
            glm::vec4 col(rgb, p.color.a);

            glm::vec3 p0 = wPos - rAxis - uAxis;
            glm::vec3 p1 = wPos + rAxis - uAxis;
            glm::vec3 p2 = wPos + rAxis + uAxis;
            glm::vec3 p3 = wPos - rAxis + uAxis;
            verts.push_back({ p0, { 0, 0 }, col, shape });
            verts.push_back({ p1, { 1, 0 }, col, shape });
            verts.push_back({ p2, { 1, 1 }, col, shape });
            verts.push_back({ p0, { 0, 0 }, col, shape });
            verts.push_back({ p2, { 1, 1 }, col, shape });
            verts.push_back({ p3, { 0, 1 }, col, shape });
        }
        const uint32_t count = static_cast<uint32_t>(verts.size()) - batchStart;
        if (count > 0) {
            if (!batches.empty() && batches.back().additive == additive) batches.back().count += count;
            else batches.push_back({ batchStart, count, additive });
        }
    }
    if (verts.empty()) return false;
    if (!CreateParticlePipelines()) return false;

    VulkanBuffer& vb = m_ParticleVertexBuffers[slot];
    if (!vb.IsValid()) {
        if (!vb.Create(m_Context, sizeof(ParticleGpuVertex) * kMaxParticleVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true)) return false;
    }
    vb.Upload(m_Context, verts.data(), verts.size() * sizeof(ParticleGpuVertex), 0);

    // HDR (GENERAL tras el compute de iluminacion) -> color attachment; depth -> attachment (solo lectura via depthWrite off)
    VulkanTexture::TransitionImageLayout(cmd, m_HdrColor.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufDepth.GetImage(), m_GBufDepthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_GBufDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkRenderingAttachmentInfo colorAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAtt.imageView = m_HdrColor.GetImageView();
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAtt.imageView = m_GBufDepth.GetImageView();
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderInfo.renderArea = { { 0, 0 }, { width, height } };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAtt;
    renderInfo.pDepthAttachment = &depthAtt;

    vkCmdBeginRendering(cmd, &renderInfo);
    VkViewport vp{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    VkRect2D scissor{ { 0, 0 }, { width, height } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkBuffer buf = vb.GetBuffer();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buf, &off);
    vkCmdPushConstants(cmd, m_ParticlePipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &viewProj);
    for (const auto& b : batches) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b.additive ? m_ParticleAdditivePipeline : m_ParticleAlphaPipeline);
        vkCmdDraw(cmd, b.count, 1, b.first, 0);
    }
    vkCmdEndRendering(cmd);

    VulkanTexture::TransitionImageLayout(cmd, m_HdrColor.GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufDepth.GetImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_GBufDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_LastDrawCallCount += static_cast<uint32_t>(batches.size());
    return true;
}

bool VulkanSceneRenderer::RenderScene(
    VkCommandBuffer cmd,
    uint32_t frameIndex,
    SlipLevel* level,
    const glm::mat4& view,
    const glm::mat4& glProjection,
    const glm::vec3& cameraPos,
    VkImage targetImage,
    VkImageView targetView,
    uint32_t width,
    uint32_t height,
    VkImageLayout targetInitialLayout,
    VkImageLayout targetFinalLayout,
    uint64_t targetGeneration)
{
    if (!m_Initialized || cmd == VK_NULL_HANDLE || targetImage == VK_NULL_HANDLE || targetView == VK_NULL_HANDLE) {
        return false;
    }
    if (width == 0 || height == 0) return false;
    if (!EnsureRenderTargets(width, height)) return false;

    const uint32_t slot = frameIndex % kFrameSlots;

    // Vulkan clip-space correction: invert Y and map Z from [-1, 1] to [0, 1]
    const glm::mat4 clipCorr(
        1.0f,  0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f,  0.0f, 0.5f, 0.0f,
        0.0f,  0.0f, 0.5f, 1.0f
    );
    glm::mat4 vkProj = clipCorr * glProjection;
    glm::mat4 viewProj = vkProj * view;
    glm::mat4 prevViewProj = m_HasPrevViewProj ? m_PrevViewProj : viewProj;
    glm::mat4 invViewProj = glm::inverse(viewProj);

    GpuCameraUBO camUbo{};
    camUbo.view = view;
    camUbo.proj = vkProj;
    camUbo.viewProj = viewProj;
    camUbo.prevViewProj = prevViewProj;
    camUbo.invViewProj = invViewProj;
    camUbo.cameraPos = glm::vec4(cameraPos, 1.0f);
    m_CameraUBOs[slot].Upload(m_Context, &camUbo, sizeof(camUbo), 0);

    m_PrevViewProj = viewProj;
    m_HasPrevViewProj = true;

    // Collect draw calls, materials, and lights from level
    m_Bindless.BeginFrameMaterials();
    std::vector<GpuInstanceData> instances;
    std::vector<DrawCall> drawCalls;
    std::vector<RtInstanceInput> rtInstances;
    const bool canRayTrace = m_Context->GetCapabilities().supportsRayQuery && m_RtGI.IsRayQueryReady();

    GpuSceneLightingUBO lightUbo{};
    lightUbo.invViewProj = invViewProj;
    lightUbo.cameraPos = glm::vec4(cameraPos, 1.0f);
    lightUbo.currViewProj = viewProj;
    lightUbo.prevViewProj = prevViewProj;
    lightUbo.view = view;

    const auto& gfx = SlipGraphicsSettings::Get().GetData();
    lightUbo.skyZenith = glm::vec4(gfx.skyZenithColor, gfx.skyEnabled ? 1.0f : 0.0f);
    lightUbo.skyHorizon = glm::vec4(gfx.skyHorizonColor, gfx.skyHorizonSharpness);
    lightUbo.skyGround = glm::vec4(gfx.skyGroundColor, gfx.sunDiscEnabled ? 1.0f : 0.0f);
    lightUbo.sunColorParams = glm::vec4(gfx.sunColor, gfx.sunSize);
    lightUbo.sunDirIntensity = glm::vec4(glm::normalize(glm::vec3(0.35f, 0.85f, 0.40f)), gfx.sunIntensity);

    int dirCount = 0, pointCount = 0, spotCount = 0;

    if (level) {
        const auto& entities = level->GetEntities();
        for (SlipEntity* ent : entities) {
            if (!ent) continue;

            if (auto lc = ent->GetComponent<LightComponent>()) {
                if (lc->IsEnabled()) {
                    glm::mat4 m = ent->getMatrix();
                    glm::vec3 worldPos = glm::vec3(m[3]);
                    glm::vec3 worldDir = glm::normalize(glm::vec3(m * glm::vec4(0.0f, -1.0f, -0.5f, 0.0f)));
                    if (lc->type == LightType::Directional && dirCount < 4) {
                        lightUbo.dirLights[dirCount].directionIntensity = glm::vec4(worldDir, lc->intensity);
                        lightUbo.dirLights[dirCount].colorPad = glm::vec4(lc->color, 1.0f);
                        if (dirCount == 0) {
                            lightUbo.sunDirIntensity = glm::vec4(-worldDir, gfx.sunIntensity);
                        }
                        ++dirCount;
                    } else if (lc->type == LightType::Point && pointCount < 32) {
                        lightUbo.pointLights[pointCount].positionRange = glm::vec4(worldPos, lc->range);
                        lightUbo.pointLights[pointCount].colorIntensity = glm::vec4(lc->color, lc->intensity);
                        ++pointCount;
                    } else if (lc->type == LightType::Spot && spotCount < 16) {
                        float cosOuter = std::cos(glm::radians(lc->spotAngle));
                        lightUbo.spotLights[spotCount].positionRange = glm::vec4(worldPos, lc->range);
                        lightUbo.spotLights[spotCount].directionCosOuter = glm::vec4(worldDir, cosOuter);
                        lightUbo.spotLights[spotCount].colorIntensityCosIn = glm::vec4(lc->color, lc->intensity);
                        ++spotCount;
                    }
                }
            }

            glm::mat4 model = ent->getMatrix();
            glm::mat4 prevModel = model;
            auto prevIt = m_PrevModels.find(ent);
            if (prevIt != m_PrevModels.end()) prevModel = prevIt->second;
            m_PrevModels[ent] = model;

            const uint32_t selFlag = (ent == m_SelectedEntity) ? 1u : 0u;

            if (auto mc = ent->GetComponent<MeshComponent>()) {
                if (mc->IsEnabled() && mc->model) {
                    const GpuMesh* gpuMesh = GetOrUploadMesh(mc->model);
                    if (gpuMesh) {
                        for (const auto& sm : gpuMesh->submeshes) {
                            if (instances.size() >= kMaxInstances) break;
                            size_t matSlot = static_cast<size_t>(std::max(0, sm.materialIndex));
                            SlipMaterial* mat = nullptr;
                            if (matSlot < mc->submeshMaterials.size() && mc->submeshMaterials[matSlot] != nullptr) {
                                mat = mc->submeshMaterials[matSlot];
                            } else if (matSlot == 0 && mc->material != nullptr) {
                                mat = mc->material;
                            } else {
                                mat = mc->GetSubmeshMaterial(matSlot);
                            }
                            if (!mat) mat = mc->material;

                            uint32_t matIdx = m_Bindless.PushSlipMaterial(m_Context, mat);
                            uint32_t instIdx = static_cast<uint32_t>(instances.size());
                            instances.push_back({ model, prevModel, matIdx, selFlag, 0, 0 });
                            drawCalls.push_back({ gpuMesh, sm.firstIndex, sm.indexCount, sm.vertexOffset, instIdx });
                            if (canRayTrace && sm.blas.IsValid()) {
                                rtInstances.push_back({ &sm.blas, model, prevModel, matIdx, sm.firstIndex, sm.vertexOffset, static_cast<uint32_t>(sizeof(Vertex) / sizeof(float)) });
                            }
                        }
                    }
                }
            } else if (auto prim = dynamic_cast<SlipPrimitiveEntity*>(ent)) {
                const GpuMesh* gpuMesh = GetOrUploadPrimitive(static_cast<int>(prim->primitiveType));
                if (gpuMesh && !gpuMesh->submeshes.empty() && instances.size() < kMaxInstances) {
                    uint32_t matIdx = m_Bindless.PushSlipMaterial(m_Context, nullptr, prim->color);
                    uint32_t instIdx = static_cast<uint32_t>(instances.size());
                    instances.push_back({ model, prevModel, matIdx, selFlag, 0, 0 });
                    drawCalls.push_back({ gpuMesh, gpuMesh->submeshes[0].firstIndex, gpuMesh->submeshes[0].indexCount, gpuMesh->submeshes[0].vertexOffset, instIdx });
                    if (canRayTrace && gpuMesh->submeshes[0].blas.IsValid()) {
                        rtInstances.push_back({ &gpuMesh->submeshes[0].blas, model, prevModel, matIdx, gpuMesh->submeshes[0].firstIndex, gpuMesh->submeshes[0].vertexOffset, static_cast<uint32_t>(sizeof(Vertex) / sizeof(float)) });
                    }
                }
            }
        }
    }

    if (dirCount == 0) {
        lightUbo.dirLights[0].directionIntensity = glm::vec4(glm::normalize(glm::vec3(-0.35f, -0.85f, -0.40f)), 1.4f);
        lightUbo.dirLights[0].colorPad = glm::vec4(1.0f, 0.98f, 0.92f, 1.0f);
        dirCount = 1;
    }

    float activeSunGlow = gfx.sunGlow;
    if (SlipSkyComponent* skyComp = level ? SlipSkyComponent::FindActive(level) : nullptr) {
        if (skyComp->IsEnabled()) {
            if (skyComp->params.followDirectionalLight) {
                glm::vec3 sunVec(lightUbo.sunDirIntensity.x, lightUbo.sunDirIntensity.y, lightUbo.sunDirIntensity.z);
                if (glm::length(sunVec) > 0.0001f) {
                    skyComp->params.sunDirection = glm::normalize(sunVec);
                }
            } else if (glm::length(skyComp->params.sunDirection) > 0.0001f) {
                glm::vec3 sDir = glm::normalize(skyComp->params.sunDirection);
                lightUbo.sunDirIntensity.x = sDir.x;
                lightUbo.sunDirIntensity.y = sDir.y;
                lightUbo.sunDirIntensity.z = sDir.z;
            }

            skyComp->UpdateAtmosphereFromSun();
            if (skyComp->params.dynamicAtmosphere) {
                lightUbo.skyZenith = glm::vec4(skyComp->currentAtmosphere.zenithColor, gfx.skyEnabled ? 1.0f : 0.0f);
                lightUbo.skyHorizon = glm::vec4(skyComp->currentAtmosphere.horizonColor, gfx.skyHorizonSharpness);
                lightUbo.skyGround = glm::vec4(skyComp->currentAtmosphere.groundColor, gfx.sunDiscEnabled ? 1.0f : 0.0f);
                lightUbo.sunColorParams = glm::vec4(skyComp->currentAtmosphere.sunColor, gfx.sunSize);
                lightUbo.sunDirIntensity.w = skyComp->currentAtmosphere.sunIntensity;
                activeSunGlow = skyComp->currentAtmosphere.sunGlow;
            }

            glm::vec2 windDir = (glm::length(skyComp->params.windDirection) > 0.0001f)
                                ? glm::normalize(skyComp->params.windDirection)
                                : glm::vec2(1.0f, 0.0f);
            glm::vec2 windOffset = windDir * (skyComp->params.windSpeed * skyComp->GetCurrentTime() * skyComp->params.cloudScale);
            lightUbo.cloudParams0 = glm::vec4(1.0f, skyComp->params.cloudCoverage, skyComp->params.cloudDensity, skyComp->params.cloudBaseHeight);
            lightUbo.cloudParams1 = glm::vec4(skyComp->params.cloudLayerThickness, skyComp->params.cloudScale, skyComp->params.cloudEdgeErosion, skyComp->params.absorption);
            lightUbo.cloudParams2 = glm::vec4(windOffset.x, windOffset.y, skyComp->params.henyeyG, skyComp->params.powderFactor);
            lightUbo.cloudAmbient = glm::vec4(
                skyComp->params.dynamicAtmosphere ? skyComp->currentAtmosphere.cloudAmbientColor : skyComp->params.cloudAmbientColor,
                skyComp->params.silverSpread
            );
        }
    }
    lightUbo.counts = glm::ivec4(dirCount, pointCount, spotCount, static_cast<int>(activeSunGlow * 1000.0f));

    m_Bindless.UploadMaterials(m_Context);
    if (!instances.empty()) {
        m_InstanceSSBOs[slot].Upload(m_Context, instances.data(), instances.size() * sizeof(GpuInstanceData), 0);
    }

    PostProcessSettings post = PostProcessVolumeComponent::Resolve(level, cameraPos);
    lightUbo.aoParams = glm::vec4(
        post.ssao ? 1.0f : 0.0f,
        post.ssaoRadius,
        post.ssaoBias,
        glm::clamp(post.ssaoIntensity, 0.0f, 2.0f)
    );
    lightUbo.aoParams2 = glm::vec4(post.ssaoPower, 0.0f, 0.0f, 0.0f);
    m_LightingUBOs[slot].Upload(m_Context, &lightUbo, sizeof(lightUbo), 0);

    UpdateComputeDescriptors(slot, targetView, targetGeneration);

    m_LastDrawCallCount = static_cast<uint32_t>(drawCalls.size());
    const uint32_t groupsX = (width + 7) / 8;
    const uint32_t groupsY = (height + 7) / 8;

    GpuPostProcessUBO postUbo{};
    postUbo.invViewProj = invViewProj;
    postUbo.cameraPos = glm::vec4(cameraPos, 1.0f);
    postUbo.sunDir = glm::vec4(lightUbo.sunDirIntensity.x, lightUbo.sunDirIntensity.y, lightUbo.sunDirIntensity.z, 0.0f);
    postUbo.sunColor = glm::vec4(lightUbo.sunColorParams.r, lightUbo.sunColorParams.g, lightUbo.sunColorParams.b, 1.0f);
    postUbo.bloomParams = glm::vec4(post.bloom ? 1.0f : 0.0f, post.bloomIntensity, post.bloomThreshold, post.bloomSoftKnee);
    postUbo.bloomTint = glm::vec4(post.bloomTint, post.bloomRadius);
    postUbo.lensParams = glm::vec4(post.lensBloom ? 1.0f : 0.0f, post.lensIntensity, post.lensThreshold, post.lensHalo);
    const float exposureMul = std::exp2(glm::clamp(post.exposureCompensation, -8.0f, 8.0f));
    postUbo.colorParams = glm::vec4(exposureMul, post.contrast, post.saturation, post.temperature);
    postUbo.exposureAdapt = glm::vec4(
        post.autoExposure ? 1.0f : 0.0f,
        post.exposureKey,
        post.exposureMin,
        (std::max)(post.exposureMax, post.exposureMin + 0.001f)
    );
    postUbo.lift = glm::vec4(post.lift, static_cast<float>(post.tonemap));
    postUbo.gamma = glm::vec4(post.gamma, post.vignetteIntensity);
    postUbo.gain = glm::vec4(post.gain, post.vignetteSmoothness);
    postUbo.colorFilter = glm::vec4(post.colorFilter, post.blurRadius);
    postUbo.radialLetterbox = glm::vec4(
        post.radialBlurStrength,
        post.radialBlurCenter.x,
        post.radialBlurCenter.y,
        glm::clamp(post.letterbox, 0.0f, 0.49f)
    );
    postUbo.fogParams0 = glm::vec4(post.fog ? 1.0f : 0.0f, post.fogDensity, post.fogHeightFalloff, post.fogHeight);
    postUbo.fogParams1 = glm::vec4(post.fogStartDistance, post.fogMaxDistance, post.fogSunScattering, post.fogSkyBlend);
    postUbo.fogColor = glm::vec4(post.fogColor, glm::clamp(post.pillarbox, 0.0f, 0.49f));
    postUbo.flags = glm::ivec4(
        m_SelectedEntity ? 1 : 0,
        1,
        gfx.fxAAEnabled ? 1 : 0,
        glm::clamp(gfx.fxAAQuality, 0, 2)
    );

    auto runPostProcessAndTonemap = [&](bool hasGBuffer) {
        postUbo.flags.y = hasGBuffer ? 1 : 0;
        m_PostProcessUBOs[slot].Upload(m_Context, &postUbo, sizeof(postUbo), 0);

        if (!hasGBuffer && !m_BloomInitialized) {
            VulkanTexture::TransitionImageLayout(cmd, m_GBufMetalEmissive.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            if (m_GBufDepthLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
                VulkanTexture::TransitionImageLayout(cmd, m_GBufDepth.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
                m_GBufDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }

        const bool runBloom = (post.bloom && post.bloomIntensity > 0.001f) ||
                              (post.lensBloom && post.lensIntensity > 0.001f);
        if (runBloom) {
            const uint32_t bloomW = (std::max)(1u, width / 2u);
            const uint32_t bloomH = (std::max)(1u, height / 2u);
            const uint32_t bGroupsX = (bloomW + 7u) / 8u;
            const uint32_t bGroupsY = (bloomH + 7u) / 8u;
            int32_t dirZero[2] = { 0, 0 };
            int32_t dirH[2] = { 1, 0 };
            int32_t dirV[2] = { 0, 1 };

            VulkanTexture::TransitionImageLayout(cmd, m_BloomPing.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomExtractPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomPipeLayout, 0, 1, &m_BloomSets[slot][0], 0, nullptr);
            vkCmdPushConstants(cmd, m_BloomPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dirZero), dirZero);
            vkCmdDispatch(cmd, bGroupsX, bGroupsY, 1);

            VulkanTexture::TransitionImageLayout(cmd, m_BloomPing.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_BloomPong.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomBlurPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomPipeLayout, 0, 1, &m_BloomSets[slot][1], 0, nullptr);
            vkCmdPushConstants(cmd, m_BloomPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dirH), dirH);
            vkCmdDispatch(cmd, bGroupsX, bGroupsY, 1);

            VulkanTexture::TransitionImageLayout(cmd, m_BloomPong.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_BloomPing.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomBlurPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_BloomPipeLayout, 0, 1, &m_BloomSets[slot][2], 0, nullptr);
            vkCmdPushConstants(cmd, m_BloomPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dirV), dirV);
            vkCmdDispatch(cmd, bGroupsX, bGroupsY, 1);

            VulkanTexture::TransitionImageLayout(cmd, m_BloomPing.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            m_BloomInitialized = true;
        } else if (!m_BloomInitialized) {
            VulkanTexture::TransitionImageLayout(cmd, m_BloomPing.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            m_BloomInitialized = true;
        }

        VulkanTexture::TransitionImageLayout(cmd, targetImage, targetInitialLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_TonemapPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_TonemapPipeLayout, 0, 1, &m_TonemapSets[slot], 0, nullptr);
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
        VulkanTexture::TransitionImageLayout(cmd, targetImage, VK_IMAGE_LAYOUT_GENERAL, targetFinalLayout, VK_IMAGE_ASPECT_COLOR_BIT);
    };

    // Scene/Camera/GI hash for temporal & progressive Path Tracing reset detection
    uint64_t sceneStateHash = 14695981039346656037ULL;
    HashCombineBytes(sceneStateHash, &view, sizeof(view));
    HashCombineBytes(sceneStateHash, &vkProj, sizeof(vkProj));
    HashCombineBytes(sceneStateHash, &gfx.giMode, sizeof(gfx.giMode));
    HashCombineBytes(sceneStateHash, &gfx.ptMode, sizeof(gfx.ptMode));
    HashCombineBytes(sceneStateHash, &gfx.giBounces, sizeof(gfx.giBounces));
    HashCombineBytes(sceneStateHash, &gfx.giIntensity, sizeof(gfx.giIntensity));
    HashCombineBytes(sceneStateHash, &gfx.sunIntensity, sizeof(gfx.sunIntensity));
    HashCombineBytes(sceneStateHash, &gfx.skyEnabled, sizeof(gfx.skyEnabled));
    HashCombineBytes(sceneStateHash, &lightUbo.cloudParams0, sizeof(lightUbo.cloudParams0));
    HashCombineBytes(sceneStateHash, &lightUbo.cloudParams1, sizeof(lightUbo.cloudParams1));
    HashCombineBytes(sceneStateHash, &lightUbo.cloudParams2, sizeof(lightUbo.cloudParams2));
    HashCombineBytes(sceneStateHash, &lightUbo.cloudAmbient, sizeof(lightUbo.cloudAmbient));
    HashCombineBytes(sceneStateHash, &lightUbo.counts, sizeof(lightUbo.counts));
    if (dirCount > 0) HashCombineBytes(sceneStateHash, lightUbo.dirLights, static_cast<size_t>(dirCount) * sizeof(GpuDirLight));
    if (pointCount > 0) HashCombineBytes(sceneStateHash, lightUbo.pointLights, static_cast<size_t>(pointCount) * sizeof(GpuPointLight));
    for (const auto& inst : instances) {
        HashCombineBytes(sceneStateHash, &inst.model, sizeof(inst.model));
        HashCombineBytes(sceneStateHash, &inst.prevModel, sizeof(inst.prevModel));
        HashCombineBytes(sceneStateHash, &inst.materialIndex, sizeof(inst.materialIndex));
    }

    const bool wantsRayTracing = (gfx.giMode == GlobalIlluminationMode::RTGI || gfx.giMode == GlobalIlluminationMode::PathTracing);
    bool hasTLAS = false;
    if (wantsRayTracing && canRayTrace && !rtInstances.empty()) {
        hasTLAS = m_ASManager.BuildTLAS(m_Context, cmd, slot, rtInstances);
    }

    // Fast path: Full Progressive Path Tracing (when TLAS ready)
    if (gfx.giMode == GlobalIlluminationMode::PathTracing && canRayTrace && hasTLAS) {
        if (m_RtGI.ExecutePathTracing(
                m_Context,
                cmd,
                slot,
                m_Bindless.GetDescriptorSet(),
                m_ASManager.GetTLAS(slot),
                m_ASManager.GetInstanceInfoBuffer(slot),
                m_LightingUBOs[slot].GetBuffer(),
                m_HdrColor,
                sceneStateHash,
                gfx))
        {
            ResolvePathTracingDepth(cmd, slot, width, height);
            if (!RenderParticles(cmd, slot, level, view, viewProj, cameraPos, width, height)) {
                VulkanTexture::TransitionImageLayout(cmd, m_HdrColor.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            }
            runPostProcessAndTonemap(false);
            return true;
        }
    }

    // ------------------------------------------------------------------------
    // Pass 1: G-Buffer Dynamic Rendering
    // ------------------------------------------------------------------------
    VulkanTexture::TransitionImageLayout(cmd, m_GBufAlbedo.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufNormalRough.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufMetalEmissive.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufMotion.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufDepth.GetImage(), m_GBufDepthLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_GBufDepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkRenderingAttachmentInfo, 4> colorAtts{};
    auto setupColorAtt = [](VkRenderingAttachmentInfo& att, VkImageView view) {
        att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        att.imageView = view;
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    };
    setupColorAtt(colorAtts[0], m_GBufAlbedo.GetImageView());
    setupColorAtt(colorAtts[1], m_GBufNormalRough.GetImageView());
    setupColorAtt(colorAtts[2], m_GBufMetalEmissive.GetImageView());
    setupColorAtt(colorAtts[3], m_GBufMotion.GetImageView());

    VkRenderingAttachmentInfo depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAtt.imageView = m_GBufDepth.GetImageView();
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderInfo.renderArea = { { 0, 0 }, { width, height } };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = static_cast<uint32_t>(colorAtts.size());
    renderInfo.pColorAttachments = colorAtts.data();
    renderInfo.pDepthAttachment = &depthAtt;

    vkCmdBeginRendering(cmd, &renderInfo);

    VkViewport vp{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    VkRect2D scissor{ { 0, 0 }, { width, height } };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (!drawCalls.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GBufPipeline);
        std::array<VkDescriptorSet, 2> bindSets = { m_Bindless.GetDescriptorSet(), m_GBufSceneSets[slot] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GBufPipeLayout, 0, 2, bindSets.data(), 0, nullptr);

        const GpuMesh* boundMesh = nullptr;
        for (const auto& dc : drawCalls) {
            if (dc.mesh != boundMesh) {
                VkBuffer vb = dc.mesh->vertexBuffer.GetBuffer();
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
                vkCmdBindIndexBuffer(cmd, dc.mesh->indexBuffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
                boundMesh = dc.mesh;
            }
            vkCmdPushConstants(cmd, m_GBufPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &dc.instanceIndex);
            vkCmdDrawIndexed(cmd, dc.indexCount, 1, dc.firstIndex, dc.vertexOffset, 0);
        }
    }

    vkCmdEndRendering(cmd);

    VulkanTexture::TransitionImageLayout(cmd, m_GBufAlbedo.GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufNormalRough.GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufMetalEmissive.GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufMotion.GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_GBufDepth.GetImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_GBufDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // ------------------------------------------------------------------------
    // Pass 2A: Hardware Ray-Traced GI + Temporal/Spatial Denoiser
    // ------------------------------------------------------------------------
    bool useRtgi = false;
    if (gfx.giMode == GlobalIlluminationMode::RTGI && canRayTrace && hasTLAS) {
        useRtgi = m_RtGI.ExecuteRTGI(
            m_Context,
            cmd,
            slot,
            m_Bindless.GetDescriptorSet(),
            m_ASManager.GetTLAS(slot),
            m_ASManager.GetInstanceInfoBuffer(slot),
            m_LightingUBOs[slot].GetBuffer(),
            m_GBufAlbedo,
            m_GBufNormalRough,
            m_GBufMetalEmissive,
            m_GBufDepth,
            m_GBufMotion,
            sceneStateHash,
            gfx
        );
    }

    if (!useRtgi) {
        GpuGiParamsUBO offGi{};
        offGi.width = width;
        offGi.height = height;
        offGi.frameIndex = frameIndex;
        offGi.sampleCount = 1;
        offGi.giMode = 0;
        offGi.giIntensity = gfx.giIntensity;
        offGi.hasValidTLAS = 0;
        m_RtGI.UpdateGiParamsBuffer(m_Context, slot, offGi);
    }

    const VulkanTexture& giTex = useRtgi ? m_RtGI.GetDenoisedGI() : m_RtGI.GetFallbackGI();
    const VulkanTexture& reflTex = useRtgi ? m_RtGI.GetDenoisedReflections() : m_RtGI.GetFallbackReflections();
    std::array<VkWriteDescriptorSet, 2> lightingWrites{};
    std::array<VkDescriptorImageInfo, 2> lightingInfos{};
    uint32_t lightingWriteCount = 0;

    if (giTex.IsValid() && m_LastLightingGiViews[slot] != giTex.GetImageView()) {
        lightingInfos[lightingWriteCount] = { giTex.GetSampler(), giTex.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        lightingWrites[lightingWriteCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lightingWrites[lightingWriteCount].dstSet = m_LightingSets[slot];
        lightingWrites[lightingWriteCount].dstBinding = 6;
        lightingWrites[lightingWriteCount].descriptorCount = 1;
        lightingWrites[lightingWriteCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        lightingWrites[lightingWriteCount].pImageInfo = &lightingInfos[lightingWriteCount];
        ++lightingWriteCount;
        m_LastLightingGiViews[slot] = giTex.GetImageView();
    }
    if (reflTex.IsValid() && m_LastLightingReflViews[slot] != reflTex.GetImageView()) {
        lightingInfos[lightingWriteCount] = { reflTex.GetSampler(), reflTex.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        lightingWrites[lightingWriteCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lightingWrites[lightingWriteCount].dstSet = m_LightingSets[slot];
        lightingWrites[lightingWriteCount].dstBinding = 8;
        lightingWrites[lightingWriteCount].descriptorCount = 1;
        lightingWrites[lightingWriteCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        lightingWrites[lightingWriteCount].pImageInfo = &lightingInfos[lightingWriteCount];
        ++lightingWriteCount;
        m_LastLightingReflViews[slot] = reflTex.GetImageView();
    }
    if (lightingWriteCount > 0) {
        vkUpdateDescriptorSets(m_Context->GetDevice(), lightingWriteCount, lightingWrites.data(), 0, nullptr);
    }

    // ------------------------------------------------------------------------
    // Pass 2B: Direct PBR Lighting + RTGI Composite + Sky (Compute)
    // ------------------------------------------------------------------------
    VulkanTexture::TransitionImageLayout(cmd, m_HdrColor.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_LightingPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_LightingPipeLayout, 0, 1, &m_LightingSets[slot], 0, nullptr);
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // ------------------------------------------------------------------------
    // Pass 3: Bloom + Post-Process + Tone Mapping + Outline -> Target RGBA8 Image
    // ------------------------------------------------------------------------
    // Pass 2C: Particles (forward, depth-tested) -> HDR, antes del bloom para que las chispas emisivas brillen
    if (!RenderParticles(cmd, slot, level, view, viewProj, cameraPos, width, height)) {
        VulkanTexture::TransitionImageLayout(cmd, m_HdrColor.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    runPostProcessAndTonemap(true);
    return true;
}
