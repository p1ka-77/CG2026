#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>

#include "../../Common/d3dx12.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct ParticleGPU
{
    XMFLOAT3 Position;
    float Age;

    XMFLOAT3 Velocity;
    float Lifetime;

    XMFLOAT4 Color;
    XMFLOAT2 Size;
    UINT Seed;
    float Padding;
};

struct ParticlePassConstants
{
    XMFLOAT4X4 ViewProj;

    XMFLOAT3 EyePosW;
    float DeltaTime;

    XMFLOAT3 CameraRightW;
    float TotalTime;

    XMFLOAT3 CameraUpW;
    UINT ParticleCount;

    XMFLOAT3 EmitterPositionW;
    UINT FrameIndex;
};

class ParticleSystem
{
public:
    ParticleSystem() = default;
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;
    ~ParticleSystem();

    void Initialize(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        UINT maxParticles,
        UINT frameCount,
        DXGI_FORMAT depthFormat);

    void Update(
        ID3D12GraphicsCommandList* commandList,
        float deltaTime,
        float totalTime,
        CXMMATRIX view,
        CXMMATRIX projection,
        XMFLOAT3 eyePosition,
        UINT frameResourceIndex);

    void Draw(ID3D12GraphicsCommandList* commandList);
    void ReleaseInitializationUpload();

    UINT GetParticleCount() const { return mMaxParticles; }

private:
    void BuildBuffers(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList);
    void BuildDescriptorHeap(ID3D12Device* device);
    void BuildConstantBuffer(ID3D12Device* device);
    void BuildRootSignatures(ID3D12Device* device);
    void BuildPipelineStates(
        ID3D12Device* device,
        DXGI_FORMAT depthFormat);

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptor(UINT index) const;
    void TransitionParticleBuffer(
        ID3D12GraphicsCommandList* commandList,
        UINT bufferIndex,
        D3D12_RESOURCE_STATES newState);

private:
    static constexpr UINT BufferCount = 2;
    static constexpr UINT DescriptorCount = 4;

    UINT mMaxParticles = 0;
    UINT mFrameCount = 0;
    UINT mReadBufferIndex = 0;
    UINT mCurrentRenderBufferIndex = 0;
    UINT mFrameSequence = 0;
    UINT mDescriptorSize = 0;
    UINT mConstantBufferStride = 0;
    bool mHasUpdated = false;

    ComPtr<ID3D12Resource> mParticleBuffers[BufferCount];
    ComPtr<ID3D12Resource> mCounterBuffers[BufferCount];
    D3D12_RESOURCE_STATES mParticleStates[BufferCount] = {};

    ComPtr<ID3D12Resource> mParticleInitializationUpload;
    ComPtr<ID3D12Resource> mCounterUpload;
    ComPtr<ID3D12Resource> mConstantBufferUpload;
    BYTE* mMappedConstants = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS mCurrentConstantBufferAddress = 0;

    ComPtr<ID3D12DescriptorHeap> mParticleDescriptorHeap;
    ComPtr<ID3D12RootSignature> mComputeRootSignature;
    ComPtr<ID3D12RootSignature> mRenderRootSignature;
    ComPtr<ID3D12PipelineState> mComputePSO;
    ComPtr<ID3D12PipelineState> mRenderPSO;
};
