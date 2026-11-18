#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <iostream>

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
#else
    #include <glad/glad.h>
#endif
#include <SDL2/SDL.h>

std::unordered_map<std::string, std::unique_ptr<SlipShader>> SlipShaderLibrary::s_Shaders;
bool SlipShaderLibrary::s_Initialized = false;

namespace {

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
const char* s_ShadowDepthVert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 2) in vec2 aTexCoords;\n"
    "layout (location = 5) in ivec4 aBoneIDs;\n"
    "layout (location = 6) in vec4 aWeights;\n"
    "out vec2 vTexCoords;\n"
    "uniform mat4 u_LightSpaceMatrix;\n"
    "uniform mat4 u_Model;\n"
    "#define MAX_BONES 100\n"
    "uniform mat4 u_BoneTransforms[MAX_BONES];\n"
    "uniform int u_SkinningQuality;\n"
    "uniform bool u_HasBones;\n"
    "void main() {\n"
    "    mat4 skinMatrix = mat4(1.0);\n"
    "    if (u_HasBones && u_SkinningQuality > 0) {\n"
    "        if (u_SkinningQuality >= 4) {\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * aWeights[0]\n"
    "                       + u_BoneTransforms[aBoneIDs[1]] * aWeights[1]\n"
    "                       + u_BoneTransforms[aBoneIDs[2]] * aWeights[2]\n"
    "                       + u_BoneTransforms[aBoneIDs[3]] * aWeights[3];\n"
    "        } else if (u_SkinningQuality == 2) {\n"
    "            float totalW = aWeights[0] + aWeights[1];\n"
    "            float w0 = (totalW > 0.0) ? (aWeights[0] / totalW) : 1.0;\n"
    "            float w1 = (totalW > 0.0) ? (aWeights[1] / totalW) : 0.0;\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * w0 + u_BoneTransforms[aBoneIDs[1]] * w1;\n"
    "        } else if (u_SkinningQuality == 1) {\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]];\n"
    "        }\n"
    "    }\n"
    "    vTexCoords = aTexCoords;\n"
    "    vec4 localPos = skinMatrix * vec4(aPos, 1.0);\n"
    "    gl_Position = u_LightSpaceMatrix * u_Model * localPos;\n"
    "}\n";

const char* s_ShadowDepthFrag =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision mediump sampler2D;\n"
    "in vec2 vTexCoords;\n"
    "uniform bool u_HasAlphaMap;\n"
    "uniform sampler2D u_AlphaMap;\n"
    "uniform bool u_AlphaFromRed;\n"
    "uniform float u_AlphaCutoff;\n"
    "void main() {\n"
    "    if (u_HasAlphaMap) {\n"
    "        vec4 samp = texture(u_AlphaMap, vTexCoords);\n"
    "        float a = u_AlphaFromRed ? ((samp.a < 0.98) ? samp.a : samp.r) : samp.a;\n"
    "        if (a < max(u_AlphaCutoff, 0.05)) discard;\n"
    "    }\n"
    "}\n";

const char* s_LineVert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout (location = 0) in vec3 aPos;\n"
    "uniform mat4 u_Model;\n"
    "uniform mat4 u_View;\n"
    "uniform mat4 u_Projection;\n"
    "void main() {\n"
    "    gl_Position = u_Projection * u_View * u_Model * vec4(aPos, 1.0);\n"
    "}\n";

const char* s_LineFrag =
    "#version 300 es\n"
    "precision highp float;\n"
    "out vec4 FragColor;\n"
    "uniform vec4 u_Color;\n"
    "void main() {\n"
    "    FragColor = u_Color;\n"
    "}\n";

const char* s_UnlitVert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aNormal;\n"
    "layout (location = 2) in vec2 aTexCoords;\n"
    "out vec2 vTexCoords;\n"
    "uniform mat4 u_Model;\n"
    "uniform mat4 u_View;\n"
    "uniform mat4 u_Projection;\n"
    "uniform float u_OutlineWidth;\n"
    "void main() {\n"
    "    vTexCoords = aTexCoords;\n"
    "    vec3 pos = aPos;\n"
    "    if (u_OutlineWidth > 0.0) {\n"
    "        vec3 n = length(aNormal) > 0.001 ? normalize(aNormal) : normalize(aPos);\n"
    "        pos += n * u_OutlineWidth;\n"
    "    }\n"
    "    gl_Position = u_Projection * u_View * u_Model * vec4(pos, 1.0);\n"
    "}\n";

const char* s_UnlitFrag =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision mediump sampler2D;\n"
    "in vec2 vTexCoords;\n"
    "out vec4 FragColor;\n"
    "uniform vec4 u_Color;\n"
    "uniform sampler2D u_MainTexture;\n"
    "uniform bool u_HasTexture;\n"
    "void main() {\n"
    "    vec4 col = u_Color;\n"
    "    if (u_HasTexture) {\n"
    "        col *= texture(u_MainTexture, vTexCoords);\n"
    "    }\n"
    "    FragColor = col;\n"
    "}\n";

const char* s_LitVert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aNormal;\n"
    "layout (location = 2) in vec2 aTexCoords;\n"
    "layout (location = 3) in vec3 aTangent;\n"
    "layout (location = 4) in vec3 aBitangent;\n"
    "layout (location = 5) in ivec4 aBoneIDs;\n"
    "layout (location = 6) in vec4 aWeights;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec2 vTexCoords;\n"
    "out vec3 vTangent;\n"
    "out vec3 vBitangent;\n"
    "uniform mat4 u_Model;\n"
    "uniform mat4 u_View;\n"
    "uniform mat4 u_Projection;\n"
    "#define MAX_BONES 100\n"
    "uniform mat4 u_BoneTransforms[MAX_BONES];\n"
    "uniform int u_SkinningQuality;\n"
    "uniform bool u_HasBones;\n"
    "void main() {\n"
    "    mat4 skinMatrix = mat4(1.0);\n"
    "    if (u_HasBones && u_SkinningQuality > 0) {\n"
    "        if (u_SkinningQuality >= 4) {\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * aWeights[0]\n"
    "                       + u_BoneTransforms[aBoneIDs[1]] * aWeights[1]\n"
    "                       + u_BoneTransforms[aBoneIDs[2]] * aWeights[2]\n"
    "                       + u_BoneTransforms[aBoneIDs[3]] * aWeights[3];\n"
    "        } else if (u_SkinningQuality == 2) {\n"
    "            float totalW = aWeights[0] + aWeights[1];\n"
    "            float w0 = (totalW > 0.0) ? (aWeights[0] / totalW) : 1.0;\n"
    "            float w1 = (totalW > 0.0) ? (aWeights[1] / totalW) : 0.0;\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * w0 + u_BoneTransforms[aBoneIDs[1]] * w1;\n"
    "        } else if (u_SkinningQuality == 1) {\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]];\n"
    "        }\n"
    "    }\n"
    "    vec4 localPos = skinMatrix * vec4(aPos, 1.0);\n"
    "    vec4 worldPos = u_Model * localPos;\n"
    "    vWorldPos = worldPos.xyz;\n"
    "    mat3 skin3 = mat3(skinMatrix);\n"
    "    mat3 model3 = mat3(u_Model);\n"
    "    vNormal = normalize(model3 * (skin3 * aNormal));\n"
    "    vTangent = model3 * (skin3 * aTangent);\n"
    "    vBitangent = model3 * (skin3 * aBitangent);\n"
    "    vTexCoords = aTexCoords;\n"
    "    gl_Position = u_Projection * u_View * worldPos;\n"
    "}\n";

