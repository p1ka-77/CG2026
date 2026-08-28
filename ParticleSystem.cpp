#include "ParticleSystem.h"

#include "GBuffer.h"
#include "../../Common/d3dUtil.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    void ThrowIfFailedParticle(HRESULT hr, const char* message)
    {
        if (FAILED(hr))
        {
            throw std::runtime_error(message);
        }
    }

    ComPtr<ID3D12RootSignature> CreateRootSignature(
        ID3D12Device* device,
        const D3D12_ROOT_SIGNATURE_DESC& description,
        const char* errorMessage)
    {
        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;

        HRESULT hr = D3D12SerializeRootSignature(
            &description,
            D3D_ROOT_SIGNATURE_VERSION_1,
            serialized.GetAddressOf(),
            errors.GetAddressOf());

        if (errors != nullptr)
        {
            OutputDebugStringA(
                static_cast<const char*>(errors->GetBufferPointer()));
        }

        ThrowIfFailedParticle(hr, errorMessage);

        ComPtr<ID3D12RootSignature> rootSignature;
        hr = device->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature));
        ThrowIfFailedParticle(hr, errorMessage);

        return rootSignature;
    }
}

ParticleSystem::~ParticleSystem()
{
    if (mConstantBufferUpload != nullptr && mMappedConstants != nullptr)
    {
        mConstantBufferUpload->Unmap(0, nullptr);
        mMappedConstants = nullptr;
    }
}

void ParticleSystem::Initialize(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    UINT maxParticles,
    UINT frameCount,
    DXGI_FORMAT depthFormat)
{
    mMaxParticles = maxParticles;
    mFrameCount = frameCount;
    mDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    BuildRootSignatures(device);
    BuildBuffers(device, commandList);
    BuildDescriptorHeap(device);
    BuildConstantBuffer(device);
    BuildPipelineStates(device, depthFormat);
}

