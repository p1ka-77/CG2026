#include "GBuffer.h"
#include <stdexcept>

void GBuffer::Init(
    ID3D12Device* device,
    UINT width,
    UINT height,
    UINT rtvDescriptorSize,
    UINT srvDescriptorSize)
{
    mWidth = width;
    mHeight = height;
    mRTVDescriptorSize = rtvDescriptorSize;
    mSRVDescriptorSize = srvDescriptorSize;

    BuildResources(device, width, height);
    BuildRTVHeap(device);
    BuildSRVHeap(device);
}

void GBuffer::BuildResources(ID3D12Device* device, UINT width, UINT height)
{
    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        mRTs[i].Reset();

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = GBUFFER_FORMATS[i];
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = GBUFFER_FORMATS[i];
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        D3D12_HEAP_PROPERTIES defaultHeap = {};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        defaultHeap.CreationNodeMask = 1;
        defaultHeap.VisibleNodeMask = 1;

        HRESULT hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(&mRTs[i]));

        if (FAILED(hr))
        {
            throw std::runtime_error("GBuffer::BuildResources failed");
        }
    }
}

void GBuffer::BuildRTVHeap(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&mRTVHeap));

    if (FAILED(hr))
    {
        throw std::runtime_error("GBuffer::BuildRTVHeap failed");
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        mRTVHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        device->CreateRenderTargetView(
            mRTs[i].Get(),
            nullptr,
            rtvHandle);

        rtvHandle.Offset(1, mRTVDescriptorSize);
    }
}

void GBuffer::BuildSRVHeap(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(
        &srvHeapDesc,
        IID_PPV_ARGS(&mSRVHeap));

    if (FAILED(hr))
    {
        throw std::runtime_error("GBuffer::BuildSRVHeap failed");
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        mSRVHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = GBUFFER_FORMATS[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView(
            mRTs[i].Get(),
            &srvDesc,
            srvHandle);

        srvHandle.Offset(1, mSRVDescriptorSize);
    }
}

void GBuffer::TransitionToWrite(ID3D12GraphicsCommandList* cmdList)
{
    D3D12_RESOURCE_BARRIER barriers[GBUFFER_COUNT];

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        barriers[i] = {};
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[i].Transition.pResource = mRTs[i].Get();
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }

    cmdList->ResourceBarrier(GBUFFER_COUNT, barriers);
}

void GBuffer::TransitionToRead(ID3D12GraphicsCommandList* cmdList)
{
    D3D12_RESOURCE_BARRIER barriers[GBUFFER_COUNT];

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        barriers[i] = {};
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[i].Transition.pResource = mRTs[i].Get();
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }

    cmdList->ResourceBarrier(GBUFFER_COUNT, barriers);
}

void GBuffer::Clear(ID3D12GraphicsCommandList* cmdList)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        mRTVHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        cmdList->ClearRenderTargetView(
            rtvHandle,
            mClearColor,
            0,
            nullptr);

        rtvHandle.Offset(1, mRTVDescriptorSize);
    }
}

void GBuffer::BindAsRenderTargets(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[GBUFFER_COUNT];

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        mRTVHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < GBUFFER_COUNT; ++i)
    {
        rtvHandles[i] = rtvHandle;
        rtvHandle.Offset(1, mRTVDescriptorSize);
    }

    cmdList->OMSetRenderTargets(
        GBUFFER_COUNT,
        rtvHandles,
        false,
        &dsv);
}

void GBuffer::BindSRVHeap(ID3D12GraphicsCommandList* cmdList)
{
    ID3D12DescriptorHeap* heaps[] = { mSRVHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::GetSRV(UINT index) const
{
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(
        mSRVHeap->GetGPUDescriptorHandleForHeapStart());

    handle.Offset(index, mSRVDescriptorSize);

    return handle;
}

void GBuffer::OnResize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    BuildResources(device, width, height);
    BuildRTVHeap(device);
    BuildSRVHeap(device);
}