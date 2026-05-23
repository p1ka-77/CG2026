// TessellationPass.hlsl
// Geometry Pass для tessellated surface.
// Пишет данные в GBuffer: position / normal / albedo+roughness.

// t0 — diffuse / albedo texture
// t1 — normal map
// t2 — displacement / height map
Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);

SamplerState gsamLinear : register(s0);

// Такой же Object CB, как в GeometryPass.hlsl
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;

    int gAnimType;
    float3 gPad;
};

// Такой же Pass CB, как в GeometryPass.hlsl
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

// Такой же Material CB
cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

// Отдельные настройки тесселяции
cbuffer cbTessellation : register(b3)
{
    float gMinTessFactor; // например 1
    float gMaxTessFactor; // например 8
    float gMinTessDistance; // например 3
    float gMaxTessDistance; // например 25

    float gDisplacementScale; // сила сдвига по height map
    float3 gTessPad;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct HullOut
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct PatchTess
{
    float EdgeTess[4] : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};

struct DomainOut
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
    VertexOut vout;

    vout.PosL = vin.PosL;
    vout.NormalL = vin.NormalL;
    vout.TexC = vin.TexC;

    return vout;
}

// Расчёт tessellation factor в зависимости от расстояния до камеры.
float CalcTessFactor(float3 worldPos)
{
    float distToEye = distance(worldPos, gEyePosW);

    // Близко к камере — tessellation высокая.
    // Далеко от камеры — tessellation низкая.
    float s = saturate(
        (gMaxTessDistance - distToEye) /
        (gMaxTessDistance - gMinTessDistance)
    );

    return lerp(gMinTessFactor, gMaxTessFactor, s);
}

// Patch constant function для quad patch из 4 control points.
PatchTess ConstantHS(InputPatch<VertexOut, 4> patch, uint patchID : SV_PrimitiveID)
{
    PatchTess pt;

    float3 centerL =
        0.25f * (
            patch[0].PosL +
            patch[1].PosL +
            patch[2].PosL +
            patch[3].PosL
        );

    float4 centerW = mul(float4(centerL, 1.0f), gWorld);

    float tess = CalcTessFactor(centerW.xyz);

    pt.EdgeTess[0] = tess;
    pt.EdgeTess[1] = tess;
    pt.EdgeTess[2] = tess;
    pt.EdgeTess[3] = tess;

    pt.InsideTess[0] = tess;
    pt.InsideTess[1] = tess;

    return pt;
}

[domain("quad")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("ConstantHS")]
HullOut HS(InputPatch<VertexOut, 4> patch, uint i : SV_OutputControlPointID)
{
    HullOut hout;

    hout.PosL = patch[i].PosL;
    hout.NormalL = patch[i].NormalL;
    hout.TexC = patch[i].TexC;

    return hout;
}

[domain("quad")]
DomainOut DS(PatchTess patchTess, float2 uv : SV_DomainLocation, const OutputPatch<HullOut, 4> patch)
{
    DomainOut dout;

    // Bilinear interpolation по 4 control points.
    float3 posL =
        lerp(
            lerp(patch[0].PosL, patch[1].PosL, uv.x),
            lerp(patch[2].PosL, patch[3].PosL, uv.x),
            uv.y
        );

    float3 normalL =
        normalize(
            lerp(
                lerp(patch[0].NormalL, patch[1].NormalL, uv.x),
                lerp(patch[2].NormalL, patch[3].NormalL, uv.x),
                uv.y
            )
        );

    float2 texC =
        lerp(
            lerp(patch[0].TexC, patch[1].TexC, uv.x),
            lerp(patch[2].TexC, patch[3].TexC, uv.x),
            uv.y
        );

    float4 texCTransformed = mul(float4(texC, 0.0f, 1.0f), gTexTransform);
    texC = mul(texCTransformed, gMatTransform).xy;

    // Height/displacement.
    // Берём значение из displacement map и двигаем вершину вдоль нормали.
    float height = gDisplacementMap.SampleLevel(gsamLinear, texC, 0.0f).r;

    // Для пола лучше двигать вершины только вверх.
    // Так поверхность не будет проваливаться под пол и выглядеть как плавающая волна.
    float displacement = height * gDisplacementScale;

    posL += normalL * displacement;

    float4 posW = mul(float4(posL, 1.0f), gWorld);

    dout.PosW = posW.xyz;
    dout.PosH = mul(posW, gViewProj);

    dout.NormalW = normalize(mul(normalL, (float3x3) gWorld));
    dout.TexC = texC;

    return dout;
}

// Восстановление normal map через экранные производные.
// Это не требует tangent в вершинах.
float3 ApplyNormalMap(float3 normalW, float3 posW, float2 texC)
{
    float3 normalSample = gNormalMap.Sample(gsamLinear, texC).xyz;
    normalSample = normalSample * 2.0f - 1.0f;

    float3 dp1 = ddx(posW);
    float3 dp2 = ddy(posW);

    float2 duv1 = ddx(texC);
    float2 duv2 = ddy(texC);

    float3 N = normalize(normalW);

    float3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    float3 B = normalize(cross(N, T));

    float3 bumpedNormal =
        normalSample.x * T +
        normalSample.y * B +
        normalSample.z * N;

    return normalize(bumpedNormal);
}

GBufferOutput PS(DomainOut pin)
{
    GBufferOutput gout;

    float4 texColor = gDiffuseMap.Sample(gsamLinear, pin.TexC);

    float3 N = ApplyNormalMap(pin.NormalW, pin.PosW, pin.TexC);

    // RT0 — world position.
    gout.Position = float4(pin.PosW, 1.0f);

    // RT1 — normal packed [-1,1] -> [0,1].
    gout.Normal = float4(N * 0.5f + 0.5f, 1.0f);

    // RT2 — albedo + roughness.
    float3 albedo = texColor.rgb * gDiffuseAlbedo.rgb;
    gout.Albedo = float4(albedo, gRoughness);

    return gout;
}