const char* s_LitFrag =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision mediump int;\n"
    "precision mediump sampler2D;\n"
    "precision highp sampler2DArray;\n"
    "precision mediump samplerCube;\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "in vec2 vTexCoords;\n"
    "in vec3 vTangent;\n"
    "in vec3 vBitangent;\n"
    "out vec4 FragColor;\n"
    "uniform vec3 u_Albedo;\n"
    "uniform int u_VClustDebugMode;\n"
    "uniform int u_VClustPrimOffset;\n"
    "uniform float u_Roughness;\n"
    "uniform float u_Metallic;\n"
    "uniform float u_AO;\n"
    "uniform vec3 u_EmissiveColor;\n"
    "uniform float u_EmissiveIntensity;\n"
    "uniform sampler2D u_AlbedoMap;\n"
    "uniform bool u_HasAlbedoMap;\n"
    "uniform sampler2D u_NormalMap;\n"
    "uniform bool u_HasNormalMap;\n"
    "uniform float u_NormalScale;\n"
    "uniform sampler2D u_RoughnessMap;\n"
    "uniform bool u_HasRoughnessMap;\n"
    "uniform sampler2D u_MetallicMap;\n"
    "uniform bool u_HasMetallicMap;\n"
    "uniform sampler2D u_AOMap;\n"
    "uniform bool u_HasAOMap;\n"
    "uniform sampler2D u_EmissiveMap;\n"
    "uniform bool u_HasEmissiveMap;\n"
    "uniform sampler2D u_OpacityMap;\n"
    "uniform bool u_HasOpacityMap;\n"
    "uniform bool u_PackedORM;\n"
    "uniform bool u_AlphaClip;\n"
    "uniform float u_AlphaCutoff;\n"
    "uniform bool u_InvertRoughness;\n"
    "uniform vec3 u_CameraPos;\n"
    "uniform samplerCube u_EnvCube;\n"
    "uniform bool u_HasEnvCube;\n"
    "uniform float u_EnvIntensity;\n"
    "uniform float u_ReflectionRoughnessCutoff;\n"
    "uniform bool u_ProbeBoxProjection;\n"
    "uniform vec3 u_ProbeOrigin;\n"
    "uniform vec3 u_ProbeBoxMin;\n"
    "uniform vec3 u_ProbeBoxMax;\n"
    "struct DirectionalLight {\n"
    "    vec3 direction;\n"
    "    vec3 color;\n"
    "    float intensity;\n"
    "};\n"
    "struct PointLight {\n"
    "    vec3 position;\n"
    "    vec3 color;\n"
    "    float intensity;\n"
    "    float range;\n"
    "};\n"
    "struct SpotLight {\n"
    "    vec3 position;\n"
    "    vec3 direction;\n"
    "    vec3 color;\n"
    "    float intensity;\n"
    "    float range;\n"
    "    float spotAngle;\n"
    "};\n"
    "#define MAX_DIR_LIGHTS 4\n"
    "#define MAX_POINT_LIGHTS 32\n"
    "#define MAX_SPOT_LIGHTS 4\n"
    "uniform int u_NumDirLights;\n"
    "uniform DirectionalLight u_DirLights[MAX_DIR_LIGHTS];\n"
    "uniform int u_NumPointLights;\n"
    "uniform PointLight u_PointLights[MAX_POINT_LIGHTS];\n"
    "uniform int u_NumSpotLights;\n"
    "uniform SpotLight u_SpotLights[MAX_SPOT_LIGHTS];\n"
    "uniform bool u_HasLightmap;\n"
    "uniform sampler2D u_LightmapAtlas;\n"
    "uniform vec4 u_LightmapScaleOffset;\n"
    "uniform highp sampler2DArray u_ShadowMapArray;\n"
    "uniform mat4 u_LightSpaceMatrices[4];\n"
    "uniform float u_CascadeEndClipSpace[4];\n"
    "uniform int u_CascadeCount;\n"
    "uniform float u_ShadowBias;\n"
    "uniform bool u_ShadowsEnabled;\n"
    "uniform int u_PCFQuality;\n"
    "uniform mat4 u_View;\n"
    "uniform bool u_CloudShadowsEnabled;\n"
    "uniform float u_CloudShadowIntensity;\n"
    "uniform sampler2D u_CloudShadowNoise;\n"
    "uniform float u_CloudBaseHeight;\n"
    "uniform float u_CloudScale;\n"
    "uniform vec2 u_CloudWindOffset;\n"
    "uniform float u_CloudCoverage;\n"
    "uniform float u_CloudLayerThickness;\n"
    "uniform float u_CloudDensity;\n"
    "uniform float u_CloudEdgeErosion;\n"
    "uniform float u_CloudDetailScale;\n"
    "uniform float u_Absorption;\n"
    "float SampleCascadeLayer(int l, vec3 bPos, float b, vec2 tSize) {\n"
    "    vec4 fragPosLightSpace = u_LightSpaceMatrices[l] * vec4(bPos, 1.0);\n"
    "    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;\n"
    "    projCoords = projCoords * 0.5 + 0.5;\n"
    "    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {\n"
    "        return 0.0;\n"
    "    }\n"
    "    float currentDepth = projCoords.z;\n"
    "    float s = 0.0;\n"
    "    if (u_PCFQuality == 0) {\n"
    "        float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy, float(l))).r;\n"
    "        s = (currentDepth - b > pcfDepth) ? 1.0 : 0.0;\n"
    "    } else if (u_PCFQuality == 1) {\n"
    "        for (int x = -1; x <= 1; ++x) {\n"
    "            for (int y = -1; y <= 1; ++y) {\n"
    "                float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy + vec2(x, y) * tSize, float(l))).r;\n"
    "                s += (currentDepth - b > pcfDepth) ? 1.0 : 0.0;\n"
    "            }\n"
    "        }\n"
    "        s /= 9.0;\n"
    "    } else {\n"
    "        for (int x = -2; x <= 2; ++x) {\n"
    "            for (int y = -2; y <= 2; ++y) {\n"
    "                float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy + vec2(x, y) * tSize, float(l))).r;\n"
    "                s += (currentDepth - b > pcfDepth) ? 1.0 : 0.0;\n"
    "            }\n"
    "        }\n"
    "        s /= 25.0;\n"
    "    }\n"
    "    return s;\n"
    "}\n"
    "float CalculateShadowCSM(vec3 worldPos, vec3 normal, vec3 lightDir) {\n"
    "    if (!u_ShadowsEnabled || u_CascadeCount <= 0) return 0.0;\n"
    "    vec4 fragPosView = u_View * vec4(worldPos, 1.0);\n"
    "    float depthValue = abs(fragPosView.z);\n"
    "    int layer = -1;\n"
    "    for (int c = 0; c < u_CascadeCount && c < 4; ++c) {\n"
    "        if (depthValue < u_CascadeEndClipSpace[c]) {\n"
    "            layer = c;\n"
    "            break;\n"
    "        }\n"
    "    }\n"
    "    if (layer == -1) return 0.0;\n"
    "    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);\n"
    "    float bias = max(u_ShadowBias * 3.0 * (1.0 - cosTheta), u_ShadowBias * 0.4);\n"
    "    vec3 biasedPos = worldPos + normal * (u_ShadowBias * 4.0 * (1.0 - cosTheta) + 0.003);\n"
    "    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMapArray, 0).xy);\n"
    "    float shadow = SampleCascadeLayer(layer, biasedPos, bias, texelSize);\n"
    "    if (layer < u_CascadeCount - 1 && layer < 3) {\n"
    "        float splitEnd = u_CascadeEndClipSpace[layer];\n"
    "        float splitStart = (layer > 0) ? u_CascadeEndClipSpace[layer - 1] : 0.0;\n"
    "        float blendRegion = max((splitEnd - splitStart) * 0.20, 1.5);\n"
    "        if (depthValue > splitEnd - blendRegion) {\n"
    "            float blendWeight = smoothstep(splitEnd - blendRegion, splitEnd, depthValue);\n"
    "            float nextBias = max(u_ShadowBias * 3.5 * (1.0 - cosTheta), u_ShadowBias * 0.5);\n"
    "            vec3 nextBiasedPos = worldPos + normal * (u_ShadowBias * 5.0 * (1.0 - cosTheta) + 0.005);\n"
    "            float nextShadow = SampleCascadeLayer(layer + 1, nextBiasedPos, nextBias, texelSize);\n"
    "            shadow = mix(shadow, nextShadow, blendWeight);\n"
    "        }\n"
    "    }\n"
    "    return shadow;\n"
    "}\n"
    "float RemapCloud(float val, float inMin, float inMax, float outMin, float outMax) {\n"
    "    return clamp(outMin + ((val - inMin) / max(inMax - inMin, 0.0001)) * (outMax - outMin), outMin, outMax);\n"
    "}\n"
    "float SampleCloudDensityForShadow(vec2 xzPos, float heightFrac) {\n"
    "    float baseShape = smoothstep(0.0, 0.08, heightFrac);\n"
    "    float topShape  = smoothstep(1.0, 0.28, heightFrac);\n"
    "    float verticalShape = baseShape * topShape;\n"
    "    vec2 baseUV = xzPos * u_CloudScale + u_CloudWindOffset;\n"
    "    vec2 heightTurbulence = vec2(sin(heightFrac * 3.14159), cos(heightFrac * 3.14159)) * 0.012;\n"
    "    vec2 uv = baseUV + heightTurbulence;\n"
    "    vec4 nMacro = texture(u_CloudShadowNoise, uv);\n"
    "    float erosionFactor = clamp(u_CloudEdgeErosion > 0.0 ? u_CloudEdgeErosion : 0.60, 0.0, 1.0);\n"
    "    vec2 warp = (nMacro.gb - vec2(0.5)) * (0.05 * erosionFactor);\n"
    "    vec2 uvTurb = uv + warp;\n"
    "    float detailFreq = max(u_CloudDetailScale > 0.0 ? u_CloudDetailScale : 4.5, 1.0);\n"
    "    vec4 nMid = texture(u_CloudShadowNoise, uvTurb * detailFreq + vec2(0.23, 0.41));\n"
    "    vec4 nHigh = texture(u_CloudShadowNoise, uvTurb * (detailFreq * 2.8) + vec2(0.67, 0.19));\n"
    "    float highFreqErosion = nMid.r * 0.48 + nMid.g * 0.34 + nHigh.b * 0.18;\n"
    "    float coverage = clamp(u_CloudCoverage, 0.01, 0.99);\n"
    "    float baseCloud = RemapCloud(nMacro.r, 1.0 - coverage, 1.0, 0.0, 1.0);\n"
    "    if (baseCloud <= 0.0001) return 0.0;\n"
    "    float heightErosionMod = mix(0.40, 0.90, heightFrac);\n"
    "    float erosionAmount = highFreqErosion * clamp(erosionFactor * heightErosionMod, 0.0, 0.92);\n"
    "    float erodedCloud = RemapCloud(baseCloud, erosionAmount, 1.0, 0.0, 1.0);\n"
    "    float crispDensity = pow(erodedCloud, mix(1.0, 1.45, erosionFactor));\n"
    "    return crispDensity * verticalShape * (u_CloudDensity * 8.5);\n"
    "}\n"
    "float CalculateCloudShadow(vec3 worldPos, vec3 lightDir) {\n"
    "    if (!u_CloudShadowsEnabled || lightDir.y <= 0.02) return 0.0;\n"
    "    float safeLy = max(lightDir.y, 0.12);\n"
    "    float cloudMidY = u_CloudBaseHeight + u_CloudLayerThickness * 0.45;\n"
    "    float distToCloud = max((cloudMidY - worldPos.y) / safeLy, 0.0);\n"
    "    vec3 cloudPos = worldPos + lightDir * distToCloud;\n"
    "    float penumbraUV = clamp(distToCloud * 0.0016 * u_CloudScale, 0.0006, 0.004);\n"
    "    float dCenter = SampleCloudDensityForShadow(cloudPos.xz, 0.45);\n"
    "    float d0 = SampleCloudDensityForShadow(cloudPos.xz + vec2( penumbraUV,  0.0), 0.45);\n"
    "    float d1 = SampleCloudDensityForShadow(cloudPos.xz - vec2( penumbraUV,  0.0), 0.45);\n"
    "    float d2 = SampleCloudDensityForShadow(cloudPos.xz + vec2( 0.0,  penumbraUV), 0.45);\n"
    "    float d3 = SampleCloudDensityForShadow(cloudPos.xz - vec2( 0.0, -penumbraUV), 0.45);\n"
    "    float densityAvg = dCenter * 0.40 + (d0 + d1 + d2 + d3) * 0.15;\n"
    "    if (densityAvg <= 0.0001) return 0.0;\n"
    "    float absorption = (u_Absorption > 0.0) ? u_Absorption : 1.0;\n"
    "    float thicknessFactor = clamp(u_CloudLayerThickness / 100.0, 0.5, 3.0);\n"
    "    float opticalDepth = densityAvg * (absorption * 4.2) * thicknessFactor;\n"
    "    float shadowFactor = 1.0 - exp(-opticalDepth);\n"
    "    float sunHorizonFade = smoothstep(0.02, 0.10, lightDir.y);\n"
    "    return clamp(shadowFactor * u_CloudShadowIntensity * sunHorizonFade, 0.0, 0.98);\n"
    "}\n"
    "const float PI = 3.14159265359;\n"
    "float DistributionGGX(vec3 N, vec3 H, float roughness) {\n"
    "    float a = roughness * roughness;\n"
    "    float a2 = a * a;\n"
    "    float NdotH = max(dot(N, H), 0.0);\n"
    "    float NdotH2 = NdotH * NdotH;\n"
    "    float nom = a2;\n"
    "    float denom = (NdotH2 * (a2 - 1.0) + 1.0);\n"
    "    denom = PI * denom * denom;\n"
    "    return nom / max(denom, 0.0000001);\n"
    "}\n"
    "float GeometrySchlickGGX(float NdotV, float roughness) {\n"
    "    float r = (roughness + 1.0);\n"
    "    float k = (r * r) / 8.0;\n"
    "    float nom = NdotV;\n"
    "    float denom = NdotV * (1.0 - k) + k;\n"
    "    return nom / max(denom, 0.0000001);\n"
    "}\n"
    "float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {\n"
    "    float NdotV = max(dot(N, V), 0.0);\n"
    "    float NdotL = max(dot(N, L), 0.0);\n"
    "    float ggx2 = GeometrySchlickGGX(NdotV, roughness);\n"
    "    float ggx1 = GeometrySchlickGGX(NdotL, roughness);\n"
    "    return ggx1 * ggx2;\n"
    "}\n"
    "vec3 FresnelSchlick(float cosTheta, vec3 F0) {\n"
    "    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
    "}\n"
    "vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {\n"
    "    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
    "}\n"
    "vec4 SampleTextureSeamless(sampler2D tex, vec2 uv) {\n"
    "    vec2 dx = dFdx(uv);\n"
    "    vec2 dy = dFdy(uv);\n"
    "    float maxGradSq = max(dot(dx, dx), dot(dy, dy));\n"
    "    if (maxGradSq > 0.0625) {\n"
    "        float scale = 0.05 * inversesqrt(maxGradSq);\n"
    "        return textureGrad(tex, uv, dx * scale, dy * scale);\n"
    "    }\n"
    "    return texture(tex, uv);\n"
    "}\n"
    "vec3 GetSurfaceNormal(vec3 geomNormal) {\n"
    "    if (!u_HasNormalMap) return geomNormal;\n"
    "    vec3 mapN = SampleTextureSeamless(u_NormalMap, vTexCoords).xyz * 2.0 - 1.0;\n"
    "    float nScale = (u_NormalScale > 0.0001) ? u_NormalScale : 1.0;\n"
    "    mapN.xy *= nScale;\n"
    "    mapN = normalize(mapN);\n"
    "    vec3 N = normalize(geomNormal);\n"
    "    vec3 T = vTangent;\n"
    "    vec3 B = vBitangent;\n"
    "    if (dot(T, T) > 0.0001 && dot(B, B) > 0.0001) {\n"
    "        T = normalize(T - dot(T, N) * N);\n"
    "        B = normalize(B - dot(B, N) * N - dot(B, T) * T);\n"
    "        if (dot(cross(N, T), B) < 0.0) B = -B;\n"
    "    } else {\n"
    "        vec3 dp1 = dFdx(vWorldPos);\n"
    "        vec3 dp2 = dFdy(vWorldPos);\n"
    "        vec2 duv1 = dFdx(vTexCoords);\n"
    "        vec2 duv2 = dFdy(vTexCoords);\n"
    "        float uvGradSq = max(dot(duv1, duv1), dot(duv2, duv2));\n"
    "        if (uvGradSq > 0.0025) return N;\n"
    "        vec3 dp2perp = cross(dp2, N);\n"
    "        vec3 dp1perp = cross(N, dp1);\n"
    "        T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
    "        B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
    "        float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 0.00001);\n"
    "        T *= invmax;\n"
    "        B *= invmax;\n"
    "    }\n"
    "    mat3 TBN = mat3(T, B, N);\n"
    "    return normalize(TBN * mapN);\n"
    "}\n"
    "void main() {\n"
    "    vec3 albedo = u_Albedo;\n"
    "    float alpha = 1.0;\n"
    "    if (u_HasAlbedoMap) {\n"
    "        vec4 albSample = SampleTextureSeamless(u_AlbedoMap, vTexCoords);\n"
    "        albedo *= albSample.rgb;\n"
    "        alpha = albSample.a;\n"
    "    }\n"
    "    if (u_HasOpacityMap) {\n"
    "        vec4 opSample = SampleTextureSeamless(u_OpacityMap, vTexCoords);\n"
    "        float opVal = (opSample.a < 0.98) ? opSample.a : opSample.r;\n"
    "        alpha = min(alpha, opVal);\n"
    "    }\n"
    "    if ((u_AlphaClip || u_HasOpacityMap) && alpha < max(u_AlphaCutoff, 0.05)) discard;\n"
    "    vec3 geomN = length(vNormal) > 0.001 ? normalize(vNormal) : vec3(0.0, 1.0, 0.0);\n"
    "    vec3 faceN = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));\n"
    "    if (length(faceN) > 0.001 && dot(geomN, faceN) < 0.0) geomN = -geomN;\n"
    "    vec3 N = GetSurfaceNormal(geomN);\n"
    "    float roughness = u_Roughness;\n"
    "    if (u_HasRoughnessMap) {\n"
    "        vec4 rSample = SampleTextureSeamless(u_RoughnessMap, vTexCoords);\n"
    "        float rVal = u_PackedORM ? rSample.g : rSample.r;\n"
    "        if (u_InvertRoughness) rVal = 1.0 - rVal;\n"
    "        roughness = clamp(rVal * max(u_Roughness, 0.05), 0.04, 1.0);\n"
    "    }\n"
    "    float metallic = u_Metallic;\n"
    "    if (u_HasMetallicMap) {\n"
    "        vec4 mSample = SampleTextureSeamless(u_MetallicMap, vTexCoords);\n"
    "        float mVal = u_PackedORM ? mSample.b : mSample.r;\n"
    "        metallic = clamp(mVal * (u_Metallic > 0.001 ? u_Metallic : 1.0), 0.0, 1.0);\n"
    "    }\n"
    "    float ao = u_AO;\n"
    "    if (u_HasAOMap) {\n"
    "        ao *= SampleTextureSeamless(u_AOMap, vTexCoords).r;\n"
    "    }\n"
    "    vec3 emissive = u_EmissiveColor * u_EmissiveIntensity;\n"
    "    if (u_HasEmissiveMap) {\n"
    "        vec3 emTex = texture(u_EmissiveMap, vTexCoords).rgb;\n"
    "        vec3 emFactor = (length(u_EmissiveColor) > 0.001) ? u_EmissiveColor : vec3(1.0);\n"
    "        float emInt = (u_EmissiveIntensity > 0.001) ? u_EmissiveIntensity : 1.0;\n"
    "        emissive = emTex * emFactor * emInt;\n"
    "    }\n"
    "    vec3 V = normalize(u_CameraPos - vWorldPos);\n"
    "    vec3 F0 = mix(vec3(0.04), albedo, metallic);\n"
    "    vec3 Lo = vec3(0.0);\n"
    "    for (int i = 0; i < u_NumDirLights && i < MAX_DIR_LIGHTS; ++i) {\n"
    "        vec3 L = normalize(-u_DirLights[i].direction);\n"
    "        vec3 H = normalize(V + L);\n"
    "        vec3 radiance = u_DirLights[i].color * u_DirLights[i].intensity;\n"
    "        float NDF = DistributionGGX(N, H, roughness);\n"
    "        float G = GeometrySmith(N, V, L, roughness);\n"
    "        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
    "        vec3 numerator = NDF * G * F;\n"
    "        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
    "        vec3 specular = numerator / denominator;\n"
    "        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "        float NdotL = max(dot(N, L), 0.0);\n"
    "        float shadowFactor = (i == 0) ? CalculateShadowCSM(vWorldPos, N, L) : 0.0;\n"
    "        if (i == 0 && u_CloudShadowsEnabled) {\n"
    "            float cloudShadow = CalculateCloudShadow(vWorldPos, L);\n"
    "            shadowFactor = 1.0 - (1.0 - shadowFactor) * (1.0 - cloudShadow);\n"
    "        }\n"
    "        Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadowFactor);\n"
    "    }\n"
    "    for (int i = 0; i < u_NumPointLights && i < MAX_POINT_LIGHTS; ++i) {\n"
    "            vec3 lightDir = u_PointLights[i].position - vWorldPos;\n"
    "            float dist = length(lightDir);\n"
    "            if (dist > u_PointLights[i].range && u_PointLights[i].range > 0.0) continue;\n"
    "            vec3 L = normalize(lightDir);\n"
    "            vec3 H = normalize(V + L);\n"
    "            float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));\n"
    "            if (u_PointLights[i].range > 0.0) attenuation *= clamp(1.0 - (dist / u_PointLights[i].range), 0.0, 1.0);\n"
    "            vec3 radiance = u_PointLights[i].color * u_PointLights[i].intensity * attenuation;\n"
    "            float NDF = DistributionGGX(N, H, roughness);\n"
    "            float G = GeometrySmith(N, V, L, roughness);\n"
    "            vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
    "            vec3 numerator = NDF * G * F;\n"
    "            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
    "            vec3 specular = numerator / denominator;\n"
    "            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "            float NdotL = max(dot(N, L), 0.0);\n"
    "            float shadowFactor = 0.0;\n"
    "            Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadowFactor);\n"
    "        }\n"
    "        for (int i = 0; i < u_NumSpotLights && i < MAX_SPOT_LIGHTS; ++i) {\n"
    "            vec3 lightDir = u_SpotLights[i].position - vWorldPos;\n"
    "            float dist = length(lightDir);\n"
    "            if (dist > u_SpotLights[i].range && u_SpotLights[i].range > 0.0) continue;\n"
    "            vec3 L = normalize(lightDir);\n"
    "            float theta = dot(L, normalize(-u_SpotLights[i].direction));\n"
    "            float innerCut = cos(radians(u_SpotLights[i].spotAngle * 0.5));\n"
    "            float outerCut = cos(radians(u_SpotLights[i].spotAngle * 0.65));\n"
    "            float spotFactor = clamp((theta - outerCut) / max(innerCut - outerCut, 0.0001), 0.0, 1.0);\n"
    "            if (spotFactor <= 0.0) continue;\n"
    "            vec3 H = normalize(V + L);\n"
    "            float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));\n"
    "            vec3 radiance = u_SpotLights[i].color * u_SpotLights[i].intensity * attenuation * spotFactor;\n"
    "            float NDF = DistributionGGX(N, H, roughness);\n"
    "            float G = GeometrySmith(N, V, L, roughness);\n"
    "            vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
    "            vec3 numerator = NDF * G * F;\n"
    "            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
    "            vec3 specular = numerator / denominator;\n"
    "            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "            float NdotL = max(dot(N, L), 0.0);\n"
    "            float shadowFactor = 0.0;\n"
    "            Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadowFactor);\n"
    "        }\n"
    "    }\n"
    "    if (u_HasLightmap) {\n"
    "        vec2 lmUV = vTexCoords2 * u_LightmapScaleOffset.xy + u_LightmapScaleOffset.zw;\n"
    "        vec3 bakedIrr = texture(u_LightmapAtlas, lmUV).rgb;\n"
    "        Lo += bakedIrr * albedo * (1.0 - metallic);\n"
    "    }\n"
    "    vec3 ambient = vec3(0.04) * albedo * ao;\n"
    "    vec3 color = ambient + Lo + emissive;\n"
    "    color = color / (color + vec3(1.0));\n"
    "    color = pow(color, vec3(1.0 / 2.2));\n"
    "    FragColor = vec4(color, 1.0);\n"
    "}\n";

#else

const char* s_ShadowDepthVert =
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 2) in vec2 aTexCoords;\n"
    "layout (location = 5) in ivec4 aBoneIDs;\n"
    "layout (location = 6) in vec4 aWeights;\n"
    "out vec2 vTexCoords;\n"
    "uniform mat4 u_LightSpaceMatrix;\n"
    "uniform mat4 u_Model;\n"
    "#define MAX_BONES 100\n"
    "uniform mat4 u_BoneTransforms[MAX_BONES];\n"
    "uniform int u_SkinningQuality;\n"
    "uniform bool u_HasBones;\n"
    "void main() {\n"
    "    mat4 skinMatrix = mat4(1.0);\n"
    "    if (u_HasBones && u_SkinningQuality > 0) {\n"
    "        if (u_SkinningQuality >= 4) {\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * aWeights[0]\n"
    "                       + u_BoneTransforms[aBoneIDs[1]] * aWeights[1]\n"
    "                       + u_BoneTransforms[aBoneIDs[2]] * aWeights[2]\n"
    "                       + u_BoneTransforms[aBoneIDs[3]] * aWeights[3];\n"
    "        } else if (u_SkinningQuality == 2) {\n"
    "            float totalW = aWeights[0] + aWeights[1];\n"
    "            float w0 = (totalW > 0.0) ? (aWeights[0] / totalW) : 1.0;\n"
    "            float w1 = (totalW > 0.0) ? (aWeights[1] / totalW) : 0.0;\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * w0 + u_BoneTransforms[aBoneIDs[1]] * w1;\n"
    "        } else if (u_SkinningQuality == 1) {\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]];\n"
    "        }\n"
    "    }\n"
    "    vTexCoords = aTexCoords;\n"
    "    vec4 localPos = skinMatrix * vec4(aPos, 1.0);\n"
    "    gl_Position = u_LightSpaceMatrix * u_Model * localPos;\n"
    "}\n";

const char* s_ShadowDepthFrag =
    "#version 330 core\n"
    "in vec2 vTexCoords;\n"
    "uniform bool u_HasAlphaMap;\n"
    "uniform sampler2D u_AlphaMap;\n"
    "uniform bool u_AlphaFromRed;\n"
    "uniform float u_AlphaCutoff;\n"
    "void main() {\n"
    "    if (u_HasAlphaMap) {\n"
    "        vec4 samp = texture(u_AlphaMap, vTexCoords);\n"
    "        float a = u_AlphaFromRed ? ((samp.a < 0.98) ? samp.a : samp.r) : samp.a;\n"
    "        if (a < max(u_AlphaCutoff, 0.05)) discard;\n"
    "    }\n"
    "}\n";

const char* s_LineVert =
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "uniform mat4 u_Model;\n"
    "uniform mat4 u_View;\n"
    "uniform mat4 u_Projection;\n"
    "void main() {\n"
    "    gl_Position = u_Projection * u_View * u_Model * vec4(aPos, 1.0);\n"
    "}\n";

const char* s_LineFrag =
    "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec4 u_Color;\n"
    "void main() {\n"
    "    FragColor = u_Color;\n"
    "}\n";

const char* s_UnlitVert =
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aNormal;\n"
    "layout (location = 2) in vec2 aTexCoords;\n"
    "out vec2 vTexCoords;\n"
    "uniform mat4 u_Model;\n"
    "uniform mat4 u_View;\n"
    "uniform mat4 u_Projection;\n"
    "uniform float u_OutlineWidth;\n"
    "void main() {\n"
    "    vTexCoords = aTexCoords;\n"
    "    vec3 pos = aPos;\n"
    "    if (u_OutlineWidth > 0.0) {\n"
    "        vec3 n = length(aNormal) > 0.001 ? normalize(aNormal) : normalize(aPos);\n"
    "        pos += n * u_OutlineWidth;\n"
    "    }\n"
    "    gl_Position = u_Projection * u_View * u_Model * vec4(pos, 1.0);\n"
    "}\n";

const char* s_UnlitFrag =
    "#version 330 core\n"
    "in vec2 vTexCoords;\n"
    "out vec4 FragColor;\n"
    "uniform vec4 u_Color;\n"
    "uniform sampler2D u_MainTexture;\n"
    "uniform bool u_HasTexture;\n"
    "uniform bool u_ToLinear;\n"
    "void main() {\n"
    "    vec4 col = u_Color;\n"
    "    if (u_HasTexture) {\n"
    "        col *= texture(u_MainTexture, vTexCoords);\n"
    "    }\n"
    "    if (u_ToLinear) col.rgb = pow(max(col.rgb, vec3(0.0)), vec3(2.2));\n"
    "    FragColor = col;\n"
    "}\n";

const char* s_LitVert =
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aNormal;\n"
    "layout (location = 2) in vec2 aTexCoords;\n"
    "layout (location = 3) in vec3 aTangent;\n"
    "layout (location = 4) in vec3 aBitangent;\n"
    "layout (location = 5) in ivec4 aBoneIDs;\n"
    "layout (location = 6) in vec4 aWeights;\n"
    "layout (location = 7) in vec2 aTexCoords2;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec2 vTexCoords;\n"
    "out vec2 vTexCoords2;\n"
    "out vec3 vTangent;\n"
    "out vec3 vBitangent;\n"
    "out vec3 vViewPos;\n"
    "uniform mat4 u_Model;\n"
    "uniform mat4 u_View;\n"
    "uniform mat4 u_Projection;\n"
    "#define MAX_BONES 100\n"
    "uniform mat4 u_BoneTransforms[MAX_BONES];\n"
    "uniform int u_SkinningQuality;\n"
    "uniform bool u_HasBones;\n"
    "void main() {\n"
    "    mat4 skinMatrix = mat4(1.0);\n"
    "    if (u_HasBones && u_SkinningQuality > 0) {\n"
    "        if (u_SkinningQuality >= 4) {\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * aWeights[0]\n"
    "                       + u_BoneTransforms[aBoneIDs[1]] * aWeights[1]\n"
    "                       + u_BoneTransforms[aBoneIDs[2]] * aWeights[2]\n"
    "                       + u_BoneTransforms[aBoneIDs[3]] * aWeights[3];\n"
    "        } else if (u_SkinningQuality == 2) {\n"
    "            float totalW = aWeights[0] + aWeights[1];\n"
    "            float w0 = (totalW > 0.0) ? (aWeights[0] / totalW) : 1.0;\n"
    "            float w1 = (totalW > 0.0) ? (aWeights[1] / totalW) : 0.0;\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * w0 + u_BoneTransforms[aBoneIDs[1]] * w1;\n"
    "        } else if (u_SkinningQuality == 1) {\n"
    "            skinMatrix = u_BoneTransforms[aBoneIDs[0]];\n"
    "        }\n"
    "    }\n"
    "    vec4 localPos = skinMatrix * vec4(aPos, 1.0);\n"
    "    vec4 worldPos = u_Model * localPos;\n"
    "    vWorldPos = worldPos.xyz;\n"
    "    vViewPos = (u_View * worldPos).xyz;\n"
    "    mat3 skin3 = mat3(skinMatrix);\n"
    "    mat3 model3 = mat3(u_Model);\n"
    "    vNormal = normalize(model3 * (skin3 * aNormal));\n"
    "    vTangent = model3 * (skin3 * aTangent);\n"
    "    vBitangent = model3 * (skin3 * aBitangent);\n"
    "    vTexCoords = aTexCoords;\n"
    "    vTexCoords2 = aTexCoords2;\n"
    "    gl_Position = u_Projection * u_View * worldPos;\n"
    "}\n";

