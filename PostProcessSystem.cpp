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
    float strength)
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
    constants.EffectMode = effectMode;
    constants.Strength = strength;
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