void ParticleSystem::BuildBuffers(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    const UINT64 particleBufferSize =
        static_cast<UINT64>(mMaxParticles) * sizeof(ParticleGPU);

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC particleDesc =
        CD3DX12_RESOURCE_DESC::Buffer(
            particleBufferSize,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &particleDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mParticleBuffers[0]));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to create particle buffer A");

    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &particleDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&mParticleBuffers[1]));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to create particle buffer B");

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadDesc =
        CD3DX12_RESOURCE_DESC::Buffer(particleBufferSize);

    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mParticleInitializationUpload));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to create particle upload buffer");

    std::vector<ParticleGPU> initialParticles(mMaxParticles);
    const XMFLOAT3 emitter = { 0.0f, 0.25f, 0.0f };
    constexpr float goldenAngle = 2.39996323f;
    constexpr float gravity = 2.8f;

    for (UINT i = 0; i < mMaxParticles; ++i)
    {
        float normalizedIndex =
            (static_cast<float>(i) + 0.5f) /
            static_cast<float>(mMaxParticles);
        float angle = goldenAngle * static_cast<float>(i);
        float variation =
            static_cast<float>((i * 37u) % 101u) / 100.0f;
        float lifetime = 2.2f + 1.6f * variation;
        float age = normalizedIndex * lifetime;
        float radialSpeed = 0.35f + 0.9f * variation;
        float verticalSpeed = 2.7f + 1.8f * (1.0f - variation);

        ParticleGPU particle = {};
        particle.Velocity = XMFLOAT3(
            cosf(angle) * radialSpeed,
            verticalSpeed,
            sinf(angle) * radialSpeed);
        particle.Lifetime = lifetime;
        particle.Age = age;
        particle.Position = XMFLOAT3(
            emitter.x + particle.Velocity.x * age,
            emitter.y + particle.Velocity.y * age -
                0.5f * gravity * age * age,
            emitter.z + particle.Velocity.z * age);

        if (particle.Position.y < 0.18f)
        {
            particle.Position = emitter;
            particle.Age = 0.0f;
        }

        particle.Color = XMFLOAT4(
            0.2f + 0.8f * variation,
            0.45f + 0.45f * (1.0f - variation),
            1.0f - 0.65f * variation,
            1.0f);
        float size = 0.09f + 0.08f * variation;
        particle.Size = XMFLOAT2(size, size);
        particle.Seed = i * 747796405u + 2891336453u;

        initialParticles[i] = particle;
    }

    void* mappedParticles = nullptr;
    hr = mParticleInitializationUpload->Map(
        0,
        nullptr,
        &mappedParticles);
    ThrowIfFailedParticle(hr, "ParticleSystem failed to map particle upload buffer");
    std::memcpy(
        mappedParticles,
        initialParticles.data(),
        static_cast<size_t>(particleBufferSize));
    mParticleInitializationUpload->Unmap(0, nullptr);

    commandList->CopyBufferRegion(
        mParticleBuffers[0].Get(),
        0,
        mParticleInitializationUpload.Get(),
        0,
        particleBufferSize);

    D3D12_RESOURCE_DESC counterDesc =
        CD3DX12_RESOURCE_DESC::Buffer(
            D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    for (UINT i = 0; i < BufferCount; ++i)
    {
        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&mCounterBuffers[i]));
        ThrowIfFailedParticle(hr, "ParticleSystem failed to create UAV counter");
    }

    D3D12_RESOURCE_DESC counterUploadDesc =
        CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * 2);
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &counterUploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mCounterUpload));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to create counter upload buffer");

    UINT initialCounterValues[2] = { mMaxParticles, 0u };
    void* mappedCounters = nullptr;
    hr = mCounterUpload->Map(0, nullptr, &mappedCounters);
    ThrowIfFailedParticle(hr, "ParticleSystem failed to map counter upload buffer");
    std::memcpy(mappedCounters, initialCounterValues, sizeof(initialCounterValues));
    mCounterUpload->Unmap(0, nullptr);

    commandList->CopyBufferRegion(
        mCounterBuffers[0].Get(),
        0,
        mCounterUpload.Get(),
        0,
        sizeof(UINT));
    commandList->CopyBufferRegion(
        mCounterBuffers[1].Get(),
        0,
        mCounterUpload.Get(),
        sizeof(UINT),
        sizeof(UINT));

    D3D12_RESOURCE_BARRIER barriers[3] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mParticleBuffers[0].Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(
            mCounterBuffers[0].Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(
            mCounterBuffers[1].Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    commandList->ResourceBarrier(_countof(barriers), barriers);

    mParticleStates[0] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    mParticleStates[1] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void ParticleSystem::BuildDescriptorHeap(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = DescriptorCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device->CreateDescriptorHeap(
        &heapDesc,
        IID_PPV_ARGS(&mParticleDescriptorHeap));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to create descriptor heap");

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        mParticleDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < BufferCount; ++i)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = mMaxParticles;
        uavDesc.Buffer.StructureByteStride = sizeof(ParticleGPU);
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        device->CreateUnorderedAccessView(
            mParticleBuffers[i].Get(),
            mCounterBuffers[i].Get(),
            &uavDesc,
            handle);
        handle.Offset(1, mDescriptorSize);
    }

    for (UINT i = 0; i < BufferCount; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = mMaxParticles;
        srvDesc.Buffer.StructureByteStride = sizeof(ParticleGPU);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        device->CreateShaderResourceView(
            mParticleBuffers[i].Get(),
            &srvDesc,
            handle);
        handle.Offset(1, mDescriptorSize);
    }
}

void ParticleSystem::BuildConstantBuffer(ID3D12Device* device)
{
    mConstantBufferStride =
        (sizeof(ParticlePassConstants) + 255u) & ~255u;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
        static_cast<UINT64>(mConstantBufferStride) * mFrameCount);

    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mConstantBufferUpload));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to create constant buffer");

    hr = mConstantBufferUpload->Map(
        0,
        nullptr,
        reinterpret_cast<void**>(&mMappedConstants));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to map constant buffer");
}

