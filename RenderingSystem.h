#pragma once

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <array>
#include <vector>

#include "../../Common/d3dx12.h"
#include "GBuffer.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

enum class LightType : int
{
    Directional = 0,
    Point = 1,
    Spot = 2
};

// Структура должна совпадать с LightData в LightingPass.hlsl
struct LightData
{
    XMFLOAT3 Position;
    float    Range;

    XMFLOAT3 Direction;
    float    SpotAngle;

    XMFLOAT3 Strength;
    int      Type;

    float    SpotFalloff;
    XMFLOAT3 Pad;
};

static constexpr int MAX_LIGHTS = 16;
static constexpr UINT SHADOW_CASCADE_COUNT = 4;
static constexpr UINT SHADOW_MAP_SIZE = 2048;

struct ShadowPassConstants
{
    XMFLOAT4X4 LightViewProj;
    float TotalTime = 0.0f;
    XMFLOAT3 Pad = { 0.0f, 0.0f, 0.0f };
};

// Constant buffer для Lighting Pass.
// alignas(256) нужен под требования D3D12 constant buffer.
struct alignas(256) LightingPassCB
{
    LightData Lights[MAX_LIGHTS];

    int      LightCount;
    XMFLOAT3 EyePosW;

    XMFLOAT4 AmbientLight;

    XMFLOAT4X4 ShadowTransforms[SHADOW_CASCADE_COUNT];
    XMFLOAT4 CascadeSplits;
    XMFLOAT4 ShadowMapInfo;
    XMFLOAT4 CameraForward;

    int ShadowsEnabled;
    int VisualizeCascades;
    XMFLOAT2 ShadowPad;
};

class RenderingSystem
{
public:
    void Init(
        ID3D12Device* device,
        UINT width,
        UINT height,
        DXGI_FORMAT backBufferFormat,
        DXGI_FORMAT depthFormat);

    void AddDirectionalLight(XMFLOAT3 direction, XMFLOAT3 strength);

    void AddPointLight(
        XMFLOAT3 position,
        float range,
        XMFLOAT3 strength);

    void AddSpotLight(
        XMFLOAT3 position,
        XMFLOAT3 direction,
        float range,
        float spotAngle,
        float falloff,
        XMFLOAT3 strength);

    void ClearLights() { mLights.clear(); }

    void BeginGeometryPass(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        D3D12_VIEWPORT viewport,
        D3D12_RECT scissorRect);

    void EndGeometryPass(ID3D12GraphicsCommandList* cmdList);

    void BeginShadowPass(ID3D12GraphicsCommandList* cmdList);
    void SetShadowCascade(
        ID3D12GraphicsCommandList* cmdList,
        UINT cascadeIndex,
        const ShadowPassConstants& constants);
    void EndShadowPass(ID3D12GraphicsCommandList* cmdList);

    void SetShadowData(
        const std::array<XMFLOAT4X4, SHADOW_CASCADE_COUNT>& shadowTransforms,
        XMFLOAT4 cascadeSplits,
        XMFLOAT3 cameraForward,
        bool shadowsEnabled,
        bool visualizeCascades);

    void LightingPass(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV,
        D3D12_VIEWPORT viewport,
        D3D12_RECT scissorRect,
        XMFLOAT3 eyePos,
        XMFLOAT4 ambientLight);

    void OnResize(ID3D12Device* device, UINT width, UINT height);

    void BuildGeometryRootSignature(ID3D12Device* device);
    void BuildLightingRootSignature(ID3D12Device* device);
    void BuildShadowRootSignature(ID3D12Device* device);

    void BuildGeometryPSO(
        ID3D12Device* device,
        const D3D12_INPUT_LAYOUT_DESC& inputLayout,
        ID3DBlob* vsBlob,
        ID3DBlob* psBlob,
        DXGI_FORMAT depthFormat);

    void BuildLightingPSO(
        ID3D12Device* device,
        ID3DBlob* vsBlob,
        ID3DBlob* psBlob,
        DXGI_FORMAT backBufferFormat);

    void BuildShadowPSO(
        ID3D12Device* device,
        const D3D12_INPUT_LAYOUT_DESC& inputLayout,
        ID3DBlob* vsBlob);

    void BuildFullscreenQuad(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList);

    void UpdateLightingCB(XMFLOAT3 eyePos, XMFLOAT4 ambientLight);

    GBuffer& GetGBuffer() { return mGBuffer; }

    ID3D12RootSignature* GetGeometryRootSignature() const
    {
        return mGeometryRootSig.Get();
    }

    ID3D12PipelineState* GetGeometryPSO() const
    {
        return mGeometryPSO.Get();
    }

    UINT GetShadowMapSize() const { return SHADOW_MAP_SIZE; }

private:
    void BuildLightingSRVHeap(ID3D12Device* device);
    void BuildShadowResources(ID3D12Device* device);
    void BuildShadowSRV(ID3D12Device* device);
    D3D12_GPU_DESCRIPTOR_HANDLE GetLightingSRV(UINT index) const;

    GBuffer mGBuffer;

    ComPtr<ID3D12RootSignature> mGeometryRootSig;
    ComPtr<ID3D12RootSignature> mLightingRootSig;
    ComPtr<ID3D12RootSignature> mShadowRootSig;

    ComPtr<ID3D12PipelineState> mGeometryPSO;
    ComPtr<ID3D12PipelineState> mLightingPSO;
    ComPtr<ID3D12PipelineState> mShadowPSO;

    ComPtr<ID3D12Resource> mShadowMap;
    ComPtr<ID3D12DescriptorHeap> mShadowDSVHeap;
    ComPtr<ID3D12DescriptorHeap> mLightingSRVHeap;

    D3D12_VIEWPORT mShadowViewport = {};
    D3D12_RECT mShadowScissorRect = {};
    UINT mDSVDescriptorSize = 0;
    UINT mSRVDescriptorSize = 0;

    ComPtr<ID3D12Resource> mQuadVB;
    ComPtr<ID3D12Resource> mQuadIB;
    ComPtr<ID3D12Resource> mQuadVBUpload;
    ComPtr<ID3D12Resource> mQuadIBUpload;

    D3D12_VERTEX_BUFFER_VIEW mQuadVBView = {};
    D3D12_INDEX_BUFFER_VIEW  mQuadIBView = {};

    ComPtr<ID3D12Resource> mLightingCBUpload;
    LightingPassCB* mLightingCBMapped = nullptr;

    std::vector<LightData> mLights;

    std::array<XMFLOAT4X4, SHADOW_CASCADE_COUNT> mShadowTransforms = {};
    XMFLOAT4 mCascadeSplits = {};
    XMFLOAT3 mCameraForward = { 0.0f, 0.0f, 1.0f };
    bool mShadowsEnabled = true;
    bool mVisualizeCascades = false;

    UINT mWidth = 0;
    UINT mHeight = 0;
};
