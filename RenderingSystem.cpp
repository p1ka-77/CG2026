#include "RenderingSystem.h"

#include <stdexcept>
#include <cstring>
#include <string>

struct QuadVertex
{
    XMFLOAT3 Pos;
    XMFLOAT2 UV;
};

static void ThrowIfFailedLocal(HRESULT hr, const char* message)
{
    if (FAILED(hr))
    {
        throw std::runtime_error(message);
    }
}

void RenderingSystem::Init(
    ID3D12Device* device,
    UINT width,
    UINT height,
    DXGI_FORMAT backBufferFormat,
    DXGI_FORMAT depthFormat)
{
    mWidth = width;
    mHeight = height;

    // Ёти параметры будут использоватьс€ позже при создании PSO.
    // ¬ Init они не нужны напр€мую.
    (void)backBufferFormat;
    (void)depthFormat;

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    UINT srvSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mGBuffer.Init(device, width, height, rtvSize, srvSize);

    UINT64 cbSize = (sizeof(LightingPassCB) + 255) & ~255;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = cbSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mLightingCBUpload));

    ThrowIfFailedLocal(hr, "RenderingSystem::Init failed to create lighting constant buffer");

    hr = mLightingCBUpload->Map(
        0,
        nullptr,
        reinterpret_cast<void**>(&mLightingCBMapped));

    ThrowIfFailedLocal(hr, "RenderingSystem::Init failed to map lighting constant buffer");
}

void RenderingSystem::AddDirectionalLight(XMFLOAT3 direction, XMFLOAT3 strength)
{
    if ((int)mLights.size() >= MAX_LIGHTS)
        return;

    LightData light = {};
    light.Type = (int)LightType::Directional;
    light.Direction = direction;
    light.Strength = strength;
    light.Range = 0.0f;
    light.SpotAngle = 0.0f;
    light.SpotFalloff = 0.0f;

    mLights.push_back(light);
}

void RenderingSystem::AddPointLight(
    XMFLOAT3 position,
    float range,
    XMFLOAT3 strength)
{
    if ((int)mLights.size() >= MAX_LIGHTS)
        return;

    LightData light = {};
    light.Type = (int)LightType::Point;
    light.Position = position;
    light.Range = range;
    light.Strength = strength;
    light.SpotAngle = 0.0f;
    light.SpotFalloff = 0.0f;

    mLights.push_back(light);
}

void RenderingSystem::AddSpotLight(
    XMFLOAT3 position,
    XMFLOAT3 direction,
    float range,
    float spotAngle,
    float falloff,
    XMFLOAT3 strength)
{
    if ((int)mLights.size() >= MAX_LIGHTS)
        return;

    LightData light = {};
    light.Type = (int)LightType::Spot;
    light.Position = position;
    light.Direction = direction;
    light.Range = range;
    light.SpotAngle = spotAngle;
    light.SpotFalloff = falloff;
    light.Strength = strength;

    mLights.push_back(light);
}

void RenderingSystem::BeginGeometryPass(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv,
    D3D12_VIEWPORT viewport,
    D3D12_RECT scissorRect)
{
    mGBuffer.TransitionToWrite(cmdList);
    mGBuffer.Clear(cmdList);
    mGBuffer.BindAsRenderTargets(cmdList, dsv);

    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    cmdList->SetPipelineState(mGeometryPSO.Get());
    cmdList->SetGraphicsRootSignature(mGeometryRootSig.Get());
}

void RenderingSystem::EndGeometryPass(ID3D12GraphicsCommandList* cmdList)
{
    mGBuffer.TransitionToRead(cmdList);
}

void RenderingSystem::UpdateLightingCB(
    XMFLOAT3 eyePos,
    XMFLOAT4 ambientLight)
{
    LightingPassCB cb = {};
    cb.LightCount = (int)mLights.size();
    cb.EyePosW = eyePos;
    cb.AmbientLight = ambientLight;

    for (int i = 0; i < cb.LightCount && i < MAX_LIGHTS; ++i)
    {
        cb.Lights[i] = mLights[i];
    }

    std::memcpy(mLightingCBMapped, &cb, sizeof(LightingPassCB));
}

