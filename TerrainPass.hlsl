Texture2DArray<float> gHeightTiles : register(t0);
Texture2D<float4> gPaint : register(t1); // Premultiplied RGB and coverage, independent of LOD.
SamplerState gPaintSampler : register(s0);
cbuffer TerrainConstants : register(b0)
{
    float4x4 gViewProjection;
    float4 gTile; // origin XZ, world size, array layer
    float4 gOptions; // height scale, skirt depth, tree depth, debug mode
    float4 gBrush; // center XZ, radius in world metres, visible
    float4 gBrushColor;
    float4 gPaintMap; // inverse world size, paint visible, unused
};

struct VertexIn { float3 Grid : POSITION; }; // x,z,skirt flag
struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 UV : TEXCOORD;
};

float Height(int2 texel)
{
    float height=gHeightTiles.Load(int4(texel,(int)gTile.w,0))*gOptions.x;
    // Same runtime clearing as TerrainSystem::SceneHeight (also used by picking).
    float2 worldXZ=gTile.xy+(float2(texel)-1.0f)*(gTile.z/32.0f);
    return lerp(-2.0f,height,smoothstep(24.0f,180.0f,length(worldXZ)));
}

VertexOut VS(VertexIn input)
{
    VertexOut output;
    int2 texel=int2(round(input.Grid.xy*32.0f))+1; // apron preserves edge normals
    float spacing=gTile.z/32.0f;
    output.PosW=float3(gTile.x+input.Grid.x*gTile.z,
        Height(texel)-input.Grid.z*gOptions.y,gTile.y+input.Grid.y*gTile.z);
    output.NormalW=normalize(float3(Height(texel-int2(1,0))-Height(texel+int2(1,0)),
        2.0f*spacing,Height(texel-int2(0,1))-Height(texel+int2(0,1))));
    output.PosH=mul(float4(output.PosW,1.0f),gViewProjection);
    output.UV=input.Grid.xy;
    return output;
}

struct GBufferOutput
{
    float4 Position : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Albedo : SV_Target2;
};

GBufferOutput PS(VertexOut input)
{
    float3 normal=normalize(input.NormalW);
    float altitude=saturate(input.PosW.y/gOptions.x);
    float3 grass=lerp(float3(0.12,0.24,0.065),float3(0.38,0.36,0.15),altitude);
    float3 color=lerp(grass,float3(0.36,0.33,0.29),smoothstep(0.12,0.55,1.0-normal.y));
    color=lerp(color,float3(0.85,0.89,0.92),smoothstep(0.73,0.92,altitude)*smoothstep(0.45,0.85,normal.y));
    float2 paintUV=input.PosW.xz*gPaintMap.x+0.5;
    float4 paint=gPaint.SampleLevel(gPaintSampler,paintUV,0)*gPaintMap.y;
    color=color*(1.0-paint.a)+paint.rgb;
    if (gOptions.w>0.5)
    {
        const float3 colors[6]={float3(0.85,0.15,0.12),float3(0.9,0.5,0.08),float3(0.9,0.85,0.08),
            float3(0.15,0.8,0.22),float3(0.1,0.65,0.95),float3(0.65,0.3,0.95)};
        color=colors[(int)gOptions.z];
        float2 border=min(input.UV,1.0-input.UV);
        if(min(border.x,border.y)<0.0035) color*=0.22;
    }
    if(gBrush.w>0.5)
    {
        float radiusDistance=length(input.PosW.xz-gBrush.xy);
        float width=max(fwidth(radiusDistance)*1.5,0.5);
        float ring=1.0-smoothstep(width,width*2.0,abs(radiusDistance-gBrush.z));
        float center=1.0-smoothstep(width,width*2.0,radiusDistance);
        color=lerp(color,gBrushColor.rgb,max(ring,center)*0.9);
    }
    GBufferOutput output;
    output.Position=float4(input.PosW,1);
    output.Normal=float4(normal*0.5+0.5,1);
    output.Albedo=float4(color,0.9);
    return output;
}
