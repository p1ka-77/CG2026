// Depth-only pass for one cascade of the directional-light shadow map.

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;

    int gAnimType;
    float3 gObjectPad;
};

cbuffer cbShadowPass : register(b1)
{
    float4x4 gLightViewProj;
    float gTotalTime;
    float3 gShadowPassPad;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

float4 VS(VertexIn vin) : SV_POSITION
{
    float3 posL = vin.PosL;

    // Keep the existing animated artwork consistent with GeometryPass.hlsl.
    if (gAnimType == 1)
    {
        float hang = (vin.PosL.y + 1.0f) * 0.5f;
        float swing = sin(gTotalTime * 1.5f) * 0.08f;
        posL.x += swing * (1.0f - hang);
    }

    float4 posW = mul(float4(posL, 1.0f), gWorld);
    return mul(posW, gLightViewProj);
}
