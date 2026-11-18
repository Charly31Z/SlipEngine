#include "SlipEngine/render/SlipPostProcess.h"
#include "SlipEngine/render/SlipShader.h"

#include <algorithm>
#include <iostream>

namespace {

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
#define PP_VERT_HEAD "#version 300 es\nprecision highp float;\n"
#define PP_FRAG_HEAD "#version 300 es\nprecision highp float;\nprecision mediump sampler2D;\n"
#else
#define PP_VERT_HEAD "#version 330 core\n"
#define PP_FRAG_HEAD "#version 330 core\n"
#endif

const char* kFullscreenVert =
    PP_VERT_HEAD
    "layout(location = 0) in vec2 aPos;\n"
    "out vec2 vUv;\n"
    "void main() {\n"
    "    vUv = aPos * 0.5 + 0.5;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

const char* kPrefilterFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Src;\n"
    "uniform float u_Threshold;\n"
    "uniform float u_SoftKnee;\n"
    "void main() {\n"
    "    vec3 c = max(texture(u_Src, vUv).rgb, vec3(0.0));\n"
    "    float br = max(c.r, max(c.g, c.b));\n"
    "    float knee = max(u_Threshold * clamp(u_SoftKnee, 0.0, 1.0), 0.0001);\n"
    "    float soft = clamp(br - u_Threshold + knee, 0.0, 2.0 * knee);\n"
    "    soft = (soft * soft) / (4.0 * knee);\n"
    "    float contrib = max(br - u_Threshold, soft) / max(br, 0.0001);\n"
    "    FragColor = vec4(c * contrib, 1.0);\n"
    "}\n";

const char* kDownFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Src;\n"
    "uniform vec2 u_Texel;\n"
    "void main() {\n"
    "    vec3 c = texture(u_Src, vUv + vec2(-u_Texel.x, -u_Texel.y)).rgb\n"
    "           + texture(u_Src, vUv + vec2( u_Texel.x, -u_Texel.y)).rgb\n"
    "           + texture(u_Src, vUv + vec2(-u_Texel.x,  u_Texel.y)).rgb\n"
    "           + texture(u_Src, vUv + vec2( u_Texel.x,  u_Texel.y)).rgb;\n"
    "    FragColor = vec4(c * 0.25, 1.0);\n"
    "}\n";

const char* kUpFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_High;\n"
    "uniform sampler2D u_Low;\n"
    "uniform float u_Scatter;\n"
    "void main() {\n"
    "    vec3 high = texture(u_High, vUv).rgb;\n"
    "    vec3 low = texture(u_Low, vUv).rgb;\n"
    "    FragColor = vec4(high + low * clamp(u_Scatter, 0.0, 1.5), 1.0);\n"
    "}\n";

const char* kLensFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Src;\n"
    "uniform float u_Streak;\n"
    "uniform float u_Halo;\n"
    "void main() {\n"
    "    vec3 acc = vec3(0.0);\n"
    "    float wsum = 0.0;\n"
    "    const int taps = 8;\n"
    "    float len = max(u_Streak, 0.0);\n"
    "    for (int i = -taps; i <= taps; ++i) {\n"
    "        float fi = float(i) / float(taps);\n"
    "        float w = exp(-abs(fi) * 2.2);\n"
    "        vec2 oh = vec2(fi * len, 0.0);\n"
    "        vec3 sh;\n"
    "        sh.r = texture(u_Src, vUv + oh * vec2(1.04, 1.0)).r;\n"
    "        sh.g = texture(u_Src, vUv + oh).g;\n"
    "        sh.b = texture(u_Src, vUv + oh * vec2(0.96, 1.0)).b;\n"
    "        acc += sh * w;\n"
    "        wsum += w;\n"
    "        vec2 ov = vec2(0.0, fi * len);\n"
    "        acc += texture(u_Src, vUv + ov).rgb * w;\n"
    "        wsum += w;\n"
    "        float wd = w * 0.4;\n"
    "        acc += texture(u_Src, vUv + vec2(fi, fi) * len * 0.6).rgb * wd;\n"
    "        acc += texture(u_Src, vUv + vec2(fi, -fi) * len * 0.6).rgb * wd;\n"
    "        wsum += wd * 2.0;\n"
    "    }\n"
    "    vec3 streaks = acc / max(wsum, 0.0001);\n"
    "    vec3 halo = vec3(0.0);\n"
    "    const int haloTaps = 8;\n"
    "    for (int i = 0; i < haloTaps; ++i) {\n"
    "        float a = float(i) * 0.785398;\n"
    "        vec2 h = vec2(cos(a), sin(a)) * (0.02 + 0.12 * u_Halo);\n"
    "        halo += texture(u_Src, vUv + h).rgb;\n"
    "    }\n"
    "    halo /= float(haloTaps);\n"
    "    FragColor = vec4(streaks + halo * u_Halo, 1.0);\n"
    "}\n";

const char* kSsaoFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Depth;\n"
    "uniform sampler2D u_Normal;\n"
    "uniform sampler2D u_Noise;\n"
    "uniform mat4 u_InvProj;\n"
    "uniform mat4 u_Proj;\n"
    "uniform mat4 u_View;\n"
    "uniform vec2 u_NoiseScale;\n"
    "uniform float u_Radius;\n"
    "uniform float u_Bias;\n"
    "vec3 DecodeOct(vec2 e) {\n"
    "    vec2 f = e * 2.0 - 1.0;\n"
    "    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));\n"
    "    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);\n"
    "    return normalize(n);\n"
    "}\n"
    "vec3 ViewPos(vec2 uv) {\n"
    "    float d = texture(u_Depth, uv).r;\n"
    "    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
    "    vec4 view = u_InvProj * clip;\n"
    "    return view.xyz / max(view.w, 0.0001);\n"
    "}\n"
    "void main() {\n"
    "    float depth = texture(u_Depth, vUv).r;\n"
    "    if (depth > 0.9995) { FragColor = vec4(1.0); return; }\n"
    "    vec3 pos = ViewPos(vUv);\n"
    "    vec3 N = normalize(mat3(u_View) * DecodeOct(texture(u_Normal, vUv).rg));\n"
    "    vec3 random = normalize(texture(u_Noise, vUv * u_NoiseScale).xyz * 2.0 - 1.0);\n"
    "    vec3 T = normalize(random - N * dot(random, N));\n"
    "    vec3 B = cross(N, T);\n"
    "    mat3 TBN = mat3(T, B, N);\n"
    "    const vec3 kernel[16] = vec3[16](\n"
    "        vec3(0.10, 0.22, 0.15), vec3(-0.18, 0.12, 0.22), vec3(0.24, -0.08, 0.18), vec3(-0.06, -0.20, 0.16),\n"
    "        vec3(0.32, 0.18, 0.40), vec3(-0.28, 0.26, 0.36), vec3(0.14, -0.34, 0.42), vec3(-0.36, -0.12, 0.30),\n"
    "        vec3(0.42, 0.08, 0.62), vec3(-0.16, 0.44, 0.55), vec3(0.22, -0.40, 0.70), vec3(-0.48, 0.16, 0.58),\n"
    "        vec3(0.08, 0.50, 0.84), vec3(-0.40, -0.36, 0.72), vec3(0.52, 0.28, 0.90), vec3(-0.22, -0.18, 0.96)\n"
    "    );\n"
    "    float occ = 0.0;\n"
    "    for (int i = 0; i < 16; ++i) {\n"
    "        float scale = float(i) / 15.0;\n"
    "        vec3 s = kernel[i] * mix(0.12, 1.0, scale * scale);\n"
    "        vec3 samplePos = pos + (TBN * s) * u_Radius;\n"
    "        vec4 off = u_Proj * vec4(samplePos, 1.0);\n"
    "        off.xyz /= max(off.w, 0.0001);\n"
    "        vec2 suv = off.xy * 0.5 + 0.5;\n"
    "        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;\n"
    "        float sd = ViewPos(suv).z;\n"
    "        float rangeCheck = smoothstep(0.0, 1.0, u_Radius / max(abs(pos.z - sd), 0.0001));\n"
    "        occ += (sd >= samplePos.z + u_Bias ? 1.0 : 0.0) * rangeCheck;\n"
    "    }\n"
    "    FragColor = vec4(clamp(1.0 - occ / 16.0, 0.0, 1.0), 0.0, 0.0, 1.0);\n"
    "}\n";

const char* kAoBlurFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Ao;\n"
    "uniform sampler2D u_Depth;\n"
    "uniform mat4 u_InvProj;\n"
    "float ViewZ(vec2 uv) {\n"
    "    float d = texture(u_Depth, uv).r;\n"
    "    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
    "    vec4 view = u_InvProj * clip;\n"
    "    return view.z / max(view.w, 0.0001);\n"
    "}\n"
    "void main() {\n"
    "    float d0 = ViewZ(vUv);\n"
    "    vec2 texel = 1.0 / vec2(textureSize(u_Ao, 0));\n"
    "    float sum = 0.0;\n"
    "    float wsum = 0.0;\n"
    "    for (int y = -2; y <= 2; ++y) {\n"
    "        for (int x = -2; x <= 2; ++x) {\n"
    "            vec2 uv = vUv + vec2(float(x), float(y)) * texel;\n"
    "            float w = exp(-abs(d0 - ViewZ(uv)) * 3.5);\n"
    "            sum += texture(u_Ao, uv).r * w;\n"
    "            wsum += w;\n"
    "        }\n"
    "    }\n"
    "    FragColor = vec4(sum / max(wsum, 0.0001), 0.0, 0.0, 1.0);\n"
    "}\n";

const char* kApplyAoFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Scene;\n"
    "uniform sampler2D u_Ao;\n"
    "uniform float u_Intensity;\n"
    "uniform float u_Power;\n"
    "void main() {\n"
    "    float ao = pow(clamp(texture(u_Ao, vUv).r, 0.0, 1.0), max(u_Power, 0.01));\n"
    "    vec3 c = texture(u_Scene, vUv).rgb;\n"
    "    float f = mix(1.0, ao, clamp(u_Intensity, 0.0, 1.0));\n"
    "    FragColor = vec4(c * f, 1.0);\n"
    "}\n";

const char* kGaussFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Src;\n"
    "uniform vec2 u_Direction;\n"
    "uniform float u_Radius;\n"
    "void main() {\n"
    "    vec2 texel = 1.0 / vec2(textureSize(u_Src, 0));\n"
    "    float sigma = max(u_Radius * 0.45, 0.8);\n"
    "    float stepPx = max(u_Radius / 8.0, 1.0);\n"
    "    vec3 acc = vec3(0.0);\n"
    "    float wsum = 0.0;\n"
    "    for (int i = -8; i <= 8; ++i) {\n"
    "        float x = float(i) * stepPx;\n"
    "        float w = exp(-0.5 * (x * x) / (sigma * sigma));\n"
    "        acc += texture(u_Src, vUv + u_Direction * texel * x).rgb * w;\n"
    "        wsum += w;\n"
    "    }\n"
    "    FragColor = vec4(acc / max(wsum, 0.0001), 1.0);\n"
    "}\n";

const char* kRadialFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Src;\n"
    "uniform vec2 u_Center;\n"
    "uniform float u_Strength;\n"
    "void main() {\n"
    "    vec2 toCenter = u_Center - vUv;\n"
    "    vec3 acc = vec3(0.0);\n"
    "    const int taps = 12;\n"
    "    for (int i = 0; i < taps; ++i) {\n"
    "        float t = float(i) / float(taps - 1);\n"
    "        acc += texture(u_Src, vUv + toCenter * t * u_Strength).rgb;\n"
    "    }\n"
    "    FragColor = vec4(acc / float(taps), 1.0);\n"
    "}\n";

const char* kLogLumFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Image;\n"
    "uniform sampler2D u_Bloom;\n"
    "uniform sampler2D u_Lens;\n"
    "uniform float u_BloomIntensity;\n"
    "uniform vec3 u_BloomTint;\n"
    "uniform float u_LensIntensity;\n"
    "void main() {\n"
    "    vec3 c = texture(u_Image, vUv).rgb;\n"
    "    c += texture(u_Bloom, vUv).rgb * u_BloomIntensity * u_BloomTint;\n"
    "    c += texture(u_Lens, vUv).rgb * u_LensIntensity;\n"
    "    float l = dot(max(c, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));\n"
    "    FragColor = vec4(log(max(l, 0.0001)), 0.0, 0.0, 1.0);\n"
    "}\n";

const char* kReduceFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Src;\n"
    "void main() {\n"
    "    vec2 texel = 0.5 / vec2(textureSize(u_Src, 0));\n"
    "    float l = texture(u_Src, vUv + vec2(-texel.x, -texel.y)).r\n"
    "            + texture(u_Src, vUv + vec2( texel.x, -texel.y)).r\n"
    "            + texture(u_Src, vUv + vec2(-texel.x,  texel.y)).r\n"
    "            + texture(u_Src, vUv + vec2( texel.x,  texel.y)).r;\n"
    "    FragColor = vec4(l * 0.25, 0.0, 0.0, 1.0);\n"
    "}\n";

const char* kAdaptFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Cur;\n"
    "uniform sampler2D u_Prev;\n"
    "uniform float u_Dt;\n"
    "uniform float u_Speed;\n"
    "uniform float u_Min;\n"
    "uniform float u_Max;\n"
    "void main() {\n"
    "    float target = exp(texture(u_Cur, vec2(0.5)).r);\n"
    "    target = clamp(target, u_Min, u_Max);\n"
    "    float prev = texture(u_Prev, vec2(0.5)).r;\n"
    "    float t = clamp(1.0 - exp(-max(u_Dt, 0.0) * max(u_Speed, 0.0)), 0.0, 1.0);\n"
    "    FragColor = vec4(mix(prev, target, t), 0.0, 0.0, 1.0);\n"
    "}\n";

const char* kCompositeFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Image;\n"
    "uniform sampler2D u_Bloom;\n"
    "uniform sampler2D u_Lens;\n"
    "uniform sampler2D u_Adapted;\n"
    "uniform float u_BloomIntensity;\n"
    "uniform vec3 u_BloomTint;\n"
    "uniform float u_LensIntensity;\n"
    "uniform float u_Auto;\n"
    "uniform float u_Key;\n"
    "uniform float u_Compensation;\n"
    "uniform float u_Contrast;\n"
    "uniform float u_Saturation;\n"
    "uniform float u_Temperature;\n"
    "uniform vec3 u_Lift;\n"
    "uniform vec3 u_Gamma;\n"
    "uniform vec3 u_Gain;\n"
    "uniform vec3 u_Filter;\n"
    "uniform int u_Tonemap;\n"
    "uniform float u_Vignette;\n"
    "uniform float u_VignetteSmooth;\n"
    "uniform float u_Letterbox;\n"
    "uniform float u_Pillarbox;\n"
    "vec3 ACESFilm(vec3 x) {\n"
    "    const float a = 2.51;\n"
    "    const float b = 0.03;\n"
    "    const float c = 2.43;\n"
    "    const float d = 0.59;\n"
    "    const float e = 0.14;\n"
    "    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);\n"
    "}\n"
    "void main() {\n"
    "    vec3 color = max(texture(u_Image, vUv).rgb, vec3(0.0));\n"
    "    color += texture(u_Bloom, vUv).rgb * u_BloomIntensity * u_BloomTint;\n"
    "    color += texture(u_Lens, vUv).rgb * u_LensIntensity;\n"
    "    color = max(color, vec3(0.0));\n"
    "    float exposure = 1.0;\n"
    "    if (u_Auto > 0.5) {\n"
    "        float lum = max(texture(u_Adapted, vec2(0.5)).r, 0.0001);\n"
    "        exposure = (u_Key / lum) * exp2(u_Compensation);\n"
    "    }\n"
    "    color *= exposure;\n"
    "    float temp = clamp(u_Temperature, -1.0, 1.0);\n"
    "    color.r *= 1.0 + temp * 0.18;\n"
    "    color.b *= 1.0 - temp * 0.18;\n"
    "    color = max(color * u_Gain + u_Lift, vec3(0.0));\n"
    "    color = pow(color, 1.0 / max(u_Gamma, vec3(0.001)));\n"
    "    color = (color - vec3(0.18)) * u_Contrast + vec3(0.18);\n"
    "    float luma = dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));\n"
    "    color = max(mix(vec3(luma), color, u_Saturation), vec3(0.0)) * u_Filter;\n"
    "    if (u_Tonemap == 1) color = ACESFilm(max(color, vec3(0.0)));\n"
    "    else color = color / (color + vec3(1.0));\n"
    "    float edge = clamp(u_VignetteSmooth, 0.05, 0.95);\n"
    "    float vig = smoothstep(1.0, 1.0 - edge, length(vUv - vec2(0.5)) * 1.35);\n"
    "    color = mix(color, color * (1.0 - clamp(u_Vignette, 0.0, 1.0)), 1.0 - vig);\n"
    "    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));\n"
    "    if (vUv.y < u_Letterbox || vUv.y > (1.0 - u_Letterbox) || vUv.x < u_Pillarbox || vUv.x > (1.0 - u_Pillarbox)) {\n"
    "        FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "    } else {\n"
    "        FragColor = vec4(color, 1.0);\n"
    "    }\n"
    "}\n";

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
const char* kFxaaFrag =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision mediump sampler2D;\n"
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Color;\n"
    "uniform vec2 u_TexelSize;\n"
    "#define FXAA_EDGE_THRESHOLD_MIN 0.0312\n"
    "#define FXAA_EDGE_THRESHOLD_MAX 0.125\n"
    "#define FXAA_SUBPIX_TRIM        0.25\n"
    "#define FXAA_SUBPIX_TRIM_SCALE  1.3333333\n"
    "#define FXAA_SUBPIX_CAP         0.75\n"
    "#define FXAA_SEARCH_STEPS       10\n"
    "float rgb2luma(vec3 rgb) {\n"
    "    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));\n"
    "}\n"
    "void main() {\n"
    "    vec3 colorM = texture(u_Color, vUv).rgb;\n"
    "    float lumaM = rgb2luma(colorM);\n"
    "    float lumaU = rgb2luma(texture(u_Color, vUv + vec2(0.0, u_TexelSize.y)).rgb);\n"
    "    float lumaD = rgb2luma(texture(u_Color, vUv - vec2(0.0, u_TexelSize.y)).rgb);\n"
    "    float lumaL = rgb2luma(texture(u_Color, vUv - vec2(u_TexelSize.x, 0.0)).rgb);\n"
    "    float lumaR = rgb2luma(texture(u_Color, vUv + vec2(u_TexelSize.x, 0.0)).rgb);\n"
    "    float lumaMin = min(lumaM, min(min(lumaU, lumaD), min(lumaL, lumaR)));\n"
    "    float lumaMax = max(lumaM, max(max(lumaU, lumaD), max(lumaL, lumaR)));\n"
    "    float lumaRange = lumaMax - lumaMin;\n"
    "    if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD_MAX)) {\n"
    "        FragColor = vec4(colorM, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    float lumaUL = rgb2luma(texture(u_Color, vUv + vec2(-u_TexelSize.x,  u_TexelSize.y)).rgb);\n"
    "    float lumaUR = rgb2luma(texture(u_Color, vUv + vec2( u_TexelSize.x,  u_TexelSize.y)).rgb);\n"
    "    float lumaDL = rgb2luma(texture(u_Color, vUv + vec2(-u_TexelSize.x, -u_TexelSize.y)).rgb);\n"
    "    float lumaDR = rgb2luma(texture(u_Color, vUv + vec2( u_TexelSize.x, -u_TexelSize.y)).rgb);\n"
    "    float lumaLplusR = lumaL + lumaR;\n"
    "    float lumaUplusD = lumaU + lumaD;\n"
    "    float edgeH = abs(-2.0 * lumaL + lumaDL + lumaUL) + abs(-2.0 * lumaM + lumaUplusD) * 2.0 + abs(-2.0 * lumaR + lumaDR + lumaUR);\n"
    "    float edgeV = abs(-2.0 * lumaU + lumaUL + lumaUR) + abs(-2.0 * lumaM + lumaLplusR) * 2.0 + abs(-2.0 * lumaD + lumaDL + lumaDR);\n"
    "    bool isHorizontal = (edgeH >= edgeV);\n"
    "    float lumaNeg = isHorizontal ? lumaD : lumaL;\n"
    "    float lumaPos = isHorizontal ? lumaU : lumaR;\n"
    "    float gradientNeg = abs(lumaNeg - lumaM);\n"
    "    float gradientPos = abs(lumaPos - lumaM);\n"
    "    float stepLength = isHorizontal ? u_TexelSize.y : u_TexelSize.x;\n"
    "    float lumaEdge = (gradientNeg >= gradientPos) ? (stepLength = -stepLength, lumaNeg) : lumaPos;\n"
    "    float gradient = max(gradientNeg, gradientPos);\n"
    "    vec2 posEdge = vUv;\n"
    "    if (isHorizontal) posEdge.y += stepLength * 0.5;\n"
    "    else posEdge.x += stepLength * 0.5;\n"
    "    vec2 stepTangent = isHorizontal ? vec2(u_TexelSize.x, 0.0) : vec2(0.0, u_TexelSize.y);\n"
    "    float lumaAvg = (lumaM + lumaEdge) * 0.5;\n"
    "    float gradientThreshold = gradient * 0.25;\n"
    "    vec2 posNeg = posEdge - stepTangent;\n"
    "    vec2 posPos = posEdge + stepTangent;\n"
    "    float lumaEndNeg = rgb2luma(texture(u_Color, posNeg).rgb) - lumaAvg;\n"
    "    float lumaEndPos = rgb2luma(texture(u_Color, posPos).rgb) - lumaAvg;\n"
    "    bool doneNeg = abs(lumaEndNeg) >= gradientThreshold;\n"
    "    bool donePos = abs(lumaEndPos) >= gradientThreshold;\n"
    "    for (int i = 2; i <= FXAA_SEARCH_STEPS; ++i) {\n"
    "        if (!doneNeg) {\n"
    "            posNeg -= stepTangent;\n"
    "            lumaEndNeg = rgb2luma(texture(u_Color, posNeg).rgb) - lumaAvg;\n"
    "            doneNeg = abs(lumaEndNeg) >= gradientThreshold;\n"
    "        }\n"
    "        if (!donePos) {\n"
    "            posPos += stepTangent;\n"
    "            lumaEndPos = rgb2luma(texture(u_Color, posPos).rgb) - lumaAvg;\n"
    "            donePos = abs(lumaEndPos) >= gradientThreshold;\n"
    "        }\n"
    "        if (doneNeg && donePos) break;\n"
    "    }\n"
    "    float distNeg = isHorizontal ? (vUv.x - posNeg.x) : (vUv.y - posNeg.y);\n"
    "    float distPos = isHorizontal ? (posPos.x - vUv.x) : (posPos.y - vUv.y);\n"
    "    bool isNegCloser = distNeg < distPos;\n"
    "    float distMin = min(distNeg, distPos);\n"
    "    float spanLength = distNeg + distPos;\n"
    "    float edgeLumaEnd = isNegCloser ? lumaEndNeg : lumaEndPos;\n"
    "    bool goodSpan = ((lumaM - lumaAvg < 0.0) != (edgeLumaEnd < 0.0));\n"
    "    float pixelOffset = goodSpan ? (0.5 - distMin / spanLength) : 0.0;\n"
    "    float subpixLuma = (lumaLplusR + lumaUplusD) * 2.0 + (lumaUL + lumaUR + lumaDL + lumaDR);\n"
    "    float subpixRange = abs(subpixLuma * (1.0 / 12.0) - lumaM);\n"
    "    float subpixFactor = clamp(subpixRange / max(lumaRange, 0.001), 0.0, 1.0);\n"
    "    float subpixBlend = clamp(max(0.0, subpixFactor - FXAA_SUBPIX_TRIM) * FXAA_SUBPIX_TRIM_SCALE, 0.0, FXAA_SUBPIX_CAP);\n"
    "    float finalOffset = max(pixelOffset, subpixBlend);\n"
    "    vec2 finalUv = vUv;\n"
    "    if (isHorizontal) finalUv.y += stepLength * finalOffset;\n"
    "    else finalUv.x += stepLength * finalOffset;\n"
    "    FragColor = vec4(texture(u_Color, finalUv).rgb, 1.0);\n"
    "}\n";
#else
const char* kFxaaFrag =
    "#version 330 core\n"
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Color;\n"
    "uniform vec2 u_TexelSize;\n"
    "#define FXAA_EDGE_THRESHOLD_MIN 0.0312\n"
    "#define FXAA_EDGE_THRESHOLD_MAX 0.125\n"
    "#define FXAA_SUBPIX_TRIM        0.25\n"
    "#define FXAA_SUBPIX_TRIM_SCALE  1.3333333\n"
    "#define FXAA_SUBPIX_CAP         0.75\n"
    "#define FXAA_SEARCH_STEPS       10\n"
    "float rgb2luma(vec3 rgb) {\n"
    "    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));\n"
    "}\n"
    "void main() {\n"
    "    vec3 colorM = texture(u_Color, vUv).rgb;\n"
    "    float lumaM = rgb2luma(colorM);\n"
    "    float lumaU = rgb2luma(texture(u_Color, vUv + vec2(0.0, u_TexelSize.y)).rgb);\n"
    "    float lumaD = rgb2luma(texture(u_Color, vUv - vec2(0.0, u_TexelSize.y)).rgb);\n"
    "    float lumaL = rgb2luma(texture(u_Color, vUv - vec2(u_TexelSize.x, 0.0)).rgb);\n"
    "    float lumaR = rgb2luma(texture(u_Color, vUv + vec2(u_TexelSize.x, 0.0)).rgb);\n"
    "    float lumaMin = min(lumaM, min(min(lumaU, lumaD), min(lumaL, lumaR)));\n"
    "    float lumaMax = max(lumaM, max(max(lumaU, lumaD), max(lumaL, lumaR)));\n"
    "    float lumaRange = lumaMax - lumaMin;\n"
    "    if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD_MAX)) {\n"
    "        FragColor = vec4(colorM, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    float lumaUL = rgb2luma(texture(u_Color, vUv + vec2(-u_TexelSize.x,  u_TexelSize.y)).rgb);\n"
    "    float lumaUR = rgb2luma(texture(u_Color, vUv + vec2( u_TexelSize.x,  u_TexelSize.y)).rgb);\n"
    "    float lumaDL = rgb2luma(texture(u_Color, vUv + vec2(-u_TexelSize.x, -u_TexelSize.y)).rgb);\n"
    "    float lumaDR = rgb2luma(texture(u_Color, vUv + vec2( u_TexelSize.x, -u_TexelSize.y)).rgb);\n"
    "    float lumaLplusR = lumaL + lumaR;\n"
    "    float lumaUplusD = lumaU + lumaD;\n"
    "    float edgeH = abs(-2.0 * lumaL + lumaDL + lumaUL) + abs(-2.0 * lumaM + lumaUplusD) * 2.0 + abs(-2.0 * lumaR + lumaDR + lumaUR);\n"
    "    float edgeV = abs(-2.0 * lumaU + lumaUL + lumaUR) + abs(-2.0 * lumaM + lumaLplusR) * 2.0 + abs(-2.0 * lumaD + lumaDL + lumaDR);\n"
    "    bool isHorizontal = (edgeH >= edgeV);\n"
    "    float lumaNeg = isHorizontal ? lumaD : lumaL;\n"
    "    float lumaPos = isHorizontal ? lumaU : lumaR;\n"
    "    float gradientNeg = abs(lumaNeg - lumaM);\n"
    "    float gradientPos = abs(lumaPos - lumaM);\n"
    "    float stepLength = isHorizontal ? u_TexelSize.y : u_TexelSize.x;\n"
    "    float lumaEdge = (gradientNeg >= gradientPos) ? (stepLength = -stepLength, lumaNeg) : lumaPos;\n"
    "    float gradient = max(gradientNeg, gradientPos);\n"
    "    vec2 posEdge = vUv;\n"
    "    if (isHorizontal) posEdge.y += stepLength * 0.5;\n"
    "    else posEdge.x += stepLength * 0.5;\n"
    "    vec2 stepTangent = isHorizontal ? vec2(u_TexelSize.x, 0.0) : vec2(0.0, u_TexelSize.y);\n"
    "    float lumaAvg = (lumaM + lumaEdge) * 0.5;\n"
    "    float gradientThreshold = gradient * 0.25;\n"
    "    vec2 posNeg = posEdge - stepTangent;\n"
    "    vec2 posPos = posEdge + stepTangent;\n"
    "    float lumaEndNeg = rgb2luma(texture(u_Color, posNeg).rgb) - lumaAvg;\n"
    "    float lumaEndPos = rgb2luma(texture(u_Color, posPos).rgb) - lumaAvg;\n"
    "    bool doneNeg = abs(lumaEndNeg) >= gradientThreshold;\n"
    "    bool donePos = abs(lumaEndPos) >= gradientThreshold;\n"
    "    for (int i = 2; i <= FXAA_SEARCH_STEPS; ++i) {\n"
    "        if (!doneNeg) {\n"
    "            posNeg -= stepTangent;\n"
    "            lumaEndNeg = rgb2luma(texture(u_Color, posNeg).rgb) - lumaAvg;\n"
    "            doneNeg = abs(lumaEndNeg) >= gradientThreshold;\n"
    "        }\n"
    "        if (!donePos) {\n"
    "            posPos += stepTangent;\n"
    "            lumaEndPos = rgb2luma(texture(u_Color, posPos).rgb) - lumaAvg;\n"
    "            donePos = abs(lumaEndPos) >= gradientThreshold;\n"
    "        }\n"
    "        if (doneNeg && donePos) break;\n"
    "    }\n"
    "    float distNeg = isHorizontal ? (vUv.x - posNeg.x) : (vUv.y - posNeg.y);\n"
    "    float distPos = isHorizontal ? (posPos.x - vUv.x) : (posPos.y - vUv.y);\n"
    "    bool isNegCloser = distNeg < distPos;\n"
    "    float distMin = min(distNeg, distPos);\n"
    "    float spanLength = distNeg + distPos;\n"
    "    float edgeLumaEnd = isNegCloser ? lumaEndNeg : lumaEndPos;\n"
    "    bool goodSpan = ((lumaM - lumaAvg < 0.0) != (edgeLumaEnd < 0.0));\n"
    "    float pixelOffset = goodSpan ? (0.5 - distMin / spanLength) : 0.0;\n"
    "    float subpixLuma = (lumaLplusR + lumaUplusD) * 2.0 + (lumaUL + lumaUR + lumaDL + lumaDR);\n"
    "    float subpixRange = abs(subpixLuma * (1.0 / 12.0) - lumaM);\n"
    "    float subpixFactor = clamp(subpixRange / max(lumaRange, 0.001), 0.0, 1.0);\n"
    "    float subpixBlend = clamp(max(0.0, subpixFactor - FXAA_SUBPIX_TRIM) * FXAA_SUBPIX_TRIM_SCALE, 0.0, FXAA_SUBPIX_CAP);\n"
    "    float finalOffset = max(pixelOffset, subpixBlend);\n"
    "    vec2 finalUv = vUv;\n"
    "    if (isHorizontal) finalUv.y += stepLength * finalOffset;\n"
    "    else finalUv.x += stepLength * finalOffset;\n"
    "    FragColor = vec4(texture(u_Color, finalUv).rgb, 1.0);\n"
    "}\n";
#endif

const char* kFogFrag =
    PP_FRAG_HEAD
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D u_Scene;\n"
    "uniform sampler2D u_Depth;\n"
    "uniform mat4 u_InvViewProj;\n"
    "uniform vec3 u_CameraPos;\n"
    "uniform float u_Density;\n"
    "uniform float u_HeightFalloff;\n"
    "uniform float u_Height;\n"
    "uniform float u_StartDist;\n"
    "uniform float u_MaxDist;\n"
    "uniform vec3 u_FogColor;\n"
    "uniform float u_SunScattering;\n"
    "uniform float u_SkyBlend;\n"
    "uniform vec3 u_SunDir;\n"
    "uniform vec3 u_SunColor;\n"
    "void main() {\n"
    "    vec4 sceneColor = texture(u_Scene, vUv);\n"
    "    float depth = texture(u_Depth, vUv).r;\n"
    "    vec4 clip = vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n"
    "    vec4 worldH = u_InvViewProj * clip;\n"
    "    vec3 worldPos = worldH.xyz / max(worldH.w, 0.00001);\n"
    "    vec3 rayDir = worldPos - u_CameraPos;\n"
    "    float dist = length(rayDir);\n"
    "    if (dist < 0.001) {\n"
    "        FragColor = sceneColor;\n"
    "        return;\n"
    "    }\n"
    "    vec3 rayNorm = rayDir / dist;\n"
    "    bool isSky = (depth >= 0.9999);\n"
    "    float effectiveDist = isSky ? (u_MaxDist * clamp(u_SkyBlend, 0.0, 1.0)) : dist;\n"
    "    if (effectiveDist <= u_StartDist) {\n"
    "        FragColor = sceneColor;\n"
    "        return;\n"
    "    }\n"
    "    float fogDist = min(effectiveDist - u_StartDist, u_MaxDist);\n"
    "    vec3 targetPos = u_CameraPos + rayNorm * effectiveDist;\n"
    "    float camY = u_CameraPos.y - u_Height;\n"
    "    float targetY = targetPos.y - u_Height;\n"
    "    float deltaY = targetY - camY;\n"
    "    float falloff = max(u_HeightFalloff, 0.0001);\n"
    "    float heightFactor = exp(-camY * falloff);\n"
    "    float integral = (abs(deltaY) > 0.001) ? (1.0 - exp(-deltaY * falloff)) / (deltaY * falloff) : 1.0;\n"
    "    float opticalDepth = u_Density * heightFactor * integral * fogDist;\n"
    "    opticalDepth = clamp(opticalDepth, 0.0, 30.0);\n"
    "    float transmittance = exp(-opticalDepth);\n"
    "    vec3 sunDirNorm = (length(u_SunDir) > 0.001) ? normalize(u_SunDir) : vec3(0.0, 1.0, 0.0);\n"
    "    float cosTheta = dot(rayNorm, sunDirNorm);\n"
    "    float g = 0.65;\n"
    "    float miePhase = (1.0 - g * g) / (4.0 * 3.14159265 * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 0.01), 1.5));\n"
    "    vec3 sunScattering = u_SunColor * (miePhase * u_SunScattering * 2.5);\n"
    "    vec3 finalFogColor = u_FogColor + sunScattering;\n"
    "    vec3 blended = mix(finalFogColor, sceneColor.rgb, transmittance);\n"
    "    FragColor = vec4(blended, sceneColor.a);\n"
    "}\n";

SlipShader* CompileFrag(const char* frag) {
    SlipShader* shader = SlipShader::CreateFromSource(kFullscreenVert, frag);
    if (!shader) {
        std::cerr << "[SlipPostProcess] No se pudo compilar un pase de post-proceso." << std::endl;
    }
    return shader;
}

} // namespace

