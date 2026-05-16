#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include "../../Common/d3dx12.h"

using Microsoft::WRL::ComPtr;

//  оличество render target'ов в GBuffer
static constexpr UINT GBUFFER_COUNT = 3;

// ‘орматы GBuffer:
// RT0 Ч World Position
// RT1 Ч Normal
// RT2 Ч Albedo + Roughness
static constexpr DXGI_FORMAT GBUFFER_FORMATS[GBUFFER_COUNT] =
{
    DXGI_FORMAT_R32G32B32A32_FLOAT,  // RT0: World position.xyz + flag
    DXGI_FORMAT_R16G16B16A16_FLOAT,  // RT1: Normal.xyz packed
    DXGI_FORMAT_R8G8B8A8_UNORM       // RT2: Albedo.rgb + roughness
};

class GBuffer
{
public:
    void Init(
        ID3D12Device* device,
        UINT width,
        UINT height,
        UINT rtvDescriptorSize,
        UINT srvDescriptorSize);

    void BuildRTVHeap(ID3D12Device* device);
    void BuildSRVHeap(ID3D12Device* device);

    void TransitionToWrite(ID3D12GraphicsCommandList* cmdList);
    void TransitionToRead(ID3D12GraphicsCommandList* cmdList);

    void Clear(ID3D12GraphicsCommandList* cmdList);

    void BindAsRenderTargets(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv);

    void BindSRVHeap(ID3D12GraphicsCommandList* cmdList);

    D3D12_GPU_DESCRIPTOR_HANDLE GetSRV(UINT index) const;

    void OnResize(ID3D12Device* device, UINT width, UINT height);

    ID3D12DescriptorHeap* GetSRVHeap() const { return mSRVHeap.Get(); }

private:
    void BuildResources(ID3D12Device* device, UINT width, UINT height);

private:
    ComPtr<ID3D12Resource>       mRTs[GBUFFER_COUNT];
    ComPtr<ID3D12DescriptorHeap> mRTVHeap;
    ComPtr<ID3D12DescriptorHeap> mSRVHeap;

    UINT mRTVDescriptorSize = 0;
    UINT mSRVDescriptorSize = 0;

    UINT mWidth = 0;
    UINT mHeight = 0;

    float mClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};
