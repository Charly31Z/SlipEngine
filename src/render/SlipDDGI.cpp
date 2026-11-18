#include "SlipEngine/render/SlipDDGI.h"

#include <iostream>

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)

#include "SlipEngine/render/SlipGL.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/render/SlipCascadedShadows.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipSkyComponent.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <numeric>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace {

constexpr int kCamX = 16;
constexpr int kCamY = 16;
constexpr int kCamZ = 16;
constexpr int kOctRes = 8;
constexpr int kProbeTexel = kOctRes + 2;
constexpr int kProbesPerRow = 32;
constexpr int kMaxProbes = 4096;
constexpr int kMaxAtlasW = kProbesPerRow * kProbeTexel;
constexpr int kMaxAtlasH = ((kMaxProbes + kProbesPerRow - 1) / kProbesPerRow) * kProbeTexel;
constexpr int kMaxTris = 16384;

struct GpuNode {
    glm::vec4 bmin;
    glm::vec4 bmax;
};

struct GpuTri {
    glm::vec4 p0;
    glm::vec4 p1;
    glm::vec4 p2;
    glm::vec4 nrm;
    glm::vec4 emis;
};

struct BuildItem {
    int tri = 0;
    glm::vec3 centroid{ 0.0f };
    glm::vec3 bmin{ 0.0f };
    glm::vec3 bmax{ 0.0f };
};

int ProbeIndex(int x, int y, int z, const glm::ivec3& counts) {
    return x + y * counts.x + z * counts.x * counts.y;
}

void TileOrigin(int index, int& x, int& y) {
    int px = index % kProbesPerRow;
    int py = index / kProbesPerRow;
    x = px * kProbeTexel;
    y = py * kProbeTexel;
}

void MixHash(uint32_t& h, uint32_t v) {
    h ^= v;
    h *= 16777619u;
}

void MixFloat(uint32_t& h, float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(float));
    MixHash(h, u);
}

void MixVec3(uint32_t& h, const glm::vec3& v) {
    MixFloat(h, v.x);
    MixFloat(h, v.y);
    MixFloat(h, v.z);
}

const uint16_t* ZeroRgbaTile() {
    static std::vector<uint16_t> data(static_cast<size_t>(kProbeTexel * kProbeTexel * 4), 0);
    return data.data();
}

const uint16_t* ZeroRgTile() {
    static std::vector<uint16_t> data(static_cast<size_t>(kProbeTexel * kProbeTexel * 2), 0);
    return data.data();
}

const uint16_t* ZeroRgbaAtlas() {
    static std::vector<uint16_t> data(static_cast<size_t>(kMaxAtlasW * kMaxAtlasH * 4), 0);
    return data.data();
}

const uint16_t* ZeroRgAtlas() {
    static std::vector<uint16_t> data(static_cast<size_t>(kMaxAtlasW * kMaxAtlasH * 2), 0);
    return data.data();
}

void UploadClear(unsigned int tex, bool rgba, int x, int y, int w, int h, const uint16_t* pixels) {
    GLint align = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
                    rgba ? GL_RGBA : GL_RG,
                    GL_HALF_FLOAT,
                    pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
}

void ClearFull(unsigned int irr, unsigned int vis, int width, int height) {
    UploadClear(irr, true, 0, 0, width, height, ZeroRgbaAtlas());
    UploadClear(vis, false, 0, 0, width, height, ZeroRgAtlas());
}

void ClearTile(unsigned int irr, unsigned int vis, int index) {
    int x = 0, y = 0;
    TileOrigin(index, x, y);
    UploadClear(irr, true, x, y, kProbeTexel, kProbeTexel, ZeroRgbaTile());
    UploadClear(vis, false, x, y, kProbeTexel, kProbeTexel, ZeroRgTile());
}

void CopyTile(unsigned int src, unsigned int dst, int srcIndex, int dstIndex) {
    int sx = 0, sy = 0, dx = 0, dy = 0;
    TileOrigin(srcIndex, sx, sy);
    TileOrigin(dstIndex, dx, dy);
    glCopyImageSubData(src, GL_TEXTURE_2D, 0, sx, sy, 0,
                       dst, GL_TEXTURE_2D, 0, dx, dy, 0,
                       kProbeTexel, kProbeTexel, 1);
}

unsigned int MakeAtlas(GLenum internalFormat, int width, int height) {
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

int BuildRange(std::vector<BuildItem>& items, std::vector<GpuNode>& nodes, int start, int count) {
    glm::vec3 bmin(1e9f);
    glm::vec3 bmax(-1e9f);
    for (int i = 0; i < count; ++i) {
        bmin = glm::min(bmin, items[start + i].bmin);
        bmax = glm::max(bmax, items[start + i].bmax);
    }
    int node = static_cast<int>(nodes.size());
    nodes.push_back(GpuNode{});
    if (count <= 1) {
        int tri = items[start].tri;
        nodes[node].bmin = glm::vec4(bmin, static_cast<float>(-(tri + 1)));
        nodes[node].bmax = glm::vec4(bmax, 0.0f);
        return node;
    }

    glm::vec3 extent = bmax - bmin;
    int axis = 0;
    if (extent.y > extent[axis]) axis = 1;
    if (extent.z > extent[axis]) axis = 2;
    int mid = count / 2;
    std::nth_element(items.begin() + start, items.begin() + start + mid, items.begin() + start + count,
                     [axis](const BuildItem& a, const BuildItem& b) {
                         return a.centroid[axis] < b.centroid[axis];
                     });
    int left = BuildRange(items, nodes, start, mid);
    int right = BuildRange(items, nodes, start + mid, count - mid);
    nodes[node].bmin = glm::vec4(bmin, static_cast<float>(left));
    nodes[node].bmax = glm::vec4(bmax, static_cast<float>(right));
    return node;
}

const char* kComputeSrc = R"GLSL(
#version 430
layout(local_size_x = 64) in;

struct BVHNode { vec4 bmin; vec4 bmax; };
struct Tri { vec4 p0; vec4 p1; vec4 p2; vec4 nrm; vec4 emis; };

layout(std430, binding = 0) readonly buffer NodeBuffer { BVHNode nodes[]; };
layout(std430, binding = 1) readonly buffer TriBuffer { Tri tris[]; };

layout(rgba16f) uniform image2D u_IrrImg;
layout(rg16f) uniform image2D u_VisImg;
uniform sampler2D u_ReadIrr;
uniform sampler2D u_ReadVis;
uniform sampler2DArray u_ShadowMap;

uniform int u_RayCount;
uniform ivec3 u_Counts;
uniform int u_ProbesPerRow;
uniform vec2 u_AtlasSize;
struct ProbeTask {
    int probeId;
    int isNew;
};
layout(std430, binding = 2) readonly buffer TaskBuffer {
    ProbeTask u_Tasks[];
};
uniform int u_NodeCount;
uniform int u_TriCount;
uniform float u_Hysteresis;
uniform vec3 u_Spacing;
uniform vec3 u_Origin;
uniform vec3 u_SkyColor;
uniform int u_ShadowsEnabled;
uniform mat4 u_View;
uniform mat4 u_LightSpaceMatrices[4];
uniform float u_CascadeEndClipSpace[4];
uniform int u_CascadeCount;
uniform float u_ShadowBias;
uniform int u_PCFQuality;

uniform int u_NumDir;
uniform vec3 u_DirDir[4];
uniform vec3 u_DirColor[4];
uniform float u_DirIntensity[4];
uniform int u_NumPoint;
uniform vec3 u_PointPos[8];
uniform vec3 u_PointColor[8];
uniform float u_PointIntensity[8];
uniform float u_PointRange[8];
uniform int u_NumSpot;
uniform vec3 u_SpotPos[4];
uniform vec3 u_SpotDir[4];
uniform vec3 u_SpotColor[4];
uniform float u_SpotIntensity[4];
uniform float u_SpotRange[4];
uniform float u_SpotAngle[4];

shared vec4 s_rad[64];
shared vec4 s_dir[64];

vec3 OctDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        vec2 s = sign(n.xy);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

vec2 OctEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 1e-6);
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    return n.xy * 0.5 + 0.5;
}

float ShadowCSM(vec3 worldPos, vec3 normal, vec3 lightDir) {
    if (u_ShadowsEnabled == 0 || u_CascadeCount <= 0) return 0.0;
    vec4 fragPosView = u_View * vec4(worldPos, 1.0);
    float depthValue = abs(fragPosView.z);
    int layer = -1;
    for (int c = 0; c < u_CascadeCount && c < 4; ++c) {
        if (depthValue < u_CascadeEndClipSpace[c]) { layer = c; break; }
    }
    if (layer < 0) return 0.0;
    vec4 lp = u_LightSpaceMatrices[layer] * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;
    float bias = max(u_ShadowBias * (1.0 - max(dot(normal, lightDir), 0.0)), u_ShadowBias * 0.2);
    vec2 texel = 1.0 / vec2(textureSize(u_ShadowMap, 0).xy);
    float shadow = 0.0;
    if (u_PCFQuality <= 0) {
        float d = texture(u_ShadowMap, vec3(proj.xy, float(layer))).r;
        shadow = (proj.z - bias > d) ? 1.0 : 0.0;
    } else {
        int radius = (u_PCFQuality == 1) ? 1 : 2;
        int taps = 0;
        for (int x = -radius; x <= radius; ++x) {
            for (int y = -radius; y <= radius; ++y) {
                float d = texture(u_ShadowMap, vec3(proj.xy + vec2(x, y) * texel, float(layer))).r;
                shadow += (proj.z - bias > d) ? 1.0 : 0.0;
                taps++;
            }
        }
        shadow /= float(taps);
    }
    return shadow;
}

