#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>

#include "../../Common/d3dx12.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// These values are copied to b0 as root constants and must match
// cbPostProcess in PostProcessPass.hlsl.
struct PostProcessConstants
{
    UINT EffectMode = 1;
    float Strength = 1.0f;
    float EdgeThreshold = 0.08f;
    float Padding0 = 0.0f;

    XMFLOAT2 InvRenderTargetSize = { 1.0f, 1.0f };
    XMFLOAT2 Padding1 = { 0.0f, 0.0f };
};

static_assert(sizeof(PostProcessConstants) == 32,
    "PostProcessConstants must match the HLSL constant buffer");

class PostProcessSystem
{
public:
    void Initialize(
        ID3D12Device* device,
        UINT width,
        UINT height,
        DXGI_FORMAT renderTargetFormat,
        ID3D12DescriptorHeap* gBufferSrvHeap);

    void OnResize(
        ID3D12Device* device,
        UINT width,
        UINT height,
        ID3D12DescriptorHeap* gBufferSrvHeap);

    void BeginSceneColorPass(ID3D12GraphicsCommandList* commandList);
    void EndSceneColorPass(ID3D12GraphicsCommandList* commandList);

    void Draw(
        ID3D12GraphicsCommandList* commandList,
        D3D12_CPU_DESCRIPTOR_HANDLE outputRtv,
        D3D12_VIEWPORT viewport,
        D3D12_RECT scissorRect,
        UINT effectMode,
        float strength);

    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneColorRTV() const;
    bool IsInitialized() const { return mInitialized; }

private:
    void BuildRootSignature(ID3D12Device* device);
    void BuildPipelineState(ID3D12Device* device);
    void BuildSizeDependentResources(
        ID3D12Device* device,
        UINT width,
        UINT height,
        ID3D12DescriptorHeap* gBufferSrvHeap);

private:
    static constexpr UINT PostProcessSrvCount = 4;

    bool mInitialized = false;
    UINT mWidth = 1;
    UINT mHeight = 1;
    UINT mSrvDescriptorSize = 0;
    DXGI_FORMAT mRenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    ComPtr<ID3D12Resource> mSceneColor;
    ComPtr<ID3D12DescriptorHeap> mSceneColorRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mPostProcessSrvHeap;
    ComPtr<ID3D12RootSignature> mRootSignature;
    ComPtr<ID3D12PipelineState> mPipelineState;
};