void RenderingSystem::LightingPass(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
    D3D12_VIEWPORT viewport,
    D3D12_RECT scissorRect,
    XMFLOAT3 eyePos,
    XMFLOAT4 ambientLight)
{
    UpdateLightingCB(eyePos, ambientLight);

    cmdList->OMSetRenderTargets(1, &backBufferRTV, false, nullptr);

    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissorRect);

    cmdList->SetPipelineState(mLightingPSO.Get());
    cmdList->SetGraphicsRootSignature(mLightingRootSig.Get());

    mGBuffer.BindSRVHeap(cmdList);

    cmdList->SetGraphicsRootDescriptorTable(0, mGBuffer.GetSRV(0));
    cmdList->SetGraphicsRootDescriptorTable(1, mGBuffer.GetSRV(1));
    cmdList->SetGraphicsRootDescriptorTable(2, mGBuffer.GetSRV(2));

    cmdList->SetGraphicsRootConstantBufferView(
        3,
        mLightingCBUpload->GetGPUVirtualAddress());

    cmdList->IASetVertexBuffers(0, 1, &mQuadVBView);
    cmdList->IASetIndexBuffer(&mQuadIBView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void RenderingSystem::BuildFullscreenQuad(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList)
{
    QuadVertex vertices[4] =
    {
        { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(1.0f,  1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }
    };

    uint16_t indices[6] =
    {
        0, 1, 2,
        1, 3, 2
    };

    UINT vbSize = sizeof(vertices);
    UINT ibSize = sizeof(indices);

    auto createBuffer = [](
        ID3D12Device* device,
        UINT size,
        ComPtr<ID3D12Resource>& defaultBuffer,
        ComPtr<ID3D12Resource>& uploadBuffer)
        {
            D3D12_RESOURCE_DESC bufferDesc = {};
            bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Alignment = 0;
            bufferDesc.Width = size;
            bufferDesc.Height = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels = 1;
            bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.SampleDesc.Quality = 0;
            bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            D3D12_HEAP_PROPERTIES defaultHeap = {};
            defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
            defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            defaultHeap.CreationNodeMask = 1;
            defaultHeap.VisibleNodeMask = 1;

            HRESULT hr = device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&defaultBuffer));

            ThrowIfFailedLocal(hr, "RenderingSystem::BuildFullscreenQuad failed to create default buffer");

            D3D12_HEAP_PROPERTIES uploadHeap = {};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
            uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            uploadHeap.CreationNodeMask = 1;
            uploadHeap.VisibleNodeMask = 1;

            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&uploadBuffer));

            ThrowIfFailedLocal(hr, "RenderingSystem::BuildFullscreenQuad failed to create upload buffer");
        };

    createBuffer(device, vbSize, mQuadVB, mQuadVBUpload);
    createBuffer(device, ibSize, mQuadIB, mQuadIBUpload);

    void* mappedData = nullptr;

    HRESULT hr = mQuadVBUpload->Map(0, nullptr, &mappedData);
    ThrowIfFailedLocal(hr, "RenderingSystem::BuildFullscreenQuad failed to map vertex upload buffer");

    std::memcpy(mappedData, vertices, vbSize);
    mQuadVBUpload->Unmap(0, nullptr);

    hr = mQuadIBUpload->Map(0, nullptr, &mappedData);
    ThrowIfFailedLocal(hr, "RenderingSystem::BuildFullscreenQuad failed to map index upload buffer");

    std::memcpy(mappedData, indices, ibSize);
    mQuadIBUpload->Unmap(0, nullptr);

    cmdList->CopyResource(mQuadVB.Get(), mQuadVBUpload.Get());
    cmdList->CopyResource(mQuadIB.Get(), mQuadIBUpload.Get());

    D3D12_RESOURCE_BARRIER barriers[2] = {};

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[0].Transition.pResource = mQuadVB.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barriers[1].Transition.pResource = mQuadIB.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList->ResourceBarrier(2, barriers);

    mQuadVBView.BufferLocation = mQuadVB->GetGPUVirtualAddress();
    mQuadVBView.StrideInBytes = sizeof(QuadVertex);
    mQuadVBView.SizeInBytes = vbSize;

    mQuadIBView.BufferLocation = mQuadIB->GetGPUVirtualAddress();
    mQuadIBView.Format = DXGI_FORMAT_R16_UINT;
    mQuadIBView.SizeInBytes = ibSize;
}