vec3 SampleRead(vec3 worldPos, vec3 normal) {
    float spacingMin = min(u_Spacing.x, min(u_Spacing.y, u_Spacing.z));
    vec3 biased = worldPos + normal * (spacingMin * 0.25);
    vec3 grid = (biased - u_Origin) / u_Spacing - vec3(0.5);
    vec3 baseF = floor(grid);
    vec3 alpha = clamp(grid - baseF, vec3(0.0), vec3(1.0));
    ivec3 base = ivec3(baseF);
    ivec3 maxIdx = max(u_Counts - ivec3(1), ivec3(0));
    vec3 irr = vec3(0.0);
    float wSum = 0.0;
    for (int i = 0; i < 8; ++i) {
        ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 idx = clamp(base + offset, ivec3(0), maxIdx);
        vec3 probePos = u_Origin + (vec3(idx) + 0.5) * u_Spacing;
        vec3 toPoint = biased - probePos;
        float dist = length(toPoint);
        vec3 dir = toPoint / max(dist, 1e-4);
        vec3 triW = mix(vec3(1.0) - alpha, alpha, vec3(offset));
        float w = triW.x * triW.y * triW.z;
        float wrap = clamp(dot(-dir, normal) * 0.5 + 0.5, 0.0, 1.0);
        wrap = wrap * wrap;
        int probeIndex = idx.x + idx.y * u_Counts.x + idx.z * u_Counts.x * u_Counts.y;
        ivec2 probeCoord = ivec2(probeIndex % u_ProbesPerRow, probeIndex / u_ProbesPerRow);
        vec2 uvIrr = (vec2(probeCoord * 10) + vec2(1.0) + OctEncode(normal) * 8.0) / u_AtlasSize;
        vec4 probeTex = texture(u_ReadIrr, uvIrr);
        if (probeTex.a < 0.04) continue;
        w *= probeTex.a;
        vec3 probeIrr = probeTex.rgb;
        vec2 uvVis = (vec2(probeCoord * 10) + vec2(1.0) + OctEncode(dir) * 8.0) / u_AtlasSize;
        vec2 moments = texture(u_ReadVis, uvVis).rg;
        float mean = moments.r + spacingMin * 0.08;
        float variance = abs(moments.g - moments.r * moments.r);
        float cheb = 1.0;
        if (dist > mean) {
            float d = dist - mean;
            cheb = variance / (variance + d * d + 1e-4);
            cheb = cheb * cheb * cheb;
        }
        w *= wrap * cheb;
        irr += probeIrr * w;
        wSum += w;
    }
    if (wSum < 1e-4) return vec3(0.04);
    return irr / wSum;
}

vec3 DirectIrradiance(vec3 pos, vec3 N) {
    vec3 E = vec3(0.0);
    for (int i = 0; i < u_NumDir && i < 4; ++i) {
        vec3 L = normalize(-u_DirDir[i]);
        float ndl = max(dot(N, L), 0.0);
        float shadow = (i == 0) ? ShadowCSM(pos, N, L) : 0.0;
        E += u_DirColor[i] * u_DirIntensity[i] * ndl * (1.0 - shadow);
    }
    for (int i = 0; i < u_NumPoint && i < 8; ++i) {
        vec3 lightDir = u_PointPos[i] - pos;
        float dist = length(lightDir);
        if (u_PointRange[i] > 0.0 && dist > u_PointRange[i]) continue;
        vec3 L = lightDir / max(dist, 1e-4);
        float att = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        if (u_PointRange[i] > 0.0) att *= clamp(1.0 - dist / u_PointRange[i], 0.0, 1.0);
        E += u_PointColor[i] * u_PointIntensity[i] * att * max(dot(N, L), 0.0);
    }
    for (int i = 0; i < u_NumSpot && i < 4; ++i) {
        vec3 lightDir = u_SpotPos[i] - pos;
        float dist = length(lightDir);
        if (u_SpotRange[i] > 0.0 && dist > u_SpotRange[i]) continue;
        vec3 L = lightDir / max(dist, 1e-4);
        float theta = dot(L, normalize(-u_SpotDir[i]));
        float innerCut = cos(radians(u_SpotAngle[i] * 0.5));
        float outerCut = cos(radians(u_SpotAngle[i] * 0.65));
        float spot = clamp((theta - outerCut) / max(innerCut - outerCut, 1e-4), 0.0, 1.0);
        if (spot <= 0.0) continue;
        float att = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        E += u_SpotColor[i] * u_SpotIntensity[i] * att * spot * max(dot(N, L), 0.0);
    }
    return E;
}

bool RayBox(vec3 ro, vec3 invRd, vec3 bmin, vec3 bmax, float tMax, out float tEnter) {
    vec3 t0 = (bmin - ro) * invRd;
    vec3 t1 = (bmax - ro) * invRd;
    vec3 tsm = min(t0, t1);
    vec3 tbg = max(t0, t1);
    float tmin = max(max(tsm.x, tsm.y), tsm.z);
    float tmax = min(min(tbg.x, tbg.y), tbg.z);
    tEnter = tmin;
    return tmax >= max(tmin, 0.0) && tmin < tMax;
}

bool RayTri(vec3 ro, vec3 rd, vec3 a, vec3 b, vec3 c, float tMax, out float t, out vec3 n) {
    vec3 e1 = b - a;
    vec3 e2 = c - a;
    vec3 p = cross(rd, e2);
    float det = dot(e1, p);
    if (abs(det) < 1e-7) return false;
    float inv = 1.0 / det;
    vec3 s = ro - a;
    float u = dot(s, p) * inv;
    if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(s, e1);
    float v = dot(rd, q) * inv;
    if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, q) * inv;
    if (t < 0.03 || t > tMax) return false;
    n = normalize(cross(e1, e2));
    return true;
}

void Trace(vec3 ro, vec3 rd, out bool hit, out float tHit, out vec3 nHit, out vec3 albedo, out vec3 emissive, out bool inside) {
    hit = false;
    inside = false;
    tHit = 100.0;
    nHit = vec3(0.0, 1.0, 0.0);
    albedo = vec3(0.8);
    emissive = vec3(0.0);
    if (u_TriCount <= 0 || u_NodeCount <= 0) return;
    vec3 invRd = vec3(
        abs(rd.x) > 1e-8 ? 1.0 / rd.x : (1e8),
        abs(rd.y) > 1e-8 ? 1.0 / rd.y : (1e8),
        abs(rd.z) > 1e-8 ? 1.0 / rd.z : (1e8));
    if (rd.x < 0.0) invRd.x = -abs(invRd.x);
    if (rd.y < 0.0) invRd.y = -abs(invRd.y);
    if (rd.z < 0.0) invRd.z = -abs(invRd.z);

    int stack[16];
    int sp = 0;
    stack[0] = 0;
    sp = 1;
    while (sp > 0) {
        sp--;
        int ni = stack[clamp(sp, 0, 15)];
        if (ni < 0 || ni >= u_NodeCount) continue;
        float tBox;
        if (!RayBox(ro, invRd, nodes[ni].bmin.xyz, nodes[ni].bmax.xyz, tHit, tBox)) continue;
        int left = int(round(nodes[ni].bmin.w));
        if (left < 0) {
            int tri = -left - 1;
            if (tri < 0 || tri >= u_TriCount) continue;
            float t;
            vec3 n;
            if (RayTri(ro, rd, tris[tri].p0.xyz, tris[tri].p1.xyz, tris[tri].p2.xyz, tHit, t, n)) {
                tHit = t;
                nHit = n;
                albedo = vec3(tris[tri].p0.w, tris[tri].p1.w, tris[tri].p2.w);
                emissive = tris[tri].emis.rgb;
                hit = true;
            }
        } else if (sp < 14) {
            int right = int(round(nodes[ni].bmax.w));
            stack[clamp(sp, 0, 15)] = right;
            sp++;
            stack[clamp(sp, 0, 15)] = left;
            sp++;
        }
    }
    if (hit && dot(nHit, rd) > 0.0) {
        if (tHit < min(u_Spacing.x, min(u_Spacing.y, u_Spacing.z)) * 0.45) inside = true;
        nHit = -nHit;
    }
}