void SlipPostProcess::Target::Destroy() {
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    if (tex != 0) {
        glDeleteTextures(1, &tex);
        tex = 0;
    }
    w = 0;
    h = 0;
}

void SlipPostProcess::Target::Alloc(int width, int height) {
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    if (tex != 0 && w == width && h == height) return;
    Destroy();
    w = width;
    h = height;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        // Fallback a GL_RGBA8 si el hardware no permite renderizar a targets half-float
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

SlipPostProcess& SlipPostProcess::Get() {
    static SlipPostProcess instance;
    return instance;
}

void SlipPostProcess::BindTex(int unit, unsigned int tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

void SlipPostProcess::DrawTo(unsigned int fbo, int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void SlipPostProcess::Init() {
    if (m_Ready) return;

    float quad[] = {
        -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,   1.0f, 1.0f
    };
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    unsigned char black[4] = { 0, 0, 0, 255 };
    glGenTextures(1, &m_Black);
    glBindTexture(GL_TEXTURE_2D, m_Black);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    unsigned char noiseBytes[16 * 4];
    for (int i = 0; i < 16; ++i) {
        noiseBytes[i * 4 + 0] = (unsigned char)(((i * 47) % 100) * 255 / 100);
        noiseBytes[i * 4 + 1] = (unsigned char)(((i * 91) % 100) * 255 / 100);
        noiseBytes[i * 4 + 2] = 0;
        noiseBytes[i * 4 + 3] = 255;
    }
    glGenTextures(1, &m_Noise);
    glBindTexture(GL_TEXTURE_2D, m_Noise);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, noiseBytes);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    float adapted[4] = { 0.18f, 0.0f, 0.0f, 1.0f };
    for (int i = 0; i < 2; ++i) {
        m_Adapt[i].Alloc(1, 1);
        glBindTexture(GL_TEXTURE_2D, m_Adapt[i].tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, adapted);
    }

    m_Prefilter = CompileFrag(kPrefilterFrag);
    m_Down = CompileFrag(kDownFrag);
    m_Up = CompileFrag(kUpFrag);
    m_LensShader = CompileFrag(kLensFrag);
    m_Ssao = CompileFrag(kSsaoFrag);
    m_AoBlurShader = CompileFrag(kAoBlurFrag);
    m_ApplyAo = CompileFrag(kApplyAoFrag);
    m_Gauss = CompileFrag(kGaussFrag);
    m_RadialShader = CompileFrag(kRadialFrag);
    m_LogLum = CompileFrag(kLogLumFrag);
    m_Reduce = CompileFrag(kReduceFrag);
    m_AdaptShader = CompileFrag(kAdaptFrag);
    m_Composite = CompileFrag(kCompositeFrag);
    m_FogShader = CompileFrag(kFogFrag);
    m_FxaaShader = CompileFrag(kFxaaFrag);
    m_Ready = m_Composite != nullptr;
}

void SlipPostProcess::Resize(int width, int height) {
    if (m_Width == width && m_Height == height) return;
    m_Width = width;
    m_Height = height;

    int bw = (std::max)(1, width / 2);
    int bh = (std::max)(1, height / 2);
    for (int i = 0; i < 5; ++i) {
        m_Bloom[i].Alloc(bw, bh);
        if (i < 4) m_BloomUp[i].Alloc(bw, bh);
        bw = (std::max)(1, bw / 2);
        bh = (std::max)(1, bh / 2);
    }
    m_LensExtract.Alloc((std::max)(1, width / 4), (std::max)(1, height / 4));
    m_Lens.Alloc(m_LensExtract.w, m_LensExtract.h);
    m_Ao.Alloc((std::max)(1, width / 2), (std::max)(1, height / 2));
    m_AoBlur.Alloc(m_Ao.w, m_Ao.h);
    m_Base.Alloc(width, height);
    m_FogTarget.Alloc(width, height);
    m_Blur[0].Alloc(width, height);
    m_Blur[1].Alloc(width, height);
    m_Radial.Alloc(width, height);
    m_Fxaa.Alloc(width, height);

    int side = 128;
    while (side > 1 && (side > width || side > height)) side /= 2;
    m_LumLevels = 0;
    for (int level = 0; level < 8 && side >= 1; ++level) {
        m_Lum[level].Alloc(side, side);
        m_LumLevels++;
        if (side == 1) break;
        side = (std::max)(1, side / 2);
    }
}

void SlipPostProcess::Shutdown() {
    for (int i = 0; i < 5; ++i) m_Bloom[i].Destroy();
    for (int i = 0; i < 4; ++i) m_BloomUp[i].Destroy();
    m_LensExtract.Destroy();
    m_Lens.Destroy();
    m_Ao.Destroy();
    m_AoBlur.Destroy();
    m_Base.Destroy();
    m_FogTarget.Destroy();
    m_Blur[0].Destroy();
    m_Blur[1].Destroy();
    m_Radial.Destroy();
    m_Fxaa.Destroy();
    for (int i = 0; i < 8; ++i) m_Lum[i].Destroy();
    m_Adapt[0].Destroy();
    m_Adapt[1].Destroy();
    if (m_VAO != 0) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO != 0) glDeleteBuffers(1, &m_VBO);
    if (m_Noise != 0) glDeleteTextures(1, &m_Noise);
    if (m_Black != 0) glDeleteTextures(1, &m_Black);
    m_VAO = m_VBO = m_Noise = m_Black = 0;
    auto drop = [](SlipShader*& shader) {
        if (shader) {
            shader->destroy();
            delete shader;
            shader = nullptr;
        }
    };
    drop(m_Prefilter);
    drop(m_Down);
    drop(m_Up);
    drop(m_LensShader);
    drop(m_Ssao);
    drop(m_AoBlurShader);
    drop(m_ApplyAo);
    drop(m_Gauss);
    drop(m_RadialShader);
    drop(m_LogLum);
    drop(m_Reduce);
    drop(m_AdaptShader);
    drop(m_Composite);
    drop(m_FogShader);
    m_Ready = false;
    m_Width = 0;
    m_Height = 0;
    m_LumLevels = 0;
}

void SlipPostProcess::ApplySSAO(unsigned int sceneColor, unsigned int depthTex, unsigned int normalTex,
                                const glm::mat4& view, const glm::mat4& projection, const glm::mat4& invProjection,
                                const PostProcessSettings& settings,
                                unsigned int targetFbo, int width, int height) {
    if (!settings.ssao || settings.ssaoIntensity <= 0.001f || sceneColor == 0 || targetFbo == 0 || depthTex == 0 || normalTex == 0 || width < 1 || height < 1) {
        return;
    }
    Init();
    if (!m_Ready || !m_Ssao || !m_AoBlurShader || !m_ApplyAo) return;
    Resize(width, height);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    m_Ssao->use();
    BindTex(0, depthTex);
    BindTex(1, normalTex);
    BindTex(2, m_Noise);
    m_Ssao->setInt("u_Depth", 0);
    m_Ssao->setInt("u_Normal", 1);
    m_Ssao->setInt("u_Noise", 2);
    m_Ssao->setMat4("u_InvProj", invProjection);
    m_Ssao->setMat4("u_Proj", projection);
    m_Ssao->setMat4("u_View", view);
    m_Ssao->setVec2("u_NoiseScale", (float)m_Ao.w / 4.0f, (float)m_Ao.h / 4.0f);
    m_Ssao->setFloat("u_Radius", settings.ssaoRadius);
    m_Ssao->setFloat("u_Bias", settings.ssaoBias);
    DrawTo(m_Ao.fbo, m_Ao.w, m_Ao.h);

    m_AoBlurShader->use();
    BindTex(0, m_Ao.tex);
    BindTex(1, depthTex);
    m_AoBlurShader->setInt("u_Ao", 0);
    m_AoBlurShader->setInt("u_Depth", 1);
    m_AoBlurShader->setMat4("u_InvProj", invProjection);
    DrawTo(m_AoBlur.fbo, m_AoBlur.w, m_AoBlur.h);

    // Copiar resultado con AO a m_Base y luego volcarlo de regreso a targetFbo (GL_COLOR_ATTACHMENT0)
    m_ApplyAo->use();
    BindTex(0, sceneColor);
    BindTex(1, m_AoBlur.tex);
    m_ApplyAo->setInt("u_Scene", 0);
    m_ApplyAo->setInt("u_Ao", 1);
    m_ApplyAo->setFloat("u_Intensity", settings.ssaoIntensity);
    m_ApplyAo->setFloat("u_Power", settings.ssaoPower);
    DrawTo(m_Base.fbo, m_Base.w, m_Base.h);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Base.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFbo);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);
