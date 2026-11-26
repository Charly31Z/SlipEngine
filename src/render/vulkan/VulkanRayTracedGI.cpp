#include "SlipEngine/render/vulkan/VulkanRayTracedGI.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"
#include "SlipEngine/render/vulkan/SlipSpirvCompiler.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#include <algorithm>
#include <array>
#include <iostream>

namespace {

const char* kFallbackRtgiTraceCompA = R"GLSL(#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(buffer_reference, scalar) readonly buffer FloatBuf { float data[]; };
layout(buffer_reference, scalar) readonly buffer UintBuf  { uint data[]; };
struct GpuMaterialData { vec4 albedoColor; vec4 emissiveColor; vec4 pbrParams; vec4 extraParams; ivec4 texIndices0; ivec4 texIndices1; };
struct GpuRtInstanceInfo { uint64_t vertexAddress; uint64_t indexAddress; uint materialIndex; uint firstIndex; int vertexOffset; uint vertexStrideFloats; mat4 prevModel; };
struct GpuDirLight { vec4 directionIntensity; vec4 colorPad; };
struct GpuPointLight { vec4 positionRange; vec4 colorIntensity; };
struct GpuSpotLight { vec4 positionRange; vec4 directionCosOuter; vec4 colorIntensityCosIn; };
layout(set = 0, binding = 0) uniform sampler2D uTextures[];
layout(set = 0, binding = 1) readonly buffer MaterialBuffer { GpuMaterialData materials[]; } uMaterials;
layout(set = 1, binding = 0) uniform sampler2D uAlbedoTex;
layout(set = 1, binding = 1) uniform sampler2D uNormalRoughTex;
layout(set = 1, binding = 2) uniform sampler2D uMetalEmissiveTex;
layout(set = 1, binding = 3) uniform sampler2D uDepthTex;
layout(set = 1, binding = 4, rgba16f) uniform writeonly image2D uRtgiRawOut;
layout(set = 1, binding = 5) uniform accelerationStructureEXT uTLAS;
layout(set = 1, binding = 6) readonly buffer RtInstanceInfoBuffer { GpuRtInstanceInfo rtInstances[]; } uRtInstances;
layout(set = 1, binding = 7) uniform SceneLightingUBO {
    mat4 invViewProj; vec4 cameraPos; vec4 skyZenith; vec4 skyHorizon; vec4 skyGround;
    vec4 sunDirIntensity; vec4 sunColorParams; ivec4 counts;
    GpuDirLight dirLights[4]; GpuPointLight pointLights[32]; GpuSpotLight spotLights[16];
    vec4 cloudParams0; vec4 cloudParams1; vec4 cloudParams2; vec4 cloudAmbient;
} uScene;
layout(set = 1, binding = 8) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
layout(set = 1, binding = 9, rgba16f) uniform writeonly image2D uRtReflRawOut;
const float PI = 3.14159265359;
uint PcgHash(uint v) { uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
float Rand01(inout uint seed) { seed = PcgHash(seed); return float(seed) * (1.0 / 4294967296.0); }
vec2 R2Sequence(uint n, vec2 pixelOffset) { return fract(pixelOffset + vec2(0.7548776662, 0.5698402910) * float(n)); }
vec3 ReconstructWorldPos(vec2 uv, float depth) { vec4 w = uScene.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0); return w.xyz / max(abs(w.w), 1e-6); }
void BuildBasis(vec3 N, out vec3 T, out vec3 B) { vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0); T = normalize(cross(up, N)); B = cross(N, T); }
vec3 SampleCosineHemisphere(vec3 N, float u1, float u2) { float r = sqrt(max(u1, 0.0)); float th = 2.0 * PI * u2; vec3 T, B; BuildBasis(N, T, B); return normalize(T * (r * cos(th)) + B * (r * sin(th)) + N * sqrt(max(1.0 - u1, 0.0))); }
vec3 SampleGGXHalfVector(vec3 N, float r, float u1, float u2) { float a = r * r; float phi = 2.0 * PI * u1; float cosT = sqrt((1.0 - u2) / max(1.0 + (a * a - 1.0) * u2, 1e-5)); float sinT = sqrt(max(1.0 - cosT * cosT, 0.0)); vec3 T, B; BuildBasis(N, T, B); return normalize(T * (cos(phi) * sinT) + B * (sin(phi) * sinT) + N * cosT); }
vec3 EvaluateSky(vec3 dir, bool includeSunDisc) {
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
float ReflCloudHash31(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
float ReflCloudNoise3D(vec3 p) {
    vec3 i = floor(p); vec3 f = fract(p); f = f * f * (3.0 - 2.0 * f);
    float n000 = ReflCloudHash31(i); float n100 = ReflCloudHash31(i + vec3(1,0,0));
    float n010 = ReflCloudHash31(i + vec3(0,1,0)); float n110 = ReflCloudHash31(i + vec3(1,1,0));
    float n001 = ReflCloudHash31(i + vec3(0,0,1)); float n101 = ReflCloudHash31(i + vec3(1,0,1));
    float n011 = ReflCloudHash31(i + vec3(0,1,1)); float n111 = ReflCloudHash31(i + vec3(1,1,1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y), mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float ReflCloudFbm(vec3 p) {
    float v = 0.0; float amp = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += amp * ReflCloudNoise3D(p);
        p = vec3(0.80 * p.x - 0.60 * p.z, 1.15 * p.y, 0.60 * p.x + 0.80 * p.z) * 2.03 + vec3(13.7, 7.3, 19.1);
        amp *= 0.5;
    }
    return v;
}
vec3 EvaluateReflClouds(vec3 ro, vec3 rd, vec3 skyCol) {
    if (uScene.cloudParams0.x < 0.5 || rd.y <= 0.015) return skyCol;
    float coverage = clamp(uScene.cloudParams0.y, 0.0, 1.0); float density = max(uScene.cloudParams0.z, 0.0);
    if (coverage <= 0.01 || density <= 0.00005) return skyCol;
    float baseH = uScene.cloudParams0.w; float thick = max(uScene.cloudParams1.x, 5.0);
    float tStart = max((baseH - ro.y) / rd.y, 0.0);
    float tEnd = min(min(max((baseH + thick - ro.y) / rd.y, tStart), tStart + 7500.0), 18000.0);
    if (tEnd <= tStart + 0.1) return skyCol;
    float freq = max(uScene.cloudParams1.y, 0.00005) * 6.0; float absorption = max(uScene.cloudParams1.w, 0.05);
    vec3 wind3 = vec3(uScene.cloudParams2.x, 0.0, uScene.cloudParams2.y) * 6.0; float threshold = 1.0 - clamp(coverage, 0.01, 0.99);
    const int RSTEPS = 10; float dt = (tEnd - tStart) / float(RSTEPS); vec3 accum = vec3(0.0); float trans = 1.0;
    vec3 cloudLit = uScene.cloudAmbient.rgb * 0.85 + uScene.sunColorParams.rgb * uScene.sunDirIntensity.w * 0.45;
    for (int i = 0; i < RSTEPS; ++i) {
        vec3 pos = ro + rd * (tStart + (float(i) + 0.5) * dt);
        float hFrac = clamp((pos.y - baseH) / thick, 0.0, 1.0);
        float hEnv = smoothstep(0.0, 0.16, hFrac) * smoothstep(1.0, 0.34, hFrac);
        float n = ReflCloudFbm(vec3(pos.x * freq, hFrac * 1.85, pos.z * freq) + wind3);
        float d = clamp((n - threshold) / max(1.0 - threshold, 0.02), 0.0, 1.0) * hEnv * (density * 8.5);
        if (d > 0.00005) { float sT = exp(-d * dt * absorption); accum += trans * cloudLit * mix(0.75, 1.15, hFrac) * (1.0 - sT); trans *= sT; }
    }
    return mix(skyCol, accum + skyCol * trans, smoothstep(0.015, 0.085, rd.y));
}
)GLSL";

const char* kFallbackRtgiTraceCompB = R"GLSL(bool TraceVisibility(vec3 origin, vec3 dir, float tMin, float tMax) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, uTLAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFFu, origin, tMin, dir, tMax);
    while (rayQueryProceedEXT(rq)) {}
    return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT;
}
vec3 EvaluateDirectOnHit(vec3 hitPos, vec3 hitN, vec3 hitAlbedo) {
    vec3 total = vec3(0.0);
    for (int i = 0; i < clamp(uScene.counts.x, 0, 4); ++i) {
        vec3 L = normalize(-uScene.dirLights[i].directionIntensity.xyz);
        float NdotL = max(dot(hitN, L), 0.0);
        if (NdotL > 0.0 && ((i != 0 || uGi.giRayTracedShadows == 0) || TraceVisibility(hitPos + hitN * 0.005, L, 0.002, 500.0))) {
            total += (hitAlbedo / PI) * uScene.dirLights[i].colorPad.rgb * uScene.dirLights[i].directionIntensity.w * NdotL;
        }
    }
    for (int i = 0; i < clamp(uScene.counts.y, 0, 32); ++i) {
        vec3 toL = uScene.pointLights[i].positionRange.xyz - hitPos;
        float dist = length(toL); float range = max(uScene.pointLights[i].positionRange.w, 0.001);
        if (dist < range) {
            vec3 L = toL / max(dist, 1e-4); float NdotL = max(dot(hitN, L), 0.0);
            if (NdotL > 0.0 && TraceVisibility(hitPos + hitN * 0.005, L, 0.002, max(dist - 0.01, 0.003))) {
                float att = clamp(1.0 - dist / range, 0.0, 1.0); att = (att * att) / (1.0 + 0.1 * dist * dist);
                total += (hitAlbedo / PI) * uScene.pointLights[i].colorIntensity.rgb * uScene.pointLights[i].colorIntensity.w * att * NdotL;
            }
        }
    }
    return total;
}
void FetchHitSurface(rayQueryEXT rq, vec3 ro, vec3 rd, out vec3 hitPos, out vec3 hitN, out vec3 hitAlbedo, out float hitRough, out float hitMetal, out vec3 hitEmis) {
    float tHit = rayQueryGetIntersectionTEXT(rq, true);
    uint instIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
    uint primIdx = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
    vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
    vec3 w = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
    GpuRtInstanceInfo info = uRtInstances.rtInstances[instIdx];
    UintBuf idxBuf = UintBuf(info.indexAddress); FloatBuf vBuf = FloatBuf(info.vertexAddress);
    uint i0 = idxBuf.data[info.firstIndex + primIdx * 3u + 0u] + uint(info.vertexOffset);
    uint i1 = idxBuf.data[info.firstIndex + primIdx * 3u + 1u] + uint(info.vertexOffset);
    uint i2 = idxBuf.data[info.firstIndex + primIdx * 3u + 2u] + uint(info.vertexOffset);
    uint s = info.vertexStrideFloats; uint b0 = i0 * s; uint b1 = i1 * s; uint b2 = i2 * s;
    vec3 localN = normalize(vec3(vBuf.data[b0+3u], vBuf.data[b0+4u], vBuf.data[b0+5u]) * w.x +
                            vec3(vBuf.data[b1+3u], vBuf.data[b1+4u], vBuf.data[b1+5u]) * w.y +
                            vec3(vBuf.data[b2+3u], vBuf.data[b2+4u], vBuf.data[b2+5u]) * w.z);
    hitN = normalize(transpose(mat3(rayQueryGetIntersectionWorldToObjectEXT(rq, true))) * localN);
    if (dot(hitN, -rd) < 0.0) hitN = -hitN;
    vec2 hitUv = vec2(vBuf.data[b0+6u], vBuf.data[b0+7u]) * w.x + vec2(vBuf.data[b1+6u], vBuf.data[b1+7u]) * w.y + vec2(vBuf.data[b2+6u], vBuf.data[b2+7u]) * w.z;
    hitPos = ro + rd * tHit;
    GpuMaterialData mat = uMaterials.materials[info.materialIndex];
    hitAlbedo = mat.albedoColor.rgb;
    if (mat.texIndices0.x >= 0) hitAlbedo *= textureLod(uTextures[nonuniformEXT(mat.texIndices0.x)], hitUv, 0.0).rgb;
    hitAlbedo = pow(clamp(hitAlbedo, vec3(0.0), vec3(1.0)), vec3(2.2));
    hitRough = clamp(mat.pbrParams.x, 0.04, 1.0);
    if (mat.texIndices0.z >= 0) hitRough *= textureLod(uTextures[nonuniformEXT(mat.texIndices0.z)], hitUv, 0.0).g;
    if (mat.extraParams.z > 0.5) hitRough = 1.0 - hitRough;
    hitRough = clamp(hitRough, 0.04, 1.0);
    hitMetal = clamp(mat.pbrParams.y, 0.0, 1.0);
    if (mat.texIndices0.w >= 0) hitMetal *= textureLod(uTextures[nonuniformEXT(mat.texIndices0.w)], hitUv, 0.0).b;
    hitMetal = clamp(hitMetal, 0.0, 1.0);
    hitEmis = mat.emissiveColor.rgb * mat.emissiveColor.a;
    if (mat.texIndices1.y >= 0) hitEmis *= textureLod(uTextures[nonuniformEXT(mat.texIndices1.y)], hitUv, 0.0).rgb;
}
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uRtgiRawOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    float depth = texelFetch(uDepthTex, pix, 0).r;
    if (depth >= 0.99999 || uGi.hasValidTLAS == 0) { imageStore(uRtgiRawOut, pix, vec4(0.0, 0.0, 0.0, 1.0)); imageStore(uRtReflRawOut, pix, vec4(0.0, 0.0, 0.0, 1.0)); return; }
    vec2 uv = (vec2(pix) + 0.5) / vec2(size); vec3 worldPos = ReconstructWorldPos(uv, depth);
    vec4 normRough = texelFetch(uNormalRoughTex, pix, 0);
    vec3 N = normalize(normRough.xyz); float roughness = clamp(normRough.w, 0.04, 1.0);
    float metallic = clamp(texelFetch(uMetalEmissiveTex, pix, 0).x, 0.0, 1.0);
    vec3 V = normalize(uScene.cameraPos.xyz - worldPos);
    if (dot(N, V) < 0.0) N = normalize(N - V * (dot(N, V) - 0.001));
    uint baseSeed = uint(pix.x) * 1973u + uint(pix.y) * 9277u;
    uint rotSeed = PcgHash(baseSeed); vec2 pixelR2Offset = vec2(Rand01(rotSeed), Rand01(rotSeed));
    float sunVis = 1.0;
    if (uGi.giRayTracedShadows != 0 && clamp(uScene.counts.x, 0, 4) > 0) {
        vec3 sunL = normalize(-uScene.dirLights[0].directionIntensity.xyz);
        if (dot(N, sunL) > 0.0) {
            float coneRadius = clamp(uScene.sunColorParams.w * 0.35, 0.004, 0.025);
            vec3 sunT, sunB; BuildBasis(sunL, sunT, sunB);
            float accumVis = 0.0;
            for (uint si = 0u; si < 2u; ++si) {
                vec2 uSun = R2Sequence(uGi.frameIndex * 2u + si + 1u, pixelR2Offset);
                float rDisk = sqrt(uSun.x) * coneRadius; float phiDisk = 2.0 * PI * uSun.y;
                vec3 jitterSunL = normalize(sunL + (sunT * cos(phiDisk) + sunB * sin(phiDisk)) * rDisk);
                vec3 raySunL = (dot(N, jitterSunL) > 0.0) ? jitterSunL : sunL;
                accumVis += TraceVisibility(worldPos + N * 0.008, raySunL, 0.002, 500.0) ? 1.0 : 0.0;
            }
            sunVis = accumVis * 0.5;
        } else { sunVis = 0.0; }
    }
    int rays = clamp(uGi.giRaysPerPixel, 1, 4); int bounces = clamp(uGi.giBounces, 1, 4);
    vec3 irr = vec3(0.0);
    for (int r = 0; r < rays; ++r) {
        uint sampleIdx = (uGi.frameIndex * uint(rays)) + uint(r) + 1u;
        vec2 uGiRand = R2Sequence(sampleIdx, pixelR2Offset); uint seed = PcgHash(baseSeed + sampleIdx * 9781u);
        vec3 tp = vec3(1.0); vec3 ro = worldPos + N * 0.008; vec3 curN = N; vec3 rayIrr = vec3(0.0);
        for (int b = 0; b < bounces; ++b) {
            vec2 uH = (b == 0) ? uGiRand : vec2(Rand01(seed), Rand01(seed));
            vec3 rd = SampleCosineHemisphere(curN, uH.x, uH.y);
            rayQueryEXT rq; rayQueryInitializeEXT(rq, uTLAS, gl_RayFlagsOpaqueEXT, 0xFFu, ro, 0.002, rd, 200.0);
            while (rayQueryProceedEXT(rq)) {}
            if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) { rayIrr += tp * EvaluateSky(rd, false); break; }
            vec3 hitPos, hitN, hitAlbedo, hitEmis; float hitRough, hitMetal;
            FetchHitSurface(rq, ro, rd, hitPos, hitN, hitAlbedo, hitRough, hitMetal, hitEmis);
            rayIrr += tp * (hitEmis + EvaluateDirectOnHit(hitPos, hitN, hitAlbedo));
            tp *= hitAlbedo * (1.0 - hitMetal * 0.5);
            ro = hitPos + hitN * 0.005; curN = hitN;
        }
        irr += rayIrr;
    }
    irr = min(irr / float(rays), vec3(16.0));
    if (any(isnan(irr)) || any(isinf(irr))) irr = vec3(0.0);
    vec3 reflRad = vec3(0.0);
    if (roughness < 0.85 || metallic > 0.05) {
        vec3 reflDir = reflect(-V, N);
        if (roughness > 0.06) {
            vec2 uRefl = R2Sequence(uGi.frameIndex + 17u, fract(pixelR2Offset + vec2(0.37, 0.61)));
            vec3 H = SampleGGXHalfVector(N, roughness, uRefl.x, uRefl.y);
            vec3 cand = reflect(-V, H);
            if (dot(N, cand) > 0.0) reflDir = cand;
        }
        rayQueryEXT rqRefl; rayQueryInitializeEXT(rqRefl, uTLAS, gl_RayFlagsOpaqueEXT, 0xFFu, worldPos + N * 0.01, 0.002, reflDir, 300.0);
        while (rayQueryProceedEXT(rqRefl)) {}
        if (rayQueryGetIntersectionTypeEXT(rqRefl, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
            reflRad = EvaluateReflClouds(worldPos, reflDir, EvaluateSky(reflDir, true));
        } else {
            vec3 hitPos, hitN, hitAlbedo, hitEmis; float hitRough, hitMetal;
            FetchHitSurface(rqRefl, worldPos + N * 0.01, reflDir, hitPos, hitN, hitAlbedo, hitRough, hitMetal, hitEmis);
            reflRad = hitEmis + EvaluateDirectOnHit(hitPos, hitN, hitAlbedo) + EvaluateSky(hitN, false) * hitAlbedo * 0.28;
        }
        reflRad = min(reflRad, vec3(16.0));
        if (any(isnan(reflRad)) || any(isinf(reflRad))) reflRad = vec3(0.0);
    }
    imageStore(uRtgiRawOut, pix, vec4(irr, sunVis));
    imageStore(uRtReflRawOut, pix, vec4(reflRad, 1.0));
}
)GLSL";

const char* kFallbackRtgiDenoiseComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uRtgiRaw;
layout(set = 0, binding = 1) uniform sampler2D uRtgiPrevHistory;
layout(set = 0, binding = 2) uniform sampler2D uRtReflRaw;
layout(set = 0, binding = 3) uniform sampler2D uRtReflPrevHistory;
layout(set = 0, binding = 4) uniform sampler2D uNormalRough;
layout(set = 0, binding = 5) uniform sampler2D uDepth;
layout(set = 0, binding = 6) uniform sampler2D uMotion;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D uRtgiCurrHistory;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D uRtReflCurrHistory;
layout(set = 0, binding = 9) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uRtgiCurrHistory);
    if (pix.x >= size.x || pix.y >= size.y) return;
    float depth = texelFetch(uDepth, pix, 0).r;
    if (depth >= 0.99999 || uGi.hasValidTLAS == 0) { imageStore(uRtgiCurrHistory, pix, vec4(0,0,0,1)); imageStore(uRtReflCurrHistory, pix, vec4(0,0,0,1)); return; }
    vec4 centerGi = texelFetch(uRtgiRaw, pix, 0); vec4 centerRefl = texelFetch(uRtReflRaw, pix, 0);
    if (any(isnan(centerGi)) || any(isinf(centerGi))) centerGi = vec4(0,0,0,1);
    if (any(isnan(centerRefl)) || any(isinf(centerRefl))) centerRefl = vec4(0,0,0,1);
    if (uGi.giDenoiserEnabled == 0) { imageStore(uRtgiCurrHistory, pix, centerGi); imageStore(uRtReflCurrHistory, pix, centerRefl); return; }
    vec4 centerNR = texelFetch(uNormalRough, pix, 0);
    vec3 centerN = normalize(centerNR.xyz); float centerRough = clamp(centerNR.w, 0.04, 1.0);
    float centerLogD = log(max(1.0 - depth, 1e-5));
    vec4 giM1 = vec4(0); vec4 giM2 = vec4(0); vec3 reflM1 = vec3(0); vec3 reflM2 = vec3(0); float cnt = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 q = clamp(pix + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            float qDepth = texelFetch(uDepth, q, 0).r;
            if (qDepth >= 0.99999) continue;
            vec3 qN = normalize(texelFetch(uNormalRough, q, 0).xyz);
            float qLogD = log(max(1.0 - qDepth, 1e-5));
            if (dot(centerN, qN) < 0.70 || abs(centerLogD - qLogD) > 0.25) continue;
            vec4 qGi = texelFetch(uRtgiRaw, q, 0); vec3 qRefl = texelFetch(uRtReflRaw, q, 0).rgb;
            if (any(isnan(qGi)) || any(isinf(qGi))) qGi = centerGi;
            if (any(isnan(qRefl)) || any(isinf(qRefl))) qRefl = centerRefl.rgb;
            giM1 += qGi; giM2 += qGi * qGi; reflM1 += qRefl; reflM2 += qRefl * qRefl; cnt += 1.0;
        }
    }
    vec4 giMean = (cnt > 0.5) ? (giM1 / cnt) : centerGi; vec4 giStd = (cnt > 0.5) ? sqrt(max(giM2 / cnt - giMean * giMean, vec4(0))) : vec4(0);
    vec3 reflMean = (cnt > 0.5) ? (reflM1 / cnt) : centerRefl.rgb; vec3 reflStd = (cnt > 0.5) ? sqrt(max(reflM2 / cnt - reflMean * reflMean, vec3(0))) : vec3(0);
    vec2 uv = (vec2(pix) + 0.5) / vec2(size); vec2 prevUV = uv - texelFetch(uMotion, pix, 0).rg;
    vec4 giRes = centerGi; vec3 reflRes = centerRefl.rgb;
    if (uGi.frameIndex > 0u && all(greaterThan(prevUV, vec2(0))) && all(lessThan(prevUV, vec2(1)))) {
        float prevDepth = textureLod(uDepth, prevUV, 0.0).r;
        vec3 prevN = normalize(textureLod(uNormalRough, prevUV, 0.0).xyz);
        if (prevDepth < 0.99999 && dot(centerN, prevN) > 0.85 && abs(centerLogD - log(max(1.0 - prevDepth, 1e-5))) < 0.08) {
            vec4 prevGi = textureLod(uRtgiPrevHistory, prevUV, 0.0);
            if (!any(isnan(prevGi)) && !any(isinf(prevGi))) giRes = mix(clamp(prevGi, giMean - giStd * 1.75, giMean + giStd * 1.75), centerGi, clamp(uGi.giTemporalAlpha, 0.03, 0.35));
            vec3 prevRefl = textureLod(uRtReflPrevHistory, prevUV, 0.0).rgb;
            if (!any(isnan(prevRefl)) && !any(isinf(prevRefl))) {
                float alphaR = mix(clamp(uGi.giTemporalAlpha, 0.04, 0.25), 0.28, 1.0 - smoothstep(0.06, 0.35, centerRough));
                reflRes = mix(clamp(prevRefl, reflMean - reflStd * 1.85, reflMean + reflStd * 1.85), centerRefl.rgb, alphaR);
            }
        }
    }
    if (any(isnan(giRes)) || any(isinf(giRes))) giRes = vec4(0,0,0,1);
    if (any(isnan(reflRes)) || any(isinf(reflRes))) reflRes = vec3(0);
    imageStore(uRtgiCurrHistory, pix, vec4(max(giRes.rgb, vec3(0)), clamp(giRes.a, 0.0, 1.0)));
    imageStore(uRtReflCurrHistory, pix, vec4(max(reflRes, vec3(0)), 1.0));
}
)GLSL";

const char* kFallbackRtgiAtrousComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uRtgiIn;
layout(set = 0, binding = 1) uniform sampler2D uRtReflIn;
layout(set = 0, binding = 2) uniform sampler2D uNormalRough;
layout(set = 0, binding = 3) uniform sampler2D uDepth;
layout(set = 0, binding = 4, rgba16f) uniform writeonly image2D uRtgiOut;
layout(set = 0, binding = 5, rgba16f) uniform writeonly image2D uRtReflOut;
layout(set = 0, binding = 6) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
layout(push_constant) uniform AtrousPC { int stepSize; } pc;
float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uRtgiOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    float depth = texelFetch(uDepth, pix, 0).r;
    if (depth >= 0.99999 || uGi.hasValidTLAS == 0) { imageStore(uRtgiOut, pix, vec4(0,0,0,1)); imageStore(uRtReflOut, pix, vec4(0,0,0,1)); return; }
    vec4 centerGi = texelFetch(uRtgiIn, pix, 0); vec4 centerRefl = texelFetch(uRtReflIn, pix, 0);
    if (any(isnan(centerGi)) || any(isinf(centerGi))) centerGi = vec4(0,0,0,1);
    if (any(isnan(centerRefl)) || any(isinf(centerRefl))) centerRefl = vec4(0,0,0,1);
    if (uGi.giDenoiserEnabled == 0 || pc.stepSize <= 0) { imageStore(uRtgiOut, pix, centerGi); imageStore(uRtReflOut, pix, centerRefl); return; }
    vec4 centerNR = texelFetch(uNormalRough, pix, 0);
    vec3 centerN = normalize(centerNR.xyz); float centerRough = clamp(centerNR.w, 0.04, 1.0);
    float centerLogD = log(max(1.0 - depth, 1e-5));
    float cGiL = Luminance(centerGi.rgb); float cSh = centerGi.a; float cRfL = Luminance(centerRefl.rgb);
    float reflBlurFactor = smoothstep(0.07, 0.32, centerRough);
    float phiRefl = mix(0.35, 2.2, centerRough);
    const float kWeights[3] = float[3](0.375, 0.25, 0.0625);
    vec3 giSum = centerGi.rgb * 0.140625; float giW = 0.140625;
    float shSum = cSh * 0.140625; float shW = 0.140625;
    vec3 rfSum = centerRefl.rgb * 0.140625; float rfW = 0.140625;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;
            ivec2 q = pix + ivec2(dx, dy) * pc.stepSize;
            if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y) continue;
            float qD = texelFetch(uDepth, q, 0).r;
            if (qD >= 0.99999) continue;
            vec4 qNR = texelFetch(uNormalRough, q, 0);
            float nW = pow(max(dot(centerN, normalize(qNR.xyz)), 0.0), 32.0);
            float dRel = abs(centerLogD - log(max(1.0 - qD, 1e-5)));
            float baseW = kWeights[abs(dx)] * kWeights[abs(dy)] * nW * exp(-dRel * dRel * 18.0);
            if (baseW <= 1e-5) continue;
            vec4 qGi = texelFetch(uRtgiIn, q, 0);
            if (!any(isnan(qGi)) && !any(isinf(qGi))) {
                float gDiff = abs(cGiL - Luminance(qGi.rgb)) / 1.65;
                float gW = baseW * exp(-gDiff * gDiff * 0.5);
                giSum += qGi.rgb * gW; giW += gW;
                float sDiff = abs(cSh - qGi.a) / 0.65;
                float sW = baseW * exp(-sDiff * sDiff * 0.5);
                shSum += qGi.a * sW; shW += sW;
            }
            if (reflBlurFactor > 0.01) {
                vec4 qRf = texelFetch(uRtReflIn, q, 0);
                if (!any(isnan(qRf)) && !any(isinf(qRf))) {
                    float rDiff = abs(cRfL - Luminance(qRf.rgb)) / phiRefl;
                    float rW = baseW * exp(-abs(centerRough - clamp(qNR.w, 0.04, 1.0)) * 8.0) * exp(-rDiff * rDiff * 0.5);
                    rfSum += qRf.rgb * rW; rfW += rW;
                }
            }
        }
    }
    vec3 outRf = mix(centerRefl.rgb, max(rfSum / max(rfW, 1e-4), vec3(0)), reflBlurFactor);
    imageStore(uRtgiOut, pix, vec4(max(giSum / max(giW, 1e-4), vec3(0)), clamp(shSum / max(shW, 1e-4), 0.0, 1.0)));
    imageStore(uRtReflOut, pix, vec4(max(outRf, vec3(0)), 1.0));
}
)GLSL";

