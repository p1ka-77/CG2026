#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <memory>
#include "DirectXTex.h"   // из пакета DirectXTex
#include "C:\d3d12book\d3d12book-master\Common\d3dx12.h"       // из пакета DirectX12 Agility SDK / d3dx12

using Microsoft::WRL::ComPtr;

// ─────────────────────────────────────────────────────────────────────────────
// Структура для хранения одной текстуры
// ─────────────────────────────────────────────────────────────────────────────
struct TextureData
{
    std::wstring            Name;
    ComPtr<ID3D12Resource>  Resource;           // GPU-ресурс (DEFAULT heap)
    ComPtr<ID3D12Resource>  UploadHeap;         // Upload-буфер (держим до завершения копирования)
    UINT                    SrvHeapIndex = 0;   // индекс в SRV Descriptor Heap
};

// ─────────────────────────────────────────────────────────────────────────────
// TextureManager — загрузка DDS/PNG/JPG → GPU
// ─────────────────────────────────────────────────────────────────────────────
class TextureManager
{
public:
    // Инициализация: передаём устройство, command-list и размер одного SRV-дескриптора
    void Init(ID3D12Device* device, UINT srvDescriptorSize);

    // Создаёт Descriptor Heap под `maxTextures` текстур
    void CreateSrvHeap(UINT maxTextures);

    // Загружает файл (DDS через DDSTextureLoader, остальное через WIC/ScratchImage)
    // Возвращает индекс в SRV-куче
    UINT LoadTexture(const std::wstring& filePath,
                     const std::wstring& name,
                     ID3D12GraphicsCommandList* cmdList);

    // Привязывает кучу к command-list
    void BindHeap(ID3D12GraphicsCommandList* cmdList) const;

    // GPU-дескриптор по индексу (для SetGraphicsRootDescriptorTable)
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT index) const;

    // После выполнения командного списка можно освободить upload-буферы
    void ReleaseUploadHeaps();

    ID3D12DescriptorHeap* GetHeap() const { return mSrvHeap.Get(); }

private:
    ID3D12Device*                mDevice            = nullptr;
    UINT                         mSrvDescriptorSize = 0;
    ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    UINT                         mNextIndex = 0;

    std::unordered_map<std::wstring, std::unique_ptr<TextureData>> mTextures;

    // Внутренний метод: создаёт SRV для ресурса и записывает его в кучу
    void CreateSRV(ID3D12Resource* resource, UINT heapIndex);
};