#else
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
#endif
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glDepthMask(depthMask);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void SlipPostProcess::Apply(unsigned int sceneColor, unsigned int depthTex, unsigned int normalTex,
                            const glm::mat4& view, const glm::mat4& projection, const glm::mat4& invProjection,
                            float deltaTime, const PostProcessSettings& settings,
                            unsigned int presentFbo, int width, int height) {
    if (sceneColor == 0 || presentFbo == 0 || width < 1 || height < 1) return;
    Init();
    if (!m_Ready || !m_Composite) return;
    Resize(width, height);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blend = glIsEnabled(GL_BLEND);
    GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    unsigned int image = sceneColor;
    if (settings.ssao && settings.ssaoIntensity > 0.001f && depthTex != 0 && normalTex != 0 && m_Ssao && m_AoBlurShader && m_ApplyAo) {
        m_Ssao->use();
        BindTex(0, depthTex);
        BindTex(1, normalTex);
        BindTex(2, m_Noise);
        m_Ssao->setInt("u_Depth", 0);
        m_Ssao->setInt("u_Normal", 1);
        m_Ssao->setInt("u_Noise", 2);
        m_Ssao->setMat4("u_InvProj", invProjection);
        m_Ssao->setMat4("u_Proj", projection);
        m_Ssao->setMat4("u_View", view);
        m_Ssao->setVec2("u_NoiseScale", (float)m_Ao.w / 4.0f, (float)m_Ao.h / 4.0f);
        m_Ssao->setFloat("u_Radius", settings.ssaoRadius);
        m_Ssao->setFloat("u_Bias", settings.ssaoBias);
        DrawTo(m_Ao.fbo, m_Ao.w, m_Ao.h);

        m_AoBlurShader->use();
        BindTex(0, m_Ao.tex);
        BindTex(1, depthTex);
        m_AoBlurShader->setInt("u_Ao", 0);
        m_AoBlurShader->setInt("u_Depth", 1);
        m_AoBlurShader->setMat4("u_InvProj", invProjection);
        DrawTo(m_AoBlur.fbo, m_AoBlur.w, m_AoBlur.h);

        m_ApplyAo->use();
        BindTex(0, sceneColor);
        BindTex(1, m_AoBlur.tex);
        m_ApplyAo->setInt("u_Scene", 0);
        m_ApplyAo->setInt("u_Ao", 1);
        m_ApplyAo->setFloat("u_Intensity", settings.ssaoIntensity);
        m_ApplyAo->setFloat("u_Power", settings.ssaoPower);
        DrawTo(m_Base.fbo, m_Base.w, m_Base.h);
        image = m_Base.tex;
    }

    if (settings.fog && settings.fogDensity > 0.00001f && depthTex != 0 && m_FogShader) {
        glm::mat4 invView = glm::inverse(view);
        glm::mat4 invViewProj = invView * invProjection;
        glm::vec3 camPos = glm::vec3(invView[3]);

        m_FogShader->use();
        BindTex(0, image);
        BindTex(1, depthTex);
        m_FogShader->setInt("u_Scene", 0);
        m_FogShader->setInt("u_Depth", 1);
        m_FogShader->setMat4("u_InvViewProj", invViewProj);
        m_FogShader->setVec3("u_CameraPos", camPos);
        m_FogShader->setFloat("u_Density", settings.fogDensity);
        m_FogShader->setFloat("u_HeightFalloff", settings.fogHeightFalloff);
        m_FogShader->setFloat("u_Height", settings.fogHeight);
        m_FogShader->setFloat("u_StartDist", settings.fogStartDistance);
        m_FogShader->setFloat("u_MaxDist", settings.fogMaxDistance);
        m_FogShader->setVec3("u_FogColor", settings.fogColor);
        m_FogShader->setFloat("u_SunScattering", settings.fogSunScattering);
        m_FogShader->setFloat("u_SkyBlend", settings.fogSkyBlend);
        m_FogShader->setVec3("u_SunDir", settings.fogSunDirection);
        m_FogShader->setVec3("u_SunColor", settings.fogSunColor);
        DrawTo(m_FogTarget.fbo, m_FogTarget.w, m_FogTarget.h);
        image = m_FogTarget.tex;
    }

    unsigned int bloomTex = m_Black;
    float bloomIntensity = 0.0f;
    if (settings.bloom && settings.bloomIntensity > 0.001f && m_Prefilter && m_Down && m_Up) {
        m_Prefilter->use();
        BindTex(0, image);
        m_Prefilter->setInt("u_Src", 0);
        m_Prefilter->setFloat("u_Threshold", settings.bloomThreshold);
        m_Prefilter->setFloat("u_SoftKnee", settings.bloomSoftKnee);
        DrawTo(m_Bloom[0].fbo, m_Bloom[0].w, m_Bloom[0].h);

        for (int i = 1; i < 5; ++i) {
            m_Down->use();
            BindTex(0, m_Bloom[i - 1].tex);
            m_Down->setInt("u_Src", 0);
            m_Down->setVec2("u_Texel", 0.5f / (float)m_Bloom[i - 1].w, 0.5f / (float)m_Bloom[i - 1].h);
            DrawTo(m_Bloom[i].fbo, m_Bloom[i].w, m_Bloom[i].h);
        }

        unsigned int low = m_Bloom[4].tex;
        for (int i = 3; i >= 0; --i) {
            m_Up->use();
            BindTex(0, m_Bloom[i].tex);
            BindTex(1, low);
            m_Up->setInt("u_High", 0);
            m_Up->setInt("u_Low", 1);
            m_Up->setFloat("u_Scatter", settings.bloomRadius);
            DrawTo(m_BloomUp[i].fbo, m_BloomUp[i].w, m_BloomUp[i].h);
            low = m_BloomUp[i].tex;
        }
        bloomTex = m_BloomUp[0].tex;
        bloomIntensity = settings.bloomIntensity;
    }

    unsigned int lensTex = m_Black;
    float lensIntensity = 0.0f;
    if (settings.lensBloom && settings.lensIntensity > 0.001f && m_Prefilter && m_LensShader) {
        m_Prefilter->use();
        BindTex(0, image);
        m_Prefilter->setInt("u_Src", 0);
        m_Prefilter->setFloat("u_Threshold", settings.lensThreshold);
        m_Prefilter->setFloat("u_SoftKnee", 0.4f);
        DrawTo(m_LensExtract.fbo, m_LensExtract.w, m_LensExtract.h);

        m_LensShader->use();
        BindTex(0, m_LensExtract.tex);
        m_LensShader->setInt("u_Src", 0);
        m_LensShader->setFloat("u_Streak", settings.lensStreak * 0.35f);
        m_LensShader->setFloat("u_Halo", settings.lensHalo);
        DrawTo(m_Lens.fbo, m_Lens.w, m_Lens.h);
        lensTex = m_Lens.tex;
        lensIntensity = settings.lensIntensity;
    }

    if (settings.blurRadius > 0.5f && m_Gauss) {
        m_Gauss->use();
        BindTex(0, image);
        m_Gauss->setInt("u_Src", 0);
        m_Gauss->setVec2("u_Direction", 1.0f, 0.0f);
        m_Gauss->setFloat("u_Radius", settings.blurRadius);
        DrawTo(m_Blur[0].fbo, m_Blur[0].w, m_Blur[0].h);

        BindTex(0, m_Blur[0].tex);
        m_Gauss->setVec2("u_Direction", 0.0f, 1.0f);
        DrawTo(m_Blur[1].fbo, m_Blur[1].w, m_Blur[1].h);
        image = m_Blur[1].tex;
    }

    if (settings.radialBlurStrength > 0.001f && m_RadialShader) {
        m_RadialShader->use();
        BindTex(0, image);
        m_RadialShader->setInt("u_Src", 0);
        m_RadialShader->setVec2("u_Center", settings.radialBlurCenter);
        m_RadialShader->setFloat("u_Strength", settings.radialBlurStrength);
        DrawTo(m_Radial.fbo, m_Radial.w, m_Radial.h);
        image = m_Radial.tex;
    }

    unsigned int adapted = m_Adapt[m_AdaptIndex].tex;
    if (settings.autoExposure && m_LogLum && m_Reduce && m_AdaptShader && m_LumLevels > 0) {
        m_LogLum->use();
        BindTex(0, image);
        BindTex(1, bloomTex);
        BindTex(2, lensTex);
        m_LogLum->setInt("u_Image", 0);
        m_LogLum->setInt("u_Bloom", 1);
        m_LogLum->setInt("u_Lens", 2);
        m_LogLum->setFloat("u_BloomIntensity", bloomIntensity);
        m_LogLum->setVec3("u_BloomTint", settings.bloomTint);
        m_LogLum->setFloat("u_LensIntensity", lensIntensity);
        DrawTo(m_Lum[0].fbo, m_Lum[0].w, m_Lum[0].h);

        for (int i = 1; i < m_LumLevels; ++i) {
            m_Reduce->use();
            BindTex(0, m_Lum[i - 1].tex);
            m_Reduce->setInt("u_Src", 0);
            DrawTo(m_Lum[i].fbo, m_Lum[i].w, m_Lum[i].h);
        }

        int next = 1 - m_AdaptIndex;
        m_AdaptShader->use();
        BindTex(0, m_Lum[m_LumLevels - 1].tex);
        BindTex(1, m_Adapt[m_AdaptIndex].tex);
        m_AdaptShader->setInt("u_Cur", 0);
        m_AdaptShader->setInt("u_Prev", 1);
        m_AdaptShader->setFloat("u_Dt", deltaTime);
        m_AdaptShader->setFloat("u_Speed", settings.exposureSpeed);
        m_AdaptShader->setFloat("u_Min", settings.exposureMin);
        m_AdaptShader->setFloat("u_Max", (std::max)(settings.exposureMax, settings.exposureMin + 0.001f));
        DrawTo(m_Adapt[next].fbo, 1, 1);
        m_AdaptIndex = next;
        adapted = m_Adapt[m_AdaptIndex].tex;
    }

    m_Composite->use();
    BindTex(0, image);
    BindTex(1, bloomTex);
    BindTex(2, lensTex);
    BindTex(3, adapted);
    m_Composite->setInt("u_Image", 0);
    m_Composite->setInt("u_Bloom", 1);
    m_Composite->setInt("u_Lens", 2);
    m_Composite->setInt("u_Adapted", 3);
    m_Composite->setFloat("u_BloomIntensity", bloomIntensity);
    m_Composite->setVec3("u_BloomTint", settings.bloomTint);
    m_Composite->setFloat("u_LensIntensity", lensIntensity);
    m_Composite->setFloat("u_Auto", settings.autoExposure ? 1.0f : 0.0f);
    m_Composite->setFloat("u_Key", settings.exposureKey);
    m_Composite->setFloat("u_Compensation", settings.exposureCompensation);
    m_Composite->setFloat("u_Contrast", settings.contrast);
    m_Composite->setFloat("u_Saturation", settings.saturation);
    m_Composite->setFloat("u_Temperature", settings.temperature);
    m_Composite->setVec3("u_Lift", settings.lift);
    m_Composite->setVec3("u_Gamma", settings.gamma);
    m_Composite->setVec3("u_Gain", settings.gain);
    m_Composite->setVec3("u_Filter", settings.colorFilter);
    m_Composite->setInt("u_Tonemap", settings.tonemap);
    m_Composite->setFloat("u_Vignette", settings.vignetteIntensity);
    m_Composite->setFloat("u_VignetteSmooth", settings.vignetteSmoothness);
    m_Composite->setFloat("u_Letterbox", glm::clamp(settings.letterbox, 0.0f, 0.49f));
    m_Composite->setFloat("u_Pillarbox", glm::clamp(settings.pillarbox, 0.0f, 0.49f));

    bool useFxaa = settings.fxAA && (m_FxaaShader != nullptr) && (m_Fxaa.fbo != 0);
    unsigned int compositeTarget = useFxaa ? m_Fxaa.fbo : presentFbo;
    DrawTo(compositeTarget, width, height);

    if (useFxaa) {
        m_FxaaShader->use();
        BindTex(0, m_Fxaa.tex);
        m_FxaaShader->setInt("u_Color", 0);
        m_FxaaShader->setVec2("u_TexelSize", 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
        DrawTo(presentFbo, width, height);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glDepthMask(depthMask);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}