const char* kFallbackPathtraceCompA = R"GLSL(#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(buffer_reference, scalar) readonly buffer FloatBuf { float data[]; };
layout(buffer_reference, scalar) readonly buffer UintBuf  { uint data[]; };
struct GpuMaterialData { vec4 albedoColor; vec4 emissiveColor; vec4 pbrParams; vec4 extraParams; ivec4 texIndices0; ivec4 texIndices1; };
struct GpuRtInstanceInfo { uint64_t vertexAddress; uint64_t indexAddress; uint materialIndex; uint firstIndex; int vertexOffset; uint vertexStrideFloats; mat4 prevModel; };
struct GpuDirLight { vec4 directionIntensity; vec4 colorPad; };
struct GpuPointLight { vec4 positionRange; vec4 colorIntensity; };
struct GpuSpotLight { vec4 positionRange; vec4 directionCosOuter; vec4 colorIntensityCosIn; };
layout(set = 0, binding = 0) uniform sampler2D uTextures[];
layout(set = 0, binding = 1) readonly buffer MaterialBuffer { GpuMaterialData materials[]; } uMaterials;
layout(set = 1, binding = 0, rgba16f) uniform image2D uPtAccum;
layout(set = 1, binding = 1, rgba16f) uniform writeonly image2D uHdrOut;
layout(set = 1, binding = 2) uniform accelerationStructureEXT uTLAS;
layout(set = 1, binding = 3) readonly buffer RtInstanceInfoBuffer { GpuRtInstanceInfo rtInstances[]; } uRtInstances;
layout(set = 1, binding = 4) uniform SceneLightingUBO {
    mat4 invViewProj; vec4 cameraPos; vec4 skyZenith; vec4 skyHorizon; vec4 skyGround;
    vec4 sunDirIntensity; vec4 sunColorParams; ivec4 counts;
    GpuDirLight dirLights[4]; GpuPointLight pointLights[32]; GpuSpotLight spotLights[16];
    vec4 cloudParams0; vec4 cloudParams1; vec4 cloudParams2; vec4 cloudAmbient;
    mat4 currViewProj; mat4 prevViewProj;
} uScene;
layout(set = 1, binding = 5) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
layout(set = 1, binding = 6, rgba16f) uniform writeonly image2D uPtNormalRoughOut;
layout(set = 1, binding = 7, rgba16f) uniform image2D uPtAlbedoOut;
layout(set = 1, binding = 8, rgba16f) uniform image2D uPtEmissiveOut;
layout(set = 1, binding = 9, rgba32f) uniform writeonly image2D uPtDepthInstOut;
layout(set = 1, binding = 10, rg16f)  uniform writeonly image2D uPtMotionOut;
layout(set = 1, binding = 11, rgba16f) uniform image2D uPtSpecAccum;
const float PI = 3.14159265359;
uint PcgHash(uint v) { uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }
float Rand01(inout uint seed) { seed = PcgHash(seed); return float(seed) * (1.0 / 4294967296.0); }
vec2 R2Sequence(uint n, vec2 pixelOffset) { return fract(pixelOffset + vec2(0.7548776662, 0.5698402910) * float(n)); }
vec2 SampleBlueNoise2D(ivec2 pix, uint sampleIndex, uint dimension) {
    uvec2 p = uvec2(pix) & 127u;
    uint h0 = (p.x * 47u + p.y * 97u + (p.x ^ p.y) * 131u) & 255u;
    uint h1 = (p.x * 73u + p.y * 53u + ((p.x + p.y) ^ (p.x * 3u)) * 179u) & 255u;
    h0 = ((h0 & 0x55u) << 1u) | ((h0 & 0xAAu) >> 1u);
    h0 = ((h0 & 0x33u) << 2u) | ((h0 & 0xCCu) >> 2u);
    h0 = ((h0 & 0x0Fu) << 4u) | ((h0 & 0xF0u) >> 4u);
    h1 = ((h1 & 0x55u) << 1u) | ((h1 & 0xAAu) >> 1u);
    h1 = ((h1 & 0x33u) << 2u) | ((h1 & 0xCCu) >> 2u);
    h1 = ((h1 & 0x0Fu) << 4u) | ((h1 & 0xF0u) >> 4u);
    vec2 spatialBlue = (vec2(float(h0), float(h1)) + 0.5) * (1.0 / 256.0);
    float t = float(sampleIndex) + float(dimension) * 17.0;
    vec2 temporalOffset = fract(vec2(0.7548776662, 0.5698402910) * t + vec2(0.6180339887, 0.3819660112) * float(dimension));
    return fract(spatialBlue + temporalOffset);
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
vec3 EvaluateVolumetricClouds(vec3 ro, vec3 rd, vec3 skyCol, float ignJitter) {
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
    const int STEPS = 32; float dt = (tEnd - tStart) / float(STEPS);
    vec3 accumCol = vec3(0.0); float transmittance = 1.0;
    for (int i = 0; i < STEPS; ++i) {
        vec3 pos = ro + rd * (tStart + (float(i) + ignJitter) * dt);
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
vec3 EvaluateSky(vec3 dir, bool includeSunDisc) {
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
)GLSL";

const char* kFallbackPathtraceCompB = R"GLSL(float DistributionGGX(vec3 N, vec3 H, float r) { float a = r * r; float a2 = a * a; float NH = max(dot(N, H), 0.0); float d = NH * NH * (a2 - 1.0) + 1.0; return a2 / max(PI * d * d, 1e-5); }
float GeometrySchlickGGX(float NV, float r) { float k = (r + 1.0) * (r + 1.0) / 8.0; return NV / max(NV * (1.0 - k) + k, 1e-5); }
float GeometrySmith(vec3 N, vec3 V, vec3 L, float r) { return GeometrySchlickGGX(max(dot(N, V), 0.0), r) * GeometrySchlickGGX(max(dot(N, L), 0.0), r); }
vec3 FresnelSchlick(float cosT, vec3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0); }
void EvaluateBRDFSplit(vec3 N, vec3 V, vec3 L, vec3 rad, vec3 albedo, float rough, float metal, out vec3 outDiffIrr, out vec3 outSpecRad) {
    float NL = max(dot(N, L), 0.0); if (NL <= 0.0) { outDiffIrr = vec3(0.0); outSpecRad = vec3(0.0); return; }
    vec3 H = normalize(V + L); vec3 F0 = mix(vec3(0.04), albedo, metal); vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (DistributionGGX(N, H, rough) * GeometrySmith(N, V, L, rough) * F) / max(4.0 * max(dot(N, V), 0.0) * NL, 0.001);
    outDiffIrr = ((vec3(1.0) - F) * (1.0 - metal) / PI) * rad * NL;
    outSpecRad = spec * rad * NL;
}
vec3 EvaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 rad, vec3 albedo, float rough, float metal) {
    vec3 dI, sR; EvaluateBRDFSplit(N, V, L, rad, albedo, rough, metal, dI, sR); return dI * albedo + sR;
}
vec3 ApplyNormalMap(vec3 geomN, vec3 localT, vec3 localB, mat4x3 worldToObj, vec2 uv, GpuMaterialData mat) {
    vec3 N = geomN;
    if (mat.texIndices0.y < 0 || dot(localT, localT) < 1e-8 || dot(localB, localB) < 1e-8) return N;
    mat3 objToWorld = inverse(mat3(worldToObj));
    vec3 T = objToWorld * localT; vec3 B = objToWorld * localB;
    T = T - N * dot(T, N); B = B - N * dot(B, N);
    if (dot(T, T) < 1e-8 || dot(B, B) < 1e-8) return N;
    T = normalize(T); B = normalize(B - T * dot(B, T));
    if (dot(B, B) < 1e-8) return N;
    if (dot(cross(N, T), B) < 0.0) B = -B;
    vec3 mapN = textureLod(uTextures[nonuniformEXT(mat.texIndices0.y)], uv, 0.0).xyz * 2.0 - 1.0;
    mapN.xy *= mat.pbrParams.w;
    return normalize(mat3(T, B, N) * mapN);
}
void BuildBasis(vec3 N, out vec3 T, out vec3 B) { vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0); T = normalize(cross(up, N)); B = cross(N, T); }
vec3 SampleCosineHemisphere(vec3 N, float u1, float u2) { float r = sqrt(max(u1, 0.0)); float th = 2.0 * PI * u2; vec3 T, B; BuildBasis(N, T, B); return normalize(T * (r * cos(th)) + B * (r * sin(th)) + N * sqrt(max(1.0 - u1, 0.0))); }
vec3 SampleGGXHalfVector(vec3 N, float r, float u1, float u2) { float a = r * r; float phi = 2.0 * PI * u1; float cosT = sqrt((1.0 - u2) / max(1.0 + (a * a - 1.0) * u2, 1e-5)); float sinT = sqrt(max(1.0 - cosT * cosT, 0.0)); vec3 T, B; BuildBasis(N, T, B); return normalize(T * (cos(phi) * sinT) + B * (sin(phi) * sinT) + N * cosT); }
bool TraceVisibility(vec3 origin, vec3 dir, float tMin, float tMax) { rayQueryEXT rq; rayQueryInitializeEXT(rq, uTLAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFFu, origin, tMin, dir, tMax); while (rayQueryProceedEXT(rq)) {} return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT; }
void EvaluateDirectNEESplit(vec3 hitPos, vec3 N, vec3 V, vec3 hitAlbedo, float rough, float metal, vec2 bnLight, out vec3 outDiffIrr, out vec3 outSpecRad) {
    outDiffIrr = vec3(0.0); outSpecRad = vec3(0.0);
    float coneR = clamp(uScene.sunColorParams.w * 0.35, 0.004, 0.025);
    for (int i = 0; i < clamp(uScene.counts.x, 0, 4); ++i) {
        vec3 bL = normalize(-uScene.dirLights[i].directionIntensity.xyz); vec3 L = bL;
        if (i == 0 && coneR > 0.0001) {
            vec3 sT, sB; BuildBasis(bL, sT, sB);
            float rD = sqrt(max(bnLight.x, 0.0)) * coneR; float pD = 2.0 * PI * bnLight.y;
            vec3 jL = normalize(bL + (sT * cos(pD) + sB * sin(pD)) * rD);
            if (dot(N, jL) > 0.0) L = jL;
        }
        if (dot(N, L) > 0.0 && TraceVisibility(hitPos + N * 0.004, L, 0.002, 500.0)) {
            vec3 dI, sR; EvaluateBRDFSplit(N, V, L, uScene.dirLights[i].colorPad.rgb * uScene.dirLights[i].directionIntensity.w, hitAlbedo, rough, metal, dI, sR);
            outDiffIrr += dI; outSpecRad += sR;
        }
    }
    for (int i = 0; i < clamp(uScene.counts.y, 0, 32); ++i) {
        vec3 toL = uScene.pointLights[i].positionRange.xyz - hitPos;
        float dist = length(toL); float range = max(uScene.pointLights[i].positionRange.w, 0.001);
        if (dist < range) {
            vec3 L = toL / max(dist, 1e-4);
            if (dot(N, L) > 0.0 && TraceVisibility(hitPos + N * 0.004, L, 0.002, max(dist - 0.01, 0.003))) {
                float att = clamp(1.0 - dist / range, 0.0, 1.0); att = (att * att) / (1.0 + 0.1 * dist * dist);
                vec3 dI, sR; EvaluateBRDFSplit(N, V, L, uScene.pointLights[i].colorIntensity.rgb * uScene.pointLights[i].colorIntensity.w * att, hitAlbedo, rough, metal, dI, sR);
                outDiffIrr += dI; outSpecRad += sR;
            }
        }
    }
    for (int i = 0; i < clamp(uScene.counts.z, 0, 16); ++i) {
        vec3 toL = uScene.spotLights[i].positionRange.xyz - hitPos;
        float dist = length(toL); float range = max(uScene.spotLights[i].positionRange.w, 0.001);
        if (dist < range) {
            vec3 L = toL / max(dist, 1e-4);
            float theta = dot(-L, normalize(uScene.spotLights[i].directionCosOuter.xyz));
            float cosOut = uScene.spotLights[i].directionCosOuter.w;
            float spot = clamp((theta - cosOut) / max(mix(cosOut, 1.0, 0.35) - cosOut, 0.001), 0.0, 1.0);
            if (spot > 0.0 && dot(N, L) > 0.0 && TraceVisibility(hitPos + N * 0.004, L, 0.002, max(dist - 0.01, 0.003))) {
                float att = clamp(1.0 - dist / range, 0.0, 1.0); att = (att * att) / (1.0 + 0.1 * dist * dist);
                vec3 dI, sR; EvaluateBRDFSplit(N, V, L, uScene.spotLights[i].colorIntensityCosIn.rgb * uScene.spotLights[i].colorIntensityCosIn.w * att * spot, hitAlbedo, rough, metal, dI, sR);
                outDiffIrr += dI; outSpecRad += sR;
            }
        }
    }
}
vec3 EvaluateDirectNEE(vec3 hitPos, vec3 N, vec3 V, vec3 hitAlbedo, float rough, float metal, vec2 bnLight) {
    vec3 dI, sR; EvaluateDirectNEESplit(hitPos, N, V, hitAlbedo, rough, metal, bnLight, dI, sR); return dI * hitAlbedo + sR;
}
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uPtAccum);
    if (pix.x >= size.x || pix.y >= size.y) return;
    if (uGi.giMode == 3 && uGi.sampleCount > 0u) return;
    const bool isRealtimeSvgf = (uGi.giMode == 4);
    uint spp = uint(clamp(uGi.giRaysPerPixel, 1, isRealtimeSvgf ? 2 : 4));
    int maxB = clamp(uGi.giBounces, 1, 8);
    vec3 accumDiffIrr = vec3(0.0); vec3 accumSpecRad = vec3(0.0); vec3 accumAlbedo = vec3(0.0); vec3 accumEmissive = vec3(0.0);
    vec3 primaryNormOut = vec3(0.0); float primaryRoughOut = 1.0; float primaryMetalOut = 0.0;
    float primaryDepthOut = 0.0; float primaryInstOut = 0.0; float primaryWindowZ = 1.0; vec2 primaryMotionOut = vec2(0.0);
    for (uint sIdx = 0u; sIdx < spp; ++sIdx) {
        uint seqIndex = (max(uGi.frameIndex, 1u) - 1u) * spp + sIdx;
        vec2 bnPrimary = SampleBlueNoise2D(pix, seqIndex, 0u);
        vec2 jitter = (!isRealtimeSvgf && (uGi.sampleCount > 1u || spp > 1u)) ? bnPrimary : vec2(0.5);
        float ign = bnPrimary.x;
        vec2 uv = (vec2(pix) + jitter) / vec2(size);
        vec4 farW = uScene.invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
        vec3 ro = uScene.cameraPos.xyz; vec3 rd = normalize(farW.xyz / max(abs(farW.w), 1e-6) - ro);
        vec3 sDiffIrr = vec3(0.0); vec3 sSpecRad = vec3(0.0); vec3 primaryAlb = vec3(1.0); vec3 primaryEmis = vec3(0.0);
        if (uGi.hasValidTLAS == 0) {
            primaryEmis = EvaluateVolumetricClouds(ro, rd, EvaluateSky(rd, true), ign);
        } else {
            vec3 tp = vec3(1.0); bool firstIsSpec = false;
            for (int b = 0; b < maxB; ++b) {
                rayQueryEXT rq; rayQueryInitializeEXT(rq, uTLAS, gl_RayFlagsOpaqueEXT, 0xFFu, ro, 0.001, rd, 500.0);
                while (rayQueryProceedEXT(rq)) {}
                if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
                    vec3 sCol = EvaluateSky(rd, b == 0 || (b == 1 && firstIsSpec));
                    if (b == 0) { primaryEmis = EvaluateVolumetricClouds(ro, rd, sCol, ign); }
                    else {
                        if (b == 1 && firstIsSpec) sCol = EvaluateVolumetricClouds(ro, rd, sCol, ign);
                        if (firstIsSpec) sSpecRad += tp * sCol; else sDiffIrr += tp * sCol;
                    }
                    break;
                }
)GLSL";

const char* kFallbackPathtraceCompC = R"GLSL(                float tHit = rayQueryGetIntersectionTEXT(rq, true);
                uint instIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
                uint primIdx = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
                vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
                vec3 w = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
                GpuRtInstanceInfo info = uRtInstances.rtInstances[instIdx];
                UintBuf idxBuf = UintBuf(info.indexAddress); FloatBuf vBuf = FloatBuf(info.vertexAddress);
                uint i0 = idxBuf.data[info.firstIndex + primIdx * 3u + 0u] + uint(info.vertexOffset);
                uint i1 = idxBuf.data[info.firstIndex + primIdx * 3u + 1u] + uint(info.vertexOffset);
                uint i2 = idxBuf.data[info.firstIndex + primIdx * 3u + 2u] + uint(info.vertexOffset);
                uint s = info.vertexStrideFloats; uint b0 = i0 * s; uint b1 = i1 * s; uint b2 = i2 * s;
                vec3 localN = normalize(vec3(vBuf.data[b0+3u], vBuf.data[b0+4u], vBuf.data[b0+5u]) * w.x +
                                        vec3(vBuf.data[b1+3u], vBuf.data[b1+4u], vBuf.data[b1+5u]) * w.y +
                                        vec3(vBuf.data[b2+3u], vBuf.data[b2+4u], vBuf.data[b2+5u]) * w.z);
                vec3 localT = vec3(vBuf.data[b0+10u], vBuf.data[b0+11u], vBuf.data[b0+12u]) * w.x +
                              vec3(vBuf.data[b1+10u], vBuf.data[b1+11u], vBuf.data[b1+12u]) * w.y +
                              vec3(vBuf.data[b2+10u], vBuf.data[b2+11u], vBuf.data[b2+12u]) * w.z;
                vec3 localB = vec3(vBuf.data[b0+13u], vBuf.data[b0+14u], vBuf.data[b0+15u]) * w.x +
                              vec3(vBuf.data[b1+13u], vBuf.data[b1+14u], vBuf.data[b1+15u]) * w.y +
                              vec3(vBuf.data[b2+13u], vBuf.data[b2+14u], vBuf.data[b2+15u]) * w.z;
                mat4x3 worldToObj = rayQueryGetIntersectionWorldToObjectEXT(rq, true);
                vec3 N = normalize(transpose(mat3(worldToObj)) * localN);
                if (dot(N, -rd) < 0.0) N = -N;
                vec2 hitUv = vec2(vBuf.data[b0+6u], vBuf.data[b0+7u]) * w.x +
                             vec2(vBuf.data[b1+6u], vBuf.data[b1+7u]) * w.y +
                             vec2(vBuf.data[b2+6u], vBuf.data[b2+7u]) * w.z;
                vec3 hitPos = ro + rd * tHit; vec3 V = -rd;
                GpuMaterialData mat = uMaterials.materials[info.materialIndex];
                N = ApplyNormalMap(N, localT, localB, worldToObj, hitUv, mat);
                vec3 hitAlbedo = mat.albedoColor.rgb;
                if (mat.texIndices0.x >= 0) hitAlbedo *= textureLod(uTextures[nonuniformEXT(mat.texIndices0.x)], hitUv, 0.0).rgb;
                hitAlbedo = max(hitAlbedo, vec3(0.0));
                float rough = clamp(mat.pbrParams.x, 0.04, 1.0);
                if (mat.texIndices0.z >= 0) rough *= textureLod(uTextures[nonuniformEXT(mat.texIndices0.z)], hitUv, 0.0).g;
                if (mat.extraParams.z > 0.5) rough = 1.0 - rough;
                rough = clamp(rough, 0.04, 1.0);
                float metal = clamp(mat.pbrParams.y, 0.0, 1.0);
                if (mat.texIndices0.w >= 0) metal *= textureLod(uTextures[nonuniformEXT(mat.texIndices0.w)], hitUv, 0.0).b;
                metal = clamp(metal, 0.0, 1.0);
                vec3 hitEmis = mat.emissiveColor.rgb * mat.emissiveColor.a;
                if (mat.texIndices1.y >= 0) hitEmis *= textureLod(uTextures[nonuniformEXT(mat.texIndices1.y)], hitUv, 0.0).rgb;
                vec3 F0 = mix(vec3(0.04), hitAlbedo, metal);
                if (b == 0) {
                    primaryAlb = hitAlbedo;
                    primaryEmis = hitEmis;
                    if (sIdx == 0u) {
                        primaryNormOut = N; primaryRoughOut = rough; primaryMetalOut = metal;
                        primaryDepthOut = tHit; primaryInstOut = float(instIdx + 1u);
                        vec3 localPos = vec3(vBuf.data[b0+0u], vBuf.data[b0+1u], vBuf.data[b0+2u]) * w.x +
                                        vec3(vBuf.data[b1+0u], vBuf.data[b1+1u], vBuf.data[b1+2u]) * w.y +
                                        vec3(vBuf.data[b2+0u], vBuf.data[b2+1u], vBuf.data[b2+2u]) * w.z;
                        vec3 prevW = (info.prevModel * vec4(localPos, 1.0)).xyz;
                        vec4 cC = uScene.currViewProj * vec4(hitPos, 1.0);
                        vec4 pC = uScene.prevViewProj * vec4(prevW, 1.0);
                        primaryMotionOut = ((cC.xy / max(abs(cC.w), 1e-6)) - (pC.xy / max(abs(pC.w), 1e-6))) * 0.5;
                        primaryWindowZ = clamp(cC.z / max(abs(cC.w), 1e-6), 0.0, 1.0);
                    }
                } else {
                    if (firstIsSpec) sSpecRad += tp * hitEmis; else sDiffIrr += tp * hitEmis;
                }
                uint dimBase = 1u + uint(b) * 3u;
                vec2 bnLight = SampleBlueNoise2D(pix, seqIndex, dimBase + 0u);
                vec2 bnLobe  = SampleBlueNoise2D(pix, seqIndex, dimBase + 1u);
                vec2 bnDir   = SampleBlueNoise2D(pix, seqIndex, dimBase + 2u);
                if (b == 0) {
                    vec3 dI, sR; EvaluateDirectNEESplit(hitPos, N, V, hitAlbedo, rough, metal, bnLight, dI, sR);
                    sDiffIrr += dI; sSpecRad += sR;
                } else {
                    vec3 nee = EvaluateDirectNEE(hitPos, N, V, hitAlbedo, rough, metal, bnLight);
                    if (firstIsSpec) sSpecRad += tp * nee; else sDiffIrr += tp * nee;
                }
                float specProb = clamp(mix(0.25, 0.90, metal) + (1.0 - rough) * 0.35, 0.10, 0.95);
                bool chooseSpec = (bnLobe.x < specProb);
                if (isRealtimeSvgf && b == 0 && spp == 2u && specProb > 0.15 && specProb < 0.88 && metal < 0.95) {
                    chooseSpec = (((sIdx + uint(pix.x + pix.y) + uGi.frameIndex) & 1u) == 0u);
                    specProb = 0.5;
                }
                vec3 nextDir; vec3 bw;
                if (chooseSpec) {
                    if (b == 0) firstIsSpec = true;
                    vec3 H = (rough < 0.055) ? N : SampleGGXHalfVector(N, rough, bnDir.x, bnDir.y);
                    nextDir = reflect(-V, H);
                    if (dot(N, nextDir) <= 0.0) break;
                    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
                    bw = (F * GeometrySmith(N, V, nextDir, rough) * max(dot(V, H), 0.001)) / max(max(dot(N, V), 0.001) * max(dot(N, H), 0.001) * specProb, 1e-4);
                } else {
                    if (b == 0) firstIsSpec = false;
                    nextDir = SampleCosineHemisphere(N, bnDir.x, bnDir.y);
                    vec3 diffBase = (b == 0) ? vec3(1.0) : hitAlbedo;
                    bw = (vec3(1.0) - FresnelSchlick(max(dot(N, V), 0.0), F0)) * (1.0 - metal) * diffBase / max(1.0 - specProb, 1e-4);
                }
                tp *= bw * ((b == 0) ? (firstIsSpec ? 1.0 : max(uGi.giIntensity, 0.0)) : 1.0);
                if (b >= 2) { float ps = clamp(max(tp.r, max(tp.g, tp.b)), 0.05, 0.95); if (bnLobe.y > ps) break; tp /= ps; }
                ro = hitPos + N * 0.004; rd = nextDir;
            }
        }
        sDiffIrr = min(max(sDiffIrr, vec3(0.0)), vec3(24.0));
        sSpecRad = min(max(sSpecRad, vec3(0.0)), vec3(24.0));
        if (!any(isnan(sDiffIrr)) && !any(isinf(sDiffIrr)) && !any(isnan(sSpecRad)) && !any(isinf(sSpecRad))) {
            accumDiffIrr += sDiffIrr;
            accumSpecRad += sSpecRad;
            accumAlbedo += primaryAlb;
            accumEmissive += min(max(primaryEmis, vec3(0.0)), vec3(16.0));
        }
    }
    float invSpp = 1.0 / float(spp);
    vec3 outDiffIrr = accumDiffIrr * invSpp;
    vec3 outSpecRad = accumSpecRad * invSpp;
    vec3 outAlbedo = accumAlbedo * invSpp;
    vec3 outEmissive = accumEmissive * invSpp;
    if (!isRealtimeSvgf && uGi.sampleCount > 1u) {
        float blend = 1.0 / float(uGi.sampleCount);
        outDiffIrr = mix(imageLoad(uPtAccum, pix).rgb, outDiffIrr, blend);
        outSpecRad = mix(imageLoad(uPtSpecAccum, pix).rgb, outSpecRad, blend);
        outAlbedo = mix(imageLoad(uPtAlbedoOut, pix).rgb, outAlbedo, blend);
        outEmissive = mix(imageLoad(uPtEmissiveOut, pix).rgb, outEmissive, blend);
    }
    imageStore(uPtNormalRoughOut, pix, vec4(primaryNormOut, primaryRoughOut));
    imageStore(uPtAlbedoOut, pix, vec4(outAlbedo, primaryMetalOut));
    imageStore(uPtEmissiveOut, pix, vec4(outEmissive, 1.0));
    imageStore(uPtDepthInstOut, pix, vec4(primaryDepthOut, primaryInstOut, primaryWindowZ, 0.0));
    imageStore(uPtMotionOut, pix, vec4(primaryMotionOut, 0.0, 0.0));
    imageStore(uPtAccum, pix, vec4(outDiffIrr, 1.0));
    imageStore(uPtSpecAccum, pix, vec4(outSpecRad, 1.0));
    imageStore(uHdrOut, pix, vec4(outDiffIrr * outAlbedo + outSpecRad + outEmissive, 1.0));
}
)GLSL";

const char* kFallbackPtDenoiseComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uPtAccumIn;
layout(set = 0, binding = 1) uniform sampler2D uPtNormalRoughIn;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D uHdrOut;
layout(set = 0, binding = 3) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
layout(set = 0, binding = 4) uniform sampler2D uPtAlbedoIn;
layout(set = 0, binding = 5) uniform sampler2D uPtEmissiveIn;
layout(set = 0, binding = 6) uniform sampler2D uPtDepthInstIn;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D uPtIrrHistoryOut;
layout(set = 0, binding = 8) uniform sampler2D uPtSpecAccumIn;
float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uHdrOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    vec4 centerIrr = texelFetch(uPtAccumIn, pix, 0);
    if (any(isnan(centerIrr)) || any(isinf(centerIrr))) centerIrr = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 centerSpec = texelFetch(uPtSpecAccumIn, pix, 0);
    if (any(isnan(centerSpec)) || any(isinf(centerSpec))) centerSpec = vec4(0.0, 0.0, 0.0, 1.0);
    vec3 primaryAlbedo = max(texelFetch(uPtAlbedoIn, pix, 0).rgb, vec3(0.0));
    vec3 primaryEmissive = max(texelFetch(uPtEmissiveIn, pix, 0).rgb, vec3(0.0));
    if (any(isnan(primaryAlbedo)) || any(isinf(primaryAlbedo))) primaryAlbedo = vec3(1.0);
    if (any(isnan(primaryEmissive)) || any(isinf(primaryEmissive))) primaryEmissive = vec3(0.0);
    vec4 centerNormRough = texelFetch(uPtNormalRoughIn, pix, 0);
    vec2 centerDepthInst = texelFetch(uPtDepthInstIn, pix, 0).rg;
    float centerDist = centerDepthInst.x; float centerInst = centerDepthInst.y;
    if (centerDist <= 0.0001) {
        imageStore(uPtIrrHistoryOut, pix, vec4(0.0));
        imageStore(uHdrOut, pix, vec4(max(centerIrr.rgb * primaryAlbedo + centerSpec.rgb + primaryEmissive, vec3(0.0)), 1.0));
        return;
    }
    const bool isRealtimeSvgf = (uGi.giMode == 4);
    if (uGi.giDenoiserEnabled == 0 || (!isRealtimeSvgf && uGi.sampleCount >= 32u)) {
        vec3 cleanIrr = max(centerIrr.rgb, vec3(0.0));
        vec3 cleanSpec = max(centerSpec.rgb, vec3(0.0));
        imageStore(uPtIrrHistoryOut, pix, vec4(cleanIrr, 1.0));
        imageStore(uHdrOut, pix, vec4(cleanIrr * primaryAlbedo + cleanSpec + primaryEmissive, 1.0));
        return;
    }
    vec3 centerN = normalize(centerNormRough.xyz);
    float centerRough = clamp(centerNormRough.w, 0.04, 1.0);
    float centerLuma = Luminance(centerIrr.rgb);
    float centerSpecLuma = Luminance(centerSpec.rgb);
    float blendStrength = isRealtimeSvgf ? 0.92 : clamp(1.0 - float(max(uGi.sampleCount, 1u) - 1u) / 31.0, 0.0, 1.0);
    float lumaPhi = isRealtimeSvgf ? 1.85 : mix(2.2, 0.45, 1.0 - blendStrength);
    float specBlurFactor = (centerRough < 0.15) ? 0.0 : smoothstep(0.15, 0.45, centerRough);
    vec3 sumIrr = centerIrr.rgb; float sumW = 1.0;
    vec3 sumSpec = centerSpec.rgb; float sumSpecW = 1.0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;
            ivec2 q = clamp(pix + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            vec2 qDepthInst = texelFetch(uPtDepthInstIn, q, 0).rg;
            if (qDepthInst.x <= 0.0001 || abs(centerInst - qDepthInst.y) > 0.5) continue;
            vec4 qNR = texelFetch(uPtNormalRoughIn, q, 0);
            vec3 qN = normalize(qNR.xyz);
            float nW = pow(max(dot(centerN, qN), 0.0), 64.0);
            float dRel = abs(centerDist - qDepthInst.x) / max(centerDist, 0.1);
            float dW = exp(-dRel * dRel * 16.0);
            float spatialW = exp(-0.18 * float(dx * dx + dy * dy));
            vec3 qIrr = texelFetch(uPtAccumIn, q, 0).rgb;
            if (!any(isnan(qIrr)) && !any(isinf(qIrr))) {
                float lDiff = abs(centerLuma - Luminance(qIrr)) / max(lumaPhi, 0.1);
                float w = spatialW * nW * dW * exp(-lDiff * lDiff * 0.5);
                if (w > 1e-4) { sumIrr += qIrr * w; sumW += w; }
            }
            if (specBlurFactor > 0.001) {
                vec3 qSpec = texelFetch(uPtSpecAccumIn, q, 0).rgb;
                if (!any(isnan(qSpec)) && !any(isinf(qSpec))) {
                    float sDiff = abs(centerSpecLuma - Luminance(qSpec)) / max(lumaPhi * centerRough, 0.1);
                    float rW = exp(-abs(centerRough - clamp(qNR.w, 0.04, 1.0)) / 0.06);
                    float wS = spatialW * pow(max(dot(centerN, qN), 0.0), 128.0) * dW * rW * exp(-sDiff * sDiff * 0.5);
                    if (wS > 1e-4) { sumSpec += qSpec * wS; sumSpecW += wS; }
                }
            }
        }
    }
    vec3 filteredIrr = max(mix(centerIrr.rgb, sumIrr / max(sumW, 1e-4), blendStrength), vec3(0.0));
    vec3 filteredSpec = max(mix(centerSpec.rgb, sumSpec / max(sumSpecW, 1e-4), blendStrength * specBlurFactor), vec3(0.0));
    if (any(isnan(filteredIrr)) || any(isinf(filteredIrr))) filteredIrr = max(centerIrr.rgb, vec3(0.0));
    if (any(isnan(filteredSpec)) || any(isinf(filteredSpec))) filteredSpec = max(centerSpec.rgb, vec3(0.0));
    imageStore(uPtIrrHistoryOut, pix, vec4(filteredIrr, 1.0));
    imageStore(uHdrOut, pix, vec4(filteredIrr * primaryAlbedo + filteredSpec + primaryEmissive, 1.0));
}
)GLSL";

const char* kFallbackPtSvgfTemporalComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uCurrRawIrr;
layout(set = 0, binding = 1) uniform sampler2D uCurrNormalRough;
layout(set = 0, binding = 2) uniform sampler2D uCurrDepthInst;
layout(set = 0, binding = 3) uniform sampler2D uMotionTex;
layout(set = 0, binding = 4) uniform sampler2D uPrevIrrHistory;
layout(set = 0, binding = 5) uniform sampler2D uPrevMoments;
layout(set = 0, binding = 6) uniform sampler2D uPrevNormalRough;
layout(set = 0, binding = 7) uniform sampler2D uPrevDepthInst;
layout(set = 0, binding = 8, rgba16f) uniform writeonly image2D uCurrIrrHistoryOut;
layout(set = 0, binding = 9, rgba16f) uniform writeonly image2D uCurrMomentsOut;
layout(set = 0, binding = 10) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
layout(set = 0, binding = 11) uniform sampler2D uCurrRawSpec;
layout(set = 0, binding = 12) uniform sampler2D uPrevSpecHistory;
layout(set = 0, binding = 13, rgba16f) uniform writeonly image2D uCurrSpecHistoryOut;
float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uCurrIrrHistoryOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    vec3 currIrr = max(texelFetch(uCurrRawIrr, pix, 0).rgb, vec3(0.0));
    if (any(isnan(currIrr)) || any(isinf(currIrr))) currIrr = vec3(0.0);
    vec3 currSpec = max(texelFetch(uCurrRawSpec, pix, 0).rgb, vec3(0.0));
    if (any(isnan(currSpec)) || any(isinf(currSpec))) currSpec = vec3(0.0);
    vec2 currDI = texelFetch(uCurrDepthInst, pix, 0).rg;
    float currDepth = currDI.x; float currInst = currDI.y;
    if (currDepth <= 0.0001) {
        imageStore(uCurrIrrHistoryOut, pix, vec4(currIrr, 0.0));
        imageStore(uCurrSpecHistoryOut, pix, vec4(currSpec, 0.0));
        imageStore(uCurrMomentsOut, pix, vec4(0.0));
        return;
    }
    vec4 currNR = texelFetch(uCurrNormalRough, pix, 0);
    vec3 currN = normalize(currNR.xyz); float currRough = clamp(currNR.w, 0.04, 1.0);
    float currLuma = Luminance(currIrr); vec2 currMom = vec2(currLuma, currLuma * currLuma);
    float currSpecLuma = Luminance(currSpec);
    float dRight = texelFetch(uCurrDepthInst, min(pix + ivec2(1, 0), size - ivec2(1)), 0).r;
    float dUp = texelFetch(uCurrDepthInst, min(pix + ivec2(0, 1), size - ivec2(1)), 0).r;
    float gradMag = length(vec2(abs(dRight - currDepth), abs(dUp - currDepth)));
    vec2 uv = (vec2(pix) + 0.5) / vec2(size);
    vec2 motion = texelFetch(uMotionTex, pix, 0).rg;
    if (any(isnan(motion)) || any(isinf(motion))) motion = vec2(0.0);
    vec2 prevUv = uv - motion;
    vec3 prevIrrSum = vec3(0.0); vec3 prevSpecSum = vec3(0.0); float prevSpecVarSum = 0.0;
    vec2 prevMomSum = vec2(0.0); float prevHistSum = 0.0; float prevSpecMu1Sum = 0.0;
    float wSum = 0.0; float wSpecSum = 0.0;
    if (uGi.frameIndex > 1u && prevUv.x >= 0.0 && prevUv.x <= 1.0 && prevUv.y >= 0.0 && prevUv.y <= 1.0) {
        vec2 prevPixF = prevUv * vec2(size) - 0.5;
        ivec2 baseP = ivec2(floor(prevPixF)); vec2 f = fract(prevPixF);
        float bw[4] = float[4]((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
        ivec2 offsets[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));
        for (int i = 0; i < 4; ++i) {
            ivec2 q = baseP + offsets[i];
            if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y) continue;
            vec2 qDI = texelFetch(uPrevDepthInst, q, 0).rg;
            if (qDI.x <= 0.0001 || abs(currInst - qDI.y) > 0.5) continue;
            vec4 qNR = texelFetch(uPrevNormalRough, q, 0);
            vec3 qN = normalize(qNR.xyz); float nDot = dot(currN, qN);
            if (nDot < 0.90) continue;
            if (abs(currDepth - qDI.x) > max(0.07 * currDepth, 2.2 * gradMag + 0.015)) continue;
            vec4 qHist = texelFetch(uPrevIrrHistory, q, 0);
            vec4 qMom = texelFetch(uPrevMoments, q, 0);
            if (qMom.z < 0.5 || any(isnan(qHist)) || any(isinf(qHist))) continue;
            float w = bw[i];
            prevIrrSum += max(qHist.rgb, vec3(0.0)) * w;
            prevMomSum += max(qMom.xy, vec2(0.0)) * w;
            prevHistSum += qMom.z * w;
            wSum += w;
            float qRough = clamp(qNR.w, 0.04, 1.0);
            if (nDot >= mix(0.985, 0.92, smoothstep(0.05, 0.45, currRough)) && abs(currRough - qRough) < 0.15) {
                vec4 qSpecHist = texelFetch(uPrevSpecHistory, q, 0);
                if (!any(isnan(qSpecHist)) && !any(isinf(qSpecHist))) {
                    prevSpecSum += max(qSpecHist.rgb, vec3(0.0)) * w;
                    prevSpecVarSum += max(qSpecHist.a, 0.0) * w;
                    prevSpecMu1Sum += max(qMom.w, 0.0) * w;
                    wSpecSum += w;
                }
            }
        }
    }
    vec3 outIrr = currIrr; vec2 outMoments = currMom; float outHistLen = 1.0;
    if (wSum > 0.01) {
        vec3 histIrr = prevIrrSum / wSum; vec2 histMom = prevMomSum / wSum;
        float histLen = min((prevHistSum / wSum) + 1.0, 32.0);
        vec3 m1 = vec3(0.0); vec3 m2 = vec3(0.0); float nCnt = 0.0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            ivec2 np = clamp(pix + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            vec3 c = max(texelFetch(uCurrRawIrr, np, 0).rgb, vec3(0.0));
            if (any(isnan(c)) || any(isinf(c))) continue;
            m1 += c; m2 += c * c; nCnt += 1.0;
        }
        m1 /= max(nCnt, 1.0);
        vec3 sigma = sqrt(max(m2 / max(nCnt, 1.0) - m1 * m1, vec3(0.0)));
        histIrr = clamp(histIrr, max(m1 - sigma * 3.5, vec3(0.0)), m1 + sigma * 3.5 + vec3(0.20));
        float alphaIrr = max(clamp(uGi.giTemporalAlpha, 0.04, 0.50), 1.0 / histLen);
        float alphaMom = max(0.20, 1.0 / histLen);
        outIrr = mix(histIrr, currIrr, alphaIrr);
        outMoments = mix(histMom, currMom, alphaMom);
        outHistLen = histLen;
    }
    vec3 outSpec = currSpec; float outSpecMu1 = currSpecLuma; float outSpecVar = currSpecLuma * currSpecLuma * 0.25;
    if (wSpecSum > 0.01) {
        vec3 histSpec = prevSpecSum / wSpecSum;
        float histSpecVar = prevSpecVarSum / wSpecSum;
        float histSpecMu1 = prevSpecMu1Sum / wSpecSum;
        vec3 sm1 = vec3(0.0); vec3 sm2 = vec3(0.0); float sCnt = 0.0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            ivec2 np = clamp(pix + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            vec3 c = max(texelFetch(uCurrRawSpec, np, 0).rgb, vec3(0.0));
            if (any(isnan(c)) || any(isinf(c))) continue;
            sm1 += c; sm2 += c * c; sCnt += 1.0;
        }
        sm1 /= max(sCnt, 1.0);
        vec3 sSigma = sqrt(max(sm2 / max(sCnt, 1.0) - sm1 * sm1, vec3(0.0)));
        float clampK = mix(2.2, 3.8, smoothstep(0.08, 0.45, currRough));
        histSpec = clamp(histSpec, max(sm1 - sSigma * clampK, vec3(0.0)), sm1 + sSigma * clampK + vec3(0.25));
        float motionLen = length(motion * vec2(size));
        float mirrorBoost = (1.0 - smoothstep(0.04, 0.22, currRough)) * clamp(motionLen * 0.15, 0.0, 0.45);
        float alphaSpec = clamp(max(clamp(uGi.giTemporalAlpha, 0.04, 0.50) + mirrorBoost, 1.0 / max(outHistLen, 1.0)), 0.05, 0.65);
        outSpec = mix(histSpec, currSpec, alphaSpec);
        outSpecMu1 = mix(histSpecMu1, currSpecLuma, max(0.20, alphaSpec));
        float specDiff = currSpecLuma - outSpecMu1;
        outSpecVar = max(0.0, mix(histSpecVar, specDiff * specDiff, max(0.20, alphaSpec)));
    }
    float temporalVariance = max(0.0, outMoments.y - outMoments.x * outMoments.x);
    imageStore(uCurrIrrHistoryOut, pix, vec4(outIrr, temporalVariance));
    imageStore(uCurrSpecHistoryOut, pix, vec4(outSpec, outSpecVar));
    imageStore(uCurrMomentsOut, pix, vec4(outMoments.x, outMoments.y, outHistLen, outSpecMu1));
}
)GLSL";

const char* kFallbackPtSvgfAtrousComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uIrrVarIn;
layout(set = 0, binding = 1) uniform sampler2D uMomentsIn;
layout(set = 0, binding = 2) uniform sampler2D uNormalRoughIn;
layout(set = 0, binding = 3) uniform sampler2D uDepthInstIn;
layout(set = 0, binding = 4) uniform sampler2D uAlbedoIn;
layout(set = 0, binding = 5) uniform sampler2D uEmissiveIn;
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D uIrrVarOut;
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D uHdrOut;
layout(set = 0, binding = 8) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
layout(set = 0, binding = 9) uniform sampler2D uSpecVarIn;
layout(set = 0, binding = 10, rgba16f) uniform writeonly image2D uSpecVarOut;
layout(push_constant) uniform AtrousPC { int stepSize; int isLastPass; } pc;
float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uIrrVarOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    vec4 centerIrrVar = texelFetch(uIrrVarIn, pix, 0);
    if (any(isnan(centerIrrVar)) || any(isinf(centerIrrVar))) centerIrrVar = vec4(0.0);
    vec3 centerIrr = max(centerIrrVar.rgb, vec3(0.0)); float centerVar = max(centerIrrVar.a, 0.0);
    vec4 centerSpecVar = texelFetch(uSpecVarIn, pix, 0);
    if (any(isnan(centerSpecVar)) || any(isinf(centerSpecVar))) centerSpecVar = vec4(0.0);
    vec3 centerSpec = max(centerSpecVar.rgb, vec3(0.0)); float centerSVar = max(centerSpecVar.a, 0.0);
    vec3 primaryAlbedo = max(texelFetch(uAlbedoIn, pix, 0).rgb, vec3(0.0));
    vec3 primaryEmissive = max(texelFetch(uEmissiveIn, pix, 0).rgb, vec3(0.0));
    if (any(isnan(primaryAlbedo)) || any(isinf(primaryAlbedo))) primaryAlbedo = vec3(1.0);
    if (any(isnan(primaryEmissive)) || any(isinf(primaryEmissive))) primaryEmissive = vec3(0.0);
    vec2 centerDI = texelFetch(uDepthInstIn, pix, 0).rg;
    float centerDepth = centerDI.x; float centerInst = centerDI.y;
    if (centerDepth <= 0.0001 || uGi.giDenoiserEnabled == 0 || pc.stepSize <= 0) {
        imageStore(uIrrVarOut, pix, vec4(centerIrr, centerVar));
        imageStore(uSpecVarOut, pix, vec4(centerSpec, centerSVar));
        if (pc.isLastPass != 0) imageStore(uHdrOut, pix, vec4(centerIrr * primaryAlbedo + centerSpec + primaryEmissive, 1.0));
        return;
    }
    vec4 centerNR = texelFetch(uNormalRoughIn, pix, 0);
    vec3 centerN = normalize(centerNR.xyz); float centerRough = clamp(centerNR.w, 0.04, 1.0);
    float centerLuma = Luminance(centerIrr); float centerSpecLuma = Luminance(centerSpec);
    float histLen = texelFetch(uMomentsIn, pix, 0).z;
    float userSigmaL = clamp(uGi.svgfSigmaLuminance, 0.25, 8.0);
    float userSigmaN = clamp(uGi.svgfSigmaNormal, 16.0, 512.0);
    float userSigmaZ = clamp(uGi.svgfSigmaDepth, 0.10, 8.0);
    float dRight = texelFetch(uDepthInstIn, min(pix + ivec2(1, 0), size - ivec2(1)), 0).r;
    float dLeft  = texelFetch(uDepthInstIn, max(pix - ivec2(1, 0), ivec2(0)), 0).r;
    float dUp    = texelFetch(uDepthInstIn, min(pix + ivec2(0, 1), size - ivec2(1)), 0).r;
    float dDown  = texelFetch(uDepthInstIn, max(pix - ivec2(0, 1), ivec2(0)), 0).r;
    vec2 depthGrad = vec2(min(abs(dRight - centerDepth), abs(centerDepth - dLeft)), min(abs(dUp - centerDepth), abs(centerDepth - dDown)));
    if (pc.stepSize == 1 && histLen < 4.0) {
        vec2 sumMom = vec2(0.0); float wMomSum = 0.0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            ivec2 q = clamp(pix + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            vec2 qDI = texelFetch(uDepthInstIn, q, 0).rg;
            if (qDI.x <= 0.0001 || abs(centerInst - qDI.y) > 0.5) continue;
            vec3 qN = normalize(texelFetch(uNormalRoughIn, q, 0).xyz);
            float expDz = dot(depthGrad, abs(vec2(dx, dy)));
            float w = pow(max(dot(centerN, qN), 0.0), userSigmaN) * exp(-abs(centerDepth - qDI.x) / max(userSigmaZ * expDz + 0.005 * centerDepth, 1e-4));
            if (w > 1e-4) { sumMom += texelFetch(uMomentsIn, q, 0).xy * w; wMomSum += w; }
        }
        if (wMomSum > 1e-4) {
            sumMom /= wMomSum;
            centerVar = max(centerVar, max(0.0, sumMom.y - sumMom.x * sumMom.x) * (4.0 / max(histLen, 1.0)));
        }
    }
    const float g3[3] = float[3](0.25, 0.50, 0.25);
    float filteredVar = 0.0; float filteredSpecVar = 0.0; float varWSum = 0.0;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        ivec2 q = clamp(pix + ivec2(dx, dy), ivec2(0), size - ivec2(1));
        vec2 qDI = texelFetch(uDepthInstIn, q, 0).rg;
        if (qDI.x <= 0.0001 || abs(centerInst - qDI.y) > 0.5) continue;
        float kw = g3[dx + 1] * g3[dy + 1];
        float qV = (dx == 0 && dy == 0) ? centerVar : max(texelFetch(uIrrVarIn, q, 0).a, 0.0);
        float qSV = (dx == 0 && dy == 0) ? centerSVar : max(texelFetch(uSpecVarIn, q, 0).a, 0.0);
        filteredVar += qV * kw; filteredSpecVar += qSV * kw; varWSum += kw;
    }
    filteredVar /= max(varWSum, 1e-4); filteredSpecVar /= max(varWSum, 1e-4);
    float lumaPhi = userSigmaL * sqrt(max(filteredVar, 1e-6)) + 1e-3;
    bool filterSpecular = (centerRough >= 0.15);
    float specRoughFade = smoothstep(0.15, 0.50, centerRough);
    float specLumaPhi = max(userSigmaL * mix(0.35, 1.0, specRoughFade) * sqrt(max(filteredSpecVar, 1e-6)) + 1e-3, 1e-4);
    float maxSpecRadius = mix(1.5, 16.0, specRoughFade);
    const float h5[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);
    vec3 sumIrr = centerIrr; float sumVar = centerVar; float sumW = 1.0; float sumW2 = 1.0;
    vec3 sumSpec = centerSpec; float sumSVar = centerSVar; float sumSpecW = 1.0; float sumSpecW2 = 1.0;
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        if (dx == 0 && dy == 0) continue;
        ivec2 offset = ivec2(dx, dy) * pc.stepSize; ivec2 q = pix + offset;
        if (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y) continue;
        vec2 qDI = texelFetch(uDepthInstIn, q, 0).rg;
        if (qDI.x <= 0.0001 || abs(centerInst - qDI.y) > 0.5) continue;
        vec4 qNR = texelFetch(uNormalRoughIn, q, 0);
        vec3 qN = normalize(qNR.xyz); float qRough = clamp(qNR.w, 0.04, 1.0);
        float wN = pow(max(dot(centerN, qN), 0.0), userSigmaN);
        if (wN < 1e-4) continue;
        float expectedDz = dot(depthGrad, abs(vec2(offset)));
        float wZ = exp(-abs(centerDepth - qDI.x) / max(userSigmaZ * expectedDz + 0.003 * centerDepth, 1e-4));
        if (wZ < 1e-4) continue;
        float kernelW = h5[dx + 2] * h5[dy + 2] / 0.140625;
        float geomW = kernelW * wN * wZ;
        vec4 qIrrVar = texelFetch(uIrrVarIn, q, 0);
        if (!any(isnan(qIrrVar)) && !any(isinf(qIrrVar))) {
            vec3 qIrr = max(qIrrVar.rgb, vec3(0.0)); float qVar = max(qIrrVar.a, 0.0);
            float wL = exp(-abs(centerLuma - Luminance(qIrr)) / max(lumaPhi, 1e-4));
            float wD = clamp(geomW * wL, 0.0, 1.0);
            if (wD > 1e-4) { sumIrr += qIrr * wD; sumVar += qVar * (wD * wD); sumW += wD; sumW2 += wD * wD; }
        }
        if (filterSpecular && length(vec2(offset)) <= maxSpecRadius) {
            vec4 qSpecVar = texelFetch(uSpecVarIn, q, 0);
            if (!any(isnan(qSpecVar)) && !any(isinf(qSpecVar))) {
                vec3 qSpec = max(qSpecVar.rgb, vec3(0.0)); float qSVar = max(qSpecVar.a, 0.0);
                float wRough = exp(-abs(centerRough - qRough) / max(0.05 * centerRough + 0.015, 0.01));
                float wSpecN = pow(max(dot(centerN, qN), 0.0), max(userSigmaN, 192.0));
                float wSpecL = exp(-abs(centerSpecLuma - Luminance(qSpec)) / specLumaPhi);
                float wS = clamp(kernelW * wSpecN * wZ * wRough * wSpecL * specRoughFade, 0.0, 1.0);
                if (wS > 1e-4) { sumSpec += qSpec * wS; sumSVar += qSVar * (wS * wS); sumSpecW += wS; sumSpecW2 += wS * wS; }
            }
        }
    }
    vec3 outIrr = max(sumIrr / max(sumW, 1e-4), vec3(0.0));
    float outVar = max(sumVar / max(sumW2, 1e-8), 0.0);
    if (any(isnan(outIrr)) || any(isinf(outIrr))) outIrr = centerIrr;
    if (isnan(outVar) || isinf(outVar)) outVar = centerVar;
    vec3 outSpec = filterSpecular ? max(sumSpec / max(sumSpecW, 1e-4), vec3(0.0)) : centerSpec;
    float outSVar = filterSpecular ? max(sumSVar / max(sumSpecW2, 1e-8), 0.0) : centerSVar;
    if (any(isnan(outSpec)) || any(isinf(outSpec))) outSpec = centerSpec;
    if (isnan(outSVar) || isinf(outSVar)) outSVar = centerSVar;
    imageStore(uIrrVarOut, pix, vec4(outIrr, outVar));
    imageStore(uSpecVarOut, pix, vec4(outSpec, outSVar));
    if (pc.isLastPass != 0) imageStore(uHdrOut, pix, vec4(outIrr * primaryAlbedo + outSpec + primaryEmissive, 1.0));
}
)GLSL";

const char* kFallbackPtSvgfModulateComp = R"GLSL(#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D uFilteredIrrVarIn;
layout(set = 0, binding = 1) uniform sampler2D uAlbedoIn;
layout(set = 0, binding = 2) uniform sampler2D uEmissiveIn;
layout(set = 0, binding = 3) uniform sampler2D uNormalRoughIn;
layout(set = 0, binding = 4) uniform sampler2D uDepthInstIn;
layout(set = 0, binding = 5) uniform sampler2D uMomentsIn;
layout(set = 0, binding = 6, rgba16f) uniform writeonly image2D uHdrOut;
layout(set = 0, binding = 7) uniform GiParamsUBO {
    uint width; uint height; uint frameIndex; uint sampleCount;
    int giMode; int giBounces; int giRaysPerPixel; float giIntensity;
    float giTemporalAlpha; int giDenoiserEnabled; int giRayTracedShadows; int hasValidTLAS;
    int svgfAtrousIterations; float svgfSigmaLuminance; float svgfSigmaNormal; float svgfSigmaDepth;
} uGi;
layout(set = 0, binding = 8) uniform sampler2D uFilteredSpecVarIn;
float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy); ivec2 size = imageSize(uHdrOut);
    if (pix.x >= size.x || pix.y >= size.y) return;
    vec4 irrVar = texelFetch(uFilteredIrrVarIn, pix, 0);
    if (any(isnan(irrVar)) || any(isinf(irrVar))) irrVar = vec4(0.0);
    vec3 filteredDiffuse = max(irrVar.rgb, vec3(0.0)); float filteredVar = max(irrVar.a, 0.0);
    vec4 specVar = texelFetch(uFilteredSpecVarIn, pix, 0);
    if (any(isnan(specVar)) || any(isinf(specVar))) specVar = vec4(0.0);
    vec3 filteredSpecular = max(specVar.rgb, vec3(0.0));
    vec3 originalAlbedo = max(texelFetch(uAlbedoIn, pix, 0).rgb, vec3(0.0));
    vec3 primaryEmissive = max(texelFetch(uEmissiveIn, pix, 0).rgb, vec3(0.0));
    if (any(isnan(originalAlbedo)) || any(isinf(originalAlbedo))) originalAlbedo = vec3(1.0);
    if (any(isnan(primaryEmissive)) || any(isinf(primaryEmissive))) primaryEmissive = vec3(0.0);
    vec2 depthInst = texelFetch(uDepthInstIn, pix, 0).rg;
    if (depthInst.x <= 0.0001) {
        imageStore(uHdrOut, pix, vec4(min(max((filteredDiffuse * originalAlbedo) + filteredSpecular + primaryEmissive, vec3(0.0)), vec3(32.0)), 1.0));
        return;
    }
    float histLen = texelFetch(uMomentsIn, pix, 0).z;
    if (uGi.giDenoiserEnabled != 0 && (histLen < 4.0 || filteredVar > 0.25)) {
        vec3 nCenter = normalize(texelFetch(uNormalRoughIn, pix, 0).xyz);
        vec3 neighMean = vec3(0.0); float neighW = 0.0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            ivec2 q = clamp(pix + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            vec2 qDI = texelFetch(uDepthInstIn, q, 0).rg;
            if (qDI.x <= 0.0001 || abs(depthInst.y - qDI.y) > 0.5) continue;
            vec3 qN = normalize(texelFetch(uNormalRoughIn, q, 0).xyz);
            if (dot(nCenter, qN) < 0.90) continue;
            vec3 qI = max(texelFetch(uFilteredIrrVarIn, q, 0).rgb, vec3(0.0));
            if (any(isnan(qI)) || any(isinf(qI))) continue;
            neighMean += qI; neighW += 1.0;
        }
        if (neighW > 0.5) {
            neighMean /= neighW;
            float maxLuma = max(Luminance(neighMean) * 3.0 + 0.35, 0.1);
            float currLuma = Luminance(filteredDiffuse);
            if (currLuma > maxLuma) filteredDiffuse *= (maxLuma / max(currLuma, 1e-4));
        }
    }
    vec3 finalHdr = (filteredDiffuse * originalAlbedo) + filteredSpecular + primaryEmissive;
    if (any(isnan(finalHdr)) || any(isinf(finalHdr))) finalHdr = primaryEmissive;
    imageStore(uHdrOut, pix, vec4(clamp(finalHdr, vec3(0.0), vec3(32.0)), 1.0));
}
)GLSL";

std::string LoadShaderTextWithFallback(const std::string& relPath, const std::string& fallback) {
    std::string text;
    if (SlipFileSystem::Exists(relPath) && SlipFileSystem::ReadAllText(relPath, text) && !text.empty()) {
        return text;
    }
    return fallback;
}

VkShaderModule CompileComputeModule(VkDevice device, const std::string& glsl, const char* name) {
    SpirvCompileResult res = SlipSpirvCompiler::CompileGLSL(glsl, SpirvShaderStage::Compute, name, true, "cache/spirv");
    if (!res.success || res.spirv.empty()) {
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = res.spirv.size() * sizeof(uint32_t);
    info.pCode = res.spirv.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &mod) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return mod;
}

} // namespace

