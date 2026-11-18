#version 330 core

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

float LinearizeDepth(float d)
{
    float zNdc = d * 2.0 - 1.0;
    return (2.0 * u_NearPlane * u_FarPlane) / (u_FarPlane + u_NearPlane - zNdc * (u_FarPlane - u_NearPlane));
}

// Intersección analítica del rayo con la placa atmosférica horizontal [yMin, yMax]
bool IntersectCloudLayer(vec3 ro, vec3 rd, float yMin, float yMax, out float tNear, out float tFar)
{
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

float Remap(float val, float inMin, float inMax, float outMin, float outMax)
{
    return clamp(outMin + ((val - inMin) / max(inMax - inMin, 0.0001)) * (outMax - outMin), outMin, outMax);
}

// Muestreo volumétrico de densidad con modelado físico de cúmulos y desperfectos en bordes
float SampleDensity(vec3 pos, float heightFrac)
{
    // Perfil vertical de cúmulo: base plana de condensación con caída rápida abajo y cima esculpida
    float baseShape = smoothstep(0.0, 0.07, heightFrac);
    float topShape  = smoothstep(1.0, 0.28, heightFrac);
    float verticalShape = baseShape * topShape;
    if (verticalShape <= 0.001) return 0.0;

    // Desplazamiento base uniforme en XZ sin cizalladura temporal
    vec2 baseUV = pos.xz * u_CloudScale + u_WindOffset;
    // Micro-variación sutil acotada por altura (evita estiramientos diagonales con el tiempo)
    vec2 heightTurbulence = vec2(sin(heightFrac * 3.14159), cos(heightFrac * 3.14159)) * 0.012;
    vec2 uv = baseUV + heightTurbulence;
    vec4 nMacro = texture(u_CloudNoise, uv);

    // 1. Domain Warping (desplazamiento turbulento por cizalladura de viento)
    // Desplaza los bordes de la nube creando filamentos, giros y rizos en lugar de bordes lisos
    float erosionFactor = clamp(u_CloudEdgeErosion > 0.0 ? u_CloudEdgeErosion : 0.60, 0.0, 1.0);
    vec2 warp = (nMacro.gb - vec2(0.5)) * (0.06 * erosionFactor);
    vec2 uvTurb = uv + warp;

    // 2. Muestreo multi-escala para micro-desperfectos y fractales
    float detailFreq = max(u_CloudDetailScale > 0.0 ? u_CloudDetailScale : 4.5, 1.0);
    vec4 nMid = texture(u_CloudNoise, uvTurb * detailFreq + vec2(0.23, 0.41));
    vec4 nHigh = texture(u_CloudNoise, uvTurb * (detailFreq * 2.8) + vec2(0.67, 0.19));

    // FBM de detalle de alta frecuencia (G y B son octavas finas de Worley)
    float highFreqErosion = nMid.r * 0.48 + nMid.g * 0.34 + nHigh.b * 0.18;

    // 3. Forma macro del cúmulo con modulación de cobertura
    float coverage = clamp(u_CloudCoverage, 0.01, 0.99);
    float baseCloud = Remap(nMacro.r, 1.0 - coverage, 1.0, 0.0, 1.0);

    if (baseCloud <= 0.0001) {
        return 0.0;
    }

    // 4. Esculpido de bordes: la erosión de alta frecuencia carva cavidades y jirones irregulares
    float heightErosionMod = mix(0.40, 0.90, heightFrac);
    float erosionAmount = highFreqErosion * clamp(erosionFactor * heightErosionMod, 0.0, 0.92);

    // Algoritmo de remapado de densidad para bordes nítidos y deshilachados
    float erodedCloud = Remap(baseCloud, erosionAmount, 1.0, 0.0, 1.0);

    // Contraste para eliminar el aspecto de difuminado o spray uniforme
    float crispDensity = pow(erodedCloud, mix(1.0, 1.45, erosionFactor));

    return crispDensity * verticalShape * (u_CloudDensity * 8.5);
}

// Muestreo ultrarrápido de densidad macro para rayos de auto-sombra solar (sin FBM de alta frecuencia)
float SampleDensityShadow(vec3 pos, float heightFrac)
{
    float baseShape = smoothstep(0.0, 0.07, heightFrac);
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

float HenyeyGreenstein(float cosTheta, float g)
{
    return (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 0.01), 1.5));
}