void ParticleSystem::BuildRootSignatures(ID3D12Device* device)
{
    CD3DX12_DESCRIPTOR_RANGE consumeRange;
    consumeRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE appendRange;
    appendRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);

    CD3DX12_ROOT_PARAMETER computeParameters[3];
    computeParameters[0].InitAsDescriptorTable(1, &consumeRange);
    computeParameters[1].InitAsDescriptorTable(1, &appendRange);
    computeParameters[2].InitAsConstantBufferView(0);

    CD3DX12_ROOT_SIGNATURE_DESC computeDesc(
        _countof(computeParameters),
        computeParameters,
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);
    mComputeRootSignature = CreateRootSignature(
        device,
        computeDesc,
        "ParticleSystem failed to create compute root signature");

    CD3DX12_DESCRIPTOR_RANGE particleRange;
    particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER renderParameters[2];
    renderParameters[0].InitAsDescriptorTable(
        1,
        &particleRange,
        D3D12_SHADER_VISIBILITY_VERTEX);
    renderParameters[1].InitAsConstantBufferView(0);

    CD3DX12_ROOT_SIGNATURE_DESC renderDesc(
        _countof(renderParameters),
        renderParameters,
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    mRenderRootSignature = CreateRootSignature(
        device,
        renderDesc,
        "ParticleSystem failed to create render root signature");
}

void ParticleSystem::BuildPipelineStates(
    ID3D12Device* device,
    DXGI_FORMAT depthFormat)
{
    ComPtr<ID3DBlob> computeShader = d3dUtil::CompileShader(
        L"Shaders\\ParticlePass.hlsl",
        nullptr,
        "CS",
        "cs_5_0");
    ComPtr<ID3DBlob> vertexShader = d3dUtil::CompileShader(
        L"Shaders\\ParticlePass.hlsl",
        nullptr,
        "VS",
        "vs_5_0");
    ComPtr<ID3DBlob> geometryShader = d3dUtil::CompileShader(
        L"Shaders\\ParticlePass.hlsl",
        nullptr,
        "GS",
        "gs_5_0");
    ComPtr<ID3DBlob> pixelShader = d3dUtil::CompileShader(
        L"Shaders\\ParticlePass.hlsl",
        nullptr,
        "PS",
        "ps_5_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
    computeDesc.pRootSignature = mComputeRootSignature.Get();
    computeDesc.CS =
    {
        reinterpret_cast<BYTE*>(computeShader->GetBufferPointer()),
        computeShader->GetBufferSize()
    };

    HRESULT hr = device->CreateComputePipelineState(
        &computeDesc,
        IID_PPV_ARGS(&mComputePSO));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to create compute PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC renderDesc = {};
    renderDesc.InputLayout = { nullptr, 0 };
    renderDesc.pRootSignature = mRenderRootSignature.Get();
    renderDesc.VS =
    {
        reinterpret_cast<BYTE*>(vertexShader->GetBufferPointer()),
        vertexShader->GetBufferSize()
    };
    renderDesc.GS =
    {
        reinterpret_cast<BYTE*>(geometryShader->GetBufferPointer()),
        geometryShader->GetBufferSize()
    };
    renderDesc.PS =
    {
        reinterpret_cast<BYTE*>(pixelShader->GetBufferPointer()),
        pixelShader->GetBufferSize()
    };

    renderDesc.RasterizerState =
        CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    renderDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    renderDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    renderDesc.DepthStencilState =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    renderDesc.SampleMask = UINT_MAX;
    renderDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    renderDesc.NumRenderTargets = GBUFFER_COUNT;

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        renderDesc.RTVFormats[i] = GBUFFER_FORMATS[i];
    }

    renderDesc.DSVFormat = depthFormat;
    renderDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(
        &renderDesc,
        IID_PPV_ARGS(&mRenderPSO));
    ThrowIfFailedParticle(hr, "ParticleSystem failed to create render PSO");
}