void main() {
    uint ray = gl_LocalInvocationIndex;
    int probeIndex = u_Tasks[int(gl_WorkGroupID.x)].probeId;
    bool activeProbe = probeIndex >= 0;
    int safeIndex = activeProbe ? probeIndex : 0;
    int x = safeIndex % u_Counts.x;
    int y = (safeIndex / u_Counts.x) % u_Counts.y;
    int z = safeIndex / (u_Counts.x * u_Counts.y);
    vec3 probePos = u_Origin + (vec3(float(x), float(y), float(z)) + 0.5) * u_Spacing;
    ivec2 probeOrigin = ivec2((safeIndex % u_ProbesPerRow) * 10, (safeIndex / u_ProbesPerRow) * 10);

    vec3 rad = u_SkyColor;
    vec3 dir = vec3(0.0, 1.0, 0.0);
    float dist = 80.0;
    float valid = 0.0;
    if (activeProbe && int(ray) < u_RayCount) {
        float nRays = float(u_RayCount);
        float fi = float(ray);
        float py = 1.0 - (fi + 0.5) / nRays * 2.0;
        float radius = sqrt(max(0.0, 1.0 - py * py));
        float theta = 2.39996323 * fi + float(safeIndex) * 0.37;
        dir = normalize(vec3(cos(theta) * radius, py, sin(theta) * radius));
        bool hit, inside;
        float tHit;
        vec3 nHit, albedo, emissive;
        Trace(probePos, dir, hit, tHit, nHit, albedo, emissive, inside);
        if (inside) {
            valid = -1.0;
            rad = vec3(0.0);
            dist = 0.0;
        } else if (hit) {
            vec3 hitPos = probePos + dir * tHit;
            vec3 E = DirectIrradiance(hitPos, nHit) + SampleRead(hitPos, nHit);
            rad = min(albedo * E + emissive, vec3(8.0));
            dist = tHit;
            valid = 1.0;
        } else {
            rad = u_SkyColor;
            dist = 80.0;
            valid = 1.0;
        }
    }
    s_rad[ray] = vec4(rad, valid);
    s_dir[ray] = vec4(dir, dist);
    barrier();

    if (int(ray) < 64) {
        int tx = int(ray) % 8;
        int ty = int(ray) / 8;
        vec3 texelDir = OctDecode((vec2(tx, ty) + 0.5) / 8.0);
        vec3 sumR = vec3(0.0);
        float sumW = 0.0;
        float sumD = 0.0;
        float sumD2 = 0.0;
        for (int r = 0; r < u_RayCount && r < 64; ++r) {
            if (s_rad[r].w < 0.0) continue;
            float w = max(dot(texelDir, s_dir[r].xyz), 0.0);
            w = w * w;
            sumR += s_rad[r].rgb * w;
            sumD += s_dir[r].w * w;
            sumD2 += s_dir[r].w * s_dir[r].w * w;
            sumW += w;
        }
        ivec2 coord = probeOrigin + ivec2(1 + tx, 1 + ty);
        int insideCount = 0;
        for (int r2 = 0; r2 < u_RayCount && r2 < 64; ++r2) {
            if (s_rad[r2].w < 0.0) insideCount++;
        }
        bool buried = insideCount * 2 > u_RayCount;
        float h = (u_Tasks[int(gl_WorkGroupID.x)].isNew != 0) ? 0.0 : u_Hysteresis;
        if (activeProbe && buried) {
            imageStore(u_IrrImg, coord, vec4(0.0));
            imageStore(u_VisImg, coord, vec4(0.0));
        } else if (activeProbe && sumW > 1e-4) {
            vec3 newIrr = min(sumR / sumW, vec3(8.0));
            float newMean = sumD / sumW;
            float newMean2 = sumD2 / sumW;
            vec3 oldIrr = imageLoad(u_IrrImg, coord).rgb;
            vec2 oldVis = imageLoad(u_VisImg, coord).rg;
            float oldA = imageLoad(u_IrrImg, coord).a;
            if (dot(oldIrr, vec3(1.0)) < 0.001) h = 0.0;
            else h = max(h, 0.72);
            float fadeIn = min(1.0, max(oldA, 0.0) + 0.18);
            imageStore(u_IrrImg, coord, vec4(mix(newIrr, oldIrr, h), fadeIn));
            imageStore(u_VisImg, coord, vec4(mix(newMean, oldVis.r, h), mix(newMean2, oldVis.g, h), 0.0, 0.0));
        }
    }
    memoryBarrierImage();
    barrier();

    if (activeProbe && ray == 0u) {
        int res = 8;
        for (int i = 1; i <= res; ++i) {
            imageStore(u_IrrImg, probeOrigin + ivec2(i, 0), imageLoad(u_IrrImg, probeOrigin + ivec2(res + 1 - i, 1)));
            imageStore(u_VisImg, probeOrigin + ivec2(i, 0), imageLoad(u_VisImg, probeOrigin + ivec2(res + 1 - i, 1)));
            imageStore(u_IrrImg, probeOrigin + ivec2(i, res + 1), imageLoad(u_IrrImg, probeOrigin + ivec2(res + 1 - i, res)));
            imageStore(u_VisImg, probeOrigin + ivec2(i, res + 1), imageLoad(u_VisImg, probeOrigin + ivec2(res + 1 - i, res)));
            imageStore(u_IrrImg, probeOrigin + ivec2(0, i), imageLoad(u_IrrImg, probeOrigin + ivec2(1, res + 1 - i)));
            imageStore(u_VisImg, probeOrigin + ivec2(0, i), imageLoad(u_VisImg, probeOrigin + ivec2(1, res + 1 - i)));
            imageStore(u_IrrImg, probeOrigin + ivec2(res + 1, i), imageLoad(u_IrrImg, probeOrigin + ivec2(res, res + 1 - i)));
            imageStore(u_VisImg, probeOrigin + ivec2(res + 1, i), imageLoad(u_VisImg, probeOrigin + ivec2(res, res + 1 - i)));
        }
        imageStore(u_IrrImg, probeOrigin + ivec2(0, 0), imageLoad(u_IrrImg, probeOrigin + ivec2(res, res)));
        imageStore(u_VisImg, probeOrigin + ivec2(0, 0), imageLoad(u_VisImg, probeOrigin + ivec2(res, res)));
        imageStore(u_IrrImg, probeOrigin + ivec2(res + 1, 0), imageLoad(u_IrrImg, probeOrigin + ivec2(1, res)));
        imageStore(u_VisImg, probeOrigin + ivec2(res + 1, 0), imageLoad(u_VisImg, probeOrigin + ivec2(1, res)));
        imageStore(u_IrrImg, probeOrigin + ivec2(0, res + 1), imageLoad(u_IrrImg, probeOrigin + ivec2(res, 1)));
        imageStore(u_VisImg, probeOrigin + ivec2(0, res + 1), imageLoad(u_VisImg, probeOrigin + ivec2(res, 1)));
        imageStore(u_IrrImg, probeOrigin + ivec2(res + 1, res + 1), imageLoad(u_IrrImg, probeOrigin + ivec2(1, 1)));
        imageStore(u_VisImg, probeOrigin + ivec2(res + 1, res + 1), imageLoad(u_VisImg, probeOrigin + ivec2(1, 1)));
    }
}
)GLSL";

int CompileCompute() {
    unsigned int shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &kComputeSrc, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[SlipDDGI] Compute shader: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    unsigned int program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[SlipDDGI] Link compute: " << log << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    return static_cast<int>(program);
}

const char* kScrollComputeSrc = R"GLSL(
#version 430
layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform writeonly image2D u_DstIrr;
layout(rg16f, binding = 1) uniform writeonly image2D u_DstVis;
uniform sampler2D u_SrcIrr;
uniform sampler2D u_SrcVis;

uniform ivec3 u_Delta;
uniform ivec3 u_Counts;
uniform int u_ProbesPerRow;
uniform int u_ProbeTexel;

void main() {
    ivec2 dstCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_DstIrr);
    if (dstCoord.x >= size.x || dstCoord.y >= size.y) return;

    int probeX = dstCoord.x / u_ProbeTexel;
    int probeY = dstCoord.y / u_ProbeTexel;
    int dstProbe = probeX + probeY * u_ProbesPerRow;

    int totalProbes = u_Counts.x * u_Counts.y * u_Counts.z;
    if (dstProbe >= totalProbes) {
        imageStore(u_DstIrr, dstCoord, vec4(0.0));
        imageStore(u_DstVis, dstCoord, vec4(0.0));
        return;
    }

    int xy = u_Counts.x * u_Counts.y;
    int z = dstProbe / xy;
    int rem = dstProbe % xy;
    int y = rem / u_Counts.x;
    int x = rem % u_Counts.x;

    int sx = x + u_Delta.x;
    int sy = y + u_Delta.y;
    int sz = z + u_Delta.z;

    if (sx >= 0 && sy >= 0 && sz >= 0 && sx < u_Counts.x && sy < u_Counts.y && sz < u_Counts.z) {
        int srcProbe = sx + sy * u_Counts.x + sz * xy;
        ivec2 localOffset = dstCoord % u_ProbeTexel;
        int srcPx = (srcProbe % u_ProbesPerRow) * u_ProbeTexel + localOffset.x;
        int srcPy = (srcProbe / u_ProbesPerRow) * u_ProbeTexel + localOffset.y;
        vec4 irrVal = texelFetch(u_SrcIrr, ivec2(srcPx, srcPy), 0);
        vec4 visVal = texelFetch(u_SrcVis, ivec2(srcPx, srcPy), 0);
        imageStore(u_DstIrr, dstCoord, irrVal);
        imageStore(u_DstVis, dstCoord, visVal);
    } else {
        imageStore(u_DstIrr, dstCoord, vec4(0.0));
        imageStore(u_DstVis, dstCoord, vec4(0.0));
    }
}
)GLSL";

