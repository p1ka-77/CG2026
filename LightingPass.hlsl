// LightingPass.hlsl
// Второй проход deferred rendering.
// Читает GBuffer и считает финальное освещение.

Texture2D gPosition : register(t0);
Texture2D gNormal : register(t1);
Texture2D gAlbedo : register(t2);
Texture2DArray<float> gShadowMap : register(t3);

SamplerState gsamPoint : register(s0);
SamplerComparisonState gsamShadow : register(s1);

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2

struct LightData
{
    float3 Position;
    float Range;

    float3 Direction;
    float SpotAngle;

    float3 Strength;
    int Type;

    float SpotFalloff;
    float3 Pad;
};

#define MAX_LIGHTS 16

cbuffer cbLighting : register(b0)
{
    LightData gLights[MAX_LIGHTS];

    int gLightCount;
    float3 gEyePosW;

    float4 gAmbientLight;

    float4x4 gShadowTransforms[4];
    float4 gCascadeSplits;
    // x = resolution, y = inverse resolution, z = depth bias, w = normal bias.
    float4 gShadowMapInfo;
    // xyz = camera forward, w = maximum shadow distance.
    float4 gCameraForward;

    int gShadowsEnabled;
    int gVisualizeCascades;
    float2 gShadowPad;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    // Вершины fullscreen quad уже находятся в NDC.
    vout.PosH = float4(vin.PosL, 1.0f);
    vout.TexC = vin.TexC;

    return vout;
}

float3 ComputeLightContribution(
    LightData light,
    float3 posW,
    float3 N,
    float3 toEye,
    float3 albedo,
    float roughness)
{
    float3 L = float3(0.0f, 0.0f, 0.0f);
    float attenuation = 1.0f;

    if (light.Type == LIGHT_DIRECTIONAL)
    {
        L = normalize(-light.Direction);
    }
    else if (light.Type == LIGHT_POINT)
    {
        float3 toLight = light.Position - posW;
        float dist = length(toLight);

        if (dist > light.Range)
        {
            return float3(0.0f, 0.0f, 0.0f);
        }

        L = toLight / dist;

        attenuation = 1.0f - saturate(dist / light.Range);
        attenuation *= attenuation;
    }
    else if (light.Type == LIGHT_SPOT)
    {
        float3 toLight = light.Position - posW;
        float dist = length(toLight);

        if (dist > light.Range)
        {
            return float3(0.0f, 0.0f, 0.0f);
        }

        L = toLight / dist;

        float cosAngle = dot(-L, normalize(light.Direction));
        float cosOuter = cos(light.SpotAngle);
        float cosInner = cos(light.SpotAngle * 0.5f);

        float spotFactor = saturate(
            (cosAngle - cosOuter) / (cosInner - cosOuter + 0.0001f));

        spotFactor = pow(spotFactor, light.SpotFalloff);

        attenuation = 1.0f - saturate(dist / light.Range);
        attenuation *= spotFactor;
    }

    float NdotL = max(dot(N, L), 0.0f);

    float3 diffuse = albedo * NdotL;

    float3 H = normalize(L + toEye);

    float shininess = max(1.0f - roughness, 0.01f) * 256.0f;
    float NdotH = max(dot(N, H), 0.0f);

    float3 specular = pow(NdotH, shininess) * float3(0.3f, 0.3f, 0.3f);

    return (diffuse + specular) * light.Strength * attenuation;
}

int SelectShadowCascade(float viewDepth)
{
    int cascadeIndex = 0;
    cascadeIndex += viewDepth > gCascadeSplits.x;
    cascadeIndex += viewDepth > gCascadeSplits.y;
    cascadeIndex += viewDepth > gCascadeSplits.z;
    return min(cascadeIndex, 3);
}

float ComputeShadowFactor(
    float3 posW,
    float3 normalW,
    int cascadeIndex)
{
    float3 biasedPosW = posW + normalW * gShadowMapInfo.w;
    float4 shadowPos = mul(
        float4(biasedPosW, 1.0f),
        gShadowTransforms[cascadeIndex]);

    shadowPos.xyz /= shadowPos.w;

    float2 uv;
    uv.x = shadowPos.x * 0.5f + 0.5f;
    uv.y = -shadowPos.y * 0.5f + 0.5f;

    if (shadowPos.z <= 0.0f || shadowPos.z >= 1.0f ||
        any(uv < 0.0f) || any(uv > 1.0f))
    {
        return 1.0f;
    }

    float comparisonDepth = shadowPos.z - gShadowMapInfo.z;
    float texelSize = gShadowMapInfo.y;
    float shadow = 0.0f;

    // 3x3 percentage-closer filtering.
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += gShadowMap.SampleCmpLevelZero(
                gsamShadow,
                float3(uv + offset, cascadeIndex),
                comparisonDepth);
        }
    }

    return shadow / 9.0f;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 posData = gPosition.Sample(gsamPoint, pin.TexC);
    float4 normalData = gNormal.Sample(gsamPoint, pin.TexC);
    float4 albedoData = gAlbedo.Sample(gsamPoint, pin.TexC);

    // Если GBuffer в этом пикселе пустой — возвращаем тёмный фон.
    if (posData.w < 0.5f)
    {
        return float4(0.1f, 0.1f, 0.1f, 1.0f);
    }

    float3 posW = posData.xyz;

    // Распаковка нормали из [0, 1] обратно в [-1, 1].
    float3 N = normalize(normalData.xyz * 2.0f - 1.0f);

    float3 albedo = albedoData.rgb;
    float roughness = albedoData.a;

    float3 toEye = normalize(gEyePosW - posW);

    float3 ambient = gAmbientLight.rgb * gAmbientLight.a * albedo;
    float3 result = ambient;
    float viewDepth = dot(posW - gEyePosW, normalize(gCameraForward.xyz));
    int cascadeIndex = SelectShadowCascade(viewDepth);
    float shadowFactor = 1.0f;

    if (gShadowsEnabled != 0 &&
        viewDepth >= 0.0f &&
        viewDepth <= gCascadeSplits.w)
    {
        shadowFactor = ComputeShadowFactor(posW, N, cascadeIndex);
    }

    for (int i = 0; i < gLightCount && i < MAX_LIGHTS; ++i)
    {
        float3 contribution = ComputeLightContribution(
            gLights[i],
            posW,
            N,
            toEye,
            albedo,
            roughness);

        if (gLights[i].Type == LIGHT_DIRECTIONAL)
        {
            contribution *= shadowFactor;
        }

        result += contribution;
    }

    if (gVisualizeCascades != 0 &&
        viewDepth >= 0.0f &&
        viewDepth <= gCascadeSplits.w)
    {
        const float3 cascadeColors[4] =
        {
            float3(1.0f, 0.35f, 0.35f),
            float3(0.35f, 1.0f, 0.35f),
            float3(0.35f, 0.55f, 1.0f),
            float3(1.0f, 0.85f, 0.3f)
        };
        result = lerp(result, result * cascadeColors[cascadeIndex], 0.35f);
    }

    // Простая Reinhard tone mapping.
    result = result / (result + 1.0f);

    return float4(result, 1.0f);
}
