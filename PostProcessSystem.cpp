#include "PostProcessSystem.h"

#include "../../Common/d3dUtil.h"
#include "GBuffer.h"

#include <algorithm>
#include <stdexcept>

namespace
{
    void ThrowIfFailedPostProcess(HRESULT hr, const char* message)
    {
        if (FAILED(hr))
        {
            throw std::runtime_error(message);
        }
    }
}

void PostProcessSystem::Initialize(
    ID3D12Device* device,
    UINT width,
    UINT height,
    DXGI_FORMAT renderTargetFormat,
    ID3D12DescriptorHeap* gBufferSrvHeap)
{
    mRenderTargetFormat = renderTargetFormat;
    mSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    BuildRootSignature(device);
    BuildPipelineState(device);
    BuildSizeDependentResources(device, width, height, gBufferSrvHeap);
    mInitialized = true;
}

void PostProcessSystem::OnResize(
    ID3D12Device* device,
    UINT width,
    UINT height,
    ID3D12DescriptorHeap* gBufferSrvHeap)
{
    if (!mInitialized)
    {
        return;
    }

    BuildSizeDependentResources(device, width, height, gBufferSrvHeap);
}

void PostProcessSystem::BuildRootSignature(ID3D12Device* device)
{
    // t0 = lit scene, t1/t2/t3 = position/normal/albedo from the GBuffer.
    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        PostProcessSrvCount,
        0);

    CD3DX12_ROOT_PARAMETER rootParameters[2];
    rootParameters[0].InitAsDescriptorTable(
        1,
        &srvRange,
        D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[1].InitAsConstants(
        sizeof(PostProcessConstants) / sizeof(UINT),
        0,
        0,
        D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
        _countof(rootParameters),
        rootParameters,
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSignature.GetAddressOf(),
        errors.GetAddressOf());

    if (errors != nullptr)
    {
        OutputDebugStringA(
            static_cast<const char*>(errors->GetBufferPointer()));
    }

    ThrowIfFailedPostProcess(
        hr,
        "PostProcessSystem failed to serialize the root signature");

    hr = device->CreateRootSignature(
        0,
        serializedRootSignature->GetBufferPointer(),
        serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature));
    ThrowIfFailedPostProcess(
        hr,
        "PostProcessSystem failed to create the root signature");
}