int CompileScrollCompute() {
    unsigned int shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &kScrollComputeSrc, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[SlipDDGI] Scroll compute shader: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    unsigned int program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[SlipDDGI] Link scroll compute: " << log << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    return static_cast<int>(program);
}

void AddTriangle(std::vector<GpuTri>& tris, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& albedo, const glm::vec3& emissive) {
    glm::vec3 n = glm::cross(b - a, c - a);
    if (glm::dot(n, n) < 1e-12f) return;
    if (static_cast<int>(tris.size()) >= kMaxTris) return;
    n = glm::normalize(n);
    GpuTri tri;
    tri.p0 = glm::vec4(a, albedo.r);
    tri.p1 = glm::vec4(b, albedo.g);
    tri.p2 = glm::vec4(c, albedo.b);
    tri.nrm = glm::vec4(n, 0.0f);
    tri.emis = glm::vec4(emissive, 0.0f);
    tris.push_back(tri);
}

void AddAabbProxy(std::vector<GpuTri>& tris, const glm::mat4& model, const glm::vec3& bmin, const glm::vec3& bmax, const glm::vec3& albedo, const glm::vec3& emissive) {
    glm::vec3 p[8] = {
        glm::vec3(model * glm::vec4(bmin.x, bmin.y, bmin.z, 1.0f)),
        glm::vec3(model * glm::vec4(bmax.x, bmin.y, bmin.z, 1.0f)),
        glm::vec3(model * glm::vec4(bmax.x, bmax.y, bmin.z, 1.0f)),
        glm::vec3(model * glm::vec4(bmin.x, bmax.y, bmin.z, 1.0f)),
        glm::vec3(model * glm::vec4(bmin.x, bmin.y, bmax.z, 1.0f)),
        glm::vec3(model * glm::vec4(bmax.x, bmin.y, bmax.z, 1.0f)),
        glm::vec3(model * glm::vec4(bmax.x, bmax.y, bmax.z, 1.0f)),
        glm::vec3(model * glm::vec4(bmin.x, bmax.y, bmax.z, 1.0f))
    };
    const int faces[12][3] = {
        {0, 2, 1}, {0, 3, 2}, // -Z
        {4, 5, 6}, {4, 6, 7}, // +Z
        {0, 1, 5}, {0, 5, 4}, // -Y
        {3, 6, 2}, {3, 7, 6}, // +Y
        {0, 7, 3}, {0, 4, 7}, // -X
        {1, 2, 6}, {1, 6, 5}  // +X
    };
    for (int f = 0; f < 12; ++f) {
        AddTriangle(tris, p[faces[f][0]], p[faces[f][1]], p[faces[f][2]], albedo, emissive);
    }
}

void AddIndexedMesh(std::vector<GpuTri>& tris, const glm::mat4& model, const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, const glm::vec3& albedo, const glm::vec3& emissive) {
    if (vertices.empty() || indices.size() < 3) return;
    size_t totalTris = indices.size() / 3;
    constexpr size_t kMaxSubmeshTrisForDDGI = 256;
    size_t triStride = 1;
    if (totalTris > kMaxSubmeshTrisForDDGI) {
        triStride = (totalTris + kMaxSubmeshTrisForDDGI - 1) / kMaxSubmeshTrisForDDGI;
    }
    size_t stepIndex = triStride * 3;
    for (size_t i = 0; i + 2 < indices.size() && static_cast<int>(tris.size()) < kMaxTris; i += stepIndex) {
        unsigned int i0 = indices[i];
        unsigned int i1 = indices[i + 1];
        unsigned int i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;
        glm::vec3 a = glm::vec3(model * glm::vec4(vertices[i0].Position, 1.0f));
        glm::vec3 b = glm::vec3(model * glm::vec4(vertices[i1].Position, 1.0f));
        glm::vec3 c = glm::vec3(model * glm::vec4(vertices[i2].Position, 1.0f));
        AddTriangle(tris, a, b, c, albedo, emissive);
    }
}

glm::ivec3 ClampProbeCounts(glm::ivec3 c) {
    c = glm::clamp(c, glm::ivec3(1), glm::ivec3(32));
    while (c.x * c.y * c.z > kMaxProbes) {
        if (c.x >= c.y && c.x >= c.z && c.x > 1) --c.x;
        else if (c.y >= c.z && c.y > 1) --c.y;
        else if (c.z > 1) --c.z;
        else break;
    }
    return c;
}

} // namespace

#endif

SlipDDGI& SlipDDGI::Get() {
    static SlipDDGI instance;
    return instance;
}

SlipDDGI::~SlipDDGI() {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    DestroyGpu();
#endif
}

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)

bool SlipDDGI::IsActive() const { return false; }

void SlipDDGI::Update(const glm::vec3&, const glm::mat4&, SlipLevel*,
                      const DDGIDirectionalLight*, int,
                      const DDGIPointLight*, int,
                      const DDGISpotLight*, int,
                      bool,
                      bool) {}

void SlipDDGI::Bind(SlipShader*) {}

void SlipDDGI::DrawDebug(const glm::mat4&, const glm::mat4&) {}

void SlipDDGI::DrawVolumeProbes(const glm::mat4&, const glm::mat4&, const glm::vec3&, const glm::vec3&, const glm::ivec3&) {}

#else

bool SlipDDGI::IsActive() const {
    const auto& settings = SlipGraphicsSettings::Get().GetData();
    return m_Supported && m_Ready && settings.ddgiEnabled;
}

bool SlipDDGI::EnsureGpu() {
    if (m_Attempted) return m_Supported;
    m_Attempted = true;

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 4 || (major == 4 && minor < 3) || !GLAD_GL_VERSION_4_3) {
        std::cerr << "[SlipDDGI] Se requiere OpenGL 4.3. Se mantiene el ambiente constante." << std::endl;
        m_Supported = false;
        return false;
    }

    m_Program = static_cast<unsigned int>(CompileCompute());
    if (m_Program == 0) {
        m_Supported = false;
        return false;
    }
    m_ScrollProgram = static_cast<unsigned int>(CompileScrollCompute());

    const char* debugVert =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec3 aColor;\n"
        "uniform mat4 u_View;\n"
        "uniform mat4 u_Projection;\n"
        "out vec3 vColor;\n"
        "void main(){ vColor=aColor; gl_Position=u_Projection*u_View*vec4(aPos,1.0); }\n";
    const char* debugFrag =
        "#version 330 core\n"
        "in vec3 vColor;\n"
        "out vec4 FragColor;\n"
        "void main(){ FragColor=vec4(vColor,1.0); }\n";
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &debugVert, nullptr);
    glShaderSource(fs, 1, &debugFrag, nullptr);
    glCompileShader(vs);
    glCompileShader(fs);
    m_DebugProgram = glCreateProgram();
    glAttachShader(m_DebugProgram, vs);
    glAttachShader(m_DebugProgram, fs);
    glLinkProgram(m_DebugProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenBuffers(1, &m_NodeSSBO);
    glGenBuffers(1, &m_TriSSBO);
    glGenBuffers(1, &m_TaskSSBO);
    GpuNode dummy{};
    dummy.bmin = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
    dummy.bmax = glm::vec4(0.0f);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_NodeSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuNode), &dummy, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_TriSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuTri), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    m_NodeCount = 1;
    m_TriCount = 0;

    m_Supported = true;
    std::cout << "[SlipDDGI] Volumen de sondas activo (OpenGL " << major << "." << minor << ")." << std::endl;
    return true;
}

void SlipDDGI::DestroyGpu() {
    if (m_Program != 0) {
        glDeleteProgram(m_Program);
        m_Program = 0;
    }
    if (m_ScrollProgram != 0) {
        glDeleteProgram(m_ScrollProgram);
        m_ScrollProgram = 0;
    }
    if (m_DebugProgram != 0) {
        glDeleteProgram(m_DebugProgram);
        m_DebugProgram = 0;
    }
    for (auto& volume : m_Volumes) DestroyVolume(volume);
    if (m_NodeSSBO != 0) glDeleteBuffers(1, &m_NodeSSBO);
    if (m_TriSSBO != 0) glDeleteBuffers(1, &m_TriSSBO);
    if (m_TaskSSBO != 0) glDeleteBuffers(1, &m_TaskSSBO);
    if (m_DebugVAO != 0) glDeleteVertexArrays(1, &m_DebugVAO);
    if (m_DebugVBO != 0) glDeleteBuffers(1, &m_DebugVBO);
    m_NodeSSBO = 0;
    m_TriSSBO = 0;
    m_TaskSSBO = 0;
    m_DebugVAO = 0;
    m_DebugVBO = 0;
    m_Supported = false;
    m_Ready = false;
    m_Attempted = false;
}

