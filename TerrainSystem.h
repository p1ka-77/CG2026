#pragma once
#include "../../Common/d3dUtil.h"
#include <DirectXCollision.h>
#include <array>

// Disk layout written by Tools/pack_terrain.py. A node owns one height tile.
struct TerrainNode
{
    float X, Z, Size, MinHeight, MaxHeight, Error;
    int Children[4];
    unsigned Level;
    float SkirtDepth;
};
static_assert(sizeof(TerrainNode) == 48, "Terrain file layout changed");

class TerrainSystem
{
public:
    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, DXGI_FORMAT depthFormat);
    void ReleaseUploadBuffers();
    void Select(DirectX::CXMMATRIX view, DirectX::CXMMATRIX projection, float viewportHeight);
    void Draw(ID3D12GraphicsCommandList* cmd, DirectX::CXMMATRIX viewProjection);
    std::wstring Status() const;
    void RunSelectionChecks();

    bool EnableLOD = true;
    bool EnableCulling = true;
    bool FreezeSelection = false;
    bool EnableSkirts = true;
    unsigned DebugMode = 0; // natural, LOD colors, wireframe
    float PixelError = 5.0f;

private:
    void Visit(unsigned index);
    DirectX::BoundingBox Bounds(const TerrainNode& node) const;
    std::vector<TerrainNode> mNodes;
    std::vector<unsigned> mSelected;
    unsigned mGridCells = 0, mMaxDepth = 0, mTileSize = 0;
    float mWorldSize = 0, mHeightScale = 0;
    unsigned mIndexCount = 0, mSurfaceIndexCount = 0;
    unsigned mVisited = 0, mRejected = 0, mHiddenLeaves = 0;
    std::array<unsigned, 8> mLevels = {};
    DirectX::BoundingFrustum mFrustum;
    DirectX::XMFLOAT3 mSelectionEye = {}, mSelectionForward = {};
    float mProjectionScale = 1;
    bool mSelectionValid = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHeightTiles, mHeightUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mVB, mIB, mVBUpload, mIBUpload;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO, mWirePSO;
    D3D12_VERTEX_BUFFER_VIEW mVBView = {};
    D3D12_INDEX_BUFFER_VIEW mIBView = {};
};
