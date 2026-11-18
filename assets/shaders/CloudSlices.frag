#version 300 es
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

float Remap(float val, float inMin, float inMax, float outMin, float outMax)
{
    return clamp(outMin + ((val - inMin) / max(inMax - inMin, 0.0001)) * (outMax - outMin), outMin, outMax);
}

void main()
{
    float erosionFactor = clamp(u_CloudEdgeErosion > 0.0 ? u_CloudEdgeErosion : 0.60, 0.0, 1.0);
    float detailFreq = max(u_CloudDetailScale > 0.0 ? u_CloudDetailScale : 4.5, 1.0);

    // Muestreo multi-octava con domain warping para desperfectos y bordes no uniformes
    vec4 nMacro = texture(u_CloudNoise, v_VolumeUV.xy);
    vec2 warp = (nMacro.gb - vec2(0.5)) * (0.05 * erosionFactor);
    vec2 uvTurb = v_VolumeUV.xy + warp;

    vec4 nDetail = texture(u_CloudNoise, uvTurb * detailFreq + vec2(0.18, 0.27));
    float highFreqDetail = mix(nDetail.r, nDetail.g, 0.40);

    // Perfil vertical de cúmulo: base plana de condensación + cúspides esponjosas
    float baseCut = smoothstep(0.0, 0.12, v_VolumeUV.z);
    float topCut  = smoothstep(1.0, 0.35, v_VolumeUV.z);
    float heightGradient = baseCut * topCut;

    // Cobertura y modulación de densidad macro
    float cov = clamp(u_CloudCoverage, 0.05, 0.95);
    float baseCloud = Remap(nMacro.r, 1.0 - cov, 1.0, 0.0, 1.0);

    // Esculpido de desperfectos en bordes
    float erosionAmount = highFreqDetail * (erosionFactor * mix(0.40, 0.85, v_VolumeUV.z));
    float rawDensity = Remap(baseCloud, erosionAmount, 1.0, 0.0, 1.0);

    // Protección crítica de fill-rate TBDR: early discard en zonas despejadas
    float discardLimit = clamp(u_DiscardThreshold, 0.005, 0.25);
    if (rawDensity < discardLimit) {
        discard;
    }

    // Grosor óptico por capa (calibrado físicamente por slice)
    float sliceThickness = u_CloudLayerThickness / max(float(u_TotalSlices), 1.0);
    float opticalDepth = rawDensity * heightGradient * u_CloudDensity * sliceThickness * 3.5;
    float sliceStepAlpha = clamp(1.0 - exp(-opticalDepth), 0.0, 0.98);

    if (sliceStepAlpha < 0.008) {
        discard;
    }

    // Auto-sombras volumétricas: penetración solar vertical (Beer-Lambert)
    float sunPenetration = exp(-(1.0 - v_VolumeUV.z) * 2.6 * clamp(u_Absorption, 0.2, 3.0));

    // Powder effect en micro-bordes
    float powder = 1.0 - exp(-rawDensity * max(u_PowderFactor, 0.1) * 3.0);

    // Dispersión frontal Henyey-Greenstein para el halo solar
    vec3 viewDirNorm = normalize(v_ViewDir);
    vec3 sunDirNorm  = normalize(u_SunDirection);
    float cosTheta = dot(viewDirNorm, sunDirNorm);
    float g = u_HenyeyG;
    float hg = (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 0.01), 1.5));
    hg = clamp(hg, 0.1, 3.5);

    // Iluminación directa y ambiental
    vec3 directSun = u_SunColor * (sunPenetration * (powder * hg * 2.0 + 0.25));
    vec3 ambient = u_CloudAmbientColor * mix(0.40, 1.15, v_VolumeUV.z * (0.3 + 0.7 * sunPenetration));
    vec3 cloudColor = directSun + ambient;

    FragColor = vec4(cloudColor, sliceStepAlpha);
}