void SlipDDGI::RebuildScene(SlipLevel* level) {
    uint32_t hash = 2166136261u;
    MixHash(hash, level ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(level) & 0xffffffffu) : 0u);

    // 1. Paso rápido O(N_entidades): calcular hash ligero ÚNICAMENTE para entidades con geometría visible
    // (Ignorar luces, cámaras, volúmenes de post-proceso, etc. para que manipular el gizmo de una luz no reconstruya el BVH)
    if (level) {
        const auto& entities = level->GetEntities();
        MixHash(hash, static_cast<uint32_t>(entities.size()));
        for (SlipEntity* entity : entities) {
            if (!entity) continue;

            auto meshComp = entity->GetComponent<MeshComponent>();
            auto* prim = dynamic_cast<SlipPrimitiveEntity*>(entity);
            if (!meshComp && !prim) continue;
            if (meshComp && (!meshComp->IsEnabled() || !meshComp->model)) continue;

            // Si el motor está en simulación física (Play Mode) y la entidad es dinámica,
            // no rastrear su movimiento frame a frame en el BVH masivo de DDGI para garantizar 60 FPS estables.
            bool isPlaying = SlipPhysics::IsInitialized() && SlipPhysics::Get().IsSimulating();
            if (isPlaying && !entity->IsStatic()) {
                continue;
            }

            MixVec3(hash, entity->position);
            MixVec3(hash, entity->rotation);
            MixVec3(hash, entity->scale);

            if (meshComp && meshComp->model) {
                MixHash(hash, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(meshComp->model) & 0xffffffffu));
                MixHash(hash, static_cast<uint32_t>(meshComp->model->meshes.size()));
                size_t slotCount = meshComp->GetMaterialSlotCount();
                MixHash(hash, static_cast<uint32_t>(slotCount));
                for (size_t s = 0; s < slotCount; ++s) {
                    SlipMaterial* subMat = meshComp->GetSubmeshMaterial(s);
                    MixHash(hash, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(subMat) & 0xffffffffu));
                    if (subMat) {
                        glm::vec3 alb = subMat->color;
                        if (subMat->diffuseTexture) alb *= subMat->diffuseTexture->averageColor;
                        MixVec3(hash, alb);
                        MixVec3(hash, subMat->emissiveColor * subMat->emissiveIntensity);
                    }
                }
                if (!meshComp->model->meshes.empty()) {
                    MixHash(hash, static_cast<uint32_t>(meshComp->model->meshes[0].indices.size()));
                    MixVec3(hash, meshComp->model->aabbMin);
                    MixVec3(hash, meshComp->model->aabbMax);
                }
            } else if (prim) {
                MixHash(hash, static_cast<uint32_t>(prim->primitiveType));
                MixVec3(hash, glm::vec3(prim->color));
            }
        }
    }

    if (hash == m_SceneHash && level == m_LastLevel) return;
    m_SceneHash = hash;
    m_LastLevel = level;

    // 2. Solo si cambió la escena o alguna transformación, construir la lista de triángulos y BVH
    std::vector<GpuTri> tris;
    tris.reserve(1024);

    if (level) {
        bool isPlaying = SlipPhysics::IsInitialized() && SlipPhysics::Get().IsSimulating();
        int aabbProxiesAdded = 0;
        for (SlipEntity* entity : level->GetEntities()) {
            if (!entity) continue;
            if (isPlaying && !entity->IsStatic()) continue;
            glm::mat4 model = entity->getMatrix();

            auto meshComp = entity->GetComponent<MeshComponent>();
            if (meshComp && meshComp->IsEnabled() && meshComp->model) {
                for (const auto& mesh : meshComp->model->meshes) {
                    size_t slotIdx = (mesh.materialIndex >= 0) ? static_cast<size_t>(mesh.materialIndex) : 0;
                    SlipMaterial* subMat = meshComp->GetSubmeshMaterial(slotIdx);
                    glm::vec3 meshAlbedo = glm::vec3(0.75f);
                    glm::vec3 meshEmission = glm::vec3(0.0f);
                    if (subMat) {
                        meshAlbedo = subMat->color;
                        if (subMat->diffuseTexture != nullptr) {
                            meshAlbedo *= subMat->diffuseTexture->averageColor;
                        }
                        meshEmission = subMat->emissiveColor * subMat->emissiveIntensity;
                    }
                    AddIndexedMesh(tris, model, mesh.vertices, mesh.indices, meshAlbedo, meshEmission);
                }
            } else if (auto* prim = dynamic_cast<SlipPrimitiveEntity*>(entity)) {
                std::vector<Vertex> vertices;
                std::vector<unsigned int> indices;
                switch (prim->primitiveType) {
                    case PrimitiveType::Cube: SlipPrimitive::GenerateCube(1.0f, vertices, indices); break;
                    case PrimitiveType::Sphere: SlipPrimitive::GenerateSphere(0.5f, 12, 12, vertices, indices); break;
                    case PrimitiveType::Plane: SlipPrimitive::GeneratePlane(4.0f, 4.0f, vertices, indices); break;
                }
                AddIndexedMesh(tris, model, vertices, indices, glm::vec3(prim->color), glm::vec3(0.0f));
            }
        }
    }

    std::vector<GpuNode> nodes;
    if (tris.empty()) {
        GpuNode dummy{};
        dummy.bmin = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
        dummy.bmax = glm::vec4(0.0f);
        nodes.push_back(dummy);
    } else {
        std::vector<BuildItem> items(tris.size());
        for (int i = 0; i < static_cast<int>(tris.size()); ++i) {
            glm::vec3 a = glm::vec3(tris[i].p0);
            glm::vec3 b = glm::vec3(tris[i].p1);
            glm::vec3 c = glm::vec3(tris[i].p2);
            items[i].tri = i;
            items[i].centroid = (a + b + c) / 3.0f;
            items[i].bmin = glm::min(a, glm::min(b, c)) - glm::vec3(1e-4f);
            items[i].bmax = glm::max(a, glm::max(b, c)) + glm::vec3(1e-4f);
        }
        nodes.reserve(tris.size() * 2);
        BuildRange(items, nodes, 0, static_cast<int>(items.size()));
    }

    m_TriCount = static_cast<int>(tris.size());
    m_NodeCount = static_cast<int>(nodes.size());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_NodeSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, nodes.size() * sizeof(GpuNode), nodes.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_TriSSBO);
    if (tris.empty()) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GpuTri), nullptr, GL_DYNAMIC_DRAW);
    } else {
        glBufferData(GL_SHADER_STORAGE_BUFFER, tris.size() * sizeof(GpuTri), tris.data(), GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void SlipDDGI::DestroyVolume(VolumeGPU& volume) {
    for (int i = 0; i < 2; ++i) {
        if (volume.irr[i] != 0) glDeleteTextures(1, &volume.irr[i]);
        if (volume.vis[i] != 0) glDeleteTextures(1, &volume.vis[i]);
        volume.irr[i] = 0;
        volume.vis[i] = 0;
    }
    volume.atlasW = 0;
    volume.atlasH = 0;
    volume.hasBase = false;
    volume.pendingCount = 0;
    volume.cursor = 0;
    volume.used = false;
}

void SlipDDGI::EnsureVolume(VolumeGPU& volume, const glm::ivec3& counts) {
    glm::ivec3 c = ClampProbeCounts(counts);
    if (volume.irr[0] != 0 && volume.counts == c) return;
    DestroyVolume(volume);
    volume.counts = c;
    volume.probesPerRow = kProbesPerRow;
    int probes = c.x * c.y * c.z;
    int rows = (probes + kProbesPerRow - 1) / kProbesPerRow;
    volume.atlasW = kProbesPerRow * kProbeTexel;
    volume.atlasH = rows * kProbeTexel;
    volume.irr[0] = MakeAtlas(GL_RGBA16F, volume.atlasW, volume.atlasH);
    volume.irr[1] = MakeAtlas(GL_RGBA16F, volume.atlasW, volume.atlasH);
    volume.vis[0] = MakeAtlas(GL_RG16F, volume.atlasW, volume.atlasH);
    volume.vis[1] = MakeAtlas(GL_RG16F, volume.atlasW, volume.atlasH);
    ClearFull(volume.irr[0], volume.vis[0], volume.atlasW, volume.atlasH);
    ClearFull(volume.irr[1], volume.vis[1], volume.atlasW, volume.atlasH);
}

void SlipDDGI::PrepareAtlas(VolumeGPU& volume, const glm::ivec3& newBase, bool reset) {
    int write = 1 - volume.read;
    if (reset || !volume.hasBase) {
        // En lugar de borrar a negro absoluto (lo que causa un parpadeo oscuro y satura el auto-exposure),
        // preservamos los datos previos de la sonda si ya existían para una transición suave.
        if (volume.hasBase && volume.irr[volume.read] != 0 && volume.vis[volume.read] != 0) {
            glCopyImageSubData(volume.irr[volume.read], GL_TEXTURE_2D, 0, 0, 0, 0, volume.irr[write], GL_TEXTURE_2D, 0, 0, 0, 0, volume.atlasW, volume.atlasH, 1);
            glCopyImageSubData(volume.vis[volume.read], GL_TEXTURE_2D, 0, 0, 0, 0, volume.vis[write], GL_TEXTURE_2D, 0, 0, 0, 0, volume.atlasW, volume.atlasH, 1);
        } else {
            ClearFull(volume.irr[write], volume.vis[write], volume.atlasW, volume.atlasH);
        }
        volume.hasBase = true;
        volume.base = newBase;
        volume.pendingCount = 0;
        int total = volume.counts.x * volume.counts.y * volume.counts.z;
        int stride = 509;
        static const int kCoprimePrimes[] = { 1013, 1009, 997, 887, 769, 509, 397, 257 };
        for (int p : kCoprimePrimes) {
            if (std::gcd(p, total) == 1) { stride = p; break; }
        }
        for (int i = 0; i < total && volume.pendingCount < 4096; ++i) {
            volume.pending[volume.pendingCount++] = static_cast<int>((static_cast<uint64_t>(i) * stride) % total);
        }
        return;
    }

    glm::ivec3 delta = newBase - volume.base;
    if (delta == glm::ivec3(0)) {
        glCopyImageSubData(volume.irr[volume.read], GL_TEXTURE_2D, 0, 0, 0, 0, volume.irr[write], GL_TEXTURE_2D, 0, 0, 0, 0, volume.atlasW, volume.atlasH, 1);
        glCopyImageSubData(volume.vis[volume.read], GL_TEXTURE_2D, 0, 0, 0, 0, volume.vis[write], GL_TEXTURE_2D, 0, 0, 0, 0, volume.atlasW, volume.atlasH, 1);
        return;
    }

    if (m_ScrollProgram != 0) {
        glUseProgram(m_ScrollProgram);
        glBindImageTexture(0, volume.irr[write], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glBindImageTexture(1, volume.vis[write], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, volume.irr[volume.read]);
        glUniform1i(glGetUniformLocation(m_ScrollProgram, "u_SrcIrr"), 2);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, volume.vis[volume.read]);
        glUniform1i(glGetUniformLocation(m_ScrollProgram, "u_SrcVis"), 3);

        glUniform3i(glGetUniformLocation(m_ScrollProgram, "u_Delta"), delta.x, delta.y, delta.z);
        glUniform3i(glGetUniformLocation(m_ScrollProgram, "u_Counts"), volume.counts.x, volume.counts.y, volume.counts.z);
        glUniform1i(glGetUniformLocation(m_ScrollProgram, "u_ProbesPerRow"), volume.probesPerRow);
        glUniform1i(glGetUniformLocation(m_ScrollProgram, "u_ProbeTexel"), kProbeTexel);

        GLuint groupsX = static_cast<GLuint>((volume.atlasW + 15) / 16);
        GLuint groupsY = static_cast<GLuint>((volume.atlasH + 15) / 16);
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(0);
    } else {
        ClearFull(volume.irr[write], volume.vis[write], volume.atlasW, volume.atlasH);
    }

    volume.pendingCount = 0;
    for (int z = 0; z < volume.counts.z; ++z) {
        for (int y = 0; y < volume.counts.y; ++y) {
            for (int x = 0; x < volume.counts.x; ++x) {
                int sx = x + delta.x;
                int sy = y + delta.y;
                int sz = z + delta.z;
                bool inside = sx >= 0 && sy >= 0 && sz >= 0 && sx < volume.counts.x && sy < volume.counts.y && sz < volume.counts.z;
                if (!inside) {
                    int dst = ProbeIndex(x, y, z, volume.counts);
                    if (volume.pendingCount < 4096) volume.pending[volume.pendingCount++] = dst;
                }
            }
        }
    }
    volume.base = newBase;
}

void SlipDDGI::DispatchVolume(VolumeGPU& volume, const glm::mat4& view,
                               const DDGIDirectionalLight* dirLights, int dirCount,
                               const DDGIPointLight* pointLights, int pointCount,
                               const DDGISpotLight* spotLights, int spotCount,
                               bool shadowsReady, int probesPerFrame, int raysPerProbe, float hysteresis) {
    int write = 1 - volume.read;
    const auto& settings = SlipGraphicsSettings::Get().GetData();
    int total = volume.counts.x * volume.counts.y * volume.counts.z;
    int maxBatch = 256;
    int ids[256];
    int isNew[256];
    int dispatchCount = 0;
    int taken = 0;
    while (dispatchCount < probesPerFrame && dispatchCount < maxBatch && taken < volume.pendingCount) {
        ids[dispatchCount] = volume.pending[taken];
        isNew[dispatchCount] = 1;
        ++dispatchCount;
        ++taken;
    }
    if (taken > 0) {
        for (int i = 0; i < volume.pendingCount - taken; ++i) volume.pending[i] = volume.pending[i + taken];
        volume.pendingCount -= taken;
    }
    int stride = 509;
    static const int kCoprimePrimes[] = { 1013, 1009, 997, 887, 769, 509, 397, 257 };
    for (int p : kCoprimePrimes) {
        if (total > 0 && std::gcd(p, total) == 1) { stride = p; break; }
    }
    while (dispatchCount < probesPerFrame && dispatchCount < maxBatch) {
        int probeId = (total > 0) ? static_cast<int>((static_cast<uint64_t>(volume.cursor) * stride) % total) : 0;
        ids[dispatchCount] = probeId;
        isNew[dispatchCount] = 0;
        volume.cursor = (volume.cursor + 1) % std::max(total, 1);
        ++dispatchCount;
    }
    for (int i = dispatchCount; i < maxBatch; ++i) { ids[i] = -1; isNew[i] = 0; }

    GLint prevProgram = 0, prevFBO = 0, prevActive = 0;
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    GLint prevTex0 = 0, prevTex1 = 0, prevArr2 = 0, prevTex2 = 0;
    GLint prevSSBO0 = 0, prevSSBO1 = 0, prevSSBO2 = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex1);
    glActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevArr2);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 0, &prevSSBO0);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 1, &prevSSBO1);
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 2, &prevSSBO2);

    glUseProgram(m_Program);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_NodeSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_TriSSBO);

    struct GpuProbeTask {
        int probeId;
        int isNew;
    };
    GpuProbeTask taskList[256];
    for (int i = 0; i < maxBatch; ++i) {
        taskList[i].probeId = ids[i];
        taskList[i].isNew = isNew[i];
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_TaskSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, maxBatch * sizeof(GpuProbeTask), taskList, GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_TaskSSBO);
    glBindImageTexture(0, volume.irr[write], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindImageTexture(1, volume.vis[write], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG16F);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, volume.irr[volume.read]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, volume.vis[volume.read]);
    glActiveTexture(GL_TEXTURE2);
    if (shadowsReady) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, SlipCascadedShadows::Get().GetDepthTextureArray());
    } else {
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    auto loc = [&](const char* name) { return glGetUniformLocation(m_Program, name); };
    glUniform1i(loc("u_IrrImg"), 0);
    glUniform1i(loc("u_VisImg"), 1);
    glUniform1i(loc("u_ReadIrr"), 0);
    glUniform1i(loc("u_ReadVis"), 1);
    glUniform1i(loc("u_ShadowMap"), 2);
    glUniform1i(loc("u_RayCount"), raysPerProbe);
    glUniform3i(loc("u_Counts"), volume.counts.x, volume.counts.y, volume.counts.z);
    glUniform1i(loc("u_ProbesPerRow"), volume.probesPerRow);
    glUniform2f(loc("u_AtlasSize"), static_cast<float>(volume.atlasW), static_cast<float>(volume.atlasH));
    glUniform1i(loc("u_NodeCount"), m_NodeCount);
    glUniform1i(loc("u_TriCount"), m_TriCount);
    glUniform1f(loc("u_Hysteresis"), hysteresis);
    glUniform3fv(loc("u_Spacing"), 1, glm::value_ptr(volume.spacing));
    glUniform3fv(loc("u_Origin"), 1, glm::value_ptr(volume.origin));
    glUniform3f(loc("u_SkyColor"), 0.55f, 0.68f, 0.90f);
    glUniform1i(loc("u_ShadowsEnabled"), shadowsReady ? 1 : 0);
    glUniformMatrix4fv(loc("u_View"), 1, GL_FALSE, glm::value_ptr(view));

    glm::mat4 lightMats[4];
    float splits[4] = { 0, 0, 0, 0 };
    int cascadeCount = 0;
    if (shadowsReady) {
        const auto& mats = SlipCascadedShadows::Get().GetLightSpaceMatrices();
        const auto& clip = SlipCascadedShadows::Get().GetCascadeEndClipSpace();
        cascadeCount = SlipCascadedShadows::Get().GetCascadeCount();
        for (int i = 0; i < 4 && i < static_cast<int>(mats.size()); ++i) lightMats[i] = mats[i];
        for (int i = 0; i < 4 && i < static_cast<int>(clip.size()); ++i) splits[i] = clip[i];
    }
    glUniformMatrix4fv(loc("u_LightSpaceMatrices"), 4, GL_FALSE, glm::value_ptr(lightMats[0]));
    glUniform1fv(loc("u_CascadeEndClipSpace"), 4, splits);
    glUniform1i(loc("u_CascadeCount"), cascadeCount);
    glUniform1f(loc("u_ShadowBias"), settings.shadowBias);
    glUniform1i(loc("u_PCFQuality"), settings.pcfQuality);

    glm::vec3 dirDir[4] = {};
    glm::vec3 dirColor[4] = {};
    float dirInt[4] = {};
    int nDir = std::clamp(dirCount, 0, 4);
    for (int i = 0; i < nDir; ++i) {
        dirDir[i] = dirLights[i].direction;
        dirColor[i] = dirLights[i].color;
        dirInt[i] = dirLights[i].intensity;
    }
    glm::vec3 pointPos[8] = {};
    glm::vec3 pointColor[8] = {};
    float pointInt[8] = {};
    float pointRange[8] = {};
    int nPoint = std::clamp(pointCount, 0, 8);
    for (int i = 0; i < nPoint; ++i) {
        pointPos[i] = pointLights[i].position;
        pointColor[i] = pointLights[i].color;
        pointInt[i] = pointLights[i].intensity;
        pointRange[i] = pointLights[i].range;
    }
    glm::vec3 spotPos[4] = {};
    glm::vec3 spotDir[4] = {};
    glm::vec3 spotColor[4] = {};
    float spotInt[4] = {};
    float spotRange[4] = {};
    float spotAngle[4] = {};
    int nSpot = std::clamp(spotCount, 0, 4);
    for (int i = 0; i < nSpot; ++i) {
        spotPos[i] = spotLights[i].position;
        spotDir[i] = spotLights[i].direction;
        spotColor[i] = spotLights[i].color;
        spotInt[i] = spotLights[i].intensity;
        spotRange[i] = spotLights[i].range;
        spotAngle[i] = spotLights[i].spotAngle;
    }

    glUniform1i(loc("u_NumDir"), nDir);
    glUniform3fv(loc("u_DirDir"), 4, glm::value_ptr(dirDir[0]));
    glUniform3fv(loc("u_DirColor"), 4, glm::value_ptr(dirColor[0]));
    glUniform1fv(loc("u_DirIntensity"), 4, dirInt);
    glUniform1i(loc("u_NumPoint"), nPoint);
    glUniform3fv(loc("u_PointPos"), 8, glm::value_ptr(pointPos[0]));
    glUniform3fv(loc("u_PointColor"), 8, glm::value_ptr(pointColor[0]));
    glUniform1fv(loc("u_PointIntensity"), 8, pointInt);
    glUniform1fv(loc("u_PointRange"), 8, pointRange);
    glUniform1i(loc("u_NumSpot"), nSpot);
    glUniform3fv(loc("u_SpotPos"), 4, glm::value_ptr(spotPos[0]));
    glUniform3fv(loc("u_SpotDir"), 4, glm::value_ptr(spotDir[0]));
    glUniform3fv(loc("u_SpotColor"), 4, glm::value_ptr(spotColor[0]));
    glUniform1fv(loc("u_SpotIntensity"), 4, spotInt);
    glUniform1fv(loc("u_SpotRange"), 4, spotRange);
    glUniform1fv(loc("u_SpotAngle"), 4, spotAngle);

    if (dispatchCount > 0) glDispatchCompute(static_cast<GLuint>(dispatchCount), 1, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
    glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG16F);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLuint>(prevSSBO0));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, static_cast<GLuint>(prevSSBO1));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, static_cast<GLuint>(prevSSBO2));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex0));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex1));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(prevArr2));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex2));
    glActiveTexture(static_cast<GLenum>(prevActive));
    glUseProgram(static_cast<GLuint>(prevProgram));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    volume.read = write;
    m_Ready = true;
}