void PostProcessSystem::BuildPipelineState(ID3D12Device* device)
{
    ComPtr<ID3DBlob> vertexShader = d3dUtil::CompileShader(
        L"Shaders\\PostProcessPass.hlsl",
        nullptr,
        "VS",
        "vs_5_0");
    ComPtr<ID3DBlob> pixelShader = d3dUtil::CompileShader(
        L"Shaders\\PostProcessPass.hlsl",
        nullptr,
        "PS",
        "ps_5_0");

    CD3DX12_DEPTH_STENCIL_DESC depthStencil(D3D12_DEFAULT);
    depthStencil.DepthEnable = FALSE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    CD3DX12_RASTERIZER_DESC rasterizer(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    // Deliberately empty: the vertex shader generates all three vertices.
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS =
    {
        reinterpret_cast<BYTE*>(vertexShader->GetBufferPointer()),
        vertexShader->GetBufferSize()
    };
    psoDesc.PS =
    {
        reinterpret_cast<BYTE*>(pixelShader->GetBufferPointer()),
        pixelShader->GetBufferSize()
    };
    psoDesc.RasterizerState = rasterizer;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = depthStencil;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mRenderTargetFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;

    HRESULT hr = device->CreateGraphicsPipelineState(
        &psoDesc,
        IID_PPV_ARGS(&mPipelineState));
    ThrowIfFailedPostProcess(
        hr,
        "PostProcessSystem failed to create the pipeline state");
}

void PostProcessSystem::BuildSizeDependentResources(
    ID3D12Device* device,
    UINT width,
    UINT height,
    ID3D12DescriptorHeap* gBufferSrvHeap)
{
    mWidth = (width > 0u) ? width : 1u;
    mHeight = (height > 0u) ? height : 1u;

    mSceneColor.Reset();
    mSceneColorRtvHeap.Reset();
    mPostProcessSrvHeap.Reset();

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = mWidth;
    textureDesc.Height = mHeight;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = mRenderTargetFormat;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = mRenderTargetFormat;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&mSceneColor));
    ThrowIfFailedPostProcess(
        hr,
        "PostProcessSystem failed to create the scene-color texture");

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = device->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&mSceneColorRtvHeap));
    ThrowIfFailedPostProcess(
        hr,
        "PostProcessSystem failed to create the RTV heap");

    device->CreateRenderTargetView(
        mSceneColor.Get(),
        nullptr,
        mSceneColorRtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = PostProcessSrvCount;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(
        &srvHeapDesc,
        IID_PPV_ARGS(&mPostProcessSrvHeap));
    ThrowIfFailedPostProcess(
        hr,
        "PostProcessSystem failed to create the SRV heap");

    D3D12_SHADER_RESOURCE_VIEW_DESC sceneColorSrv = {};
    sceneColorSrv.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sceneColorSrv.Format = mRenderTargetFormat;
    sceneColorSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sceneColorSrv.Texture2D.MostDetailedMip = 0;
    sceneColorSrv.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE destination(
        mPostProcessSrvHeap->GetCPUDescriptorHandleForHeapStart());
    device->CreateShaderResourceView(
        mSceneColor.Get(),
        &sceneColorSrv,
        destination);

    // Copy the three existing GBuffer SRVs right after scene color.
    destination.Offset(1, mSrvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE source(
        gBufferSrvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        device->CopyDescriptorsSimple(
            1,
            destination,
            source,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        destination.Offset(1, mSrvDescriptorSize);
        source.Offset(1, mSrvDescriptorSize);
    }
    ResetTAA(); mHistoryIndex=0;
    if (!mTaaRoot)
    {
        CD3DX12_DESCRIPTOR_RANGE range;
        range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0);
        CD3DX12_ROOT_PARAMETER params[2];
        params[0].InitAsDescriptorTable(1,&range,D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsConstants(40,0);
        CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        CD3DX12_ROOT_SIGNATURE_DESC root(2,params,1,&sampler);
        ComPtr<ID3DBlob> blob,error;
        ThrowIfFailed(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));
        ThrowIfFailed(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&mTaaRoot)));
        auto vs=d3dUtil::CompileShader(L"Shaders\\TemporalAA.hlsl",nullptr,"VS","vs_5_0");
        auto ps=d3dUtil::CompileShader(L"Shaders\\TemporalAA.hlsl",nullptr,"PS","ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso={};
        pso.pRootSignature=mTaaRoot.Get();
        pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
        pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        pso.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        pso.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pso.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pso.DepthStencilState.DepthEnable=FALSE;
        pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.SampleMask=UINT_MAX; pso.SampleDesc.Count=1;
        pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets=2; pso.RTVFormats[0]=mRenderTargetFormat;
        pso.RTVFormats[1]=GBUFFER_FORMATS[0];
        ThrowIfFailed(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&mTaaPSO)));
    }
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc={};
    rtvDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtvDesc.NumDescriptors=4;
    mTaaRTVs.Reset();
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc,IID_PPV_ARGS(&mTaaRTVs)));
    UINT rtvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mTaaRtvStep=rtvStep;
    for(UINT i=0;i<2;++i)
    {
        mHistory[i].Reset(); mHistoryPosition[i].Reset(); mTaaHeaps[i].Reset();
        auto colorDesc=mSceneColor->GetDesc();
        ThrowIfFailed(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&colorDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&mHistory[i])));
        colorDesc.Format=GBUFFER_FORMATS[0];
        ThrowIfFailed(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&colorDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&mHistoryPosition[i])));
        auto rtv=mTaaRTVs->GetCPUDescriptorHandleForHeapStart(); rtv.ptr+=i*2*rtvStep;
        device->CreateRenderTargetView(mHistory[i].Get(),nullptr,rtv);
        rtv.ptr+=rtvStep; device->CreateRenderTargetView(mHistoryPosition[i].Get(),nullptr,rtv);
        auto dest=mPostProcessSrvHeap->GetCPUDescriptorHandleForHeapStart(); dest.ptr+=(4+i)*mSrvDescriptorSize;
        device->CreateShaderResourceView(mHistory[i].Get(),&sceneColorSrv,dest);
    }
    for(UINT i=0;i<2;++i)
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd={}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors=4; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&mTaaHeaps[i])));
        auto dst=mTaaHeaps[i]->GetCPUDescriptorHandleForHeapStart();
        device->CreateShaderResourceView(mSceneColor.Get(),&sceneColorSrv,dst);
        dst.ptr+=mSrvDescriptorSize;
        device->CopyDescriptorsSimple(1,dst,gBufferSrvHeap->GetCPUDescriptorHandleForHeapStart(),hd.Type);
        dst.ptr+=mSrvDescriptorSize;
        device->CreateShaderResourceView(mHistory[1-i].Get(),&sceneColorSrv,dst);
        dst.ptr+=mSrvDescriptorSize;
        auto posSrv=sceneColorSrv; posSrv.Format=GBUFFER_FORMATS[0];
        device->CreateShaderResourceView(mHistoryPosition[1-i].Get(),&posSrv,dst);
    }
}

