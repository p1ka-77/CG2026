// GeometryPass.hlsl
// ѕервый проход deferred rendering.
// ѕишет данные поверхности в GBuffer.

Texture2D gDiffuseMap : register(t0);
SamplerState gsamLinear : register(s0);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;

    int gAnimType;
    float3 gPad;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;

    float3 gEyePosW;
    float cbPerObjectPad1;

    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;

    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;

    float4 gAmbientLight;
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

struct GBufferOutput
{
    float4 Position : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Albedo : SV_Target2;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

    float3 posL = vin.PosL;

    // ѕроста€ вертексна€ анимаци€.
    // –аботает только если gAnimType == 1.
    if (gAnimType == 1)
    {
        float hang = (vin.PosL.y + 1.0f) * 0.5f;
        float swing = sin(gTotalTime * 1.5f) * 0.08f;

        posL.x += swing * (1.0f - hang);
    }

    float4 posW = mul(float4(posL, 1.0f), gWorld);

    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, gViewProj);

    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);

    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;

    return vout;
}

GBufferOutput PS(VertexOut pin)
{
    GBufferOutput gout;

    float4 texColor = gDiffuseMap.Sample(gsamLinear, pin.TexC);

    // RT0 Ч мирова€ позици€.
    // w = 1 означает, что пиксель был заполнен геометрией.
    gout.Position = float4(pin.PosW, 1.0f);

    // RT1 Ч нормаль.
    // ”паковка из диапазона [-1, 1] в [0, 1].
    float3 N = normalize(pin.NormalW);
    gout.Normal = float4(N * 0.5f + 0.5f, 1.0f);

    // RT2 Ч цвет поверхности + roughness.
    float3 albedo = texColor.rgb * gDiffuseAlbedo.rgb;
    gout.Albedo = float4(albedo, gRoughness);

    return gout;
}