void SlipDDGI::Update(const glm::vec3& cameraPos, const glm::mat4& view, SlipLevel* level,
                      const DDGIDirectionalLight* dirLights, int dirCount,
                      const DDGIPointLight* pointLights, int pointCount,
                      const DDGISpotLight* spotLights, int spotCount,
                      bool shadowsReady,
                      bool pauseSceneRebuild) {
    if (!EnsureGpu()) return;
    const auto& settings = SlipGraphicsSettings::Get().GetData();
    if (!settings.ddgiEnabled) {
        for (auto& volume : m_Volumes) volume.used = false;
        m_Ready = false;
        return;
    }

    if (level) {
        SlipSkyComponent* sky = SlipSkyComponent::FindActive(level);
        if (sky && sky->IsEnabled()) {
            m_SkyColor = sky->currentAtmosphere.zenithColor * 0.85f + sky->params.sunColor * 0.15f;
            m_GroundColor = sky->currentAtmosphere.groundColor * 0.65f;
        } else if (dirCount > 0) {
            m_SkyColor = glm::max(dirLights[0].color * 0.35f, glm::vec3(0.12f));
            m_GroundColor = glm::max(dirLights[0].color * 0.12f, glm::vec3(0.05f));
        }
    }

    if (!pauseSceneRebuild) {
        RebuildScene(level);
    }
    m_Ready = false;
    VolumeGPU* active[5] = {};
    int used = 0;

    if (settings.ddgiCameraVolume) {
        VolumeGPU& cam = m_Volumes[0];
        cam.follow = true;
        EnsureVolume(cam, glm::ivec3(kCamX, kCamY, kCamZ));
        float spacing = std::clamp(settings.ddgiSpacing, 0.5f, 16.0f);
        bool spacingChanged = std::abs(spacing - m_AppliedCameraSpacing) > 0.001f;
        m_AppliedCameraSpacing = spacing;
        cam.spacing = glm::vec3(spacing);
        glm::ivec3 desired = glm::ivec3(glm::floor(cameraPos / spacing)) - glm::ivec3(kCamX / 2, kCamY / 2, kCamZ / 2);
        glm::ivec3 base = desired;
        bool reset = !cam.hasBase || spacingChanged;
        if (!reset) {
            base = cam.base + glm::clamp(desired - cam.base, glm::ivec3(-1), glm::ivec3(1));
        }
        cam.origin = glm::vec3(base) * cam.spacing;
        PrepareAtlas(cam, base, reset);
        cam.used = true;
        active[used++] = &cam;
    } else {
        m_Volumes[0].used = false;
    }

    int slot = 1;
    if (level) {
        for (SlipEntity* entity : level->GetEntities()) {
            if (!entity || slot >= 5) continue;
            auto box = entity->GetComponent<DDGIVolumeComponent>();
            if (!box || !box->IsEnabled()) continue;
            VolumeGPU& gpu = m_Volumes[slot++];
            gpu.follow = false;
            glm::ivec3 counts(std::max(box->probesX, 1), std::max(box->probesY, 1), std::max(box->probesZ, 1));
            EnsureVolume(gpu, counts);
            glm::vec3 size = glm::max(box->size, glm::vec3(0.5f));
            gpu.spacing = size / glm::vec3(gpu.counts);
            gpu.origin = entity->position - size * 0.5f;
            PrepareAtlas(gpu, glm::ivec3(0), !gpu.hasBase);
            gpu.used = true;
            active[used++] = &gpu;
        }
    }
    for (int i = slot; i < 5; ++i) m_Volumes[i].used = false;

    if (used == 0) return;
    int budget = std::clamp(settings.ddgiProbesPerFrame, 1, 256);
    int rays = std::clamp(settings.ddgiRaysPerProbe, 8, 64);
    float hysteresis = std::clamp(settings.ddgiHysteresis, 0.0f, 0.98f);
    int share = std::max(1, budget / used);
    int leftover = budget - share * used;
    for (int i = 0; i < used; ++i) {
        int slice = share + (i == 0 ? leftover : 0);
        DispatchVolume(*active[i], view, dirLights, dirCount, pointLights, pointCount, spotLights, spotCount,
                       shadowsReady, slice, rays, hysteresis);
    }
}

