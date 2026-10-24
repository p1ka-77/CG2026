Texture2D<float4> Current : register(t0);
Texture2D<float4> Position : register(t1);
Texture2D<float4> History : register(t2);
Texture2D<float4> PreviousPosition : register(t3);
SamplerState LinearClamp : register(s0);
cbuffer Constants : register(b0)
{
    float4x4 PreviousVP;
    float4x4 CurrentVP;
    float2 InvSize;
    float2 Jitter;
    uint Valid;
    float HistoryWeight;
    float2 Padding;
};
struct Varying {float4 Position:SV_POSITION; float2 UV:TEXCOORD;};
Varying VS(uint id:SV_VertexID)
{
    Varying o;
    o.UV=float2((id<<1)&2,id&2);
    o.Position=float4(o.UV*float2(2,-2)+float2(-1,1),0,1);
    return o;
}
struct Output {float4 Color:SV_Target0; float4 World:SV_Target1;};
Output PS(Varying input)
{
    int2 size=int2(1.0/InvSize+0.5),p=int2(input.Position.xy);
    // Reconstruct current color on the fixed, unjittered output pixel grid.
    float2 currentUV=input.UV+Jitter*InvSize;
    float4 world=Position.SampleLevel(LinearClamp,currentUV,0);
    float3 current=Current.SampleLevel(LinearClamp,currentUV,0).rgb;
    float footprint=max(length(ddx(world.xyz)),length(ddy(world.xyz)));
    Output o; o.World=world; o.Color=float4(current,1);
    if(!Valid || world.w<0.5) return o;
    float4 previousClip=mul(float4(world.xyz,1),PreviousVP);
    float4 currentClip=mul(float4(world.xyz,1),CurrentVP);
    float2 previousUV=previousClip.xy/max(previousClip.w,0.00001)*float2(0.5,-0.5)+0.5;
    float2 projectedCurrent=currentClip.xy/max(currentClip.w,0.00001)*float2(0.5,-0.5)+0.5;
    // Unjittered matrices: a stationary camera gives exactly zero reprojection motion.
    float2 uv=input.UV+previousUV-projectedCurrent;
    if(previousClip.w<=0 || any(uv<InvSize*0.5) || any(uv>1-InvSize*0.5)) return o;
    int2 hp=clamp(int2(uv*size),0,size-1);
    float4 oldWorld=PreviousPosition.Load(int3(hp,0));
    // Reject newly exposed surfaces and moving objects using world-position agreement.
    float tolerance=0.025+min(footprint*1.5,previousClip.w*0.01);
    // At a static-camera silhouette, jitter changes coverage and interpolated
    // positions even though the surface hasn't moved. Preserve accumulation there;
    // the color neighborhood still limits stale history from animated objects.
    if(Padding.x<0.5 && (oldWorld.w<0.5 || length(oldWorld.xyz-world.xyz)>tolerance)) return o;
    float3 low=current,high=current;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x)
    {
        float3 c=Current.Load(int3(clamp(p+int2(x,y),0,size-1),0)).rgb;
        low=min(low,c); high=max(high,c);
    }
    float3 history=clamp(History.SampleLevel(LinearClamp,uv,0).rgb,low,high);
    float motion=length((uv-input.UV)/InvSize);
    float weight=lerp(HistoryWeight,0.65,saturate(motion/16));
    o.Color=float4(lerp(current,history,weight),1);
    return o;
}