bool VulkanRayTracedGI::Initialize(VulkanContext* context, VkDescriptorSetLayout bindlessSetLayout) {
    if (!context || !context->IsValid()) return false;
    Shutdown(context);

    const uint8_t zeroGiPixel[4] = { 0, 0, 0, 255 };
    if (!m_FallbackGI.CreateFromRGBA8(context, zeroGiPixel, 1, 1, false, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)) {
        return false;
    }
    if (!m_FallbackRefl.CreateFromRGBA8(context, zeroGiPixel, 1, 1, false, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)) {
        return false;
    }

    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        if (!m_GiParamsUBOs[i].Create(context, sizeof(GpuGiParamsUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, false)) {
            Shutdown(context);
            return false;
        }
    }

    if (!context->GetCapabilities().supportsRayQuery) {
        m_RayQueryPipelinesReady = false;
        return true;
    }

    VkDevice device = context->GetDevice();

    std::array<VkDescriptorPoolSize, 5> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 200 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 8 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 }
    }};
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 40;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_Pool) != VK_SUCCESS) {
        Shutdown(context);
        return false;
    }

    // 1. RTGI + Reflections Trace Set Layout
    {
        std::array<VkDescriptorSetLayoutBinding, 10> b{};
        b[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[5] = { 5, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[6] = { 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[7] = { 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[8] = { 8, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[9] = { 9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(b.size());
        info.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_RtgiTraceLayout) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
    }

    // 2. RTGI + Reflections Temporal Denoise Set Layout
    {
        std::array<VkDescriptorSetLayoutBinding, 10> b{};
        b[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[4] = { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[5] = { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[6] = { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[7] = { 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[8] = { 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[9] = { 9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(b.size());
        info.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_RtgiDenoiseLayout) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
    }

    // 2B. RTGI + Reflections A-Trous Wavelet Denoise Set Layout
    {
        std::array<VkDescriptorSetLayoutBinding, 7> b{};
        b[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[3] = { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[5] = { 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[6] = { 6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(b.size());
        info.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_RtgiAtrousLayout) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
    }

    // 3. Path Tracing Set Layout
    {
        std::array<VkDescriptorSetLayoutBinding, 12> b{};
        b[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[2] = { 2, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[4] = { 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[5] = { 5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[6] = { 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[7] = { 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[8] = { 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[9] = { 9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[10] = { 10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[11] = { 11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(b.size());
        info.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_PtLayout) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
    }

    // 4. Path Tracing Denoise Set Layout
    {
        std::array<VkDescriptorSetLayoutBinding, 9> b{};
        b[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[3] = { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[4] = { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[5] = { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[6] = { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[7] = { 7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[8] = { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(b.size());
        info.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_PtDenoiseLayout) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
    }

    // 4B. Path Tracing SVGF Temporal Reprojection Set Layout (Diffuse + Specular)
    {
        std::array<VkDescriptorSetLayoutBinding, 14> b{};
        for (uint32_t j = 0; j < 8; ++j) {
            b[j] = { j, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        }
        b[8]  = { 8,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[9]  = { 9,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[10] = { 10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[11] = { 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[12] = { 12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[13] = { 13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(b.size());
        info.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_PtSvgfTemporalLayout) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
    }

    // 4C. Path Tracing SVGF Variance-Guided A-Trous Set Layout (Diffuse + Roughness-Guided Specular)
    {
        std::array<VkDescriptorSetLayoutBinding, 11> b{};
        for (uint32_t j = 0; j < 6; ++j) {
            b[j] = { j, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        }
        b[6]  = { 6,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[7]  = { 7,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[8]  = { 8,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[9]  = { 9,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[10] = { 10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(b.size());
        info.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_PtSvgfAtrousLayout) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
    }

    // 4D. Path Tracing SVGF Final Modulation Set Layout
    {
        std::array<VkDescriptorSetLayoutBinding, 9> b{};
        for (uint32_t j = 0; j < 6; ++j) {
            b[j] = { j, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        }
        b[6] = { 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[7] = { 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        b[8] = { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = static_cast<uint32_t>(b.size());
        info.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_PtSvgfModulateLayout) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
    }

    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        std::array<VkDescriptorSetLayout, 14> layouts = {
            m_RtgiTraceLayout, m_RtgiDenoiseLayout,
            m_RtgiAtrousLayout, m_RtgiAtrousLayout, m_RtgiAtrousLayout,
            m_PtLayout, m_PtDenoiseLayout,
            m_PtSvgfTemporalLayout,
            m_PtSvgfAtrousLayout, m_PtSvgfAtrousLayout, m_PtSvgfAtrousLayout, m_PtSvgfAtrousLayout, m_PtSvgfAtrousLayout,
            m_PtSvgfModulateLayout
        };
        std::array<VkDescriptorSet, 14> sets{};
        VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc.descriptorPool = m_Pool;
        alloc.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        alloc.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &alloc, sets.data()) != VK_SUCCESS) {
            Shutdown(context);
            return false;
        }
        m_RtgiTraceSets[i] = sets[0];
        m_RtgiDenoiseSets[i] = sets[1];
        m_RtgiAtrousSets[i][0] = sets[2];
        m_RtgiAtrousSets[i][1] = sets[3];
        m_RtgiAtrousSets[i][2] = sets[4];
        m_PtSets[i] = sets[5];
        m_PtDenoiseSets[i] = sets[6];
        m_PtSvgfTemporalSets[i] = sets[7];
        m_PtSvgfAtrousSets[i][0] = sets[8];
        m_PtSvgfAtrousSets[i][1] = sets[9];
        m_PtSvgfAtrousSets[i][2] = sets[10];
        m_PtSvgfAtrousSets[i][3] = sets[11];
        m_PtSvgfAtrousSets[i][4] = sets[12];
        m_PtSvgfModulateSets[i] = sets[13];
    }

    if (!CreatePipelines(context, bindlessSetLayout)) {
        std::cerr << "[VulkanRayTracedGI] Fallo al compilar pipelines RTGI/PathTracing." << std::endl;
        Shutdown(context);
        return false;
    }

    m_RayQueryPipelinesReady = true;
    return true;
}

bool VulkanRayTracedGI::CreatePipelines(VulkanContext* context, VkDescriptorSetLayout bindlessSetLayout) {
    VkDevice device = context->GetDevice();

    // 1. RTGI Trace Compute Pipeline
    {
        std::string src = LoadShaderTextWithFallback("shaders/vulkan/rtgi_trace.comp", std::string(kFallbackRtgiTraceCompA) + kFallbackRtgiTraceCompB);
        VkShaderModule mod = CompileComputeModule(device, src, "rtgi_trace.comp");
        if (mod == VK_NULL_HANDLE) return false;

        std::array<VkDescriptorSetLayout, 2> setLayouts = { bindlessSetLayout, m_RtgiTraceLayout };
        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        plInfo.pSetLayouts = setLayouts.data();
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_RtgiTracePipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr };
        cp.layout = m_RtgiTracePipeLayout;
        VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_RtgiTracePipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    // 2. RTGI Denoise Compute Pipeline
    {
        std::string src = LoadShaderTextWithFallback("shaders/vulkan/rtgi_denoise.comp", kFallbackRtgiDenoiseComp);
        VkShaderModule mod = CompileComputeModule(device, src, "rtgi_denoise.comp");
        if (mod == VK_NULL_HANDLE) return false;

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_RtgiDenoiseLayout;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_RtgiDenoisePipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr };
        cp.layout = m_RtgiDenoisePipeLayout;
        VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_RtgiDenoisePipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    // 3. Path Tracing Compute Pipeline
    {
        std::string src = LoadShaderTextWithFallback("shaders/vulkan/pathtrace.comp", std::string(kFallbackPathtraceCompA) + kFallbackPathtraceCompB + kFallbackPathtraceCompC);
        VkShaderModule mod = CompileComputeModule(device, src, "pathtrace.comp");
        if (mod == VK_NULL_HANDLE) return false;

        std::array<VkDescriptorSetLayout, 2> setLayouts = { bindlessSetLayout, m_PtLayout };
        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        plInfo.pSetLayouts = setLayouts.data();
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_PtPipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr };
        cp.layout = m_PtPipeLayout;
        VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_PtPipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    // 4. Path Tracing Edge-Stopping Spatial Denoise Compute Pipeline
    {
        std::string src = LoadShaderTextWithFallback("shaders/vulkan/pt_denoise.comp", kFallbackPtDenoiseComp);
        VkShaderModule mod = CompileComputeModule(device, src, "pt_denoise.comp");
        if (mod == VK_NULL_HANDLE) return false;

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_PtDenoiseLayout;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_PtDenoisePipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr };
        cp.layout = m_PtDenoisePipeLayout;
        VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_PtDenoisePipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    // 5. RTGI + Reflections A-Trous Wavelet Denoise Compute Pipeline
    {
        std::string src = LoadShaderTextWithFallback("shaders/vulkan/rtgi_atrous.comp", kFallbackRtgiAtrousComp);
        VkShaderModule mod = CompileComputeModule(device, src, "rtgi_atrous.comp");
        if (mod == VK_NULL_HANDLE) return false;

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(int32_t);

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_RtgiAtrousLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_RtgiAtrousPipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr };
        cp.layout = m_RtgiAtrousPipeLayout;
        VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_RtgiAtrousPipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    // 6. Path Tracing SVGF Temporal Reprojection & Moments Pipeline
    {
        std::string src = LoadShaderTextWithFallback("shaders/vulkan/pt_svgf_temporal.comp", kFallbackPtSvgfTemporalComp);
        VkShaderModule mod = CompileComputeModule(device, src, "pt_svgf_temporal.comp");
        if (mod == VK_NULL_HANDLE) return false;

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_PtSvgfTemporalLayout;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_PtSvgfTemporalPipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr };
        cp.layout = m_PtSvgfTemporalPipeLayout;
        VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_PtSvgfTemporalPipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    // 7. Path Tracing SVGF Spatial Variance + A-Trous Wavelet Pipeline
    {
        std::string src = LoadShaderTextWithFallback("shaders/vulkan/pt_svgf_atrous.comp", kFallbackPtSvgfAtrousComp);
        VkShaderModule mod = CompileComputeModule(device, src, "pt_svgf_atrous.comp");
        if (mod == VK_NULL_HANDLE) return false;

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(int32_t) * 2;

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_PtSvgfAtrousLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_PtSvgfAtrousPipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr };
        cp.layout = m_PtSvgfAtrousPipeLayout;
        VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_PtSvgfAtrousPipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    // 8. Path Tracing SVGF Final Modulation & Composition Pipeline
    {
        std::string src = LoadShaderTextWithFallback("shaders/vulkan/pt_svgf_modulate.comp", kFallbackPtSvgfModulateComp);
        VkShaderModule mod = CompileComputeModule(device, src, "pt_svgf_modulate.comp");
        if (mod == VK_NULL_HANDLE) return false;

        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &m_PtSvgfModulateLayout;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &m_PtSvgfModulatePipeLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, mod, nullptr);
            return false;
        }

        VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr };
        cp.layout = m_PtSvgfModulatePipeLayout;
        VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_PtSvgfModulatePipeline);
        vkDestroyShaderModule(device, mod, nullptr);
        if (r != VK_SUCCESS) return false;
    }

    return true;
}

void VulkanRayTracedGI::Shutdown(VulkanContext* context) {
    if (context && context->IsValid()) {
        VkDevice device = context->GetDevice();
        DestroyTargets(context);
        m_FallbackGI.Destroy(context);
        m_FallbackRefl.Destroy(context);
        for (uint32_t i = 0; i < kFrameSlots; ++i) {
            m_GiParamsUBOs[i].Destroy(context);
        }

        if (m_RtgiTracePipeline) { vkDestroyPipeline(device, m_RtgiTracePipeline, nullptr); m_RtgiTracePipeline = VK_NULL_HANDLE; }
        if (m_RtgiTracePipeLayout) { vkDestroyPipelineLayout(device, m_RtgiTracePipeLayout, nullptr); m_RtgiTracePipeLayout = VK_NULL_HANDLE; }
        if (m_RtgiDenoisePipeline) { vkDestroyPipeline(device, m_RtgiDenoisePipeline, nullptr); m_RtgiDenoisePipeline = VK_NULL_HANDLE; }
        if (m_RtgiDenoisePipeLayout) { vkDestroyPipelineLayout(device, m_RtgiDenoisePipeLayout, nullptr); m_RtgiDenoisePipeLayout = VK_NULL_HANDLE; }
        if (m_RtgiAtrousPipeline) { vkDestroyPipeline(device, m_RtgiAtrousPipeline, nullptr); m_RtgiAtrousPipeline = VK_NULL_HANDLE; }
        if (m_RtgiAtrousPipeLayout) { vkDestroyPipelineLayout(device, m_RtgiAtrousPipeLayout, nullptr); m_RtgiAtrousPipeLayout = VK_NULL_HANDLE; }
        if (m_PtPipeline) { vkDestroyPipeline(device, m_PtPipeline, nullptr); m_PtPipeline = VK_NULL_HANDLE; }
        if (m_PtPipeLayout) { vkDestroyPipelineLayout(device, m_PtPipeLayout, nullptr); m_PtPipeLayout = VK_NULL_HANDLE; }
        if (m_PtDenoisePipeline) { vkDestroyPipeline(device, m_PtDenoisePipeline, nullptr); m_PtDenoisePipeline = VK_NULL_HANDLE; }
        if (m_PtDenoisePipeLayout) { vkDestroyPipelineLayout(device, m_PtDenoisePipeLayout, nullptr); m_PtDenoisePipeLayout = VK_NULL_HANDLE; }
        if (m_PtSvgfTemporalPipeline) { vkDestroyPipeline(device, m_PtSvgfTemporalPipeline, nullptr); m_PtSvgfTemporalPipeline = VK_NULL_HANDLE; }
        if (m_PtSvgfTemporalPipeLayout) { vkDestroyPipelineLayout(device, m_PtSvgfTemporalPipeLayout, nullptr); m_PtSvgfTemporalPipeLayout = VK_NULL_HANDLE; }
        if (m_PtSvgfAtrousPipeline) { vkDestroyPipeline(device, m_PtSvgfAtrousPipeline, nullptr); m_PtSvgfAtrousPipeline = VK_NULL_HANDLE; }
        if (m_PtSvgfAtrousPipeLayout) { vkDestroyPipelineLayout(device, m_PtSvgfAtrousPipeLayout, nullptr); m_PtSvgfAtrousPipeLayout = VK_NULL_HANDLE; }
        if (m_PtSvgfModulatePipeline) { vkDestroyPipeline(device, m_PtSvgfModulatePipeline, nullptr); m_PtSvgfModulatePipeline = VK_NULL_HANDLE; }
        if (m_PtSvgfModulatePipeLayout) { vkDestroyPipelineLayout(device, m_PtSvgfModulatePipeLayout, nullptr); m_PtSvgfModulatePipeLayout = VK_NULL_HANDLE; }

        if (m_Pool) { vkDestroyDescriptorPool(device, m_Pool, nullptr); m_Pool = VK_NULL_HANDLE; }
        if (m_RtgiTraceLayout) { vkDestroyDescriptorSetLayout(device, m_RtgiTraceLayout, nullptr); m_RtgiTraceLayout = VK_NULL_HANDLE; }
        if (m_RtgiDenoiseLayout) { vkDestroyDescriptorSetLayout(device, m_RtgiDenoiseLayout, nullptr); m_RtgiDenoiseLayout = VK_NULL_HANDLE; }
        if (m_RtgiAtrousLayout) { vkDestroyDescriptorSetLayout(device, m_RtgiAtrousLayout, nullptr); m_RtgiAtrousLayout = VK_NULL_HANDLE; }
        if (m_PtLayout) { vkDestroyDescriptorSetLayout(device, m_PtLayout, nullptr); m_PtLayout = VK_NULL_HANDLE; }
        if (m_PtDenoiseLayout) { vkDestroyDescriptorSetLayout(device, m_PtDenoiseLayout, nullptr); m_PtDenoiseLayout = VK_NULL_HANDLE; }
        if (m_PtSvgfTemporalLayout) { vkDestroyDescriptorSetLayout(device, m_PtSvgfTemporalLayout, nullptr); m_PtSvgfTemporalLayout = VK_NULL_HANDLE; }
        if (m_PtSvgfAtrousLayout) { vkDestroyDescriptorSetLayout(device, m_PtSvgfAtrousLayout, nullptr); m_PtSvgfAtrousLayout = VK_NULL_HANDLE; }
        if (m_PtSvgfModulateLayout) { vkDestroyDescriptorSetLayout(device, m_PtSvgfModulateLayout, nullptr); m_PtSvgfModulateLayout = VK_NULL_HANDLE; }
    } else {
        m_RtgiTracePipeline = m_RtgiDenoisePipeline = m_RtgiAtrousPipeline = m_PtPipeline = m_PtDenoisePipeline = m_PtSvgfTemporalPipeline = m_PtSvgfAtrousPipeline = m_PtSvgfModulatePipeline = VK_NULL_HANDLE;
        m_RtgiTracePipeLayout = m_RtgiDenoisePipeLayout = m_RtgiAtrousPipeLayout = m_PtPipeLayout = m_PtDenoisePipeLayout = m_PtSvgfTemporalPipeLayout = m_PtSvgfAtrousPipeLayout = m_PtSvgfModulatePipeLayout = VK_NULL_HANDLE;
        m_Pool = VK_NULL_HANDLE;
        m_RtgiTraceLayout = m_RtgiDenoiseLayout = m_RtgiAtrousLayout = m_PtLayout = m_PtDenoiseLayout = m_PtSvgfTemporalLayout = m_PtSvgfAtrousLayout = m_PtSvgfModulateLayout = VK_NULL_HANDLE;
        m_TargetsCreated = false;
    }

    for (uint32_t i = 0; i < kFrameSlots; ++i) {
        m_RtgiTraceSets[i] = VK_NULL_HANDLE;
        m_RtgiDenoiseSets[i] = VK_NULL_HANDLE;
        m_RtgiAtrousSets[i][0] = VK_NULL_HANDLE;
        m_RtgiAtrousSets[i][1] = VK_NULL_HANDLE;
        m_RtgiAtrousSets[i][2] = VK_NULL_HANDLE;
        m_PtSets[i] = VK_NULL_HANDLE;
        m_PtDenoiseSets[i] = VK_NULL_HANDLE;
        m_PtSvgfTemporalSets[i] = VK_NULL_HANDLE;
        for (int a = 0; a < 5; ++a) {
            m_PtSvgfAtrousSets[i][a] = VK_NULL_HANDLE;
        }
        m_PtSvgfModulateSets[i] = VK_NULL_HANDLE;
    }
    m_RayQueryPipelinesReady = false;
    m_PtSampleCount = 0;
    m_PtFrameIndex = 0;
    m_RtgiTemporalFrame = 0;
}

bool VulkanRayTracedGI::EnsureSize(VulkanContext* context, uint32_t width, uint32_t height) {
    if (!context || !context->IsValid() || width == 0 || height == 0) return false;
    if (!m_RayQueryPipelinesReady) return true;
    if (m_TargetsCreated && m_Width == width && m_Height == height) return true;

    context->WaitIdle();
    DestroyTargets(context);

    const VkImageUsageFlags rwFlags = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!m_RtgiRaw.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtgiHistory[0].CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtgiHistory[1].CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtgiTemp.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtgiDenoised.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtReflRaw.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtReflHistory[0].CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtReflHistory[1].CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtReflTemp.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_RtReflDenoised.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    if (!m_PtAccum.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_PtSpecAccum.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_PtAlbedo.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_PtEmissive.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    for (int p = 0; p < 2; ++p) {
        if (!m_PtNormalRough[p].CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
        if (!m_PtDepthInst[p].CreateImage2D(context, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
        if (!m_PtIrradianceHistory[p].CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
        if (!m_PtSpecularHistory[p].CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
        if (!m_PtMomentsHistory[p].CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_LINEAR)) return false;
    }
    if (!m_PtAtrousPing.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_PtAtrousPong.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_PtSpecAtrousPing.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_PtSpecAtrousPong.CreateImage2D(context, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;
    if (!m_PtMotion.CreateImage2D(context, width, height, VK_FORMAT_R16G16_SFLOAT, rwFlags, VK_IMAGE_ASPECT_COLOR_BIT, true, VK_FILTER_NEAREST)) return false;

    m_Width = width;
    m_Height = height;
    m_TargetsCreated = true;
    m_HistoryInitialized[0] = false;
    m_HistoryInitialized[1] = false;
    m_PtAccumInitialized = false;
    m_PtPingPongInitialized[0] = false;
    m_PtPingPongInitialized[1] = false;
    m_PtAtrousInitialized = false;
    m_HistoryReadIdx = 0;
    m_PtPingPongIdx = 0;
    m_PtLastWrittenIdx = 0;
    m_PtAtrousFinalIdx = 1;
    m_RtgiTemporalFrame = 0;
    m_PtSampleCount = 0;
    m_PtFrameIndex = 0;
    m_ForceReset = true;
    return true;
}

void VulkanRayTracedGI::DestroyTargets(VulkanContext* context) {
    if (context && context->IsValid()) {
        m_RtgiRaw.Destroy(context);
        m_RtgiHistory[0].Destroy(context);
        m_RtgiHistory[1].Destroy(context);
        m_RtgiTemp.Destroy(context);
        m_RtgiDenoised.Destroy(context);
        m_RtReflRaw.Destroy(context);
        m_RtReflHistory[0].Destroy(context);
        m_RtReflHistory[1].Destroy(context);
        m_RtReflTemp.Destroy(context);
        m_RtReflDenoised.Destroy(context);
        m_PtAccum.Destroy(context);
        m_PtSpecAccum.Destroy(context);
        m_PtAlbedo.Destroy(context);
        m_PtEmissive.Destroy(context);
        for (int p = 0; p < 2; ++p) {
            m_PtNormalRough[p].Destroy(context);
            m_PtDepthInst[p].Destroy(context);
            m_PtIrradianceHistory[p].Destroy(context);
            m_PtSpecularHistory[p].Destroy(context);
            m_PtMomentsHistory[p].Destroy(context);
        }
        m_PtAtrousPing.Destroy(context);
        m_PtAtrousPong.Destroy(context);
        m_PtSpecAtrousPing.Destroy(context);
        m_PtSpecAtrousPong.Destroy(context);
        m_PtMotion.Destroy(context);
    }
    m_Width = 0;
    m_Height = 0;
    m_TargetsCreated = false;
    m_HistoryInitialized[0] = false;
    m_HistoryInitialized[1] = false;
    m_PtAccumInitialized = false;
    m_PtPingPongInitialized[0] = false;
    m_PtPingPongInitialized[1] = false;
    m_PtAtrousInitialized = false;
}

void VulkanRayTracedGI::ResetAccumulation() {
    m_PtSampleCount = 0;
    m_PtFrameIndex = 0;
    m_PtPingPongIdx = 0;
    m_PtLastWrittenIdx = 0;
    m_RtgiTemporalFrame = 0;
    m_ForceReset = true;
}

void VulkanRayTracedGI::UpdateGiParamsBuffer(VulkanContext* context, uint32_t slot, const GpuGiParamsUBO& params) {
    m_GiParamsUBOs[slot % kFrameSlots].Upload(context, &params, sizeof(params), 0);
}

VkBuffer VulkanRayTracedGI::GetGiParamsBuffer(uint32_t slot) const {
    return m_GiParamsUBOs[slot % kFrameSlots].GetBuffer();
}

bool VulkanRayTracedGI::ExecuteRTGI(
    VulkanContext* context,
    VkCommandBuffer cmd,
    uint32_t slot,
    VkDescriptorSet bindlessSet,
    VkAccelerationStructureKHR tlas,
    VkBuffer rtInstanceInfoBuffer,
    VkBuffer lightingUboBuffer,
    const VulkanTexture& gbufAlbedo,
    const VulkanTexture& gbufNormalRough,
    const VulkanTexture& gbufMetalEmissive,
    const VulkanTexture& gbufDepth,
    const VulkanTexture& gbufMotion,
    uint64_t sceneStateHash,
    const GraphicsSettingsData& settings)
{
    if (!m_RayQueryPipelinesReady || !m_TargetsCreated || tlas == VK_NULL_HANDLE || rtInstanceInfoBuffer == VK_NULL_HANDLE) {
        return false;
    }

    const uint32_t fSlot = slot % kFrameSlots;
    if (m_ForceReset || sceneStateHash != m_LastRtgiSceneHash) {
        m_LastRtgiSceneHash = sceneStateHash;
        if (m_ForceReset) {
            m_RtgiTemporalFrame = 0;
            m_ForceReset = false;
        }
    }

    const uint32_t readHistIdx = m_HistoryReadIdx;
    const uint32_t writeHistIdx = 1u - readHistIdx;

    GpuGiParamsUBO ubo{};
    ubo.width = m_Width;
    ubo.height = m_Height;
    ubo.frameIndex = m_RtgiTemporalFrame;
    ubo.sampleCount = m_RtgiTemporalFrame + 1u;
    ubo.giMode = 1;
    ubo.giBounces = std::clamp(settings.giBounces, 1, 4);
    ubo.giRaysPerPixel = std::clamp(settings.giRaysPerPixel, 1, 4);
    ubo.giIntensity = settings.giIntensity;
    ubo.giTemporalAlpha = settings.giTemporalAlpha;
    ubo.giDenoiserEnabled = settings.giDenoiserEnabled ? 1 : 0;
    ubo.giRayTracedShadows = settings.giRayTracedShadows ? 1 : 0;
    ubo.hasValidTLAS = 1;
    UpdateGiParamsBuffer(context, fSlot, ubo);

    // Ensure history images are in expected layouts
    if (!m_HistoryInitialized[readHistIdx]) {
        VulkanTexture::TransitionImageLayout(cmd, m_RtgiHistory[readHistIdx].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtReflHistory[readHistIdx].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        m_HistoryInitialized[readHistIdx] = true;
    }

    VkDevice device = context->GetDevice();

    // Update RTGI + Reflections Trace descriptors
    {
        VkDescriptorImageInfo alb{ gbufAlbedo.GetSampler(), gbufAlbedo.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo nrm{ gbufNormalRough.GetSampler(), gbufNormalRough.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo mtl{ gbufMetalEmissive.GetSampler(), gbufMetalEmissive.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo dpt{ gbufDepth.GetSampler(), gbufDepth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo rawOut{ VK_NULL_HANDLE, m_RtgiRaw.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSetAccelerationStructureKHR asInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &tlas;
        VkDescriptorBufferInfo instBuf{ rtInstanceInfoBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo sceneBuf{ lightingUboBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo giBuf{ m_GiParamsUBOs[fSlot].GetBuffer(), 0, sizeof(GpuGiParamsUBO) };
        VkDescriptorImageInfo reflOut{ VK_NULL_HANDLE, m_RtReflRaw.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };

        std::array<VkWriteDescriptorSet, 10> w{};
        for (int i = 0; i < 10; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = m_RtgiTraceSets[fSlot];
            w[i].dstBinding = static_cast<uint32_t>(i);
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &alb;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[1].pImageInfo = &nrm;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[2].pImageInfo = &mtl;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[3].pImageInfo = &dpt;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;         w[4].pImageInfo = &rawOut;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; w[5].pNext = &asInfo;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;        w[6].pBufferInfo = &instBuf;
        w[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;        w[7].pBufferInfo = &sceneBuf;
        w[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;        w[8].pBufferInfo = &giBuf;
        w[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;         w[9].pImageInfo = &reflOut;
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    }

    // Pass 1: Trace RTGI + Soft Sun Shadows + GGX Specular Reflections
    VulkanTexture::TransitionImageLayout(cmd, m_RtgiRaw.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_RtReflRaw.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
    const uint32_t groupsX = (m_Width + 7) / 8;
    const uint32_t groupsY = (m_Height + 7) / 8;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_RtgiTracePipeline);
    std::array<VkDescriptorSet, 2> traceSets = { bindlessSet, m_RtgiTraceSets[fSlot] };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_RtgiTracePipeLayout, 0, 2, traceSets.data(), 0, nullptr);
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition raw -> SHADER_READ_ONLY, currHistories -> GENERAL
    VulkanTexture::TransitionImageLayout(cmd, m_RtgiRaw.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_RtReflRaw.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(
        cmd,
        m_RtgiHistory[writeHistIdx].GetImage(),
        m_HistoryInitialized[writeHistIdx] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    VulkanTexture::TransitionImageLayout(
        cmd,
        m_RtReflHistory[writeHistIdx].GetImage(),
        m_HistoryInitialized[writeHistIdx] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    // Pass 2: Temporal Reprojection & Variance Clamping
    {
        VkDescriptorImageInfo giRawIn{ m_RtgiRaw.GetSampler(), m_RtgiRaw.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo giPrevHist{ m_RtgiHistory[readHistIdx].GetSampler(), m_RtgiHistory[readHistIdx].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo rfRawIn{ m_RtReflRaw.GetSampler(), m_RtReflRaw.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo rfPrevHist{ m_RtReflHistory[readHistIdx].GetSampler(), m_RtReflHistory[readHistIdx].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo nrm{ gbufNormalRough.GetSampler(), gbufNormalRough.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo dpt{ gbufDepth.GetSampler(), gbufDepth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo mot{ gbufMotion.GetSampler(), gbufMotion.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo giCurrHist{ VK_NULL_HANDLE, m_RtgiHistory[writeHistIdx].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo rfCurrHist{ VK_NULL_HANDLE, m_RtReflHistory[writeHistIdx].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorBufferInfo giBuf{ m_GiParamsUBOs[fSlot].GetBuffer(), 0, sizeof(GpuGiParamsUBO) };

        std::array<VkWriteDescriptorSet, 10> w{};
        for (int i = 0; i < 10; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = m_RtgiDenoiseSets[fSlot];
            w[i].dstBinding = static_cast<uint32_t>(i);
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &giRawIn;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[1].pImageInfo = &giPrevHist;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[2].pImageInfo = &rfRawIn;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[3].pImageInfo = &rfPrevHist;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[4].pImageInfo = &nrm;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[5].pImageInfo = &dpt;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[6].pImageInfo = &mot;
        w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;         w[7].pImageInfo = &giCurrHist;
        w[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;         w[8].pImageInfo = &rfCurrHist;
        w[9].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;        w[9].pBufferInfo = &giBuf;
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_RtgiDenoisePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_RtgiDenoisePipeLayout, 0, 1, &m_RtgiDenoiseSets[fSlot], 0, nullptr);
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    VulkanTexture::TransitionImageLayout(cmd, m_RtgiHistory[writeHistIdx].GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_RtReflHistory[writeHistIdx].GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    m_HistoryInitialized[writeHistIdx] = true;

    // Pass 3: 2-Pass A-Trous Wavelet Edge-Stopping Denoiser (step=1, step=2) or Bypass
    auto runAtrousPass = [&](VkDescriptorSet atrousSet, const VulkanTexture& giIn, const VulkanTexture& rfIn, const VulkanTexture& giOut, const VulkanTexture& rfOut, int32_t stepSize) {
        VkDescriptorImageInfo giI{ giIn.GetSampler(), giIn.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo rfI{ rfIn.GetSampler(), rfIn.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo nrmI{ gbufNormalRough.GetSampler(), gbufNormalRough.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo dptI{ gbufDepth.GetSampler(), gbufDepth.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo giO{ VK_NULL_HANDLE, giOut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo rfO{ VK_NULL_HANDLE, rfOut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorBufferInfo giBuf{ m_GiParamsUBOs[fSlot].GetBuffer(), 0, sizeof(GpuGiParamsUBO) };

        std::array<VkWriteDescriptorSet, 7> w{};
        for (int i = 0; i < 7; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = atrousSet;
            w[i].dstBinding = static_cast<uint32_t>(i);
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &giI;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[1].pImageInfo = &rfI;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[2].pImageInfo = &nrmI;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[3].pImageInfo = &dptI;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;         w[4].pImageInfo = &giO;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;         w[5].pImageInfo = &rfO;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;        w[6].pBufferInfo = &giBuf;
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_RtgiAtrousPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_RtgiAtrousPipeLayout, 0, 1, &atrousSet, 0, nullptr);
        vkCmdPushConstants(cmd, m_RtgiAtrousPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int32_t), &stepSize);
        vkCmdDispatch(cmd, groupsX, groupsY, 1);
    };

    if (settings.giDenoiserEnabled) {
        VulkanTexture::TransitionImageLayout(cmd, m_RtgiDenoised.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtReflDenoised.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        runAtrousPass(m_RtgiAtrousSets[fSlot][0], m_RtgiHistory[writeHistIdx], m_RtReflHistory[writeHistIdx], m_RtgiDenoised, m_RtReflDenoised, 1);

        VulkanTexture::TransitionImageLayout(cmd, m_RtgiDenoised.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtReflDenoised.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtgiTemp.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtReflTemp.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        runAtrousPass(m_RtgiAtrousSets[fSlot][1], m_RtgiDenoised, m_RtReflDenoised, m_RtgiTemp, m_RtReflTemp, 2);

        VulkanTexture::TransitionImageLayout(cmd, m_RtgiTemp.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtReflTemp.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtgiDenoised.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtReflDenoised.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        runAtrousPass(m_RtgiAtrousSets[fSlot][2], m_RtgiTemp, m_RtReflTemp, m_RtgiDenoised, m_RtReflDenoised, 4);
    } else {
        VulkanTexture::TransitionImageLayout(cmd, m_RtgiDenoised.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_RtReflDenoised.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        runAtrousPass(m_RtgiAtrousSets[fSlot][1], m_RtgiHistory[writeHistIdx], m_RtReflHistory[writeHistIdx], m_RtgiDenoised, m_RtReflDenoised, 0);
    }

    VulkanTexture::TransitionImageLayout(cmd, m_RtgiDenoised.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    VulkanTexture::TransitionImageLayout(cmd, m_RtReflDenoised.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    m_HistoryReadIdx = writeHistIdx;
    ++m_RtgiTemporalFrame;
    return true;
}

bool VulkanRayTracedGI::ExecutePathTracing(
    VulkanContext* context,
    VkCommandBuffer cmd,
    uint32_t slot,
    VkDescriptorSet bindlessSet,
    VkAccelerationStructureKHR tlas,
    VkBuffer rtInstanceInfoBuffer,
    VkBuffer lightingUboBuffer,
    const VulkanTexture& hdrColorOut,
    uint64_t sceneStateHash,
    const GraphicsSettingsData& settings)
{
    if (!m_RayQueryPipelinesReady || !m_TargetsCreated || tlas == VK_NULL_HANDLE || rtInstanceInfoBuffer == VK_NULL_HANDLE) {
        return false;
    }

    const uint32_t fSlot = slot % kFrameSlots;
    const bool isRealtimeSvgf = (settings.ptMode == PathTracingMode::RealtimeSVGF);
    if (m_ForceReset || sceneStateHash != m_LastPtSceneHash) {
        m_PtSampleCount = 0;
        m_LastPtSceneHash = sceneStateHash;
        m_ForceReset = false;
    }

    ++m_PtFrameIndex;
    const uint32_t maxSamples = (!isRealtimeSvgf && settings.ptMaxSamples > 0) ? static_cast<uint32_t>(settings.ptMaxSamples) : 0u;
    const bool holdSample = (!isRealtimeSvgf && maxSamples > 0u && m_PtSampleCount >= maxSamples);
    if (!holdSample) {
        ++m_PtSampleCount;
    }
    const bool runTrace = isRealtimeSvgf || !holdSample || !m_PtAccumInitialized;

    GpuGiParamsUBO ubo{};
    ubo.width = m_Width;
    ubo.height = m_Height;
    ubo.frameIndex = std::max(isRealtimeSvgf ? m_PtFrameIndex : m_PtSampleCount, 1u);
    ubo.sampleCount = isRealtimeSvgf ? 1u : std::max(m_PtSampleCount, 1u);
    ubo.giMode = isRealtimeSvgf ? 4 : (runTrace ? 2 : 3);
    ubo.giBounces = std::clamp(settings.giBounces, 1, 8);
    ubo.giRaysPerPixel = std::clamp(settings.giRaysPerPixel, 1, isRealtimeSvgf ? 2 : 4);
    ubo.giIntensity = settings.giIntensity;
    ubo.giTemporalAlpha = settings.giTemporalAlpha;
    ubo.giDenoiserEnabled = settings.giDenoiserEnabled ? 1 : 0;
    ubo.giRayTracedShadows = settings.giRayTracedShadows ? 1 : 0;
    ubo.hasValidTLAS = 1;
    ubo.svgfAtrousIterations = std::clamp(settings.svgfAtrousIterations, 1, 5);
    ubo.svgfSigmaLuminance = std::clamp(settings.svgfSigmaLuminance, 0.25f, 8.0f);
    ubo.svgfSigmaNormal = std::clamp(settings.svgfSigmaNormal, 16.0f, 512.0f);
    ubo.svgfSigmaDepth = std::clamp(settings.svgfSigmaDepth, 0.10f, 8.0f);
    UpdateGiParamsBuffer(context, fSlot, ubo);

    VulkanTexture::TransitionImageLayout(cmd, hdrColorOut.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

    VkDevice device = context->GetDevice();
    const uint32_t groupsX = (m_Width + 7) / 8;
    const uint32_t groupsY = (m_Height + 7) / 8;
    const uint32_t currPP = m_PtPingPongIdx & 1u;

    if (runTrace) {
        const VkImageLayout prevAccumLayout = m_PtAccumInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        const VkImageLayout prevPPLayout = m_PtPingPongInitialized[currPP] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;

        VulkanTexture::TransitionImageLayout(cmd, m_PtAccum.GetImage(), prevAccumLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtSpecAccum.GetImage(), prevAccumLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtAlbedo.GetImage(), prevAccumLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtEmissive.GetImage(), prevAccumLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtMotion.GetImage(), prevAccumLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtNormalRough[currPP].GetImage(), prevPPLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtDepthInst[currPP].GetImage(), prevPPLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

        VkDescriptorImageInfo ptAcc{ VK_NULL_HANDLE, m_PtAccum.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo hdrOut{ VK_NULL_HANDLE, hdrColorOut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSetAccelerationStructureKHR asInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &tlas;
        VkDescriptorBufferInfo instBuf{ rtInstanceInfoBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo sceneBuf{ lightingUboBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo giBuf{ m_GiParamsUBOs[fSlot].GetBuffer(), 0, sizeof(GpuGiParamsUBO) };
        VkDescriptorImageInfo ptNormRoughOut{ VK_NULL_HANDLE, m_PtNormalRough[currPP].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo ptAlbedoOut{ VK_NULL_HANDLE, m_PtAlbedo.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo ptEmissiveOut{ VK_NULL_HANDLE, m_PtEmissive.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo ptDepthInstOut{ VK_NULL_HANDLE, m_PtDepthInst[currPP].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo ptMotionOut{ VK_NULL_HANDLE, m_PtMotion.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo ptSpecAcc{ VK_NULL_HANDLE, m_PtSpecAccum.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };

        std::array<VkWriteDescriptorSet, 12> w{};
        for (int i = 0; i < 12; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = m_PtSets[fSlot];
            w[i].dstBinding = static_cast<uint32_t>(i);
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                  w[0].pImageInfo  = &ptAcc;
        w[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                  w[1].pImageInfo  = &hdrOut;
        w[2].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;     w[2].pNext       = &asInfo;
        w[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;                 w[3].pBufferInfo = &instBuf;
        w[4].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;                 w[4].pBufferInfo = &sceneBuf;
        w[5].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;                 w[5].pBufferInfo = &giBuf;
        w[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                  w[6].pImageInfo  = &ptNormRoughOut;
        w[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                  w[7].pImageInfo  = &ptAlbedoOut;
        w[8].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                  w[8].pImageInfo  = &ptEmissiveOut;
        w[9].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                  w[9].pImageInfo  = &ptDepthInstOut;
        w[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                  w[10].pImageInfo = &ptMotionOut;
        w[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;                  w[11].pImageInfo = &ptSpecAcc;
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtPipeline);
        std::array<VkDescriptorSet, 2> ptSets = { bindlessSet, m_PtSets[fSlot] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtPipeLayout, 0, 2, ptSets.data(), 0, nullptr);
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        VulkanTexture::TransitionImageLayout(cmd, m_PtAccum.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtSpecAccum.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtAlbedo.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtEmissive.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtMotion.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtNormalRough[currPP].GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtDepthInst[currPP].GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageMemoryBarrier hdrBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        hdrBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hdrBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        hdrBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        hdrBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        hdrBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hdrBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hdrBarrier.image = hdrColorOut.GetImage();
        hdrBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &hdrBarrier);

        m_PtAccumInitialized = true;
    }

    // Pass 2: SVGF Spatiotemporal Variance-Guided Filtering (RealtimeSVGF) or Edge-Stopping Spatial Denoise (Progressive)
    if (isRealtimeSvgf) {
        const uint32_t prevPP = 1u - currPP;
        if (!m_PtPingPongInitialized[prevPP]) {
            VulkanTexture::TransitionImageLayout(cmd, m_PtNormalRough[prevPP].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtDepthInst[prevPP].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtIrradianceHistory[prevPP].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtSpecularHistory[prevPP].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtMomentsHistory[prevPP].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            m_PtPingPongInitialized[prevPP] = true;
        }

        const VkImageLayout prevCurrHistLayout = m_PtPingPongInitialized[currPP] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        VulkanTexture::TransitionImageLayout(cmd, m_PtIrradianceHistory[currPP].GetImage(), prevCurrHistLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtSpecularHistory[currPP].GetImage(), prevCurrHistLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtMomentsHistory[currPP].GetImage(), prevCurrHistLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

        // Pass 2A: SVGF Dual Temporal Reprojection (DiffuseIrradiance + SpecularRadiance) + Moments (mu1, mu2, historyLength, specMu1)
        {
            VkDescriptorImageInfo rawIrrI{ m_PtAccum.GetSampler(), m_PtAccum.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo currNrmI{ m_PtNormalRough[currPP].GetSampler(), m_PtNormalRough[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo currDptI{ m_PtDepthInst[currPP].GetSampler(), m_PtDepthInst[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo motI{ m_PtMotion.GetSampler(), m_PtMotion.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo prevIrrI{ m_PtIrradianceHistory[prevPP].GetSampler(), m_PtIrradianceHistory[prevPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo prevMomI{ m_PtMomentsHistory[prevPP].GetSampler(), m_PtMomentsHistory[prevPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo prevNrmI{ m_PtNormalRough[prevPP].GetSampler(), m_PtNormalRough[prevPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo prevDptI{ m_PtDepthInst[prevPP].GetSampler(), m_PtDepthInst[prevPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo currIrrO{ VK_NULL_HANDLE, m_PtIrradianceHistory[currPP].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorImageInfo currMomO{ VK_NULL_HANDLE, m_PtMomentsHistory[currPP].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorBufferInfo giBuf{ m_GiParamsUBOs[fSlot].GetBuffer(), 0, sizeof(GpuGiParamsUBO) };
            VkDescriptorImageInfo rawSpecI{ m_PtSpecAccum.GetSampler(), m_PtSpecAccum.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo prevSpecI{ m_PtSpecularHistory[prevPP].GetSampler(), m_PtSpecularHistory[prevPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo currSpecO{ VK_NULL_HANDLE, m_PtSpecularHistory[currPP].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };

            std::array<VkWriteDescriptorSet, 14> tw{};
            for (int i = 0; i < 14; ++i) {
                tw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                tw[i].dstSet = m_PtSvgfTemporalSets[fSlot];
                tw[i].dstBinding = static_cast<uint32_t>(i);
                tw[i].descriptorCount = 1;
            }
            tw[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[0].pImageInfo   = &rawIrrI;
            tw[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[1].pImageInfo   = &currNrmI;
            tw[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[2].pImageInfo   = &currDptI;
            tw[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[3].pImageInfo   = &motI;
            tw[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[4].pImageInfo   = &prevIrrI;
            tw[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[5].pImageInfo   = &prevMomI;
            tw[6].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[6].pImageInfo   = &prevNrmI;
            tw[7].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[7].pImageInfo   = &prevDptI;
            tw[8].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          tw[8].pImageInfo   = &currIrrO;
            tw[9].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          tw[9].pImageInfo   = &currMomO;
            tw[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         tw[10].pBufferInfo = &giBuf;
            tw[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[11].pImageInfo  = &rawSpecI;
            tw[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; tw[12].pImageInfo  = &prevSpecI;
            tw[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          tw[13].pImageInfo  = &currSpecO;
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(tw.size()), tw.data(), 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtSvgfTemporalPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtSvgfTemporalPipeLayout, 0, 1, &m_PtSvgfTemporalSets[fSlot], 0, nullptr);
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        }

        VulkanTexture::TransitionImageLayout(cmd, m_PtIrradianceHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtSpecularHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, m_PtMomentsHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        m_PtPingPongInitialized[currPP] = true;

        // Pass 2B: SVGF Spatial Variance (for historyLength < 4) + Dynamic 1..5 Iteration Variance-Guided A-Trous Wavelet (default 3)
        auto runSvgfAtrous = [&](VkDescriptorSet atrousSet, const VulkanTexture& irrVarIn, const VulkanTexture& specVarIn, const VulkanTexture& irrVarOut, const VulkanTexture& specVarOut, int32_t stepSize, int32_t isLastPass) {
            VkDescriptorImageInfo ivIn{ irrVarIn.GetSampler(), irrVarIn.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo momIn{ m_PtMomentsHistory[currPP].GetSampler(), m_PtMomentsHistory[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo nrmIn{ m_PtNormalRough[currPP].GetSampler(), m_PtNormalRough[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo dptIn{ m_PtDepthInst[currPP].GetSampler(), m_PtDepthInst[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo albIn{ m_PtAlbedo.GetSampler(), m_PtAlbedo.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo emiIn{ m_PtEmissive.GetSampler(), m_PtEmissive.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo ivOut{ VK_NULL_HANDLE, irrVarOut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorImageInfo hOut{ VK_NULL_HANDLE, hdrColorOut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorBufferInfo giBuf{ m_GiParamsUBOs[fSlot].GetBuffer(), 0, sizeof(GpuGiParamsUBO) };
            VkDescriptorImageInfo svIn{ specVarIn.GetSampler(), specVarIn.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo svOut{ VK_NULL_HANDLE, specVarOut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };

            std::array<VkWriteDescriptorSet, 11> aw{};
            for (int i = 0; i < 11; ++i) {
                aw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                aw[i].dstSet = atrousSet;
                aw[i].dstBinding = static_cast<uint32_t>(i);
                aw[i].descriptorCount = 1;
            }
            aw[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; aw[0].pImageInfo   = &ivIn;
            aw[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; aw[1].pImageInfo   = &momIn;
            aw[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; aw[2].pImageInfo   = &nrmIn;
            aw[3].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; aw[3].pImageInfo   = &dptIn;
            aw[4].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; aw[4].pImageInfo   = &albIn;
            aw[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; aw[5].pImageInfo   = &emiIn;
            aw[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          aw[6].pImageInfo   = &ivOut;
            aw[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          aw[7].pImageInfo   = &hOut;
            aw[8].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         aw[8].pBufferInfo  = &giBuf;
            aw[9].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; aw[9].pImageInfo   = &svIn;
            aw[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          aw[10].pImageInfo  = &svOut;
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(aw.size()), aw.data(), 0, nullptr);

            int32_t pc[2] = { stepSize, isLastPass };
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtSvgfAtrousPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtSvgfAtrousPipeLayout, 0, 1, &atrousSet, 0, nullptr);
            vkCmdPushConstants(cmd, m_PtSvgfAtrousPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        };

        if (settings.giDenoiserEnabled) {
            const int numIterations = std::clamp(settings.svgfAtrousIterations, 1, 5);

            // Iteration 0 (step = 1): m_PtIrradianceHistory[currPP] / m_PtSpecularHistory[currPP] -> Ping
            VulkanTexture::TransitionImageLayout(cmd, m_PtAtrousPing.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtSpecAtrousPing.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            runSvgfAtrous(m_PtSvgfAtrousSets[fSlot][0], m_PtIrradianceHistory[currPP], m_PtSpecularHistory[currPP], m_PtAtrousPing, m_PtSpecAtrousPing, 1, 0);
            VulkanTexture::TransitionImageLayout(cmd, m_PtAtrousPing.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtSpecAtrousPing.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

            // Feed 1st-iteration diffuse wavelet output back into m_PtIrradianceHistory[currPP] to stabilize next frame's temporal history (Schied et al. 2017)
            // Keep m_PtSpecularHistory[currPP] at its unblurred temporal value so glossy/mirror specular never accumulates spatial blur across frames.
            VulkanTexture::TransitionImageLayout(cmd, m_PtIrradianceHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtAtrousPing.GetImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VkImageCopy histCopy{};
            histCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            histCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            histCopy.extent = { m_Width, m_Height, 1 };
            vkCmdCopyImage(cmd, m_PtAtrousPing.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_PtIrradianceHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &histCopy);
            VulkanTexture::TransitionImageLayout(cmd, m_PtIrradianceHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtAtrousPing.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

            m_PtAtrousFinalIdx = 0; // Ping holds iteration 0 output
            for (int iter = 1; iter < numIterations; ++iter) {
                const int32_t stepSize = 1 << iter;
                const bool pingToPong = ((iter & 1) == 1);
                const VulkanTexture& diffIn  = pingToPong ? m_PtAtrousPing : m_PtAtrousPong;
                const VulkanTexture& specIn  = pingToPong ? m_PtSpecAtrousPing : m_PtSpecAtrousPong;
                const VulkanTexture& diffOut = pingToPong ? m_PtAtrousPong : m_PtAtrousPing;
                const VulkanTexture& specOut = pingToPong ? m_PtSpecAtrousPong : m_PtSpecAtrousPing;

                const VkImageLayout prevOutLayout = (iter == 1) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VulkanTexture::TransitionImageLayout(cmd, diffOut.GetImage(), prevOutLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
                VulkanTexture::TransitionImageLayout(cmd, specOut.GetImage(), prevOutLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);

                runSvgfAtrous(m_PtSvgfAtrousSets[fSlot][iter], diffIn, specIn, diffOut, specOut, stepSize, 0);

                VulkanTexture::TransitionImageLayout(cmd, diffOut.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
                VulkanTexture::TransitionImageLayout(cmd, specOut.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
                m_PtAtrousFinalIdx = pingToPong ? 1u : 0u;
            }
        } else {
            VulkanTexture::TransitionImageLayout(cmd, m_PtAtrousPong.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtSpecAtrousPong.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
            runSvgfAtrous(m_PtSvgfAtrousSets[fSlot][0], m_PtIrradianceHistory[currPP], m_PtSpecularHistory[currPP], m_PtAtrousPong, m_PtSpecAtrousPong, 0, 0);
            VulkanTexture::TransitionImageLayout(cmd, m_PtAtrousPong.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtSpecAtrousPong.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            m_PtAtrousFinalIdx = 1u;
        }
        m_PtAtrousInitialized = true;

        // Pass 2C: Dedicated Final Modulation & Composition: (Diffuse_Filtrado * Albedo_Original) + Specular_Filtrado + Emissive -> hdrColorOut
        {
            const VulkanTexture& finalDiff = GetPtFilteredIrradiance();
            const VulkanTexture& finalSpec = GetPtFilteredSpecular();

            VkDescriptorImageInfo filtI{ finalDiff.GetSampler(), finalDiff.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo albI{ m_PtAlbedo.GetSampler(), m_PtAlbedo.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo emiI{ m_PtEmissive.GetSampler(), m_PtEmissive.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo nrmI{ m_PtNormalRough[currPP].GetSampler(), m_PtNormalRough[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo dptI{ m_PtDepthInst[currPP].GetSampler(), m_PtDepthInst[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo momI{ m_PtMomentsHistory[currPP].GetSampler(), m_PtMomentsHistory[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorImageInfo hOut{ VK_NULL_HANDLE, hdrColorOut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorBufferInfo giBuf{ m_GiParamsUBOs[fSlot].GetBuffer(), 0, sizeof(GpuGiParamsUBO) };
            VkDescriptorImageInfo filtSpecI{ finalSpec.GetSampler(), finalSpec.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            std::array<VkWriteDescriptorSet, 9> mw{};
            for (int i = 0; i < 9; ++i) {
                mw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                mw[i].dstSet = m_PtSvgfModulateSets[fSlot];
                mw[i].dstBinding = static_cast<uint32_t>(i);
                mw[i].descriptorCount = 1;
            }
            mw[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; mw[0].pImageInfo  = &filtI;
            mw[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; mw[1].pImageInfo  = &albI;
            mw[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; mw[2].pImageInfo  = &emiI;
            mw[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; mw[3].pImageInfo  = &nrmI;
            mw[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; mw[4].pImageInfo  = &dptI;
            mw[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; mw[5].pImageInfo  = &momI;
            mw[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          mw[6].pImageInfo  = &hOut;
            mw[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         mw[7].pBufferInfo = &giBuf;
            mw[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; mw[8].pImageInfo  = &filtSpecI;
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(mw.size()), mw.data(), 0, nullptr);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtSvgfModulatePipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtSvgfModulatePipeLayout, 0, 1, &m_PtSvgfModulateSets[fSlot], 0, nullptr);
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        }
    } else {
        const VkImageLayout prevHistLayout = m_PtPingPongInitialized[currPP] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        VulkanTexture::TransitionImageLayout(cmd, m_PtIrradianceHistory[currPP].GetImage(), prevHistLayout, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!m_PtPingPongInitialized[currPP]) {
            VulkanTexture::TransitionImageLayout(cmd, m_PtSpecularHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            VulkanTexture::TransitionImageLayout(cmd, m_PtMomentsHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        VkDescriptorImageInfo ptAccIn{ m_PtAccum.GetSampler(), m_PtAccum.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo ptNormRoughIn{ m_PtNormalRough[currPP].GetSampler(), m_PtNormalRough[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo hdrOut{ VK_NULL_HANDLE, hdrColorOut.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorBufferInfo giBuf{ m_GiParamsUBOs[fSlot].GetBuffer(), 0, sizeof(GpuGiParamsUBO) };
        VkDescriptorImageInfo ptAlbedoIn{ m_PtAlbedo.GetSampler(), m_PtAlbedo.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo ptEmissiveIn{ m_PtEmissive.GetSampler(), m_PtEmissive.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo ptDepthInstIn{ m_PtDepthInst[currPP].GetSampler(), m_PtDepthInst[currPP].GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkDescriptorImageInfo ptIrrHistOut{ VK_NULL_HANDLE, m_PtIrradianceHistory[currPP].GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo ptSpecAccIn{ m_PtSpecAccum.GetSampler(), m_PtSpecAccum.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        std::array<VkWriteDescriptorSet, 9> w{};
        for (int i = 0; i < 9; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = m_PtDenoiseSets[fSlot];
            w[i].dstBinding = static_cast<uint32_t>(i);
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo  = &ptAccIn;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[1].pImageInfo  = &ptNormRoughIn;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;         w[2].pImageInfo  = &hdrOut;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;        w[3].pBufferInfo = &giBuf;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[4].pImageInfo  = &ptAlbedoIn;
        w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[5].pImageInfo  = &ptEmissiveIn;
        w[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[6].pImageInfo  = &ptDepthInstIn;
        w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;         w[7].pImageInfo  = &ptIrrHistOut;
        w[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[8].pImageInfo  = &ptSpecAccIn;
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtDenoisePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PtDenoisePipeLayout, 0, 1, &m_PtDenoiseSets[fSlot], 0, nullptr);
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        VulkanTexture::TransitionImageLayout(cmd, m_PtIrradianceHistory[currPP].GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        m_PtPingPongInitialized[currPP] = true;
    }

    m_PtLastWrittenIdx = currPP;
    if (runTrace) {
        m_PtPingPongIdx = 1u - currPP;
    }

    return true;
}