void main()
{
    vec3 rayDir = normalize(v_WorldRay);
    vec3 rayOrigin = u_CameraPos;

    float yMin = u_CloudBaseHeight;
    float yMax = u_CloudBaseHeight + u_CloudLayerThickness;

    float tNear, tFar;
    if (!IntersectCloudLayer(rayOrigin, rayDir, yMin, yMax, tNear, tFar)) {
        FragColor = vec4(0.0);
        return;
    }

    // Acotar distancia máxima para eliminar saltos gigantes en el horizonte
    tFar = min(tFar, tNear + 14000.0);
    tFar = min(tFar, 28000.0);
    if (tNear >= tFar) {
        FragColor = vec4(0.0);
        return;
    }

    // Comprobación de oclusión contra geometría opaca de la escena
    float rawDepth = texture(u_SceneDepth, v_UV).r;
    if (rawDepth < 0.99999) {
        vec4 clip = vec4(v_UV * 2.0 - 1.0, rawDepth * 2.0 - 1.0, 1.0);
        vec4 vPos = u_InvProjection * clip;
        vPos /= max(abs(vPos.w), 1e-5);
        vec3 wPos = (u_InvView * vPos).xyz;
        float sceneDistAlongRay = length(wPos - u_CameraPos);

        // Si la cámara y la geometría están por debajo de la base de las nubes, la geometría bloquea completamente
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

    // Desvanecimiento suave en el horizonte
    float horizonFade = smoothstep(0.006, 0.055, abs(rayDir.y));

    // --- BLUE NOISE JITTERING ---
    vec2 screenUV = gl_FragCoord.xy / 64.0;
    float jitter = texture(u_BlueNoise, screenUV).r;

    int steps = clamp(u_RaymarchSteps, 8, 48);
    float marchDistance = tFar - tNear;
    float stepSize = marchDistance / float(steps);

    // Acotar tamaño máximo de paso
    stepSize = min(stepSize, 280.0);
    marchDistance = stepSize * float(steps);
    tFar = tNear + marchDistance;

    // Posición inicial con offset de ruido azul
    float tCurrent = tNear + stepSize * jitter;

    // Fase dual Henyey-Greenstein: lóbulo frontal plateado + sutil retrodispersión
    vec3 sunDir = normalize(u_SunDirection);
    float cosTheta = dot(rayDir, sunDir);
    float hgFwd = HenyeyGreenstein(cosTheta, u_HenyeyG);
    float hgBwd = HenyeyGreenstein(cosTheta, -0.22);
    float phase = mix(hgBwd, hgFwd, 0.75);
    phase = clamp(phase, 0.05, 3.8);

    vec3 accumLight = vec3(0.0);
    float transmittance = 1.0;

    // Paso de luz secundario para auto-sombras
    float lightStepSize = u_CloudLayerThickness * 0.16;

    for (int i = 0; i < steps; ++i) {
        if (tCurrent > tFar || transmittance < 0.01) break;

        vec3 currentPos = rayOrigin + rayDir * tCurrent;
        float heightFrac = clamp((currentPos.y - yMin) / max(u_CloudLayerThickness, 1.0), 0.0, 1.0);

        float density = SampleDensity(currentPos, heightFrac);

        if (density > 0.0005) {
            // Marcha secundaria rápida hacia el sol para calcular auto-sombras (2 pasos macro)
            float sunOpticalDepth = 0.0;
            float stepShadow = lightStepSize * 1.75;
            for (int j = 1; j <= 2; ++j) {
                vec3 lpos = currentPos + sunDir * (float(j) * stepShadow);
                float lHeightFrac = clamp((lpos.y - yMin) / max(u_CloudLayerThickness, 1.0), 0.0, 1.0);
                sunOpticalDepth += SampleDensityShadow(lpos, lHeightFrac) * stepShadow;
            }

            // Transmitancia solar Beer-Lambert + aproximación de dispersión múltiple
            float sunTransmittance = exp(-sunOpticalDepth * u_Absorption);
            float multipleScattering = exp(-sunOpticalDepth * u_Absorption * 0.22) * 0.40;
            float sunVisibility = clamp(sunTransmittance + multipleScattering, 0.0, 1.0);

            // Powder effect para bordes plateados
            float powder = 1.0 - exp(-density * u_PowderFactor * 3.0);

            // Transmitancia del paso primario
            float opticalDepth = density * stepSize * u_Absorption;
            float stepTransmittance = exp(-opticalDepth);

            // Iluminación solar directa: auto-sombra en la panza de la nube, resplandor en la cima
            vec3 directLight = u_SunColor * (phase * powder * sunVisibility * 2.8 + 0.04 * sunVisibility);

            // Iluminación ambiental: sombras inferiores oscuras (piso del cúmulo), tope más claro
            vec3 darkAmbient = u_CloudAmbientColor * 0.32;
            vec3 topAmbient  = u_CloudAmbientColor * 1.15;
            vec3 ambientLight = mix(darkAmbient, topAmbient, heightFrac * (0.35 + 0.65 * sunVisibility));

            vec3 stepLight = directLight + ambientLight;

            // Integración de radiancia
            accumLight += stepLight * transmittance * (1.0 - stepTransmittance);
            transmittance *= stepTransmittance;
        }

        tCurrent += stepSize;
    }

    float cloudAlpha = clamp(1.0 - transmittance, 0.0, 1.0) * horizonFade;
    accumLight *= horizonFade;
    FragColor = vec4(accumLight, cloudAlpha);
}