const char* s_LitFrag =
    "#version 430 core\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "in vec2 vTexCoords;\n"
    "in vec2 vTexCoords2;\n"
    "in vec3 vTangent;\n"
    "in vec3 vBitangent;\n"
    "in vec3 vViewPos;\n"
    "layout(location = 0) out vec4 FragColor;\n"
    "layout(location = 1) out vec4 FragGeometry;\n"
    "struct GpuClusteredLight {\n"
    "    vec4 position_range;\n"
    "    vec4 color_intensity;\n"
    "    vec4 direction_spotAngle;\n"
    "    uvec4 params;\n"
    "};\n"
    "layout(std430, binding = 2) readonly buffer LightsBuffer {\n"
    "    GpuClusteredLight u_Lights[];\n"
    "};\n"
    "layout(std430, binding = 4) readonly buffer ClusterGridBuffer {\n"
    "    uvec2 u_ClusterGrid[];\n"
    "};\n"
    "layout(std430, binding = 5) readonly buffer LightIndexBuffer {\n"
    "    uint u_GlobalLightIndexList[];\n"
    "};\n"
    "uniform bool u_ClusteredLightingEnabled;\n"
    "uniform uvec3 u_ClusterGridDim;\n"
    "uniform vec2 u_ClusterTileSize;\n"
    "uniform float u_ClusterZNear;\n"
    "uniform float u_ClusterZFar;\n"
    "uniform float u_ClusterLogZFactor;\n"
    "uniform bool u_DDGIEnabled;\n"
    "uniform float u_DDGIIntensity;\n"
    "uniform vec3 u_DDGISkyColor;\n"
    "uniform vec3 u_DDGIGroundColor;\n"
    "uniform int u_DDGIOctRes;\n"
    "uniform int u_DDGIProbeTexel;\n"
    "uniform sampler2D u_DDGIIrr[5];\n"
    "uniform sampler2D u_DDGIVis[5];\n"
    "uniform vec3 u_DDGIOrigin[5];\n"
    "uniform vec3 u_DDGISpacing[5];\n"
    "uniform ivec3 u_DDGICounts[5];\n"
    "uniform vec2 u_DDGIAtlas[5];\n"
    "uniform int u_DDGIFollow[5];\n"
    "uniform int u_DDGIOn[5];\n"
    "uniform int u_DDGIRow[5];\n"
    "uniform vec3 u_EmissiveColor;\n"
    "uniform float u_EmissiveIntensity;\n"
    "vec2 DDGIOctEncode(vec3 n) {\n"
    "    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 0.000001);\n"
    "    vec2 sgn = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);\n"
    "    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sgn;\n"
    "    return n.xy * 0.5 + 0.5;\n"
    "}\n"
    "vec3 DDGIOctDecode(vec2 e) {\n"
    "    vec2 f = e * 2.0 - 1.0;\n"
    "    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));\n"
    "    vec2 sgn = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);\n"
    "    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sgn;\n"
    "    return normalize(n);\n"
    "}\n"
    "vec3 SampleDDGIVolume(sampler2D irrTex, sampler2D visTex, vec3 worldPos, vec3 normal, vec3 origin, vec3 spacing, ivec3 counts, vec2 atlasSize, int row) {\n"
    "    if (counts.x <= 0 || counts.y <= 0 || counts.z <= 0) return vec3(-1.0);\n"
    "    vec3 spacingSafe = max(spacing, vec3(0.001));\n"
    "    float spacingMin = min(spacingSafe.x, min(spacingSafe.y, spacingSafe.z));\n"
    "    vec3 biased = worldPos + normal * (spacingMin * 0.45);\n"
    "    vec3 grid = (biased - origin) / spacingSafe - vec3(0.5);\n"
    "    ivec3 maxIdx = max(counts - ivec3(1), ivec3(0));\n"
    "    ivec3 nearest = clamp(ivec3(round(grid)), ivec3(0), maxIdx);\n"
    "    vec3 irr = vec3(0.0);\n"
    "    float wSum = 0.0;\n"
    "    for (int i = 0; i < 27; ++i) {\n"
    "        ivec3 offset = ivec3(i % 3, (i / 3) % 3, i / 9) - ivec3(1);\n"
    "        ivec3 idx = nearest + offset;\n"
    "        if (any(lessThan(idx, ivec3(0))) || any(greaterThan(idx, maxIdx))) continue;\n"
    "        vec3 delta = vec3(idx) - grid;\n"
    "        float w = exp(-dot(delta, delta) * 0.85);\n"
    "        vec3 probePos = origin + (vec3(idx) + vec3(0.5)) * spacingSafe;\n"
    "        vec3 toPoint = biased - probePos;\n"
    "        float dist = length(toPoint);\n"
    "        vec3 dir = toPoint / max(dist, 0.0001);\n"
    "        float wrap = clamp(dot(-dir, normal) * 0.5 + 0.5, 0.0, 1.0);\n"
    "        wrap = wrap * wrap;\n"
    "        int probeIndex = idx.x + idx.y * counts.x + idx.z * counts.x * counts.y;\n"
    "        ivec2 probeCoord = ivec2(probeIndex % max(row, 1), probeIndex / max(row, 1));\n"
    "        vec2 uvIrr = (vec2(probeCoord * u_DDGIProbeTexel) + vec2(1.0) + DDGIOctEncode(normal) * float(u_DDGIOctRes)) / max(atlasSize, vec2(1.0));\n"
    "        vec4 probeTex = texture(irrTex, uvIrr);\n"
    "        if (probeTex.a < 0.04) continue;\n"
    "        w *= probeTex.a;\n"
    "        vec2 uvVis = (vec2(probeCoord * u_DDGIProbeTexel) + vec2(1.0) + DDGIOctEncode(dir) * float(u_DDGIOctRes)) / max(atlasSize, vec2(1.0));\n"
    "        vec2 moments = texture(visTex, uvVis).rg;\n"
    "        float mean = moments.r + spacingMin * 0.08;\n"
    "        float variance = abs(moments.g - moments.r * moments.r);\n"
    "        float cheb = 1.0;\n"
    "        if (dist > mean) {\n"
    "            float d = dist - mean;\n"
    "            cheb = variance / (variance + d * d + 0.0001);\n"
    "            cheb = cheb * cheb * cheb;\n"
    "        }\n"
    "        w *= wrap * cheb;\n"
    "        irr += probeTex.rgb * w;\n"
    "        wSum += w;\n"
    "    }\n"
    "    if (wSum < 0.0001) return vec3(-1.0);\n"
    "    return (irr / wSum) * u_DDGIIntensity;\n"
    "}\n"
    "bool DDGIInside(int v, vec3 worldPos) {\n"
    "    if (u_DDGIOn[v] == 0) return false;\n"
    "    vec3 local = (worldPos - u_DDGIOrigin[v]) / max(u_DDGISpacing[v], vec3(0.001));\n"
    "    return all(greaterThanEqual(local, vec3(0.0))) && all(lessThan(local, vec3(u_DDGICounts[v])));\n"
    "}\n"
    "float DDGIVolumeSize(int v) {\n"
    "    return u_DDGISpacing[v].x * float(u_DDGICounts[v].x) * u_DDGISpacing[v].y * float(u_DDGICounts[v].y) * u_DDGISpacing[v].z * float(u_DDGICounts[v].z);\n"
    "}\n"
    "vec3 SampleDDGI(vec3 worldPos, vec3 normal) {\n"
    "    float hemiFactor = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);\n"
    "    vec3 skyCol = (length(u_DDGISkyColor) > 0.001) ? u_DDGISkyColor : vec3(0.24, 0.27, 0.32);\n"
    "    vec3 gndCol = (length(u_DDGIGroundColor) > 0.001) ? u_DDGIGroundColor : vec3(0.08, 0.085, 0.095);\n"
    "    vec3 skyFallback = mix(gndCol, skyCol, hemiFactor) * u_DDGIIntensity;\n"
    "    if (!u_DDGIEnabled) return skyFallback;\n"
    "    int pick = -1;\n"
    "    float best = 1e20;\n"
    "    if (DDGIInside(0, worldPos)) { float s = DDGIVolumeSize(0); if (s < best) { best = s; pick = 0; } }\n"
    "    if (DDGIInside(1, worldPos)) { float s = DDGIVolumeSize(1); if (s < best) { best = s; pick = 1; } }\n"
    "    if (DDGIInside(2, worldPos)) { float s = DDGIVolumeSize(2); if (s < best) { best = s; pick = 2; } }\n"
    "    if (DDGIInside(3, worldPos)) { float s = DDGIVolumeSize(3); if (s < best) { best = s; pick = 3; } }\n"
    "    if (DDGIInside(4, worldPos)) { float s = DDGIVolumeSize(4); if (s < best) { best = s; pick = 4; } }\n"
    "    vec3 irr = vec3(-1.0);\n"
    "    float volWeight = 1.0;\n"
    "    if (pick >= 0 && pick <= 4) {\n"
    "        if (pick == 0) irr = SampleDDGIVolume(u_DDGIIrr[0], u_DDGIVis[0], worldPos, normal, u_DDGIOrigin[0], u_DDGISpacing[0], u_DDGICounts[0], u_DDGIAtlas[0], u_DDGIRow[0]);\n"
    "        else if (pick == 1) irr = SampleDDGIVolume(u_DDGIIrr[1], u_DDGIVis[1], worldPos, normal, u_DDGIOrigin[1], u_DDGISpacing[1], u_DDGICounts[1], u_DDGIAtlas[1], u_DDGIRow[1]);\n"
    "        else if (pick == 2) irr = SampleDDGIVolume(u_DDGIIrr[2], u_DDGIVis[2], worldPos, normal, u_DDGIOrigin[2], u_DDGISpacing[2], u_DDGICounts[2], u_DDGIAtlas[2], u_DDGIRow[2]);\n"
    "        else if (pick == 3) irr = SampleDDGIVolume(u_DDGIIrr[3], u_DDGIVis[3], worldPos, normal, u_DDGIOrigin[3], u_DDGISpacing[3], u_DDGICounts[3], u_DDGIAtlas[3], u_DDGIRow[3]);\n"
    "        else if (pick == 4) irr = SampleDDGIVolume(u_DDGIIrr[4], u_DDGIVis[4], worldPos, normal, u_DDGIOrigin[4], u_DDGISpacing[4], u_DDGICounts[4], u_DDGIAtlas[4], u_DDGIRow[4]);\n"
    "        volWeight = 1.0;\n"
    "    }\n"
    "    else if (u_DDGIOn[0] != 0) {\n"
    "        vec3 volExtent = max(u_DDGISpacing[0] * vec3(u_DDGICounts[0]), vec3(0.01));\n"
    "        vec3 clampedPos = clamp(worldPos, u_DDGIOrigin[0], u_DDGIOrigin[0] + volExtent);\n"
    "        float distToVol = length(worldPos - clampedPos);\n"
    "        float maxDim = max(volExtent.x, max(volExtent.y, volExtent.z));\n"
    "        volWeight = clamp(1.0 - (distToVol / max(maxDim * 1.5, 45.0)), 0.0, 1.0);\n"
    "        volWeight = volWeight * volWeight * (3.0 - 2.0 * volWeight);\n"
    "        if (volWeight > 0.005) {\n"
    "            irr = SampleDDGIVolume(u_DDGIIrr[0], u_DDGIVis[0], clampedPos, normal, u_DDGIOrigin[0], u_DDGISpacing[0], u_DDGICounts[0], u_DDGIAtlas[0], u_DDGIRow[0]);\n"
    "        }\n"
    "    }\n"
    "    if (irr.x < 0.0) return skyFallback;\n"
    "    return mix(skyFallback, irr, clamp(volWeight, 0.0, 1.0));\n"
    "}\n"
    "uniform vec3 u_Albedo;\n"
    "uniform int u_VClustDebugMode;\n"
    "uniform int u_VClustPrimOffset;\n"
    "uniform float u_Roughness;\n"
    "uniform float u_Metallic;\n"
    "uniform float u_AO;\n"
    "uniform sampler2D u_AlbedoMap;\n"
    "uniform bool u_HasAlbedoMap;\n"
    "uniform sampler2D u_NormalMap;\n"
    "uniform bool u_HasNormalMap;\n"
    "uniform float u_NormalScale;\n"
    "uniform sampler2D u_RoughnessMap;\n"
    "uniform bool u_HasRoughnessMap;\n"
    "uniform sampler2D u_MetallicMap;\n"
    "uniform bool u_HasMetallicMap;\n"
    "uniform sampler2D u_AOMap;\n"
    "uniform bool u_HasAOMap;\n"
    "uniform sampler2D u_EmissiveMap;\n"
    "uniform bool u_HasEmissiveMap;\n"
    "uniform sampler2D u_OpacityMap;\n"
    "uniform bool u_HasOpacityMap;\n"
    "uniform bool u_PackedORM;\n"
    "uniform bool u_AlphaClip;\n"
    "uniform float u_AlphaCutoff;\n"
    "uniform bool u_InvertRoughness;\n"
    "uniform vec3 u_CameraPos;\n"
    "uniform samplerCube u_EnvCube;\n"
    "uniform bool u_HasEnvCube;\n"
    "uniform float u_EnvIntensity;\n"
    "uniform float u_ReflectionRoughnessCutoff;\n"
    "uniform bool u_ProbeBoxProjection;\n"
    "uniform vec3 u_ProbeOrigin;\n"
    "uniform vec3 u_ProbeBoxMin;\n"
    "uniform vec3 u_ProbeBoxMax;\n"
    "struct DirectionalLight {\n"
    "    vec3 direction;\n"
    "    vec3 color;\n"
    "    float intensity;\n"
    "};\n"
    "struct PointLight {\n"
    "    vec3 position;\n"
    "    vec3 color;\n"
    "    float intensity;\n"
    "    float range;\n"
    "};\n"
    "struct SpotLight {\n"
    "    vec3 position;\n"
    "    vec3 direction;\n"
    "    vec3 color;\n"
    "    float intensity;\n"
    "    float range;\n"
    "    float spotAngle;\n"
    "};\n"
    "#define MAX_DIR_LIGHTS 4\n"
    "#define MAX_POINT_LIGHTS 32\n"
    "#define MAX_SPOT_LIGHTS 4\n"
    "uniform int u_NumDirLights;\n"
    "uniform DirectionalLight u_DirLights[MAX_DIR_LIGHTS];\n"
    "uniform int u_NumPointLights;\n"
    "uniform PointLight u_PointLights[MAX_POINT_LIGHTS];\n"
    "uniform int u_NumSpotLights;\n"
    "uniform SpotLight u_SpotLights[MAX_SPOT_LIGHTS];\n"
    "uniform bool u_HasLightmap;\n"
    "uniform sampler2D u_LightmapAtlas;\n"
    "uniform vec4 u_LightmapScaleOffset;\n"
    "uniform highp sampler2DArray u_ShadowMapArray;\n"
    "uniform mat4 u_LightSpaceMatrices[4];\n"
    "uniform float u_CascadeEndClipSpace[4];\n"
    "uniform int u_CascadeCount;\n"
    "uniform float u_ShadowBias;\n"
    "uniform bool u_ShadowsEnabled;\n"
    "uniform int u_PCFQuality;\n"
    "uniform mat4 u_View;\n"
    "uniform bool u_CloudShadowsEnabled;\n"
    "uniform float u_CloudShadowIntensity;\n"
    "uniform sampler2D u_CloudShadowNoise;\n"
    "uniform float u_CloudBaseHeight;\n"
    "uniform float u_CloudScale;\n"
    "uniform vec2 u_CloudWindOffset;\n"
    "uniform float u_CloudCoverage;\n"
    "uniform float u_CloudLayerThickness;\n"
    "uniform float u_CloudDensity;\n"
    "uniform float u_CloudEdgeErosion;\n"
    "uniform float u_CloudDetailScale;\n"
    "uniform float u_Absorption;\n"
    "float SampleCascadeLayer(int l, vec3 bPos, float b, vec2 tSize) {\n"
    "    vec4 fragPosLightSpace = u_LightSpaceMatrices[l] * vec4(bPos, 1.0);\n"
    "    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;\n"
    "    projCoords = projCoords * 0.5 + 0.5;\n"
    "    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {\n"
    "        return 0.0;\n"
    "    }\n"
    "    float currentDepth = projCoords.z;\n"
    "    float s = 0.0;\n"
    "    if (u_PCFQuality == 0) {\n"
    "        float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy, float(l))).r;\n"
    "        s = (currentDepth - b > pcfDepth) ? 1.0 : 0.0;\n"
    "    } else if (u_PCFQuality == 1) {\n"
    "        for (int x = -1; x <= 1; ++x) {\n"
    "            for (int y = -1; y <= 1; ++y) {\n"
    "                float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy + vec2(x, y) * tSize, float(l))).r;\n"
    "                s += (currentDepth - b > pcfDepth) ? 1.0 : 0.0;\n"
    "            }\n"
    "        }\n"
    "        s /= 9.0;\n"
    "    } else {\n"
    "        for (int x = -2; x <= 2; ++x) {\n"
    "            for (int y = -2; y <= 2; ++y) {\n"
    "                float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy + vec2(x, y) * tSize, float(l))).r;\n"
    "                s += (currentDepth - b > pcfDepth) ? 1.0 : 0.0;\n"
    "            }\n"
    "        }\n"
    "        s /= 25.0;\n"
    "    }\n"
    "    return s;\n"
    "}\n"
    "float CalculateShadowCSM(vec3 worldPos, vec3 normal, vec3 lightDir) {\n"
    "    if (!u_ShadowsEnabled || u_CascadeCount <= 0) return 0.0;\n"
    "    vec4 fragPosView = u_View * vec4(worldPos, 1.0);\n"
    "    float depthValue = abs(fragPosView.z);\n"
    "    int layer = -1;\n"
    "    for (int c = 0; c < u_CascadeCount && c < 4; ++c) {\n"
    "        if (depthValue < u_CascadeEndClipSpace[c]) {\n"
    "            layer = c;\n"
    "            break;\n"
    "        }\n"
    "    }\n"
    "    if (layer == -1) return 0.0;\n"
    "    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);\n"
    "    float bias = max(u_ShadowBias * 3.0 * (1.0 - cosTheta), u_ShadowBias * 0.4);\n"
    "    vec3 biasedPos = worldPos + normal * (u_ShadowBias * 4.0 * (1.0 - cosTheta) + 0.003);\n"
    "    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMapArray, 0).xy);\n"
    "    float shadow = SampleCascadeLayer(layer, biasedPos, bias, texelSize);\n"
    "    if (layer < u_CascadeCount - 1 && layer < 3) {\n"
    "        float splitEnd = u_CascadeEndClipSpace[layer];\n"
    "        float splitStart = (layer > 0) ? u_CascadeEndClipSpace[layer - 1] : 0.0;\n"
    "        float blendRegion = max((splitEnd - splitStart) * 0.20, 1.5);\n"
    "        if (depthValue > splitEnd - blendRegion) {\n"
    "            float blendWeight = smoothstep(splitEnd - blendRegion, splitEnd, depthValue);\n"
    "            float nextBias = max(u_ShadowBias * 3.5 * (1.0 - cosTheta), u_ShadowBias * 0.5);\n"
    "            vec3 nextBiasedPos = worldPos + normal * (u_ShadowBias * 5.0 * (1.0 - cosTheta) + 0.005);\n"
    "            float nextShadow = SampleCascadeLayer(layer + 1, nextBiasedPos, nextBias, texelSize);\n"
    "            shadow = mix(shadow, nextShadow, blendWeight);\n"
    "        }\n"
    "    }\n"
    "    return shadow;\n"
    "}\n"
    "float RemapCloud(float val, float inMin, float inMax, float outMin, float outMax) {\n"
    "    return clamp(outMin + ((val - inMin) / max(inMax - inMin, 0.0001)) * (outMax - outMin), outMin, outMax);\n"
    "}\n"
    "float SampleCloudDensityForShadow(vec2 xzPos, float heightFrac) {\n"
    "    float baseShape = smoothstep(0.0, 0.08, heightFrac);\n"
    "    float topShape  = smoothstep(1.0, 0.28, heightFrac);\n"
    "    float verticalShape = baseShape * topShape;\n"
    "    vec2 baseUV = xzPos * u_CloudScale + u_CloudWindOffset;\n"
    "    vec2 heightTurbulence = vec2(sin(heightFrac * 3.14159), cos(heightFrac * 3.14159)) * 0.012;\n"
    "    vec2 uv = baseUV + heightTurbulence;\n"
    "    vec4 nMacro = texture(u_CloudShadowNoise, uv);\n"
    "    float erosionFactor = clamp(u_CloudEdgeErosion > 0.0 ? u_CloudEdgeErosion : 0.60, 0.0, 1.0);\n"
    "    vec2 warp = (nMacro.gb - vec2(0.5)) * (0.05 * erosionFactor);\n"
    "    vec2 uvTurb = uv + warp;\n"
    "    float detailFreq = max(u_CloudDetailScale > 0.0 ? u_CloudDetailScale : 4.5, 1.0);\n"
    "    vec4 nMid = texture(u_CloudShadowNoise, uvTurb * detailFreq + vec2(0.23, 0.41));\n"
    "    vec4 nHigh = texture(u_CloudShadowNoise, uvTurb * (detailFreq * 2.8) + vec2(0.67, 0.19));\n"
    "    float highFreqErosion = nMid.r * 0.48 + nMid.g * 0.34 + nHigh.b * 0.18;\n"
    "    float coverage = clamp(u_CloudCoverage, 0.01, 0.99);\n"
    "    float baseCloud = RemapCloud(nMacro.r, 1.0 - coverage, 1.0, 0.0, 1.0);\n"
    "    if (baseCloud <= 0.0001) return 0.0;\n"
    "    float heightErosionMod = mix(0.40, 0.90, heightFrac);\n"
    "    float erosionAmount = highFreqErosion * clamp(erosionFactor * heightErosionMod, 0.0, 0.92);\n"
    "    float erodedCloud = RemapCloud(baseCloud, erosionAmount, 1.0, 0.0, 1.0);\n"
    "    float crispDensity = pow(erodedCloud, mix(1.0, 1.45, erosionFactor));\n"
    "    return crispDensity * verticalShape * (u_CloudDensity * 8.5);\n"
    "}\n"
    "float CalculateCloudShadow(vec3 worldPos, vec3 lightDir) {\n"
    "    if (!u_CloudShadowsEnabled || lightDir.y <= 0.02) return 0.0;\n"
    "    float safeLy = max(lightDir.y, 0.12);\n"
    "    float cloudMidY = u_CloudBaseHeight + u_CloudLayerThickness * 0.45;\n"
    "    float distToCloud = max((cloudMidY - worldPos.y) / safeLy, 0.0);\n"
    "    vec3 cloudPos = worldPos + lightDir * distToCloud;\n"
    "    float penumbraUV = clamp(distToCloud * 0.0016 * u_CloudScale, 0.0006, 0.004);\n"
    "    float dCenter = SampleCloudDensityForShadow(cloudPos.xz, 0.45);\n"
    "    float d0 = SampleCloudDensityForShadow(cloudPos.xz + vec2( penumbraUV,  0.0), 0.45);\n"
    "    float d1 = SampleCloudDensityForShadow(cloudPos.xz - vec2( penumbraUV,  0.0), 0.45);\n"
    "    float d2 = SampleCloudDensityForShadow(cloudPos.xz + vec2( 0.0,  penumbraUV), 0.45);\n"
    "    float d3 = SampleCloudDensityForShadow(cloudPos.xz - vec2( 0.0, -penumbraUV), 0.45);\n"
    "    float densityAvg = dCenter * 0.40 + (d0 + d1 + d2 + d3) * 0.15;\n"
    "    if (densityAvg <= 0.0001) return 0.0;\n"
    "    float absorption = (u_Absorption > 0.0) ? u_Absorption : 1.0;\n"
    "    float thicknessFactor = clamp(u_CloudLayerThickness / 100.0, 0.5, 3.0);\n"
    "    float opticalDepth = densityAvg * (absorption * 4.2) * thicknessFactor;\n"
    "    float shadowFactor = 1.0 - exp(-opticalDepth);\n"
    "    float sunHorizonFade = smoothstep(0.02, 0.10, lightDir.y);\n"
    "    return clamp(shadowFactor * u_CloudShadowIntensity * sunHorizonFade, 0.0, 0.98);\n"
    "}\n"
    "const float PI = 3.14159265359;\n"
    "float DistributionGGX(vec3 N, vec3 H, float roughness) {\n"
    "    float a = roughness * roughness;\n"
    "    float a2 = a * a;\n"
    "    float NdotH = max(dot(N, H), 0.0);\n"
    "    float NdotH2 = NdotH * NdotH;\n"
    "    float nom = a2;\n"
    "    float denom = (NdotH2 * (a2 - 1.0) + 1.0);\n"
    "    denom = PI * denom * denom;\n"
    "    return nom / max(denom, 0.0000001);\n"
    "}\n"
    "float GeometrySchlickGGX(float NdotV, float roughness) {\n"
    "    float r = (roughness + 1.0);\n"
    "    float k = (r * r) / 8.0;\n"
    "    float nom = NdotV;\n"
    "    float denom = NdotV * (1.0 - k) + k;\n"
    "    return nom / max(denom, 0.0000001);\n"
    "}\n"
    "float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {\n"
    "    float NdotV = max(dot(N, V), 0.0);\n"
    "    float NdotL = max(dot(N, L), 0.0);\n"
    "    float ggx2 = GeometrySchlickGGX(NdotV, roughness);\n"
    "    float ggx1 = GeometrySchlickGGX(NdotL, roughness);\n"
    "    return ggx1 * ggx2;\n"
    "}\n"
    "vec3 FresnelSchlick(float cosTheta, vec3 F0) {\n"
    "    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
    "}\n"
    "vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {\n"
    "    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
    "}\n"
    "vec4 SampleTextureSeamless(sampler2D tex, vec2 uv) {\n"
    "    vec2 dx = dFdx(uv);\n"
    "    vec2 dy = dFdy(uv);\n"
    "    float maxGradSq = max(dot(dx, dx), dot(dy, dy));\n"
    "    if (maxGradSq > 0.0625) {\n"
    "        float scale = 0.05 * inversesqrt(maxGradSq);\n"
    "        return textureGrad(tex, uv, dx * scale, dy * scale);\n"
    "    }\n"
    "    return texture(tex, uv);\n"
    "}\n"
    "vec3 GetSurfaceNormal(vec3 geomNormal) {\n"
    "    if (!u_HasNormalMap) return geomNormal;\n"
    "    vec3 mapN = SampleTextureSeamless(u_NormalMap, vTexCoords).xyz * 2.0 - 1.0;\n"
    "    float nScale = (u_NormalScale > 0.0001) ? u_NormalScale : 1.0;\n"
    "    mapN.xy *= nScale;\n"
    "    mapN = normalize(mapN);\n"
    "    vec3 N = normalize(geomNormal);\n"
    "    vec3 T = vTangent;\n"
    "    vec3 B = vBitangent;\n"
    "    if (dot(T, T) > 0.0001 && dot(B, B) > 0.0001) {\n"
    "        T = normalize(T - dot(T, N) * N);\n"
    "        B = normalize(B - dot(B, N) * N - dot(B, T) * T);\n"
    "        if (dot(cross(N, T), B) < 0.0) B = -B;\n"
    "    } else {\n"
    "        vec3 dp1 = dFdx(vWorldPos);\n"
    "        vec3 dp2 = dFdy(vWorldPos);\n"
    "        vec2 duv1 = dFdx(vTexCoords);\n"
    "        vec2 duv2 = dFdy(vTexCoords);\n"
    "        float uvGradSq = max(dot(duv1, duv1), dot(duv2, duv2));\n"
    "        if (uvGradSq > 0.0025) return N;\n"
    "        vec3 dp2perp = cross(dp2, N);\n"
    "        vec3 dp1perp = cross(N, dp1);\n"
    "        T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
    "        B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
    "        float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 0.00001);\n"
    "        T *= invmax;\n"
    "        B *= invmax;\n"
    "    }\n"
    "    mat3 TBN = mat3(T, B, N);\n"
    "    return normalize(TBN * mapN);\n"
    "}\n"
    "void main() {\n"
    "    vec3 albedo = u_Albedo;\n"
    "    if (u_VClustDebugMode == 1) {\n"
    "        uint cid = uint(max(u_VClustPrimOffset + gl_PrimitiveID, 0)) / 124u;\n"
    "        uint h = (cid + 1u) * 2654435761u;\n"
    "        albedo = vec3(\n"
    "            0.25 + 0.72 * (float(h & 255u) / 255.0),\n"
    "            0.25 + 0.72 * (float((h >> 8u) & 255u) / 255.0),\n"
    "            0.25 + 0.72 * (float((h >> 16u) & 255u) / 255.0)\n"
    "        );\n"
    "    }\n"
    "    float alpha = 1.0;\n"
    "    if (u_HasAlbedoMap) {\n"
    "        vec4 albSample = SampleTextureSeamless(u_AlbedoMap, vTexCoords);\n"
    "        albedo *= albSample.rgb;\n"
    "        alpha = albSample.a;\n"
    "    }\n"
    "    if (u_HasOpacityMap) {\n"
    "        vec4 opSample = SampleTextureSeamless(u_OpacityMap, vTexCoords);\n"
    "        float opVal = (opSample.a < 0.98) ? opSample.a : opSample.r;\n"
    "        alpha = min(alpha, opVal);\n"
    "    }\n"
    "    if ((u_AlphaClip || u_HasOpacityMap) && alpha < max(u_AlphaCutoff, 0.05)) discard;\n"
    "    vec3 geomN = length(vNormal) > 0.001 ? normalize(vNormal) : vec3(0.0, 1.0, 0.0);\n"
    "    vec3 faceN = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));\n"
    "    if (length(faceN) > 0.001 && dot(geomN, faceN) < 0.0) geomN = -geomN;\n"
    "    vec3 N = GetSurfaceNormal(geomN);\n"
    "    float roughness = u_Roughness;\n"
    "    if (u_HasRoughnessMap) {\n"
    "        vec4 rSample = SampleTextureSeamless(u_RoughnessMap, vTexCoords);\n"
    "        float rVal = u_PackedORM ? rSample.g : rSample.r;\n"
    "        if (u_InvertRoughness) rVal = 1.0 - rVal;\n"
    "        roughness = clamp(rVal * max(u_Roughness, 0.05), 0.04, 1.0);\n"
    "    }\n"
    "    float metallic = u_Metallic;\n"
    "    if (u_HasMetallicMap) {\n"
    "        vec4 mSample = SampleTextureSeamless(u_MetallicMap, vTexCoords);\n"
    "        float mVal = u_PackedORM ? mSample.b : mSample.r;\n"
    "        metallic = clamp(mVal * (u_Metallic > 0.001 ? u_Metallic : 1.0), 0.0, 1.0);\n"
    "    }\n"
    "    float ao = u_AO;\n"
    "    if (u_HasAOMap) {\n"
    "        ao *= SampleTextureSeamless(u_AOMap, vTexCoords).r;\n"
    "    }\n"
    "    vec3 emissive = u_EmissiveColor * u_EmissiveIntensity;\n"
    "    if (u_HasEmissiveMap) {\n"
    "        vec3 emTex = texture(u_EmissiveMap, vTexCoords).rgb;\n"
    "        vec3 emFactor = (length(u_EmissiveColor) > 0.001) ? u_EmissiveColor : vec3(1.0);\n"
    "        float emInt = (u_EmissiveIntensity > 0.001) ? u_EmissiveIntensity : 1.0;\n"
    "        emissive = emTex * emFactor * emInt;\n"
    "    }\n"
    "    vec3 V = normalize(u_CameraPos - vWorldPos);\n"
    "    vec3 F0 = mix(vec3(0.04), albedo, metallic);\n"
    "    vec3 Lo = vec3(0.0);\n"
    "    for (int i = 0; i < u_NumDirLights && i < MAX_DIR_LIGHTS; ++i) {\n"
    "        vec3 L = normalize(-u_DirLights[i].direction);\n"
    "        vec3 H = normalize(V + L);\n"
    "        vec3 radiance = u_DirLights[i].color * u_DirLights[i].intensity;\n"
    "        float NDF = DistributionGGX(N, H, roughness);\n"
    "        float G = GeometrySmith(N, V, L, roughness);\n"
    "        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
    "        vec3 numerator = NDF * G * F;\n"
    "        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
    "        vec3 specular = numerator / denominator;\n"
    "        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "        float NdotL = max(dot(N, L), 0.0);\n"
    "        float shadowFactor = (i == 0) ? CalculateShadowCSM(vWorldPos, N, L) : 0.0;\n"
    "        if (i == 0 && u_CloudShadowsEnabled) {\n"
    "            float cloudShadow = CalculateCloudShadow(vWorldPos, L);\n"
    "            shadowFactor = 1.0 - (1.0 - shadowFactor) * (1.0 - cloudShadow);\n"
    "        }\n"
    "        Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadowFactor);\n"
    "    }\n"
    "    if (u_ClusteredLightingEnabled) {\n"
    "        uint clusterX = uint(clamp(gl_FragCoord.x / max(u_ClusterTileSize.x, 1.0), 0.0, float(u_ClusterGridDim.x - 1u)));\n"
    "        uint clusterY = uint(clamp(gl_FragCoord.y / max(u_ClusterTileSize.y, 1.0), 0.0, float(u_ClusterGridDim.y - 1u)));\n"
    "        float viewZ = -vViewPos.z;\n"
    "        uint clusterZ = 0u;\n"
    "        if (viewZ > u_ClusterZNear && u_ClusterLogZFactor > 1e-5) {\n"
    "            clusterZ = uint(clamp(log(viewZ / u_ClusterZNear) / u_ClusterLogZFactor, 0.0, float(u_ClusterGridDim.z - 1u)));\n"
    "        }\n"
    "        uint clusterIdx = clusterX + clusterY * u_ClusterGridDim.x + clusterZ * u_ClusterGridDim.x * u_ClusterGridDim.y;\n"
    "        uvec2 clusterData = u_ClusterGrid[clusterIdx];\n"
    "        uint clusterOffset = clusterData.x;\n"
    "        uint clusterCount  = clusterData.y;\n"
    "        for (uint ci = 0u; ci < clusterCount; ++ci) {\n"
    "            uint lightIdx = u_GlobalLightIndexList[clusterOffset + ci];\n"
    "            GpuClusteredLight cLight = u_Lights[lightIdx];\n"
    "            uint lType = cLight.params.x;\n"
    "            if (lType >= 2u) continue;\n"
    "            vec3 lightPos = cLight.position_range.xyz;\n"
    "            float lightRange = cLight.position_range.w;\n"
    "            vec3 lightCol = cLight.color_intensity.xyz;\n"
    "            float lightInt = cLight.color_intensity.w;\n"
    "            vec3 lightDir = lightPos - vWorldPos;\n"
    "            float dist = length(lightDir);\n"
    "            if (dist > lightRange && lightRange > 0.0) continue;\n"
    "            vec3 L = normalize(lightDir);\n"
    "            vec3 H = normalize(V + L);\n"
    "            float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));\n"
    "            if (lightRange > 0.0) attenuation *= clamp(1.0 - (dist / lightRange), 0.0, 1.0);\n"
    "            float spotFactor = 1.0;\n"
    "            if (lType == 1u) {\n"
    "                float theta = dot(L, normalize(-cLight.direction_spotAngle.xyz));\n"
    "                float spotAngle = cLight.direction_spotAngle.w;\n"
    "                float innerCut = cos(radians(spotAngle * 0.5));\n"
    "                float outerCut = cos(radians(spotAngle * 0.65));\n"
    "                spotFactor = clamp((theta - outerCut) / max(innerCut - outerCut, 0.0001), 0.0, 1.0);\n"
    "                if (spotFactor <= 0.0) continue;\n"
    "            }\n"
    "            vec3 radiance = lightCol * lightInt * attenuation * spotFactor;\n"
    "            float NDF = DistributionGGX(N, H, roughness);\n"
    "            float G = GeometrySmith(N, V, L, roughness);\n"
    "            vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
    "            vec3 numerator = NDF * G * F;\n"
    "            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
    "            vec3 specular = numerator / denominator;\n"
    "            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "            float NdotL = max(dot(N, L), 0.0);\n"
    "            Lo += (kD * albedo / PI + specular) * radiance * NdotL;\n"
    "        }\n"
    "    } else {\n"
    "        for (int i = 0; i < u_NumPointLights && i < MAX_POINT_LIGHTS; ++i) {\n"
    "            vec3 lightDir = u_PointLights[i].position - vWorldPos;\n"
    "            float dist = length(lightDir);\n"
    "            if (dist > u_PointLights[i].range && u_PointLights[i].range > 0.0) continue;\n"
    "            vec3 L = normalize(lightDir);\n"
    "            vec3 H = normalize(V + L);\n"
    "            float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));\n"
    "            if (u_PointLights[i].range > 0.0) attenuation *= clamp(1.0 - (dist / u_PointLights[i].range), 0.0, 1.0);\n"
    "            vec3 radiance = u_PointLights[i].color * u_PointLights[i].intensity * attenuation;\n"
    "            float NDF = DistributionGGX(N, H, roughness);\n"
    "            float G = GeometrySmith(N, V, L, roughness);\n"
    "            vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
    "            vec3 numerator = NDF * G * F;\n"
    "            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
    "            vec3 specular = numerator / denominator;\n"
    "            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "            float NdotL = max(dot(N, L), 0.0);\n"
    "            float shadowFactor = 0.0;\n"
    "            Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadowFactor);\n"
    "        }\n"
    "        for (int i = 0; i < u_NumSpotLights && i < MAX_SPOT_LIGHTS; ++i) {\n"
    "            vec3 lightDir = u_SpotLights[i].position - vWorldPos;\n"
    "            float dist = length(lightDir);\n"
    "            if (dist > u_SpotLights[i].range && u_SpotLights[i].range > 0.0) continue;\n"
    "            vec3 L = normalize(lightDir);\n"
    "            float theta = dot(L, normalize(-u_SpotLights[i].direction));\n"
    "            float innerCut = cos(radians(u_SpotLights[i].spotAngle * 0.5));\n"
    "            float outerCut = cos(radians(u_SpotLights[i].spotAngle * 0.65));\n"
    "            float spotFactor = clamp((theta - outerCut) / max(innerCut - outerCut, 0.0001), 0.0, 1.0);\n"
    "            if (spotFactor <= 0.0) continue;\n"
    "            vec3 H = normalize(V + L);\n"
    "            float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));\n"
    "            vec3 radiance = u_SpotLights[i].color * u_SpotLights[i].intensity * attenuation * spotFactor;\n"
    "            float NDF = DistributionGGX(N, H, roughness);\n"
    "            float G = GeometrySmith(N, V, L, roughness);\n"
    "            vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
    "            vec3 numerator = NDF * G * F;\n"
    "            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
    "            vec3 specular = numerator / denominator;\n"
    "            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
    "            float NdotL = max(dot(N, L), 0.0);\n"
    "            float shadowFactor = 0.0;\n"
    "            Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadowFactor);\n"
    "        }\n"
    "    }\n"
    "    if (u_HasLightmap) {\n"
    "        vec2 lmUV = vTexCoords2 * u_LightmapScaleOffset.xy + u_LightmapScaleOffset.zw;\n"
    "        vec3 bakedIrr = texture(u_LightmapAtlas, lmUV).rgb;\n"
    "        Lo += bakedIrr * albedo * (1.0 - metallic);\n"
    "    }\n"
    "    float hemiAmbient = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);\n"
    "    vec3 ambient = mix(vec3(0.08, 0.085, 0.095), vec3(0.24, 0.27, 0.32), hemiAmbient) * albedo * ao;\n"
    "    if (u_DDGIEnabled) {\n"
    "        vec3 irr = SampleDDGI(vWorldPos, N);\n"
    "        ambient = vec3(1.0 - metallic) * irr * albedo * ao;\n"
    "    }\n"
    "    vec3 color = ambient + Lo + emissive;\n"
    "    if (u_HasEnvCube) {\n"
    "        vec3 R = reflect(-V, N);\n"
    "        if (u_ProbeBoxProjection) {\n"
    "            vec3 nrdir = normalize(R);\n"
    "            vec3 rbmax = (u_ProbeBoxMax - vWorldPos) / max(nrdir, vec3(0.00001));\n"
    "            vec3 rbmin = (u_ProbeBoxMin - vWorldPos) / min(nrdir, vec3(-0.00001));\n"
    "            vec3 rbminmax = vec3(\n"
    "                nrdir.x > 0.0 ? rbmax.x : rbmin.x,\n"
    "                nrdir.y > 0.0 ? rbmax.y : rbmin.y,\n"
    "                nrdir.z > 0.0 ? rbmax.z : rbmin.z\n"
    "            );\n"
    "            float fa = min(min(rbminmax.x, rbminmax.y), rbminmax.z);\n"
    "            vec3 posOnBox = vWorldPos + nrdir * fa;\n"
    "            R = posOnBox - u_ProbeOrigin;\n"
    "        }\n"
    "        vec3 envColor = textureLod(u_EnvCube, R, roughness * 6.0).rgb;\n"
    "        vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);\n"
    "        float roughLimit = max(u_ReflectionRoughnessCutoff, 0.05);\n"
    "        float roughFade = smoothstep(roughLimit, roughLimit * 0.15, roughness);\n"
    "        vec3 envSpecular = envColor * F * roughFade;\n"
    "        color += envSpecular * max(u_EnvIntensity, 0.0);\n"
    "    }\n"
    "    FragColor = vec4(max(color, vec3(0.0)), 1.0);\n"
    "    FragGeometry = vec4(DDGIOctEncode(N), roughness, metallic);\n"
    "}\n";

