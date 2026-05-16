// LightingPass.hlsl
// Второй проход deferred rendering.
// Читает GBuffer и считает финальное освещение.

Texture2D gPosition : register(t0);
Texture2D gNormal : register(t1);
Texture2D gAlbedo : register(t2);

SamplerState gsamPoint : register(s0);

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

    for (int i = 0; i < gLightCount && i < MAX_LIGHTS; ++i)
    {
        result += ComputeLightContribution(
            gLights[i],
            posW,
            N,
            toEye,
            albedo,
            roughness);
    }

    // Простая Reinhard tone mapping.
    result = result / (result + 1.0f);

    return float4(result, 1.0f);
}