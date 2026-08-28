// GPU particle system:
// CS consumes every particle from one StructuredBuffer and appends the
// updated particle to the second buffer. VS/GS/PS render opaque billboards.

struct Particle
{
    float3 Position;
    float Age;

    float3 Velocity;
    float Lifetime;

    float4 Color;
    float2 Size;
    uint Seed;
    float Padding;
};

cbuffer cbParticles : register(b0)
{
    float4x4 gViewProj;

    float3 gEyePosW;
    float gDeltaTime;

    float3 gCameraRightW;
    float gTotalTime;

    float3 gCameraUpW;
    uint gParticleCount;

    float3 gEmitterPositionW;
    uint gFrameIndex;
};

ConsumeStructuredBuffer<Particle> gConsumeParticles : register(u0);
AppendStructuredBuffer<Particle> gAppendParticles : register(u1);
StructuredBuffer<Particle> gParticles : register(t0);

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float Random01(inout uint state)
{
    state = Hash(state);
    return (state & 0x00ffffffu) / 16777216.0f;
}

void RespawnParticle(inout Particle particle, uint dispatchIndex)
{
    uint randomState =
        particle.Seed ^
        (gFrameIndex * 1664525u) ^
        (dispatchIndex * 1013904223u);

    float angle = Random01(randomState) * 6.2831853f;
    float radialSpeed = lerp(0.3f, 1.35f, Random01(randomState));
    float verticalSpeed = lerp(2.8f, 4.8f, Random01(randomState));
    float colorChoice = Random01(randomState);
    float size = lerp(0.09f, 0.17f, Random01(randomState));

    particle.Position = gEmitterPositionW;
    particle.Velocity = float3(
        cos(angle) * radialSpeed,
        verticalSpeed,
        sin(angle) * radialSpeed);
    particle.Age = 0.0f;
    particle.Lifetime = lerp(2.2f, 3.8f, Random01(randomState));
    particle.Color = float4(
        lerp(
            float3(0.15f, 0.65f, 1.0f),
            float3(1.0f, 0.35f, 0.08f),
            colorChoice),
        1.0f);
    particle.Size = float2(size, size);
    particle.Seed = randomState;
}

[numthreads(256, 1, 1)]
void CS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gParticleCount)
    {
        return;
    }

    // The mandatory position update happens after Consume(), on the GPU.
    Particle particle = gConsumeParticles.Consume();

    particle.Age += gDeltaTime;
    particle.Velocity.y -= 2.8f * gDeltaTime;
    particle.Position += particle.Velocity * gDeltaTime;

    if (particle.Age >= particle.Lifetime || particle.Position.y < 0.16f)
    {
        RespawnParticle(particle, dispatchThreadId.x);
    }

    gAppendParticles.Append(particle);
}

struct ParticleVertex
{
    float3 CenterW : POSITION;
    float4 Color : COLOR;
    float2 Size : TEXCOORD0;
};

ParticleVertex VS(uint vertexId : SV_VertexID)
{
    Particle particle = gParticles[vertexId];

    ParticleVertex output;
    output.CenterW = particle.Position;
    output.Color = particle.Color;
    output.Size = particle.Size;
    return output;
}

struct BillboardVertex
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION0;
    float4 Color : COLOR;
    float2 TexC : TEXCOORD0;
};

[maxvertexcount(4)]
void GS(
    point ParticleVertex input[1],
    inout TriangleStream<BillboardVertex> outputStream)
{
    const float2 cornerSigns[4] =
    {
        float2(-0.5f, -0.5f),
        float2(-0.5f,  0.5f),
        float2( 0.5f, -0.5f),
        float2( 0.5f,  0.5f)
    };

    const float2 textureCoordinates[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float3 positionW =
            input[0].CenterW +
            gCameraRightW * cornerSigns[i].x * input[0].Size.x +
            gCameraUpW * cornerSigns[i].y * input[0].Size.y;

        BillboardVertex output;
        output.PosW = positionW;
        output.PosH = mul(float4(positionW, 1.0f), gViewProj);
        output.Color = input[0].Color;
        output.TexC = textureCoordinates[i];
        outputStream.Append(output);
    }

    outputStream.RestartStrip();
}

struct GBufferOutput
{
    float4 Position : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Albedo : SV_Target2;
};

GBufferOutput PS(BillboardVertex input)
{
    float2 localPosition = input.TexC * 2.0f - 1.0f;
    localPosition.y = -localPosition.y;
    float radiusSquared = dot(localPosition, localPosition);

    // An opaque circular particle rather than a transparent quad.
    clip(1.0f - radiusSquared);

    float sphereDepth = sqrt(saturate(1.0f - radiusSquared));
    float3 toEye = normalize(gEyePosW - input.PosW);
    float3 normalW = normalize(
        gCameraRightW * localPosition.x +
        gCameraUpW * localPosition.y +
        toEye * sphereDepth);

    GBufferOutput output;
    output.Position = float4(input.PosW, 1.0f);
    output.Normal = float4(normalW * 0.5f + 0.5f, 1.0f);
    output.Albedo = float4(
        input.Color.rgb * (0.65f + 0.35f * sphereDepth),
        0.65f);
    return output;
}
