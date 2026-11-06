Texture2D<float4> Current : register(t0);
Texture2D<float4> Position : register(t1);
Texture2D<float4> History : register(t2);
Texture2D<float4> PreviousPosition : register(t3);
Texture2D<float4> PreviousSurface : register(t4);
SamplerState LinearClamp : register(s0);
cbuffer Constants : register(b0)
{
    float4x4 PreviousVP;
    float4x4 CurrentVP;
    float2 InvSize;
    float2 Jitter;
    uint Valid;
    float HistoryWeight;
    uint CameraStationary;
    float Padding;
};
struct Varying {float4 Position:SV_POSITION; float2 UV:TEXCOORD;};
Varying VS(uint id:SV_VertexID)
{
    Varying o;
    o.UV=float2((id<<1)&2,id&2);
    o.Position=float4(o.UV*float2(2,-2)+float2(-1,1),0,1);
    return o;
}
struct Output {float4 Color:SV_Target0; float4 World:SV_Target1; float2 Motion:SV_Target2;};
Output PS(Varying input)
{
    int2 size=int2(1.0/InvSize+0.5),p=int2(input.Position.xy);
    // This texel ALREADY contains a jittered subpixel sample from rasterization.
    // Do not shift/filter it a second time: accumulate a different sample each frame.
    float4 world=Position.Load(int3(p,0));
    float4 previousWorld=PreviousSurface.Load(int3(p,0));
    float3 current=Current.Load(int3(p,0)).rgb;
    float footprint=max(length(ddx(world.xyz)),length(ddy(world.xyz)));
    bool hasSurface=world.w>0.5;
    bool objectMoved=hasSurface && (previousWorld.w<0.5 || length(previousWorld.xyz-world.xyz)>0.00001);
    Output o; o.World=world; o.Color=float4(current,1); o.Motion=0;
    // History metadata: 0=background, 1=static surface, 2=moving surface.
    o.World.w=hasSurface?(objectMoved?2.0:1.0):0.0;
    float2 uv=input.UV;
    float previousDepth=1;
    if(hasSurface && previousWorld.w>0.5)
    {
        float4 previousClip=mul(float4(previousWorld.xyz,1),PreviousVP);
        float4 currentClip=mul(float4(world.xyz,1),CurrentVP);
        float2 previousUV=previousClip.xy/max(previousClip.w,0.00001)*float2(0.5,-0.5)+0.5;
        float2 projectedCurrent=currentClip.xy/max(currentClip.w,0.00001)*float2(0.5,-0.5)+0.5;
        o.Motion=previousClip.w>0?previousUV-projectedCurrent:0;
        // History is stored on the stable output grid. PreviousVP excludes jitter,
        // so add CURRENT raster jitter to return the sample to that grid.
        // Equivalent to input.UV + (previousUV - projectedCurrent), without jitter in velocity.
        uv=previousUV+Jitter*InvSize;
        previousDepth=previousClip.w;
    }
    if(!Valid || (hasSurface && previousWorld.w<0.5) || previousDepth<=0 || any(uv<InvSize*0.5) || any(uv>1-InvSize*0.5)) return o;
    int2 hp=clamp(int2(uv*size),0,size-1);
    float4 oldWorld=PreviousPosition.Load(int3(hp,0));
    // Compare against the expected PREVIOUS surface, including object motion.
    // Static silhouettes change coverage under jitter. Rejecting every foreground /
    // background change here would throw away the very samples needed for AA.
    // A moving surface in either frame must still pass disocclusion rejection.
    bool staticCoverage=CameraStationary!=0 && !objectMoved && oldWorld.w<1.5;
    if(!staticCoverage)
    {
        if(!hasSurface || previousWorld.w<0.5) return o;
        float tolerance=0.025+min(footprint*1.5,previousDepth*0.01);
        float bestDistance=1e20;
        // Jitter changes which texel represents the same surface. Look for it in
        // a one-pixel neighborhood, while keeping color reprojection subpixel-accurate.
        [unroll] for(int dy=-1;dy<=1;++dy) [unroll] for(int dx=-1;dx<=1;++dx)
        {
            float4 candidate=PreviousPosition.Load(int3(clamp(hp+int2(dx,dy),0,size-1),0));
            if(candidate.w>0.5) bestDistance=min(bestDistance,length(candidate.xyz-previousWorld.xyz));
        }
        if(bestDistance>tolerance) return o;
    }
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
