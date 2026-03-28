#include "TextureManager.h"
#include "DirectXTex.h"
#include <stdexcept>
#include <vector>
#include <algorithm>

void TextureManager::Init(ID3D12Device* device, UINT srvDescriptorSize)
{
    mDevice            = device;
    mSrvDescriptorSize = srvDescriptorSize;
}

void TextureManager::CreateSrvHeap(UINT maxTextures)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = maxTextures;
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mSrvHeap));
    if (FAILED(hr))
        throw std::runtime_error("TextureManager: failed to create SRV heap");
}

UINT TextureManager::LoadTexture(const std::wstring& filePath,
                                  const std::wstring& name,
                                  ID3D12GraphicsCommandList* cmdList)
{
    // Если уже загружена — вернуть существующий индекс
    auto it = mTextures.find(name);
    if (it != mTextures.end())
        return it->second->SrvHeapIndex;

    auto texData = std::make_unique<TextureData>();
    texData->Name = name;

    // Определяем расширение вручную (без filesystem)
    std::wstring ext;
    size_t dotPos = filePath.find_last_of(L'.');
    if (dotPos != std::wstring::npos)
    {
        ext = filePath.substr(dotPos);
        for (auto& c : ext) c = towlower(c);
    }

    DirectX::TexMetadata  metadata;
    DirectX::ScratchImage image;
    HRESULT hr;

    if (ext == L".dds")
    {
        hr = DirectX::LoadFromDDSFile(filePath.c_str(),
                                      DirectX::DDS_FLAGS_NONE,
                                      &metadata, image);
    }
    else
    {
        hr = DirectX::LoadFromWICFile(filePath.c_str(),
                                      DirectX::WIC_FLAGS_NONE,
                                      &metadata, image);

        // Генерируем mip-цепочку для WIC форматов
        if (SUCCEEDED(hr))
        {
            DirectX::ScratchImage mipChain;
            HRESULT hrMip = DirectX::GenerateMipMaps(
                *image.GetImage(0, 0, 0),
                DirectX::TEX_FILTER_DEFAULT,
                0,
                mipChain);
            if (SUCCEEDED(hrMip))
            {
                image = std::move(mipChain);
                metadata = image.GetMetadata();
            }
        }
    }

    if (FAILED(hr))
        throw std::runtime_error("TextureManager: failed to load texture file");

    // Создаём GPU-ресурс (DEFAULT heap)
    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width            = (UINT64)metadata.width;
    resDesc.Height           = (UINT)metadata.height;
    resDesc.DepthOrArraySize = (UINT16)metadata.arraySize;
    resDesc.MipLevels        = (UINT16)metadata.mipLevels;
    resDesc.Format           = metadata.format;
    resDesc.SampleDesc       = { 1, 0 };
    resDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    hr = mDevice->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(texData->Resource.GetAddressOf())
    );
    if (FAILED(hr))
        throw std::runtime_error("TextureManager: failed to create GPU texture resource");

    // Upload-буфер
    UINT64 uploadSize = GetRequiredIntermediateSize(texData->Resource.Get(), 0,
                                                    (UINT)metadata.mipLevels);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width            = uploadSize;
    bufDesc.Height           = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels        = 1;
    bufDesc.Format           = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc       = { 1, 0 };
    bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    hr = mDevice->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(texData->UploadHeap.GetAddressOf())
    );
    if (FAILED(hr))
        throw std::runtime_error("TextureManager: failed to create upload heap");

    // Subresource data
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    for (size_t mip = 0; mip < metadata.mipLevels; ++mip)
    {
        const DirectX::Image* img = image.GetImage(mip, 0, 0);
        D3D12_SUBRESOURCE_DATA sub = {};
        sub.pData      = img->pixels;
        sub.RowPitch   = (LONG_PTR)img->rowPitch;
        sub.SlicePitch = (LONG_PTR)img->slicePitch;
        subresources.push_back(sub);
    }

    UpdateSubresources(cmdList,
                       texData->Resource.Get(),
                       texData->UploadHeap.Get(),
                       0, 0,
                       (UINT)subresources.size(),
                       subresources.data());

    // Барьер: COPY_DEST -> PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = texData->Resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    // Создаём SRV
    UINT index = mNextIndex++;
    texData->SrvHeapIndex = index;
    CreateSRV(texData->Resource.Get(), index);

    mTextures[name] = std::move(texData);
    return index;
}

void TextureManager::CreateSRV(ID3D12Resource* resource, UINT heapIndex)
{
    D3D12_RESOURCE_DESC resDesc = resource->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                          = resDesc.Format;
    srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip       = 0;
    srvDesc.Texture2D.MipLevels             = resDesc.MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp   = 0.0f;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (SIZE_T)heapIndex * mSrvDescriptorSize;

    mDevice->CreateShaderResourceView(resource, &srvDesc, handle);
}

void TextureManager::BindHeap(ID3D12GraphicsCommandList* cmdList) const
{
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetGpuHandle(UINT index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += (UINT64)index * mSrvDescriptorSize;
    return handle;
}

void TextureManager::ReleaseUploadHeaps()
{
    for (auto& pair : mTextures)
        pair.second->UploadHeap.Reset();
}