void SlipDDGI::Bind(SlipShader* shader) {
    if (!shader || shader->ID == 0) return;
    if (!IsActive()) {
        shader->setBool("u_DDGIEnabled", false);
        return;
    }

    int count = 0;
    for (int i = 0; i < 5; ++i) if (m_Volumes[i].used && m_Volumes[i].hasBase) ++count;
    if (count == 0) {
        shader->setBool("u_DDGIEnabled", false);
        return;
    }

    shader->setBool("u_DDGIEnabled", true);
    shader->setFloat("u_DDGIIntensity", SlipGraphicsSettings::Get().GetData().ddgiIntensity);
    shader->setVec3("u_DDGISkyColor", m_SkyColor);
    shader->setVec3("u_DDGIGroundColor", m_GroundColor);
    shader->setInt("u_DDGIOctRes", kOctRes);
    shader->setInt("u_DDGIProbeTexel", kProbeTexel);
    for (int i = 0; i < 5; ++i) {
        const VolumeGPU& volume = m_Volumes[i];
        bool on = volume.used && volume.hasBase;
        glActiveTexture(GL_TEXTURE5 + i);
        glBindTexture(GL_TEXTURE_2D, on ? volume.irr[volume.read] : 0);
        glActiveTexture(GL_TEXTURE10 + i);
        glBindTexture(GL_TEXTURE_2D, on ? volume.vis[volume.read] : 0);
        shader->setInt(("u_DDGIIrr[" + std::to_string(i) + "]").c_str(), 5 + i);
        shader->setInt(("u_DDGIVis[" + std::to_string(i) + "]").c_str(), 10 + i);
        shader->setVec3(("u_DDGIOrigin[" + std::to_string(i) + "]").c_str(), on ? volume.origin : glm::vec3(0.0f));
        shader->setVec3(("u_DDGISpacing[" + std::to_string(i) + "]").c_str(), on ? volume.spacing : glm::vec3(1.0f));
        shader->setVec2(("u_DDGIAtlas[" + std::to_string(i) + "]").c_str(),
                        on ? glm::vec2(volume.atlasW, volume.atlasH) : glm::vec2(1.0f));
        glUniform3i(glGetUniformLocation(shader->ID, ("u_DDGICounts[" + std::to_string(i) + "]").c_str()),
                    on ? volume.counts.x : 0, on ? volume.counts.y : 0, on ? volume.counts.z : 0);
        glUniform1i(glGetUniformLocation(shader->ID, ("u_DDGIFollow[" + std::to_string(i) + "]").c_str()),
                    (on && volume.follow) ? 1 : 0);
        glUniform1i(glGetUniformLocation(shader->ID, ("u_DDGIOn[" + std::to_string(i) + "]").c_str()), on ? 1 : 0);
        glUniform1i(glGetUniformLocation(shader->ID, ("u_DDGIRow[" + std::to_string(i) + "]").c_str()),
                    on ? volume.probesPerRow : 1);
    }
    glActiveTexture(GL_TEXTURE0);
}

