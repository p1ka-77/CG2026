// Final screen-space post-processing pass.
// VS creates a full-screen quad from SV_VertexID; no vertex buffer is used.

Texture2D gSceneColor : register(t0);
Texture2D gPosition   : register(t1);
Texture2D gNormal     : register(t2);
Texture2D gAlbedo     : register(t3);

cbuffer cbPostProcess : register(b0)
{
    uint gEffectMode;
    float gStrength;
    float gEdgeThreshold;
    float gPadding0;

    float2 gInvRenderTargetSize;
    float2 gPadding1;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(uint vertexId : SV_VertexID)
{
    VertexOut output;

    // IDs 0..3 become the four corners of a triangle-strip quad.
    // No position or UV data is read from a vertex buffer.
    float2 uv = float2(vertexId & 1u, vertexId >> 1u);
    output.TexC = uv;
    output.PosH = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        0.0f,
        1.0f);

    return output;
}

float ComputeGBufferEdge(int2 centerPixel, int2 dimensions)
{
    float4 centerPosition = gPosition.Load(int3(centerPixel, 0));
    float3 centerNormal =
        normalize(gNormal.Load(int3(centerPixel, 0)).xyz * 2.0f - 1.0f);
    float3 centerAlbedo = gAlbedo.Load(int3(centerPixel, 0)).rgb;
    bool centerHasGeometry = centerPosition.w > 0.5f;
    float strongestDifference = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            int2 samplePixel = clamp(
                centerPixel + int2(x, y),
                int2(0, 0),
                dimensions - int2(1, 1));
            float4 samplePosition = gPosition.Load(int3(samplePixel, 0));
            bool sampleHasGeometry = samplePosition.w > 0.5f;

            if (centerHasGeometry != sampleHasGeometry)
            {
                strongestDifference = 1.0f;
                continue;
            }

            if (!centerHasGeometry)
            {
                continue;
            }

            float3 sampleNormal = normalize(
                gNormal.Load(int3(samplePixel, 0)).xyz * 2.0f - 1.0f);
            float3 sampleAlbedo = gAlbedo.Load(int3(samplePixel, 0)).rgb;

            float positionDifference =
                saturate(length(samplePosition.xyz - centerPosition.xyz) * 0.35f);
            float normalDifference =
                saturate(length(sampleNormal - centerNormal) * 0.75f);
            float albedoDifference =
                saturate(length(sampleAlbedo - centerAlbedo) * 0.45f);

            strongestDifference = max(
                strongestDifference,
                max(positionDifference, max(normalDifference, albedoDifference)));
        }
    }

    return smoothstep(
        gEdgeThreshold,
        gEdgeThreshold + 0.14f,
        strongestDifference);
}

float4 PS(VertexOut input) : SV_Target
{
    uint width;
    uint height;
    gSceneColor.GetDimensions(width, height);

    int2 dimensions = int2(width, height);
    int2 pixel = clamp(
        int2(input.PosH.xy),
        int2(0, 0),
        dimensions - int2(1, 1));

    float3 color = gSceneColor.Load(int3(pixel, 0)).rgb;

    // Technique 1: luminance-based grayscale.
    if (gEffectMode == 1 || gEffectMode == 3)
    {
        float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        color = lerp(color, luminance.xxx, gStrength);
    }

    // Technique 2: outlines from position, normal and albedo discontinuities.
    if (gEffectMode == 2 || gEffectMode == 3)
    {
        float edge = ComputeGBufferEdge(pixel, dimensions);
        float3 outlineColor = float3(0.005f, 0.012f, 0.025f);
        color = lerp(color, outlineColor, saturate(edge * gStrength));
    }

    return float4(color, 1.0f);
}