void RenderingSystem::BuildGeometryRootSignature(ID3D12Device* device)
{
    // Geometry Pass:
    // slot 0: diffuse texture SRV t0
    // slot 1: Object CBV b0
    // slot 2: Pass CBV b1
    // slot 3: Material CBV b2

    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER rootParameters[4];

    rootParameters[0].InitAsDescriptorTable(
        1,
        &texTable,
        D3D12_SHADER_VISIBILITY_PIXEL);

    rootParameters[1].InitAsConstantBufferView(0); // b0 Object
    rootParameters[2].InitAsConstantBufferView(1); // b1 Pass
    rootParameters[3].InitAsConstantBufferView(2); // b2 Material

    CD3DX12_STATIC_SAMPLER_DESC sampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        4,
        rootParameters,
        1,
        &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }

    ThrowIfFailedLocal(hr, "BuildGeometryRootSignature: D3D12SerializeRootSignature failed");

    hr = device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mGeometryRootSig));

    ThrowIfFailedLocal(hr, "BuildGeometryRootSignature: CreateRootSignature failed");
}

void RenderingSystem::BuildLightingRootSignature(ID3D12Device* device)
{
    // Lighting Pass:
    // slot 0: GBuffer Position SRV t0
    // slot 1: GBuffer Normal   SRV t1
    // slot 2: GBuffer Albedo   SRV t2
    // slot 3: Lighting CBV b0

    CD3DX12_DESCRIPTOR_RANGE srvRange0;
    CD3DX12_DESCRIPTOR_RANGE srvRange1;
    CD3DX12_DESCRIPTOR_RANGE srvRange2;

    srvRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    srvRange1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    srvRange2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

    CD3DX12_ROOT_PARAMETER rootParameters[4];

    rootParameters[0].InitAsDescriptorTable(
        1,
        &srvRange0,
        D3D12_SHADER_VISIBILITY_PIXEL);

    rootParameters[1].InitAsDescriptorTable(
        1,
        &srvRange1,
        D3D12_SHADER_VISIBILITY_PIXEL);

    rootParameters[2].InitAsDescriptorTable(
        1,
        &srvRange2,
        D3D12_SHADER_VISIBILITY_PIXEL);

    rootParameters[3].InitAsConstantBufferView(0); // b0 LightingCB

    CD3DX12_STATIC_SAMPLER_DESC sampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        4,
        rootParameters,
        1,
        &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }

    ThrowIfFailedLocal(hr, "BuildLightingRootSignature: D3D12SerializeRootSignature failed");

    hr = device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mLightingRootSig));

    ThrowIfFailedLocal(hr, "BuildLightingRootSignature: CreateRootSignature failed");
}

void RenderingSystem::BuildGeometryPSO(
    ID3D12Device* device,
    const D3D12_INPUT_LAYOUT_DESC& inputLayout,
    ID3DBlob* vsBlob,
    ID3DBlob* psBlob,
    DXGI_FORMAT depthFormat)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.InputLayout = inputLayout;
    psoDesc.pRootSignature = mGeometryRootSig.Get();

    psoDesc.VS =
    {
        reinterpret_cast<BYTE*>(vsBlob->GetBufferPointer()),
        vsBlob->GetBufferSize()
    };

    psoDesc.PS =
    {
        reinterpret_cast<BYTE*>(psBlob->GetBufferPointer()),
        psBlob->GetBufferSize()
    };

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    psoDesc.NumRenderTargets = GBUFFER_COUNT;

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        psoDesc.RTVFormats[i] = GBUFFER_FORMATS[i];
    }

    psoDesc.DSVFormat = depthFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    HRESULT hr = device->CreateGraphicsPipelineState(
        &psoDesc,
        IID_PPV_ARGS(&mGeometryPSO));

    ThrowIfFailedLocal(hr, "RenderingSystem::BuildGeometryPSO failed");
}

void RenderingSystem::BuildLightingPSO(
    ID3D12Device* device,
    ID3DBlob* vsBlob,
    ID3DBlob* psBlob,
    DXGI_FORMAT backBufferFormat)
{
    D3D12_INPUT_ELEMENT_DESC quadInputLayout[] =
    {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        }
    };

    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = FALSE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.InputLayout =
    {
        quadInputLayout,
        _countof(quadInputLayout)
    };

    psoDesc.pRootSignature = mLightingRootSig.Get();

    psoDesc.VS =
    {
        reinterpret_cast<BYTE*>(vsBlob->GetBufferPointer()),
        vsBlob->GetBufferSize()
    };

    psoDesc.PS =
    {
        reinterpret_cast<BYTE*>(psBlob->GetBufferPointer()),
        psBlob->GetBufferSize()
    };

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = depthStencilDesc;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = backBufferFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    HRESULT hr = device->CreateGraphicsPipelineState(
        &psoDesc,
        IID_PPV_ARGS(&mLightingPSO));

    ThrowIfFailedLocal(hr, "RenderingSystem::BuildLightingPSO failed");
}

void RenderingSystem::OnResize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    mGBuffer.OnResize(device, width, height);
}