void SlipDDGI::DrawDebug(const glm::mat4& view, const glm::mat4& projection) {
    if (!IsActive() || m_DebugProgram == 0) return;
    if (!SlipGraphicsSettings::Get().GetData().ddgiShowProbes) return;

    std::vector<float> verts;
    verts.reserve(4096);
    auto push = [&](const glm::vec3& a, const glm::vec3& color) {
        verts.push_back(a.x); verts.push_back(a.y); verts.push_back(a.z);
        verts.push_back(color.x); verts.push_back(color.y); verts.push_back(color.z);
    };

    for (const auto& volume : m_Volumes) {
        if (!volume.used || !volume.hasBase || volume.irr[volume.read] == 0) continue;
        std::vector<float> pixels(static_cast<size_t>(volume.atlasW) * volume.atlasH * 4);
        glBindTexture(GL_TEXTURE_2D, volume.irr[volume.read]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
        int total = volume.counts.x * volume.counts.y * volume.counts.z;
        for (int i = 0; i < total; ++i) {
            int px = (i % volume.probesPerRow) * kProbeTexel + 1;
            int py = (i / volume.probesPerRow) * kProbeTexel + 1;
            glm::vec3 sum(0.0f);
            int samples = 0;
            for (int ty = 0; ty < kOctRes; ++ty) {
                for (int tx = 0; tx < kOctRes; ++tx) {
                    int idx = ((py + ty) * volume.atlasW + (px + tx)) * 4;
                    if (idx < 0 || idx + 3 >= static_cast<int>(pixels.size())) continue;
                    if (pixels[idx + 3] < 0.5f) continue;
                    sum += glm::vec3(pixels[idx], pixels[idx + 1], pixels[idx + 2]);
                    ++samples;
                }
            }
            glm::vec3 color = samples > 0 ? glm::clamp(sum / static_cast<float>(samples), glm::vec3(0.05f), glm::vec3(1.0f))
                                          : glm::vec3(0.15f, 0.15f, 0.15f);
            int x = i % volume.counts.x;
            int y = (i / volume.counts.x) % volume.counts.y;
            int z = i / (volume.counts.x * volume.counts.y);
            glm::vec3 p = volume.origin + (glm::vec3(x, y, z) + 0.5f) * volume.spacing;
            float arm = std::min(volume.spacing.x, std::min(volume.spacing.y, volume.spacing.z)) * 0.08f;
            push(p - glm::vec3(arm, 0, 0), color); push(p + glm::vec3(arm, 0, 0), color);
            push(p - glm::vec3(0, arm, 0), color); push(p + glm::vec3(0, arm, 0), color);
            push(p - glm::vec3(0, 0, arm), color); push(p + glm::vec3(0, 0, arm), color);
        }
    }
    if (verts.empty()) return;

    if (m_DebugVAO == 0) {
        glGenVertexArrays(1, &m_DebugVAO);
        glGenBuffers(1, &m_DebugVBO);
    }
    glUseProgram(m_DebugProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_DebugProgram, "u_View"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(m_DebugProgram, "u_Projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBindVertexArray(m_DebugVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_DebugVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 3));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 6));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
    glActiveTexture(GL_TEXTURE0);
}

void SlipDDGI::DrawVolumeProbes(const glm::mat4& view, const glm::mat4& projection,
                                const glm::vec3& center, const glm::vec3& size, const glm::ivec3& counts) {
    if (!EnsureGpu() || m_DebugProgram == 0) return;
    glm::ivec3 c = ClampProbeCounts(counts);
    glm::vec3 box = glm::max(size, glm::vec3(0.5f));
    glm::vec3 spacing = box / glm::vec3(c);
    glm::vec3 origin = center - box * 0.5f;

    const VolumeGPU* matched = nullptr;
    for (const auto& volume : m_Volumes) {
        if (!volume.used || volume.follow || volume.irr[volume.read] == 0) continue;
        if (volume.counts != c) continue;
        if (glm::length(volume.origin - origin) > 0.05f) continue;
        matched = &volume;
        break;
    }

    std::vector<float> pixels;
    if (matched) {
        pixels.resize(static_cast<size_t>(matched->atlasW) * matched->atlasH * 4);
        glBindTexture(GL_TEXTURE_2D, matched->irr[matched->read]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    }

    std::vector<float> verts;
    verts.reserve(static_cast<size_t>(c.x * c.y * c.z) * 36);
    auto push = [&](const glm::vec3& a, const glm::vec3& color) {
        verts.push_back(a.x); verts.push_back(a.y); verts.push_back(a.z);
        verts.push_back(color.x); verts.push_back(color.y); verts.push_back(color.z);
    };

    int total = c.x * c.y * c.z;
    for (int i = 0; i < total; ++i) {
        glm::vec3 color(0.95f, 0.72f, 0.25f);
        if (matched && !pixels.empty()) {
            int px = (i % matched->probesPerRow) * kProbeTexel + 1;
            int py = (i / matched->probesPerRow) * kProbeTexel + 1;
            glm::vec3 sum(0.0f);
            int samples = 0;
            for (int ty = 0; ty < kOctRes; ++ty) {
                for (int tx = 0; tx < kOctRes; ++tx) {
                    int idx = ((py + ty) * matched->atlasW + (px + tx)) * 4;
                    if (idx < 0 || idx + 3 >= static_cast<int>(pixels.size()) || pixels[idx + 3] < 0.5f) continue;
                    sum += glm::vec3(pixels[idx], pixels[idx + 1], pixels[idx + 2]);
                    ++samples;
                }
            }
            if (samples > 0) color = glm::clamp(sum / static_cast<float>(samples), glm::vec3(0.08f), glm::vec3(1.0f));
        }
        int x = i % c.x;
        int y = (i / c.x) % c.y;
        int z = i / (c.x * c.y);
        glm::vec3 p = origin + (glm::vec3(x, y, z) + 0.5f) * spacing;
        float arm = std::min(0.45f, std::min(spacing.x, std::min(spacing.y, spacing.z)) * 0.28f);
        push(p - glm::vec3(arm, 0, 0), color); push(p + glm::vec3(arm, 0, 0), color);
        push(p - glm::vec3(0, arm, 0), color); push(p + glm::vec3(0, arm, 0), color);
        push(p - glm::vec3(0, 0, arm), color); push(p + glm::vec3(0, 0, arm), color);
    }
    if (verts.empty()) return;

    if (m_DebugVAO == 0) {
        glGenVertexArrays(1, &m_DebugVAO);
        glGenBuffers(1, &m_DebugVBO);
    }
    glUseProgram(m_DebugProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_DebugProgram, "u_View"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(m_DebugProgram, "u_Projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBindVertexArray(m_DebugVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_DebugVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 3));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 6));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
    glActiveTexture(GL_TEXTURE0);
}

#endif