#endif

} // namespace

void SlipShaderLibrary::Init() {
    if (!SDL_GL_GetCurrentContext()) return;
    if (s_Initialized && !s_Shaders.empty() && s_Shaders.find("Lit") != s_Shaders.end()) return;
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glCreateProgram) return;
#endif

    Clean();
    LoadBuiltinShaders();

    if (s_Shaders.find("Lit") != s_Shaders.end()) {
        s_Initialized = true;
    }
}

void SlipShaderLibrary::Clean() {
    s_Shaders.clear();
    s_Initialized = false;
}

void SlipShaderLibrary::LoadBuiltinShaders() {
    // 1. Unlit
    std::string unlitVertCode = s_UnlitVert;
    std::string unlitFragCode = s_UnlitFrag;
    if (SlipFileSystem::Exists("assets/shaders/Unlit.vert")) {
        std::string fileContent;
        if (SlipFileSystem::ReadAllText("assets/shaders/Unlit.vert", fileContent)) unlitVertCode = fileContent;
    }
    if (SlipFileSystem::Exists("assets/shaders/Unlit.frag")) {
        std::string fileContent;
        if (SlipFileSystem::ReadAllText("assets/shaders/Unlit.frag", fileContent)) unlitFragCode = fileContent;
    }

    SlipShader* unlitShader = SlipShader::CreateFromSource(unlitVertCode.c_str(), unlitFragCode.c_str());
    if (unlitShader) {
        s_Shaders["Unlit"] = std::unique_ptr<SlipShader>(unlitShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar Unlit shader!" << std::endl;
    }

    // 2. Lit (PBR)
    std::string litVertCode = s_LitVert;
    std::string litFragCode = s_LitFrag;
    if (SlipFileSystem::Exists("assets/shaders/Lit.vert")) {
        std::string fileContent;
        if (SlipFileSystem::ReadAllText("assets/shaders/Lit.vert", fileContent)) litVertCode = fileContent;
    }
    if (SlipFileSystem::Exists("assets/shaders/Lit.frag")) {
        std::string fileContent;
        if (SlipFileSystem::ReadAllText("assets/shaders/Lit.frag", fileContent)) litFragCode = fileContent;
    }

    SlipShader* litShader = SlipShader::CreateFromSource(litVertCode.c_str(), litFragCode.c_str());
    if (litShader) {
        s_Shaders["Lit"] = std::unique_ptr<SlipShader>(litShader);
        s_Shaders["default"] = std::unique_ptr<SlipShader>(SlipShader::CreateFromSource(litVertCode.c_str(), litFragCode.c_str()));
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar Lit shader!" << std::endl;
    }

    // 3. Line (para wireframes, frustums y líneas simples)
    std::string lineVertCode = s_LineVert;
    std::string lineFragCode = s_LineFrag;
    SlipShader* lineShader = SlipShader::CreateFromSource(lineVertCode.c_str(), lineFragCode.c_str());
    if (lineShader) {
        s_Shaders["Line"] = std::unique_ptr<SlipShader>(lineShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar Line shader!" << std::endl;
    }

    // 4. ShadowDepth (para pases de profundidad CSM)
    std::string shadowVertCode = s_ShadowDepthVert;
    std::string shadowFragCode = s_ShadowDepthFrag;
    SlipShader* shadowShader = SlipShader::CreateFromSource(shadowVertCode.c_str(), shadowFragCode.c_str());
    if (shadowShader) {
        s_Shaders["ShadowDepth"] = std::unique_ptr<SlipShader>(shadowShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar ShadowDepth shader!" << std::endl;
    }

    // 5. Sprite2D (para SpriteRendererComponent e imágenes UI con recorte UV y Flip X/Y)
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    const char* spriteVert =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout (location = 0) in vec3 aPos;\n"
        "layout (location = 1) in vec2 aTexCoords;\n"
        "out vec2 vTexCoords;\n"
        "uniform mat4 u_Model;\n"
        "uniform mat4 u_View;\n"
        "uniform mat4 u_Projection;\n"
        "uniform vec4 u_UVRect;\n"
        "uniform vec2 u_Flip;\n"
        "void main() {\n"
        "    vec2 uv = aTexCoords;\n"
        "    if (u_Flip.x > 0.5) uv.x = 1.0 - uv.x;\n"
        "    if (u_Flip.y > 0.5) uv.y = 1.0 - uv.y;\n"
        "    vTexCoords = u_UVRect.xy + uv * u_UVRect.zw;\n"
        "    gl_Position = u_Projection * u_View * u_Model * vec4(aPos, 1.0);\n"
        "}\n";
    const char* spriteFrag =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 vTexCoords;\n"
        "out vec4 FragColor;\n"
        "uniform vec4 u_Color;\n"
        "uniform sampler2D u_MainTexture;\n"
        "uniform bool u_HasTexture;\n"
        "void main() {\n"
        "    vec4 col = u_Color;\n"
        "    if (u_HasTexture) {\n"
        "        col *= texture(u_MainTexture, vTexCoords);\n"
        "    }\n"
        "    if (col.a < 0.005) discard;\n"
        "    FragColor = col;\n"
        "}\n";
    const char* fontVert =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout (location = 0) in vec4 aVertex;\n"
        "out vec2 vTexCoords;\n"
        "uniform mat4 u_Projection;\n"
        "uniform mat4 u_Model;\n"
        "void main() {\n"
        "    gl_Position = u_Projection * u_Model * vec4(aVertex.xy, 0.0, 1.0);\n"
        "    vTexCoords = aVertex.zw;\n"
        "}\n";
    const char* fontFrag =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 vTexCoords;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_FontAtlas;\n"
        "uniform vec4 u_Color;\n"
        "void main() {\n"
        "    float alpha = texture(u_FontAtlas, vTexCoords).r;\n"
        "    if (alpha < 0.01) discard;\n"
        "    FragColor = vec4(u_Color.rgb, u_Color.a * alpha);\n"
        "}\n";
#else
    const char* spriteVert =
        "#version 330 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "layout (location = 1) in vec2 aTexCoords;\n"
        "out vec2 vTexCoords;\n"
        "uniform mat4 u_Model;\n"
        "uniform mat4 u_View;\n"
        "uniform mat4 u_Projection;\n"
        "uniform vec4 u_UVRect;\n"
        "uniform vec2 u_Flip;\n"
        "void main() {\n"
        "    vec2 uv = aTexCoords;\n"
        "    if (u_Flip.x > 0.5) uv.x = 1.0 - uv.x;\n"
        "    if (u_Flip.y > 0.5) uv.y = 1.0 - uv.y;\n"
        "    vTexCoords = u_UVRect.xy + uv * u_UVRect.zw;\n"
        "    gl_Position = u_Projection * u_View * u_Model * vec4(aPos, 1.0);\n"
        "}\n";
    const char* spriteFrag =
        "#version 330 core\n"
        "in vec2 vTexCoords;\n"
        "out vec4 FragColor;\n"
        "uniform vec4 u_Color;\n"
        "uniform sampler2D u_MainTexture;\n"
        "uniform bool u_HasTexture;\n"
        "void main() {\n"
        "    vec4 col = u_Color;\n"
        "    if (u_HasTexture) {\n"
        "        col *= texture(u_MainTexture, vTexCoords);\n"
        "    }\n"
        "    if (col.a < 0.005) discard;\n"
        "    FragColor = col;\n"
        "}\n";
    const char* fontVert =
        "#version 330 core\n"
        "layout (location = 0) in vec4 aVertex;\n"
        "out vec2 vTexCoords;\n"
        "uniform mat4 u_Projection;\n"
        "uniform mat4 u_Model;\n"
        "void main() {\n"
        "    gl_Position = u_Projection * u_Model * vec4(aVertex.xy, 0.0, 1.0);\n"
        "    vTexCoords = aVertex.zw;\n"
        "}\n";
    const char* fontFrag =
        "#version 330 core\n"
        "in vec2 vTexCoords;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_FontAtlas;\n"
        "uniform vec4 u_Color;\n"
        "void main() {\n"
        "    float alpha = texture(u_FontAtlas, vTexCoords).r;\n"
        "    if (alpha < 0.01) discard;\n"
        "    FragColor = vec4(u_Color.rgb, u_Color.a * alpha);\n"
        "}\n";
#endif

    SlipShader* spriteShader = SlipShader::CreateFromSource(spriteVert, spriteFrag);
    if (spriteShader) {
        s_Shaders["Sprite2D"] = std::unique_ptr<SlipShader>(spriteShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar Sprite2D shader!" << std::endl;
    }

    SlipShader* fontShader = SlipShader::CreateFromSource(fontVert, fontFrag);
    if (fontShader) {
        s_Shaders["Font2D"] = std::unique_ptr<SlipShader>(fontShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar Font2D shader!" << std::endl;
    }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    const char* outlinePostVert =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout (location = 0) in vec3 aPos;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    vUV = aPos.xy + 0.5;\n"
        "    gl_Position = vec4(aPos.xy * 2.0, 0.0, 1.0);\n"
        "}\n";
    const char* outlinePostFrag =
        "#version 300 es\n"
        "precision highp float;\n"
        "precision mediump sampler2D;\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_MaskTex;\n"
        "uniform vec2 u_TexelSize;\n"
        "uniform vec4 u_Color;\n"
        "void main() {\n"
        "    float c = texture(u_MaskTex, vUV).r;\n"
        "    if (c > 0.5) discard;\n"
        "    const vec2 dirs[8] = vec2[8](\n"
        "        vec2( 1.0,  0.0), vec2(-1.0,  0.0),\n"
        "        vec2( 0.0,  1.0), vec2( 0.0, -1.0),\n"
        "        vec2( 0.7071,  0.7071), vec2(-0.7071,  0.7071),\n"
        "        vec2( 0.7071, -0.7071), vec2(-0.7071, -0.7071)\n"
        "    );\n"
        "    float nearHit = 0.0;\n"
        "    float farHit = 0.0;\n"
        "    for (int i = 0; i < 8; ++i) {\n"
        "        vec2 d = dirs[i] * u_TexelSize;\n"
        "        nearHit = max(nearHit, texture(u_MaskTex, vUV + d * 1.15).r);\n"
        "        nearHit = max(nearHit, texture(u_MaskTex, vUV + d * 2.0).r);\n"
        "        farHit  = max(farHit,  texture(u_MaskTex, vUV + d * 2.8).r);\n"
        "    }\n"
        "    float alpha = (nearHit > 0.5) ? 1.0 : ((farHit > 0.5) ? 0.55 : 0.0);\n"
        "    if (alpha <= 0.01) discard;\n"
        "    FragColor = vec4(u_Color.rgb, u_Color.a * alpha);\n"
        "}\n";
#else
    const char* outlinePostVert =
        "#version 330 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    vUV = aPos.xy + 0.5;\n"
        "    gl_Position = vec4(aPos.xy * 2.0, 0.0, 1.0);\n"
        "}\n";
    const char* outlinePostFrag =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_MaskTex;\n"
        "uniform vec2 u_TexelSize;\n"
        "uniform vec4 u_Color;\n"
        "void main() {\n"
        "    float c = texture(u_MaskTex, vUV).r;\n"
        "    if (c > 0.5) discard;\n"
        "    const vec2 dirs[8] = vec2[8](\n"
        "        vec2( 1.0,  0.0), vec2(-1.0,  0.0),\n"
        "        vec2( 0.0,  1.0), vec2( 0.0, -1.0),\n"
        "        vec2( 0.7071,  0.7071), vec2(-0.7071,  0.7071),\n"
        "        vec2( 0.7071, -0.7071), vec2(-0.7071, -0.7071)\n"
        "    );\n"
        "    float nearHit = 0.0;\n"
        "    float farHit = 0.0;\n"
        "    for (int i = 0; i < 8; ++i) {\n"
        "        vec2 d = dirs[i] * u_TexelSize;\n"
        "        nearHit = max(nearHit, texture(u_MaskTex, vUV + d * 1.15).r);\n"
        "        nearHit = max(nearHit, texture(u_MaskTex, vUV + d * 2.0).r);\n"
        "        farHit  = max(farHit,  texture(u_MaskTex, vUV + d * 2.8).r);\n"
        "    }\n"
        "    float alpha = (nearHit > 0.5) ? 1.0 : ((farHit > 0.5) ? 0.55 : 0.0);\n"
        "    if (alpha <= 0.01) discard;\n"
        "    FragColor = vec4(u_Color.rgb, u_Color.a * alpha);\n"
        "}\n";
#endif
    SlipShader* outlinePostShader = SlipShader::CreateFromSource(outlinePostVert, outlinePostFrag);
    if (outlinePostShader) {
        s_Shaders["OutlinePost"] = std::unique_ptr<SlipShader>(outlinePostShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar OutlinePost shader!" << std::endl;
    }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    const char* particleBillboardVert =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout (location = 0) in vec3 aWorldPos;\n"
        "layout (location = 1) in vec3 aCenterPos;\n"
        "layout (location = 2) in vec2 aTexCoords;\n"
        "layout (location = 3) in vec4 aColor;\n"
        "out vec3 vWorldPos;\n"
        "out vec3 vCenterPos;\n"
        "out vec2 vTexCoords;\n"
        "out vec4 vColor;\n"
        "uniform mat4 u_View;\n"
        "uniform mat4 u_Projection;\n"
        "void main() {\n"
        "    vWorldPos = aWorldPos;\n"
        "    vCenterPos = aCenterPos;\n"
        "    vTexCoords = aTexCoords;\n"
        "    vColor = aColor;\n"
        "    gl_Position = u_Projection * u_View * vec4(aWorldPos, 1.0);\n"
        "}\n";

    const char* particlePointVert =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout (location = 0) in vec3 aWorldPos;\n"
        "layout (location = 1) in float aSize;\n"
        "layout (location = 2) in vec4 aColor;\n"
        "out vec3 vWorldPos;\n"
        "out vec4 vColor;\n"
        "uniform mat4 u_View;\n"
        "uniform mat4 u_Projection;\n"
        "uniform float u_ViewportHeight;\n"
        "uniform float u_PointBaseScale;\n"
        "void main() {\n"
        "    vWorldPos = aWorldPos;\n"
        "    vColor = aColor;\n"
        "    vec4 viewPos = u_View * vec4(aWorldPos, 1.0);\n"
        "    gl_Position = u_Projection * viewPos;\n"
        "    float dist = max(-viewPos.z, 0.1);\n"
        "    float projFactor = abs(u_Projection[1][1]);\n"
        "    gl_PointSize = clamp((u_ViewportHeight * 0.5 * projFactor * aSize * u_PointBaseScale) / dist, 1.0, 1024.0);\n"
        "}\n";

    const char* particleCommonFragHeader =
        "#version 300 es\n"
        "precision highp float;\n"
        "precision mediump int;\n"
        "precision mediump sampler2D;\n"
        "precision highp sampler2DArray;\n";
#else
    const char* particleBillboardVert =
        "#version 330 core\n"
        "layout (location = 0) in vec3 aWorldPos;\n"
        "layout (location = 1) in vec3 aCenterPos;\n"
        "layout (location = 2) in vec2 aTexCoords;\n"
        "layout (location = 3) in vec4 aColor;\n"
        "out vec3 vWorldPos;\n"
        "out vec3 vCenterPos;\n"
        "out vec2 vTexCoords;\n"
        "out vec4 vColor;\n"
        "uniform mat4 u_View;\n"
        "uniform mat4 u_Projection;\n"
        "void main() {\n"
        "    vWorldPos = aWorldPos;\n"
        "    vCenterPos = aCenterPos;\n"
        "    vTexCoords = aTexCoords;\n"
        "    vColor = aColor;\n"
        "    gl_Position = u_Projection * u_View * vec4(aWorldPos, 1.0);\n"
        "}\n";

    const char* particlePointVert =
        "#version 330 core\n"
        "layout (location = 0) in vec3 aWorldPos;\n"
        "layout (location = 1) in float aSize;\n"
        "layout (location = 2) in vec4 aColor;\n"
        "out vec3 vWorldPos;\n"
        "out vec4 vColor;\n"
        "uniform mat4 u_View;\n"
        "uniform mat4 u_Projection;\n"
        "uniform float u_ViewportHeight;\n"
        "uniform float u_PointBaseScale;\n"
        "void main() {\n"
        "    vWorldPos = aWorldPos;\n"
        "    vColor = aColor;\n"
        "    vec4 viewPos = u_View * vec4(aWorldPos, 1.0);\n"
        "    gl_Position = u_Projection * viewPos;\n"
        "    float dist = max(-viewPos.z, 0.1);\n"
        "    float projFactor = abs(u_Projection[1][1]);\n"
        "    gl_PointSize = clamp((u_ViewportHeight * 0.5 * projFactor * aSize * u_PointBaseScale) / dist, 1.0, 1024.0);\n"
        "}\n";

    const char* particleCommonFragHeader =
        "#version 330 core\n";
#endif

    std::string particleLightingFunctions =
        "struct DirectionalLight { vec3 direction; vec3 color; float intensity; };\n"
        "struct PointLight { vec3 position; vec3 color; float intensity; float range; };\n"
        "struct SpotLight { vec3 position; vec3 direction; vec3 color; float intensity; float range; float spotAngle; };\n"
        "#define MAX_DIR_LIGHTS 4\n"
        "#define MAX_POINT_LIGHTS 32\n"
        "#define MAX_SPOT_LIGHTS 4\n"
        "uniform int u_NumDirLights;\n"
        "uniform DirectionalLight u_DirLights[MAX_DIR_LIGHTS];\n"
        "uniform int u_NumPointLights;\n"
        "uniform PointLight u_PointLights[MAX_POINT_LIGHTS];\n"
        "uniform int u_NumSpotLights;\n"
        "uniform SpotLight u_SpotLights[MAX_SPOT_LIGHTS];\n"
        "uniform highp sampler2DArray u_ShadowMapArray;\n"
        "uniform mat4 u_LightSpaceMatrices[4];\n"
        "uniform float u_CascadeEndClipSpace[4];\n"
        "uniform int u_CascadeCount;\n"
        "uniform float u_ShadowBias;\n"
        "uniform bool u_ShadowsEnabled;\n"
        "uniform int u_PCFQuality;\n"
        "uniform bool u_ReceiveShadows;\n"
        "uniform bool u_ReceiveLights;\n"
        "uniform mat4 u_View;\n"
        "uniform vec3 u_CameraPos;\n"
        "uniform int u_MaterialMode;\n" // 0 = Unlit, 1 = Lit, 2 = Emissive
        "uniform vec3 u_EmissiveColor;\n"
        "uniform float u_EmissiveIntensity;\n"
        "uniform float u_Roughness;\n"
        "uniform float u_Metallic;\n"
        "float CalculateParticleShadowCSM(vec3 shadowSamplePos) {\n"
        "    if (!u_ShadowsEnabled || !u_ReceiveShadows || u_CascadeCount <= 0) return 0.0;\n"
        "    vec4 fragPosView = u_View * vec4(shadowSamplePos, 1.0);\n"
        "    float depthValue = abs(fragPosView.z);\n"
        "    int layer = -1;\n"
        "    for (int c = 0; c < u_CascadeCount && c < 4; ++c) {\n"
        "        if (depthValue < u_CascadeEndClipSpace[c]) { layer = c; break; }\n"
        "    }\n"
        "    if (layer == -1) return 0.0;\n"
        "    vec4 fragPosLightSpace = u_LightSpaceMatrices[layer] * vec4(shadowSamplePos, 1.0);\n"
        "    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;\n"
        "    projCoords = projCoords * 0.5 + 0.5;\n"
        "    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;\n"
        "    float currentDepth = projCoords.z;\n"
        "    float bias = max(u_ShadowBias * 1.5, 0.001);\n"
        "    float shadow = 0.0;\n"
        "    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMapArray, 0).xy);\n"
        "    for (int x = -1; x <= 1; ++x) {\n"
        "        for (int y = -1; y <= 1; ++y) {\n"
        "            float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy + vec2(x, y) * texelSize * 1.5, float(layer))).r;\n"
        "            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;\n"
        "        }\n"
        "    }\n"
        "    return shadow / 9.0;\n"
        "}\n"
        "vec3 EvaluateParticleLighting(vec3 albedo, vec3 worldPos, vec3 shadowSamplePos, vec3 N, bool isSphericalPoint) {\n"
        "    if (u_MaterialMode == 0) return albedo;\n"
        "    if (u_MaterialMode == 2) return albedo + u_EmissiveColor * u_EmissiveIntensity * albedo;\n"
        "    if (!u_ReceiveLights) return albedo;\n"
        "    vec3 V = normalize(u_CameraPos - worldPos);\n"
        "    float mainShadow = (u_NumDirLights > 0 && u_ReceiveShadows) ? CalculateParticleShadowCSM(shadowSamplePos) : 0.0;\n"
        "    vec3 ambient = albedo * (u_ReceiveShadows ? mix(0.04, 0.14, clamp(1.0 - mainShadow, 0.0, 1.0)) : 0.16);\n"
        "    vec3 totalLight = ambient;\n"
        "    float shininess = mix(64.0, 4.0, clamp(u_Roughness, 0.0, 1.0));\n"
        "    float specStrength = isSphericalPoint ? (mix(0.20, 0.80, clamp(u_Metallic, 0.0, 1.0)) * (1.0 - 0.75 * clamp(u_Roughness, 0.0, 1.0))) : 0.0;\n"
        "    for (int i = 0; i < u_NumDirLights && i < MAX_DIR_LIGHTS; ++i) {\n"
        "        vec3 L = normalize(-u_DirLights[i].direction);\n"
        "        float diff = isSphericalPoint ? (max(dot(N, L), 0.0) * 0.75 + 0.25) : 0.85;\n"
        "        float spec = 0.0;\n"
        "        if (specStrength > 0.001) {\n"
        "            vec3 H = normalize(V + L);\n"
        "            spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;\n"
        "        }\n"
        "        float shadow = (i == 0) ? mainShadow : 0.0;\n"
        "        vec3 rad = u_DirLights[i].color * u_DirLights[i].intensity;\n"
        "        totalLight += (1.0 - shadow) * (albedo * diff + vec3(spec)) * rad;\n"
        "    }\n"
        "    for (int i = 0; i < u_NumPointLights && i < MAX_POINT_LIGHTS; ++i) {\n"
        "        vec3 toLight = u_PointLights[i].position - worldPos;\n"
        "        float dist = length(toLight);\n"
        "        float range = max(u_PointLights[i].range, 0.001);\n"
        "        if (dist < range) {\n"
        "            vec3 L = toLight / max(dist, 0.0001);\n"
        "            float att = clamp(1.0 - (dist / range), 0.0, 1.0);\n"
        "            att *= att;\n"
        "            float diff = isSphericalPoint ? (max(dot(N, L), 0.0) * 0.75 + 0.25) : 0.85;\n"
        "            float spec = 0.0;\n"
        "            if (specStrength > 0.001) {\n"
        "                vec3 H = normalize(V + L);\n"
        "                spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;\n"
        "            }\n"
        "            vec3 rad = u_PointLights[i].color * u_PointLights[i].intensity * att;\n"
        "            totalLight += (albedo * diff + vec3(spec)) * rad;\n"
        "        }\n"
        "    }\n"
        "    for (int i = 0; i < u_NumSpotLights && i < MAX_SPOT_LIGHTS; ++i) {\n"
        "        vec3 toLight = u_SpotLights[i].position - worldPos;\n"
        "        float dist = length(toLight);\n"
        "        float range = max(u_SpotLights[i].range, 0.001);\n"
        "        if (dist < range) {\n"
        "            vec3 L = toLight / max(dist, 0.0001);\n"
        "            float theta = dot(L, normalize(-u_SpotLights[i].direction));\n"
        "            float cutoff = cos(radians(u_SpotLights[i].spotAngle));\n"
        "            if (theta > cutoff) {\n"
        "                float spotFactor = clamp((theta - cutoff) / max(1.0 - cutoff, 0.001), 0.0, 1.0);\n"
        "                float att = clamp(1.0 - (dist / range), 0.0, 1.0);\n"
        "                att = att * att * spotFactor;\n"
        "                float diff = isSphericalPoint ? (max(dot(N, L), 0.0) * 0.75 + 0.25) : 0.85;\n"
        "                float spec = 0.0;\n"
        "                if (specStrength > 0.001) {\n"
        "                    vec3 H = normalize(V + L);\n"
        "                    spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;\n"
        "                }\n"
        "                vec3 rad = u_SpotLights[i].color * u_SpotLights[i].intensity * att;\n"
        "                totalLight += (albedo * diff + vec3(spec)) * rad;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    totalLight += u_EmissiveColor * u_EmissiveIntensity * albedo;\n"
        "    return totalLight;\n"
        "}\n";

    std::string particleBillboardFrag =
        std::string(particleCommonFragHeader) +
        "in vec3 vWorldPos;\n"
        "in vec3 vCenterPos;\n"
        "in vec2 vTexCoords;\n"
        "in vec4 vColor;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_MainTexture;\n"
        "uniform bool u_HasTexture;\n" +
        particleLightingFunctions +
        "void main() {\n"
        "    vec4 texCol = vec4(1.0);\n"
        "    if (u_HasTexture) {\n"
        "        texCol = texture(u_MainTexture, vTexCoords);\n"
        "    } else {\n"
        "        vec2 centered = vTexCoords * 2.0 - 1.0;\n"
        "        float r2 = dot(centered, centered);\n"
        "        if (r2 > 1.0) discard;\n"
        "        texCol.a = smoothstep(1.0, 0.0, r2);\n"
        "    }\n"
        "    vec4 baseCol = vColor * texCol;\n"
        "    if (baseCol.a < 0.005) discard;\n"
        "    vec3 N = normalize(u_CameraPos - vCenterPos);\n"
        "    vec3 litRgb = EvaluateParticleLighting(baseCol.rgb, vWorldPos, vCenterPos, N, false);\n"
        "    FragColor = vec4(litRgb, baseCol.a);\n"
        "}\n";

    std::string particlePointFrag =
        std::string(particleCommonFragHeader) +
        "in vec3 vWorldPos;\n"
        "in vec4 vColor;\n"
        "out vec4 FragColor;\n"
        "uniform int u_PointShape;\n" // 0 = SoftCircle, 1 = LitSphere, 2 = Square
        "uniform mat4 u_InvView;\n" +
        particleLightingFunctions +
        "void main() {\n"
        "    vec2 puv = gl_PointCoord * 2.0 - 1.0;\n"
        "    float r2 = dot(puv, puv);\n"
        "    float alphaMask = 1.0;\n"
        "    vec3 N = normalize(u_CameraPos - vWorldPos);\n"
        "    if (u_PointShape == 0) {\n"
        "        if (r2 > 1.0) discard;\n"
        "        alphaMask = smoothstep(1.0, 0.0, r2);\n"
        "    } else if (u_PointShape == 1) {\n"
        "        if (r2 > 1.0) discard;\n"
        "        float z = sqrt(max(0.0, 1.0 - r2));\n"
        "        vec3 viewN = normalize(vec3(puv.x, -puv.y, z));\n"
        "        N = normalize(mat3(u_InvView) * viewN);\n"
        "        alphaMask = smoothstep(1.0, 0.22, r2);\n"
        "    }\n"
        "    float finalAlpha = vColor.a * alphaMask;\n"
        "    if (finalAlpha < 0.005) discard;\n"
        "    vec3 litRgb = EvaluateParticleLighting(vColor.rgb, vWorldPos, vWorldPos, N, u_PointShape == 1);\n"
        "    FragColor = vec4(litRgb, finalAlpha);\n"
        "}\n";

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    const char* particleShadowVert =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout (location = 0) in vec3 aWorldPos;\n"
        "layout (location = 1) in vec2 aTexCoords;\n"
        "layout (location = 2) in float aAlpha;\n"
        "layout (location = 3) in float aPointSize;\n"
        "out vec2 vTexCoords;\n"
        "out float vAlpha;\n"
        "uniform mat4 u_LightSpaceMatrix;\n"
        "void main() {\n"
        "    vTexCoords = aTexCoords;\n"
        "    vAlpha = aAlpha;\n"
        "    gl_Position = u_LightSpaceMatrix * vec4(aWorldPos, 1.0);\n"
        "    gl_PointSize = clamp(aPointSize * 24.0, 2.0, 64.0);\n"
        "}\n";

    const char* particleShadowFrag =
        "#version 300 es\n"
        "precision highp float;\n"
        "precision mediump sampler2D;\n"
        "in vec2 vTexCoords;\n"
        "in float vAlpha;\n"
        "uniform sampler2D u_MainTexture;\n"
        "uniform bool u_HasTexture;\n"
        "uniform bool u_IsPointMode;\n"
        "uniform float u_AlphaCutoff;\n"
        "void main() {\n"
        "    float a = vAlpha;\n"
        "    if (u_IsPointMode) {\n"
        "        vec2 puv = gl_PointCoord * 2.0 - 1.0;\n"
        "        if (dot(puv, puv) > 1.0) discard;\n"
        "    } else if (u_HasTexture) {\n"
        "        a *= texture(u_MainTexture, vTexCoords).a;\n"
        "    } else {\n"
        "        vec2 centered = vTexCoords * 2.0 - 1.0;\n"
        "        if (dot(centered, centered) > 1.0) discard;\n"
        "    }\n"
        "    if (a < u_AlphaCutoff) discard;\n"
        "}\n";
#else
    const char* particleShadowVert =
        "#version 330 core\n"
        "layout (location = 0) in vec3 aWorldPos;\n"
        "layout (location = 1) in vec2 aTexCoords;\n"
        "layout (location = 2) in float aAlpha;\n"
        "layout (location = 3) in float aPointSize;\n"
        "out vec2 vTexCoords;\n"
        "out float vAlpha;\n"
        "uniform mat4 u_LightSpaceMatrix;\n"
        "void main() {\n"
        "    vTexCoords = aTexCoords;\n"
        "    vAlpha = aAlpha;\n"
        "    gl_Position = u_LightSpaceMatrix * vec4(aWorldPos, 1.0);\n"
        "    gl_PointSize = clamp(aPointSize * 24.0, 2.0, 64.0);\n"
        "}\n";

    const char* particleShadowFrag =
        "#version 330 core\n"
        "in vec2 vTexCoords;\n"
        "in float vAlpha;\n"
        "uniform sampler2D u_MainTexture;\n"
        "uniform bool u_HasTexture;\n"
        "uniform bool u_IsPointMode;\n"
        "uniform float u_AlphaCutoff;\n"
        "void main() {\n"
        "    float a = vAlpha;\n"
        "    if (u_IsPointMode) {\n"
        "        vec2 puv = gl_PointCoord * 2.0 - 1.0;\n"
        "        if (dot(puv, puv) > 1.0) discard;\n"
        "    } else if (u_HasTexture) {\n"
        "        a *= texture(u_MainTexture, vTexCoords).a;\n"
        "    } else {\n"
        "        vec2 centered = vTexCoords * 2.0 - 1.0;\n"
        "        if (dot(centered, centered) > 1.0) discard;\n"
        "    }\n"
        "    if (a < u_AlphaCutoff) discard;\n"
        "}\n";
#endif

    SlipShader* pbShader = SlipShader::CreateFromSource(particleBillboardVert, particleBillboardFrag.c_str());
    if (pbShader) {
        s_Shaders["ParticleBillboard"] = std::unique_ptr<SlipShader>(pbShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar ParticleBillboard shader!" << std::endl;
    }

    SlipShader* ppShader = SlipShader::CreateFromSource(particlePointVert, particlePointFrag.c_str());
    if (ppShader) {
        s_Shaders["ParticlePoint"] = std::unique_ptr<SlipShader>(ppShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar ParticlePoint shader!" << std::endl;
    }

    SlipShader* psShader = SlipShader::CreateFromSource(particleShadowVert, particleShadowFrag);
    if (psShader) {
        s_Shaders["ParticleShadowDepth"] = std::unique_ptr<SlipShader>(psShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar ParticleShadowDepth shader!" << std::endl;
    }

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    const char* ssrVert =
        "#version 330 core\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
        "    vUV = p;\n"
        "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    const char* ssrFrag =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D u_Color;\n"
        "uniform sampler2D u_Depth;\n"
        "uniform sampler2D u_Geometry;\n"
        "uniform mat4 u_InvViewProj;\n"
        "uniform mat4 u_ViewProj;\n"
        "uniform mat4 u_View;\n"
        "uniform vec3 u_CameraPos;\n"
        "uniform int u_Mode;\n"
        "uniform int u_Steps;\n"
        "uniform float u_MaxDist;\n"
        "uniform sampler2D u_Reflection;\n"
        "uniform samplerCube u_EnvCube;\n"
        "uniform bool u_HasEnvCube;\n"
        "uniform vec3 u_SkyZenith;\n"
        "uniform vec3 u_SkyHorizon;\n"
        "uniform vec3 u_SkyGround;\n"
        "uniform float u_RoughnessCutoff;\n"
        "uniform float u_Thickness;\n"
        "uniform float u_EdgeFade;\n"
        "vec3 WorldFromUV(vec2 uv, float depth) {\n"
        "    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n"
        "    vec4 w = u_InvViewProj * clip;\n"
        "    return w.xyz / max(w.w, 0.0001);\n"
        "}\n"
        "float ViewDistance(vec3 world) {\n"
        "    return max(-(u_View * vec4(world, 1.0)).z, 0.0);\n"
        "}\n"
        "vec3 DecodeNormal(vec2 e) {\n"
        "    vec2 f = e * 2.0 - 1.0;\n"
        "    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));\n"
        "    vec2 sgn = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);\n"
        "    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sgn;\n"
        "    return normalize(n);\n"
        "}\n"
        "void main() {\n"
        "    if (u_Mode == 1) {\n"
        "        vec3 scene = texture(u_Color, vUV).rgb;\n"
        "        vec4 refl = texture(u_Reflection, vUV);\n"
        "        FragColor = vec4(mix(scene, refl.rgb, refl.a), 1.0);\n"
        "        return;\n"
        "    }\n"
        "    vec3 scene = texture(u_Color, vUV).rgb;\n"
        "    vec4 geo = texture(u_Geometry, vUV);\n"
        "    float roughness = clamp(geo.b, 0.0, 1.0);\n"
        "    float metallic = clamp(geo.a, 0.0, 1.0);\n"
        "    float depth = texture(u_Depth, vUV).r;\n"
        "    float rCut = (u_RoughnessCutoff > 0.01) ? u_RoughnessCutoff : 0.75;\n"
        "    if (roughness > rCut || depth > 0.999) {\n"
        "        FragColor = (u_Mode == 0) ? vec4(0.0) : vec4(scene, 1.0);\n"
        "        return;\n"
        "    }\n"
        "    vec3 pos = WorldFromUV(vUV, depth);\n"
        "    vec3 N = DecodeNormal(geo.rg);\n"
        "    vec3 V = normalize(u_CameraPos - pos);\n"
        "    float gloss = (1.0 - roughness) * (1.0 - roughness);\n"
        "    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);\n"
        "    float roughWeight = smoothstep(rCut, 0.05, roughness);\n"
        "    float fade = gloss * roughWeight * mix(0.18 + 0.82 * fresnel, 1.0, metallic);\n"
        "    if (fade < 0.01) {\n"
        "        FragColor = (u_Mode == 0) ? vec4(0.0) : vec4(scene, 1.0);\n"
        "        return;\n"
        "    }\n"
        "    vec3 R = reflect(-V, N);\n"
        "    vec3 envRefl = vec3(0.0);\n"
        "    if (u_HasEnvCube) {\n"
        "        envRefl = texture(u_EnvCube, R).rgb;\n"
        "    } else {\n"
        "        float rh = R.y;\n"
        "        if (rh >= 0.0) envRefl = mix(u_SkyHorizon, u_SkyZenith, pow(clamp(rh, 0.0, 1.0), 0.5));\n"
        "        else envRefl = mix(u_SkyHorizon, u_SkyGround, pow(clamp(-rh, 0.0, 1.0), 0.5));\n"
        "    }\n"
        "    vec3 hit = envRefl;\n"
        "    float confidence = 0.0;\n"
        "    int steps = clamp(u_Steps, 12, 64);\n"
        "    float maxDist = max(u_MaxDist, 2.0);\n"
        "    float startOffset = max(0.06, depth * 0.12);\n"
        "    vec3 originW = pos + N * startOffset;\n"
        "    vec3 endW = originW + R * maxDist;\n"
        "    vec4 originC = u_ViewProj * vec4(originW, 1.0);\n"
        "    vec4 endC = u_ViewProj * vec4(endW, 1.0);\n"
        "    if (originC.w > 0.001 && endC.w > 0.001) {\n"
        "        float jitter = fract(sin(dot(vUV, vec2(12.9898, 78.233))) * 43758.5453) * 0.5;\n"
        "        vec3 originH = originC.xyz / originC.w;\n"
        "        vec3 endH = endC.xyz / endC.w;\n"
        "        float k0 = 1.0 / originC.w;\n"
        "        float k1 = 1.0 / endC.w;\n"
        "        vec3 p0 = originH * k0;\n"
        "        vec3 p1 = endH * k1;\n"
        "        float tHit = -1.0;\n"
        "        float ef = max(u_EdgeFade, 0.05);\n"
        "        for (int i = 2; i <= 64; ++i) {\n"
        "            if (i > steps) break;\n"
        "            float t = (float(i) + jitter) / float(steps);\n"
        "            float k = mix(k0, k1, t);\n"
        "            vec3 ndc = mix(p0, p1, t) / max(k, 0.00001);\n"
        "            vec2 uv = ndc.xy * 0.5 + 0.5;\n"
        "            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;\n"
        "            float sceneDepth = texture(u_Depth, uv).r;\n"
        "            if (sceneDepth > 0.999) continue;\n"
        "            float rayDepth = ndc.z * 0.5 + 0.5;\n"
        "            vec3 rayPos = WorldFromUV(uv, rayDepth);\n"
        "            vec3 scenePos = WorldFromUV(uv, sceneDepth);\n"
        "            float zRay = ViewDistance(rayPos);\n"
        "            float zScene = ViewDistance(scenePos);\n"
        "            float thick = max(u_Thickness, 0.35) * (1.0 + t * 2.0);\n"
        "            if (zRay > zScene + 0.05 && zRay < zScene + thick) { tHit = t; break; }\n"
        "        }\n"
        "        if (tHit > 0.0) {\n"
        "            float tA = max(tHit - 1.5 / float(steps), 0.0);\n"
        "            float tB = tHit;\n"
        "            vec2 bestUV = vUV;\n"
        "            for (int k = 0; k < 6; ++k) {\n"
        "                float t = 0.5 * (tA + tB);\n"
        "                float kk = mix(k0, k1, t);\n"
        "                vec3 ndc = mix(p0, p1, t) / max(kk, 0.00001);\n"
        "                vec2 uv = ndc.xy * 0.5 + 0.5;\n"
        "                float sceneDepth = texture(u_Depth, uv).r;\n"
        "                float rayDepth = ndc.z * 0.5 + 0.5;\n"
        "                float zRay = ViewDistance(WorldFromUV(uv, rayDepth));\n"
        "                float zScene = ViewDistance(WorldFromUV(uv, sceneDepth));\n"
        "                if (zRay > zScene) { tB = t; bestUV = uv; }\n"
        "                else tA = t;\n"
        "            }\n"
        "            float edge = smoothstep(0.0, ef, bestUV.x) * smoothstep(1.0, 1.0 - ef, bestUV.x) *\n"
        "                         smoothstep(0.0, ef, bestUV.y) * smoothstep(1.0, 1.0 - ef, bestUV.y);\n"
        "            vec3 screenSample = texture(u_Color, bestUV).rgb;\n"
        "            hit = mix(envRefl, screenSample, edge);\n"
        "            confidence = edge;\n"
        "        }\n"
        "    }\n"
        "    float amount = clamp(fade, 0.0, 0.95);\n"
        "    float blendWeight = amount * confidence;\n"
        "    if (u_Mode == 0) FragColor = vec4(hit, blendWeight);\n"
        "    else FragColor = vec4(mix(scene, hit, blendWeight), 1.0);\n"
        "}\n";
    SlipShader* ssrShader = SlipShader::CreateFromSource(ssrVert, ssrFrag);
    if (ssrShader) {
        s_Shaders["SSR"] = std::unique_ptr<SlipShader>(ssrShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar SSR shader!" << std::endl;
    }
#endif

    // =========================================================================
    // CIELO PROCEDIMENTAL Y GRADIENTE ATMOSFÉRICO (PC + Android OpenGL ES 3.0)
    // =========================================================================
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    const char* skyVert =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout(location = 0) in vec2 a_Pos;\n"
        "out vec3 vRayDir;\n"
        "uniform mat4 u_InvProj;\n"
        "uniform mat4 u_InvView;\n"
        "void main() {\n"
        "    vec4 clip = vec4(a_Pos, 1.0, 1.0);\n"
        "    vec4 viewRay = u_InvProj * clip;\n"
        "    vRayDir = mat3(u_InvView) * (viewRay.xyz / max(abs(viewRay.w), 0.0001));\n"
        "    gl_Position = vec4(a_Pos, 1.0, 1.0);\n"
        "}\n";
    const char* skyFrag =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec3 vRayDir;\n"
        "out vec4 FragColor;\n"
        "uniform vec3 u_ZenithColor;\n"
        "uniform vec3 u_HorizonColor;\n"
        "uniform vec3 u_GroundColor;\n"
        "uniform float u_HorizonSharpness;\n"
        "uniform bool u_SunEnabled;\n"
        "uniform vec3 u_SunDir;\n"
        "uniform vec3 u_SunColor;\n"
        "uniform float u_SunSize;\n"
        "uniform float u_SunGlow;\n"
        "uniform float u_SunIntensity;\n"
        "uniform float u_SunsetFactor;\n"
        "uniform float u_NightFactor;\n"
        "void main() {\n"
        "    vec3 rayDir = normalize(vRayDir);\n"
        "    float h = rayDir.y;\n"
        "    vec3 sky;\n"
        "    if (h >= 0.0) {\n"
        "        float t = pow(clamp(h, 0.0, 1.0), 1.0 / max(u_HorizonSharpness, 0.1));\n"
        "        sky = mix(u_HorizonColor, u_ZenithColor, t);\n"
        "    } else {\n"
        "        float t = pow(clamp(-h, 0.0, 1.0), 0.7);\n"
        "        sky = mix(u_HorizonColor, u_GroundColor, t);\n"
        "    }\n"
        "    if (u_SunsetFactor > 0.001) {\n"
        "        vec3 sDirNorm = normalize(u_SunDir);\n"
        "        vec2 rXZ = (length(rayDir.xz) > 0.001) ? normalize(rayDir.xz) : vec2(0.0);\n"
        "        vec2 sXZ = (length(sDirNorm.xz) > 0.001) ? normalize(sDirNorm.xz) : vec2(1.0, 0.0);\n"
        "        float azim = max(dot(rXZ, sXZ), 0.0);\n"
        "        float horizBand = exp(-abs(h) * 5.0);\n"
        "        vec3 sunsetGlow = vec3(1.0, 0.42, 0.12) * pow(azim, 2.8) * horizBand * (u_SunsetFactor * 1.6);\n"
        "        sky += sunsetGlow;\n"
        "    }\n"
        "    if (u_NightFactor > 0.001 && h > 0.06) {\n"
        "        vec3 p = rayDir * 220.0;\n"
        "        float n = sin(p.x * 12.9898 + p.y * 78.233 + p.z * 37.719) * 43758.5453;\n"
        "        n = fract(n);\n"
        "        if (n > 0.995) {\n"
        "            float star = smoothstep(0.995, 1.0, n) * u_NightFactor * min(h * 3.5, 1.0);\n"
        "            sky += vec3(star * 0.9, star * 0.95, star * 1.0);\n"
        "        }\n"
        "    }\n"
        "    if (u_SunEnabled && dot(u_SunDir, u_SunDir) > 0.001) {\n"
        "        vec3 sDir = normalize(u_SunDir);\n"
        "        float cosTheta = dot(rayDir, sDir);\n"
        "        if (cosTheta > 0.0) {\n"
        "            float glowExp = clamp(64.0 / max(u_SunGlow, 0.01), 2.0, 256.0);\n"
        "            float halo = pow(cosTheta, glowExp) * 0.6;\n"
        "            float discR = max(u_SunSize, 0.005);\n"
        "            float disc = smoothstep(1.0 - discR * 0.002, 1.0, cosTheta);\n"
        "            sky += (u_SunColor * u_SunIntensity) * (disc * 3.0 + halo);\n"
        "        }\n"
        "    }\n"
        "    FragColor = vec4(sky, 1.0);\n"
        "}\n";
#else
    const char* skyVert =
        "#version 330 core\n"
        "layout(location = 0) in vec2 a_Pos;\n"
        "out vec3 vRayDir;\n"
        "uniform mat4 u_InvProj;\n"
        "uniform mat4 u_InvView;\n"
        "void main() {\n"
        "    vec4 clip = vec4(a_Pos, 1.0, 1.0);\n"
        "    vec4 viewRay = u_InvProj * clip;\n"
        "    vRayDir = mat3(u_InvView) * (viewRay.xyz / max(abs(viewRay.w), 0.0001));\n"
        "    gl_Position = vec4(a_Pos, 1.0, 1.0);\n"
        "}\n";
    const char* skyFrag =
        "#version 330 core\n"
        "in vec3 vRayDir;\n"
        "out vec4 FragColor;\n"
        "uniform vec3 u_ZenithColor;\n"
        "uniform vec3 u_HorizonColor;\n"
        "uniform vec3 u_GroundColor;\n"
        "uniform float u_HorizonSharpness;\n"
        "uniform bool u_SunEnabled;\n"
        "uniform vec3 u_SunDir;\n"
        "uniform vec3 u_SunColor;\n"
        "uniform float u_SunSize;\n"
        "uniform float u_SunGlow;\n"
        "uniform float u_SunIntensity;\n"
        "uniform float u_SunsetFactor;\n"
        "uniform float u_NightFactor;\n"
        "void main() {\n"
        "    vec3 rayDir = normalize(vRayDir);\n"
        "    float h = rayDir.y;\n"
        "    vec3 sky;\n"
        "    if (h >= 0.0) {\n"
        "        float t = pow(clamp(h, 0.0, 1.0), 1.0 / max(u_HorizonSharpness, 0.1));\n"
        "        sky = mix(u_HorizonColor, u_ZenithColor, t);\n"
        "    } else {\n"
        "        float t = pow(clamp(-h, 0.0, 1.0), 0.7);\n"
        "        sky = mix(u_HorizonColor, u_GroundColor, t);\n"
        "    }\n"
        "    if (u_SunsetFactor > 0.001) {\n"
        "        vec3 sDirNorm = normalize(u_SunDir);\n"
        "        vec2 rXZ = (length(rayDir.xz) > 0.001) ? normalize(rayDir.xz) : vec2(0.0);\n"
        "        vec2 sXZ = (length(sDirNorm.xz) > 0.001) ? normalize(sDirNorm.xz) : vec2(1.0, 0.0);\n"
        "        float azim = max(dot(rXZ, sXZ), 0.0);\n"
        "        float horizBand = exp(-abs(h) * 5.0);\n"
        "        vec3 sunsetGlow = vec3(1.0, 0.42, 0.12) * pow(azim, 2.8) * horizBand * (u_SunsetFactor * 1.6);\n"
        "        sky += sunsetGlow;\n"
        "    }\n"
        "    if (u_NightFactor > 0.001 && h > 0.06) {\n"
        "        vec3 p = rayDir * 220.0;\n"
        "        float n = sin(p.x * 12.9898 + p.y * 78.233 + p.z * 37.719) * 43758.5453;\n"
        "        n = fract(n);\n"
        "        if (n > 0.995) {\n"
        "            float star = smoothstep(0.995, 1.0, n) * u_NightFactor * min(h * 3.5, 1.0);\n"
        "            sky += vec3(star * 0.9, star * 0.95, star * 1.0);\n"
        "        }\n"
        "    }\n"
        "    if (u_SunEnabled && dot(u_SunDir, u_SunDir) > 0.001) {\n"
        "        vec3 sDir = normalize(u_SunDir);\n"
        "        float cosTheta = dot(rayDir, sDir);\n"
        "        if (cosTheta > 0.0) {\n"
        "            float glowExp = clamp(64.0 / max(u_SunGlow, 0.01), 2.0, 256.0);\n"
        "            float halo = pow(cosTheta, glowExp) * 0.6;\n"
        "            float discR = max(u_SunSize, 0.005);\n"
        "            float disc = smoothstep(1.0 - discR * 0.002, 1.0, cosTheta);\n"
        "            sky += (u_SunColor * u_SunIntensity) * (disc * 3.0 + halo);\n"
        "        }\n"
        "    }\n"
        "    FragColor = vec4(sky, 1.0);\n"
        "}\n";
#endif
    SlipShader* skyShader = SlipShader::CreateFromSource(skyVert, skyFrag);
    if (skyShader) {
        s_Shaders["ProceduralSky"] = std::unique_ptr<SlipShader>(skyShader);
    } else {
        std::cerr << "[SlipShaderLibrary] Error al compilar ProceduralSky shader!" << std::endl;
    }

    // =========================================================================
    // NUBES VOLUMÉTRICAS HÍBRIDAS (CloudSlices, CloudRaymarch, CloudBilateralUpscale)
    // =========================================================================
    // 1. CloudSlices (GLSL ES 3.0 / GLSL 3.3 compatible)
    {
        const char* slicesVert = R"(#version 300 es
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

void main() {
    float sliceFactor = float(gl_InstanceID) / max(float(u_TotalSlices - 1), 1.0);
    vec2 p = a_Position.xz;
    float distNorm = clamp(length(p), 0.0, 1.0);
    float domeProfile = 1.0 - distNorm * distNorm;
    float baseH = u_CloudBaseHeight * (0.25 + 0.75 * domeProfile);
    float layerH = u_CloudLayerThickness * (0.4 + 0.6 * domeProfile);
    float localY = baseH + sliceFactor * layerH;
    vec2 localXZ = (distNorm > 0.0001 ? normalize(p) * (distNorm * u_DomeRadius) : vec2(0.0));
    vec3 relPos = vec3(localXZ.x, localY, localXZ.y);
    vec2 worldXZ = localXZ + u_CameraPos.xz;
    vec2 uv = worldXZ * u_CloudScale;
    v_VolumeUV = vec3(uv + u_WindOffset * (0.85 + 0.25 * sliceFactor), sliceFactor);
    v_WorldPos = relPos + u_CameraPos;
    v_SliceFactor = sliceFactor;
    v_ViewDir = normalize(relPos);
    vec3 viewPos = mat3(u_View) * relPos;
    gl_Position = u_Projection * vec4(viewPos, 1.0);
}
)";

        const char* slicesFrag = R"(#version 300 es
precision highp float;
in vec3  v_VolumeUV;
in vec3  v_WorldPos;
in float v_SliceFactor;
in vec3  v_ViewDir;

uniform sampler2D u_CloudNoise;
uniform vec3      u_SunDirection;
uniform vec3      u_SunColor;
uniform vec3      u_CloudAmbientColor;
uniform float     u_CloudCoverage;
uniform float     u_CloudDensity;
uniform float     u_DiscardThreshold;
uniform float     u_PowderFactor;
uniform float     u_HenyeyG;
uniform float     u_Absorption;
uniform int       u_TotalSlices;
uniform float     u_CloudLayerThickness;
uniform float     u_CloudEdgeErosion;
uniform float     u_CloudDetailScale;

out vec4 FragColor;

#define PI 3.14159265359

float Remap(float val, float inMin, float inMax, float outMin, float outMax) {
    return clamp(outMin + ((val - inMin) / max(inMax - inMin, 0.0001)) * (outMax - outMin), outMin, outMax);
}

void main() {
    float erosionFactor = clamp(u_CloudEdgeErosion > 0.0 ? u_CloudEdgeErosion : 0.60, 0.0, 1.0);
    float detailFreq = max(u_CloudDetailScale > 0.0 ? u_CloudDetailScale : 4.5, 1.0);

    vec4 nMacro = texture(u_CloudNoise, v_VolumeUV.xy);
    vec2 warp = (nMacro.gb - vec2(0.5)) * (0.05 * erosionFactor);
    vec2 uvTurb = v_VolumeUV.xy + warp;

    vec4 nDetail = texture(u_CloudNoise, uvTurb * detailFreq + vec2(0.18, 0.27));
    float highFreqDetail = mix(nDetail.r, nDetail.g, 0.40);

    float baseCut = smoothstep(0.0, 0.12, v_VolumeUV.z);
    float topCut  = smoothstep(1.0, 0.35, v_VolumeUV.z);
    float heightGradient = baseCut * topCut;

    float cov = clamp(u_CloudCoverage, 0.05, 0.95);
    float baseCloud = Remap(nMacro.r, 1.0 - cov, 1.0, 0.0, 1.0);

    float erosionAmount = highFreqDetail * (erosionFactor * mix(0.40, 0.85, v_VolumeUV.z));
    float rawDensity = Remap(baseCloud, erosionAmount, 1.0, 0.0, 1.0);

    float discardLimit = clamp(u_DiscardThreshold, 0.005, 0.25);
    if (rawDensity < discardLimit) {
        discard;
    }

    float sliceThickness = u_CloudLayerThickness / max(float(u_TotalSlices), 1.0);
    float opticalDepth = rawDensity * heightGradient * u_CloudDensity * sliceThickness * 3.5;
    float sliceStepAlpha = clamp(1.0 - exp(-opticalDepth), 0.0, 0.98);

    if (sliceStepAlpha < 0.008) {
        discard;
    }

    float sunPenetration = exp(-(1.0 - v_VolumeUV.z) * 2.6 * clamp(u_Absorption, 0.2, 3.0));
    float powder = 1.0 - exp(-rawDensity * max(u_PowderFactor, 0.1) * 3.0);

    vec3 viewDirNorm = normalize(v_ViewDir);
    vec3 sunDirNorm  = normalize(u_SunDirection);
    float cosTheta = dot(viewDirNorm, sunDirNorm);
    float g = u_HenyeyG;
    float hg = (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 0.01), 1.5));
    hg = clamp(hg, 0.1, 3.5);

    vec3 directSun = u_SunColor * (sunPenetration * (powder * hg * 2.0 + 0.25));
    vec3 ambient = u_CloudAmbientColor * mix(0.40, 1.15, v_VolumeUV.z * (0.3 + 0.7 * sunPenetration));
    vec3 cloudColor = directSun + ambient;

    FragColor = vec4(cloudColor, sliceStepAlpha);
}
)";

        std::string vCode = slicesVert;
        std::string fCode = slicesFrag;
        if (SlipFileSystem::Exists("assets/shaders/CloudSlices.vert")) {
            std::string fc;
            if (SlipFileSystem::ReadAllText("assets/shaders/CloudSlices.vert", fc)) vCode = fc;
        }
        if (SlipFileSystem::Exists("assets/shaders/CloudSlices.frag")) {
            std::string fc;
            if (SlipFileSystem::ReadAllText("assets/shaders/CloudSlices.frag", fc)) fCode = fc;
        }

        SlipShader* s = SlipShader::CreateFromSource(vCode.c_str(), fCode.c_str());
        if (s) {
            s_Shaders["CloudSlices"] = std::unique_ptr<SlipShader>(s);
        }
    }

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    // 2. CloudRaymarch (PC Desktop OpenGL 3.3 Core)
    {
        const char* raymarchVert = R"(#version 330 core
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;
uniform mat4 u_InvProjection;
uniform mat4 u_InvView;
out vec2 v_UV;
out vec3 v_WorldRay;

void main() {
    v_UV = a_UV;
    vec4 clipPos = vec4(a_Pos, 1.0, 1.0);
    vec4 viewRay = u_InvProjection * clipPos;
    v_WorldRay = mat3(u_InvView) * (viewRay.xyz / max(abs(viewRay.w), 0.0001));
    gl_Position = vec4(a_Pos, 0.0, 1.0);
}
)";

        const char* raymarchFrag = R"(#version 330 core
in vec2 v_UV;
in vec3 v_WorldRay;

uniform vec3      u_CameraPos;
uniform mat4      u_InvView;
uniform mat4      u_InvProjection;
uniform sampler2D u_SceneDepth;
uniform sampler2D u_CloudNoise;
uniform sampler2D u_BlueNoise;
uniform vec3      u_SunDirection;
uniform vec3      u_SunColor;
uniform vec3      u_CloudAmbientColor;
uniform float     u_CloudCoverage;
uniform float     u_CloudDensity;
uniform float     u_WindSpeed;
uniform vec2      u_WindOffset;
uniform float     u_CloudScale;
uniform float     u_CloudBaseHeight;
uniform float     u_CloudLayerThickness;
uniform float     u_CloudEdgeErosion;
uniform float     u_CloudDetailScale;
uniform int       u_RaymarchSteps;
uniform float     u_HenyeyG;
uniform float     u_Absorption;
uniform float     u_PowderFactor;
uniform float     u_NearPlane;
uniform float     u_FarPlane;
uniform vec2      u_ScreenResolution;

out vec4 FragColor;

#define PI 3.14159265359

float LinearizeDepth(float d) {
    float zNdc = d * 2.0 - 1.0;
    return (2.0 * u_NearPlane * u_FarPlane) / (u_FarPlane + u_NearPlane - zNdc * (u_FarPlane - u_NearPlane));
}

bool IntersectCloudLayer(vec3 ro, vec3 rd, float yMin, float yMax, out float tNear, out float tFar) {
    if (abs(rd.y) < 1e-5) {
        if (ro.y >= yMin && ro.y <= yMax) {
            tNear = 0.0;
            tFar = 16000.0;
            return true;
        }
        return false;
    }
    float t0 = (yMin - ro.y) / rd.y;
    float t1 = (yMax - ro.y) / rd.y;
    tNear = min(t0, t1);
    tFar  = max(t0, t1);
    if (tFar < 0.0) return false;
    tNear = max(tNear, 0.0);
    return tNear < tFar;
}

float Remap(float val, float inMin, float inMax, float outMin, float outMax) {
    return clamp(outMin + ((val - inMin) / max(inMax - inMin, 0.0001)) * (outMax - outMin), outMin, outMax);
}

float SampleDensity(vec3 pos, float heightFrac) {
    float baseShape = smoothstep(0.0, 0.08, heightFrac);
    float topShape  = smoothstep(1.0, 0.28, heightFrac);
    float verticalShape = baseShape * topShape;
    if (verticalShape <= 0.001) return 0.0;

    // Desplazamiento base uniforme en XZ sin cizalladura temporal
    vec2 baseUV = pos.xz * u_CloudScale + u_WindOffset;
    // Micro-variación sutil acotada por altura (evita estiramientos diagonales con el tiempo)
    vec2 heightTurbulence = vec2(sin(heightFrac * 3.14159), cos(heightFrac * 3.14159)) * 0.012;
    vec2 uv = baseUV + heightTurbulence;
    vec4 nMacro = texture(u_CloudNoise, uv);

    float erosionFactor = clamp(u_CloudEdgeErosion > 0.0 ? u_CloudEdgeErosion : 0.60, 0.0, 1.0);
    vec2 warp = (nMacro.gb - vec2(0.5)) * (0.05 * erosionFactor);
    vec2 uvTurb = uv + warp;

    float detailFreq = max(u_CloudDetailScale > 0.0 ? u_CloudDetailScale : 4.5, 1.0);
    vec4 nMid = texture(u_CloudNoise, uvTurb * detailFreq + vec2(0.23, 0.41));
    vec4 nHigh = texture(u_CloudNoise, uvTurb * (detailFreq * 2.8) + vec2(0.67, 0.19));

    float highFreqErosion = nMid.r * 0.48 + nMid.g * 0.34 + nHigh.b * 0.18;

    float coverage = clamp(u_CloudCoverage, 0.01, 0.99);
    float baseCloud = Remap(nMacro.r, 1.0 - coverage, 1.0, 0.0, 1.0);

    if (baseCloud <= 0.0001) {
        return 0.0;
    }

    float heightErosionMod = mix(0.40, 0.90, heightFrac);
    float erosionAmount = highFreqErosion * clamp(erosionFactor * heightErosionMod, 0.0, 0.92);

    float erodedCloud = Remap(baseCloud, erosionAmount, 1.0, 0.0, 1.0);
    float crispDensity = pow(erodedCloud, mix(1.0, 1.45, erosionFactor));

    return crispDensity * verticalShape * (u_CloudDensity * 8.5);
}

float SampleDensityShadow(vec3 pos, float heightFrac) {
    float baseShape = smoothstep(0.0, 0.08, heightFrac);
    float topShape  = smoothstep(1.0, 0.28, heightFrac);
    float verticalShape = baseShape * topShape;
    if (verticalShape <= 0.001) return 0.0;

    vec2 baseUV = pos.xz * u_CloudScale + u_WindOffset;
    vec4 nMacro = texture(u_CloudNoise, baseUV);
    float coverage = clamp(u_CloudCoverage, 0.01, 0.99);
    float baseCloud = Remap(nMacro.r, 1.0 - coverage, 1.0, 0.0, 1.0);
    if (baseCloud <= 0.001) return 0.0;

    return baseCloud * verticalShape * (u_CloudDensity * 8.5);
}

float HenyeyGreenstein(float cosTheta, float g) {
    return (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 0.01), 1.5));
}

void main() {
    vec3 rayDir = normalize(v_WorldRay);
    vec3 rayOrigin = u_CameraPos;
    float yMin = u_CloudBaseHeight;
    float yMax = u_CloudBaseHeight + u_CloudLayerThickness;

    float tNear, tFar;
    if (!IntersectCloudLayer(rayOrigin, rayDir, yMin, yMax, tNear, tFar)) {
        FragColor = vec4(0.0);
        return;
    }

    tFar = min(tFar, tNear + 7500.0);
    tFar = min(tFar, 18000.0);
    if (tNear >= tFar) {
        FragColor = vec4(0.0);
        return;
    }

    float rawDepth = texture(u_SceneDepth, v_UV).r;
    if (rawDepth < 0.99999) {
        vec4 clip = vec4(v_UV * 2.0 - 1.0, rawDepth * 2.0 - 1.0, 1.0);
        vec4 vPos = u_InvProjection * clip;
        vPos /= max(abs(vPos.w), 1e-5);
        vec3 wPos = (u_InvView * vPos).xyz;
        float sceneDistAlongRay = length(wPos - u_CameraPos);

        // Si la geometria y la camara estan por debajo de la base de las nubes, descartar
        if (u_CameraPos.y < u_CloudBaseHeight && wPos.y < u_CloudBaseHeight) {
            FragColor = vec4(0.0);
            return;
        }

        if (sceneDistAlongRay < tNear) {
            FragColor = vec4(0.0);
            return;
        }
        tFar = min(tFar, sceneDistAlongRay);
        if (tNear >= tFar) {
            FragColor = vec4(0.0);
            return;
        }
    }

    float horizonFade = smoothstep(0.012, 0.075, abs(rayDir.y));

    vec2 screenUV = gl_FragCoord.xy / 64.0;
    float jitter = texture(u_BlueNoise, screenUV).r;

    int steps = clamp(u_RaymarchSteps, 8, 48);
    float marchDistance = tFar - tNear;
    float stepSize = marchDistance / float(steps);
    stepSize = min(stepSize, 140.0);
    marchDistance = stepSize * float(steps);
    tFar = tNear + marchDistance;

    float tCurrent = tNear + stepSize * jitter;

    vec3 sunDir = normalize(u_SunDirection);
    float cosTheta = dot(rayDir, sunDir);
    float hgFwd = HenyeyGreenstein(cosTheta, u_HenyeyG);
    float hgBwd = HenyeyGreenstein(cosTheta, -0.22);
    float phase = mix(hgBwd, hgFwd, 0.75);
    phase = clamp(phase, 0.05, 3.8);

    vec3 accumLight = vec3(0.0);
    float transmittance = 1.0;
    float lightStepSize = u_CloudLayerThickness * 0.16;

    for (int i = 0; i < steps; ++i) {
        if (tCurrent > tFar || transmittance < 0.01) break;

        vec3 currentPos = rayOrigin + rayDir * tCurrent;
        float heightFrac = clamp((currentPos.y - yMin) / max(u_CloudLayerThickness, 1.0), 0.0, 1.0);
        float density = SampleDensity(currentPos, heightFrac);

        if (density > 0.0005) {
            float sunOpticalDepth = 0.0;
            float stepShadow = lightStepSize * 1.75;
            for (int j = 1; j <= 2; ++j) {
                vec3 lpos = currentPos + sunDir * (float(j) * stepShadow);
                float lHeightFrac = clamp((lpos.y - yMin) / max(u_CloudLayerThickness, 1.0), 0.0, 1.0);
                sunOpticalDepth += SampleDensityShadow(lpos, lHeightFrac) * stepShadow;
            }

            float sunTransmittance = exp(-sunOpticalDepth * u_Absorption);
            float multipleScattering = exp(-sunOpticalDepth * u_Absorption * 0.22) * 0.40;
            float sunVisibility = clamp(sunTransmittance + multipleScattering, 0.0, 1.0);

            float powder = 1.0 - exp(-density * u_PowderFactor * 3.0);
            float opticalDepth = density * stepSize * u_Absorption;
            float stepTransmittance = exp(-opticalDepth);

            vec3 directLight = u_SunColor * (phase * powder * sunVisibility * 2.8 + 0.04 * sunVisibility);
            vec3 darkAmbient = u_CloudAmbientColor * 0.32;
            vec3 topAmbient  = u_CloudAmbientColor * 1.15;
            vec3 ambientLight = mix(darkAmbient, topAmbient, heightFrac * (0.35 + 0.65 * sunVisibility));
            vec3 stepLight = directLight + ambientLight;

            accumLight += stepLight * transmittance * (1.0 - stepTransmittance);
            transmittance *= stepTransmittance;
        }

        tCurrent += stepSize;
    }

    float cloudAlpha = clamp(1.0 - transmittance, 0.0, 1.0) * horizonFade;
    accumLight *= horizonFade;
    FragColor = vec4(accumLight, cloudAlpha);
}
)";

        std::string vCode = raymarchVert;
        std::string fCode = raymarchFrag;
        if (SlipFileSystem::Exists("assets/shaders/CloudRaymarch.vert")) {
            std::string fc;
            if (SlipFileSystem::ReadAllText("assets/shaders/CloudRaymarch.vert", fc)) vCode = fc;
        }
        if (SlipFileSystem::Exists("assets/shaders/CloudRaymarch.frag")) {
            std::string fc;
            if (SlipFileSystem::ReadAllText("assets/shaders/CloudRaymarch.frag", fc)) fCode = fc;
        }

        SlipShader* s = SlipShader::CreateFromSource(vCode.c_str(), fCode.c_str());
        if (s) {
            s_Shaders["CloudRaymarch"] = std::unique_ptr<SlipShader>(s);
        }
    }

    // 3. CloudBilateralUpscale (PC Bilateral Depth-Aware Upscale)
    {
        const char* upscaleVert = R"(#version 330 core
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;
out vec2 v_UV;

void main() {
    v_UV = a_UV;
    gl_Position = vec4(a_Pos, 0.0, 1.0);
}
)";

        const char* upscaleFrag = R"(#version 330 core
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

float LinearizeDepth(float d) {
    float zNdc = d * 2.0 - 1.0;
    return (2.0 * u_NearPlane * u_FarPlane) / (u_FarPlane + u_NearPlane - zNdc * (u_FarPlane - u_NearPlane));
}

void main() {
    float centerDepthRaw = texture(u_SceneDepth, v_UV).r;
    if (centerDepthRaw >= 0.99999) {
        FragColor = texture(u_LowResClouds, v_UV);
        return;
    }

    vec4 clip = vec4(v_UV * 2.0 - 1.0, centerDepthRaw * 2.0 - 1.0, 1.0);
    vec4 vPos = u_InvProjection * clip;
    vPos /= max(abs(vPos.w), 1e-5);
    vec3 wPos = (u_InvView * vPos).xyz;

    // Si la geometria y la camara estan por debajo de la base de las nubes, descartar
    if (u_CameraPos.y < u_CloudBaseHeight && wPos.y < u_CloudBaseHeight) {
        FragColor = vec4(0.0);
        return;
    }

    vec4 centerCloud = texture(u_LowResClouds, v_UV);
    if (centerCloud.a <= 0.0001) {
        FragColor = vec4(0.0);
        return;
    }

    float centerDepthLinear = LinearizeDepth(centerDepthRaw);
    vec4 accumColor = vec4(0.0);
    float totalWeight = 0.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(float(x), float(y)) * u_LowResTexelSize;
            vec2 sampleUV = v_UV + offset;
            vec4 cloudSample = texture(u_LowResClouds, sampleUV);
            float sampleDepthRaw = texture(u_SceneDepth, sampleUV).r;
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
)";

        std::string vCode = upscaleVert;
        std::string fCode = upscaleFrag;
        if (SlipFileSystem::Exists("assets/shaders/CloudBilateralUpscale.vert")) {
            std::string fc;
            if (SlipFileSystem::ReadAllText("assets/shaders/CloudBilateralUpscale.vert", fc)) vCode = fc;
        }
        if (SlipFileSystem::Exists("assets/shaders/CloudBilateralUpscale.frag")) {
            std::string fc;
            if (SlipFileSystem::ReadAllText("assets/shaders/CloudBilateralUpscale.frag", fc)) fCode = fc;
        }

        SlipShader* s = SlipShader::CreateFromSource(vCode.c_str(), fCode.c_str());
        if (s) {
            s_Shaders["CloudBilateralUpscale"] = std::unique_ptr<SlipShader>(s);
        }
    }
#endif

}

void SlipShaderLibrary::Reset() {
    Clean();
}

SlipShader* SlipShaderLibrary::Get(const std::string& name) {
    if (!s_Initialized || s_Shaders.empty() || s_Shaders.find("Lit") == s_Shaders.end()) {
        Init();
    } else {
        // Comprobar si el contexto OpenGL sigue siendo válido.
        // En Android, al pausar o cambiar de app y volver, SDL/Android puede crear un nuevo contexto GL.
        // Si el programa almacenado ya no pertenece al contexto actual, glIsProgram devuelve GL_FALSE.
        auto itTest = s_Shaders.find("Lit");
        if (itTest != s_Shaders.end() && itTest->second && itTest->second->ID != 0) {
            if (!glIsProgram(itTest->second->ID)) {
                Clean();
                Init();
            }
        }
    }

    auto it = s_Shaders.find(name);
    if (it != s_Shaders.end()) {
        return it->second.get();
    }

    // Si busca "default", fallback a Lit
    if (name == "default" || name.empty()) {
        auto itLit = s_Shaders.find("Lit");
        return (itLit != s_Shaders.end()) ? itLit->second.get() : nullptr;
    }

    return nullptr;
}

SlipShader* SlipShaderLibrary::LoadOrGet(const std::string& name) {
    SlipShader* existing = Get(name);
    if (existing) return existing;

    // Intentar cargar desde archivo personalizado si existe
    std::string vert = "assets/shaders/" + name + ".vert";
    std::string frag = "assets/shaders/" + name + ".frag";
    if (SlipFileSystem::Exists(vert) && SlipFileSystem::Exists(frag)) {
        SlipShader* customShader = new SlipShader(vert.c_str(), frag.c_str());
        if (customShader && customShader->ID != 0) {
            s_Shaders[name] = std::unique_ptr<SlipShader>(customShader);
            return customShader;
        }
        delete customShader;
    }

    // Fallback a Lit
    return Get("Lit");
}

bool SlipShaderLibrary::HasShader(const std::string& name) {
    if (!s_Initialized) Init();
    return s_Shaders.find(name) != s_Shaders.end();
}