void ParticleSystem::Update(
    ID3D12GraphicsCommandList* commandList,
    float deltaTime,
    float totalTime,
    CXMMATRIX view,
    CXMMATRIX projection,
    XMFLOAT3 eyePosition,
    UINT frameResourceIndex)
{
    UINT writeBufferIndex = 1u - mReadBufferIndex;

    TransitionParticleBuffer(
        commandList,
        mReadBufferIndex,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionParticleBuffer(
        commandList,
        writeBufferIndex,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    auto counterToCopy = CD3DX12_RESOURCE_BARRIER::Transition(
        mCounterBuffers[writeBufferIndex].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    commandList->ResourceBarrier(1, &counterToCopy);
    commandList->CopyBufferRegion(
        mCounterBuffers[writeBufferIndex].Get(),
        0,
        mCounterUpload.Get(),
        sizeof(UINT),
        sizeof(UINT));
    auto counterToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        mCounterBuffers[writeBufferIndex].Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(1, &counterToUav);

    ParticlePassConstants constants = {};
    XMStoreFloat4x4(
        &constants.ViewProj,
        XMMatrixTranspose(view * projection));
    constants.EyePosW = eyePosition;
    constants.DeltaTime = (std::min)(deltaTime, 1.0f / 20.0f);
    constants.TotalTime = totalTime;
    constants.ParticleCount = mMaxParticles;
    constants.EmitterPositionW = XMFLOAT3(0.0f, 0.25f, 0.0f);
    constants.FrameIndex = mFrameSequence++;

    XMMATRIX inverseView = XMMatrixInverse(nullptr, view);
    XMVECTOR cameraRight = XMVector3Normalize(
        XMVector3TransformNormal(
            XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
            inverseView));
    XMVECTOR cameraUp = XMVector3Normalize(
        XMVector3TransformNormal(
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
            inverseView));
    XMStoreFloat3(&constants.CameraRightW, cameraRight);
    XMStoreFloat3(&constants.CameraUpW, cameraUp);

    UINT safeFrameIndex = frameResourceIndex % mFrameCount;
    BYTE* destination =
        mMappedConstants +
        static_cast<size_t>(safeFrameIndex) * mConstantBufferStride;
    std::memcpy(destination, &constants, sizeof(constants));

    mCurrentConstantBufferAddress =
        mConstantBufferUpload->GetGPUVirtualAddress() +
        static_cast<UINT64>(safeFrameIndex) * mConstantBufferStride;

    ID3D12DescriptorHeap* heaps[] = { mParticleDescriptorHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    commandList->SetPipelineState(mComputePSO.Get());
    commandList->SetComputeRootSignature(mComputeRootSignature.Get());
    commandList->SetComputeRootDescriptorTable(
        0,
        GetGpuDescriptor(mReadBufferIndex));
    commandList->SetComputeRootDescriptorTable(
        1,
        GetGpuDescriptor(writeBufferIndex));
    commandList->SetComputeRootConstantBufferView(
        2,
        mCurrentConstantBufferAddress);

    constexpr UINT threadGroupSize = 256;
    commandList->Dispatch(
        (mMaxParticles + threadGroupSize - 1) / threadGroupSize,
        1,
        1);

    D3D12_RESOURCE_BARRIER uavBarriers[2] =
    {
        CD3DX12_RESOURCE_BARRIER::UAV(
            mParticleBuffers[writeBufferIndex].Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(
            mCounterBuffers[writeBufferIndex].Get())
    };
    commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);

    TransitionParticleBuffer(
        commandList,
        writeBufferIndex,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    mCurrentRenderBufferIndex = writeBufferIndex;
    mReadBufferIndex = writeBufferIndex;
    mHasUpdated = true;
}

void ParticleSystem::Draw(ID3D12GraphicsCommandList* commandList)
{
    if (!mHasUpdated)
    {
        return;
    }

    ID3D12DescriptorHeap* heaps[] = { mParticleDescriptorHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    commandList->SetPipelineState(mRenderPSO.Get());
    commandList->SetGraphicsRootSignature(mRenderRootSignature.Get());
    commandList->SetGraphicsRootDescriptorTable(
        0,
        GetGpuDescriptor(2 + mCurrentRenderBufferIndex));
    commandList->SetGraphicsRootConstantBufferView(
        1,
        mCurrentConstantBufferAddress);

    commandList->IASetVertexBuffers(0, 0, nullptr);
    commandList->IASetIndexBuffer(nullptr);
    commandList->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    commandList->DrawInstanced(mMaxParticles, 1, 0, 0);
}

void ParticleSystem::ReleaseInitializationUpload()
{
    mParticleInitializationUpload.Reset();
}

D3D12_GPU_DESCRIPTOR_HANDLE ParticleSystem::GetGpuDescriptor(
    UINT index) const
{
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(
        mParticleDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    handle.Offset(index, mDescriptorSize);
    return handle;
}

void ParticleSystem::TransitionParticleBuffer(
    ID3D12GraphicsCommandList* commandList,
    UINT bufferIndex,
    D3D12_RESOURCE_STATES newState)
{
    if (mParticleStates[bufferIndex] == newState)
    {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mParticleBuffers[bufferIndex].Get(),
        mParticleStates[bufferIndex],
        newState);
    commandList->ResourceBarrier(1, &barrier);
    mParticleStates[bufferIndex] = newState;
}