void PostProcessSystem::ResolveTAA(ID3D12GraphicsCommandList* cmd,CXMMATRIX vp,XMFLOAT2 jitter)
{
    mCurrentJitter=jitter;
    if(!TAAEnabled) {ResetTAA(); return;}
    auto color=mHistory[mHistoryIndex].Get(),position=mHistoryPosition[mHistoryIndex].Get();
    D3D12_RESOURCE_BARRIER barriers[]={
        CD3DX12_RESOURCE_BARRIER::Transition(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(position,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET)};
    cmd->ResourceBarrier(2,barriers);
    auto rtv=mTaaRTVs->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr+=mHistoryIndex*2*mTaaRtvStep;
    cmd->OMSetRenderTargets(2,&rtv,TRUE,nullptr);
    cmd->SetGraphicsRootSignature(mTaaRoot.Get()); cmd->SetPipelineState(mTaaPSO.Get());
    ID3D12DescriptorHeap* heaps[]={mTaaHeaps[mHistoryIndex].Get()};
    cmd->SetDescriptorHeaps(1,heaps); cmd->SetGraphicsRootDescriptorTable(0,heaps[0]->GetGPUDescriptorHandleForHeapStart());
    struct Constants {XMFLOAT4X4 PreviousVP,CurrentVP; XMFLOAT2 InvSize,Jitter; UINT Valid; float Weight; XMFLOAT2 Padding;};
    static_assert(sizeof(Constants)==160,"TAA shader constants");
    Constants c={}; c.PreviousVP=mPreviousVP;
    XMStoreFloat4x4(&c.CurrentVP,XMMatrixTranspose(vp));
    c.Padding.x=1.0f;
    const float* previous=&c.PreviousVP._11;
    const float* current=&c.CurrentVP._11;
    for(UINT i=0;i<16;++i) if(fabsf(previous[i]-current[i])>0.000001f) c.Padding.x=0.0f;
    c.InvSize={1.0f/mWidth,1.0f/mHeight}; c.Jitter=jitter;
    c.Valid=mHistoryValid?1u:0u; c.Weight=0.9f;
    cmd->SetGraphicsRoot32BitConstants(1,40,&c,0);
    cmd->IASetVertexBuffers(0,0,nullptr); cmd->IASetIndexBuffer(nullptr);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); cmd->DrawInstanced(3,1,0,0);
    for(auto& b:barriers) std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    cmd->ResourceBarrier(2,barriers);
    XMStoreFloat4x4(&mPreviousVP,XMMatrixTranspose(vp));
    mResolvedIndex=mHistoryIndex; mHistoryIndex=1-mHistoryIndex; mHistoryValid=true;
}

void PostProcessSystem::BeginSceneColorPass(
    ID3D12GraphicsCommandList* commandList)
{
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneColor.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &barrier);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList->ClearRenderTargetView(
        GetSceneColorRTV(),
        clearColor,
        0,
        nullptr);
}

void PostProcessSystem::EndSceneColorPass(
    ID3D12GraphicsCommandList* commandList)
{
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneColor.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &barrier);
}

void PostProcessSystem::Draw(
    ID3D12GraphicsCommandList* commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE outputRtv,
    D3D12_VIEWPORT viewport,
    D3D12_RECT scissorRect,
    UINT effectMode,
    float strength,
    bool vignetteEnabled)
{
    commandList->OMSetRenderTargets(1, &outputRtv, false, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);
    commandList->SetPipelineState(mPipelineState.Get());
    commandList->SetGraphicsRootSignature(mRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { mPostProcessSrvHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    commandList->SetGraphicsRootDescriptorTable(
        0,
        mPostProcessSrvHeap->GetGPUDescriptorHandleForHeapStart());

    PostProcessConstants constants;
    constants.CurrentJitter=mCurrentJitter;
    constants.Padding1={TAAEnabled?float(mResolvedIndex+1):0.0f,TAAEnabled && TAASplit?1.0f:0.0f};
    constants.EffectMode = effectMode;
    constants.Strength = strength;
    constants.VignetteStrength = vignetteEnabled ? strength : 0.0f;
    if (constants.Strength < 0.0f)
        constants.Strength = 0.0f;
    else if (constants.Strength > 1.0f)
        constants.Strength = 1.0f;

    constants.InvRenderTargetSize = XMFLOAT2(
        1.0f / static_cast<float>(mWidth),
        1.0f / static_cast<float>(mHeight));
    commandList->SetGraphicsRoot32BitConstants(
        1,
        sizeof(PostProcessConstants) / sizeof(UINT),
        &constants,
        0);

    // No vertex or index buffer: VS uses SV_VertexID to generate a full-screen quad.
    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    commandList->DrawInstanced(4, 1, 0, 0);
}

D3D12_CPU_DESCRIPTOR_HANDLE PostProcessSystem::GetSceneColorRTV() const
{
    return mSceneColorRtvHeap->GetCPUDescriptorHandleForHeapStart();
}
