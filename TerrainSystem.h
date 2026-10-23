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
    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, DXGI_FORMAT depthFormat, unsigned frameCount = 3);
    void ReleaseUploadBuffers();
    void Select(DirectX::CXMMATRIX view, DirectX::CXMMATRIX projection, float viewportHeight);
    void Draw(ID3D12GraphicsCommandList* cmd, DirectX::CXMMATRIX viewProjection);
    std::wstring Status() const;
    void RunSelectionChecks();
    void UpdateBrush(DirectX::CXMMATRIX view, DirectX::CXMMATRIX projection,
        int width, int height, int mouseX, int mouseY, bool paint, bool erase, float dt);
    void UploadPaint(ID3D12GraphicsCommandList* cmd, unsigned frameIndex);
    void EndStroke();
    void UndoPaint();
    void RedoPaint();
    bool SavePaint(const std::string& path = "Assets/Terrain/terrain_paint.bin");
    bool LoadPaint(const std::string& path = "Assets/Terrain/terrain_paint.bin");
    void RunBrushChecks();
    void MakePaintDemo(); // In-memory capture fixture; does not modify saved paint.

    bool BrushEnabled = true;
    bool PaintVisible = true;
    float BrushRadius = 65.0f;
    unsigned BrushColor = 0;

    bool EnableLOD = true;
    bool EnableCulling = true;
    bool FreezeSelection = false;
    bool EnableSkirts = true;
    unsigned DebugMode = 0; // natural, LOD colors, wireframe
    float PixelError = 5.0f;

private:
    // Runtime clearing only: source height tiles and saved paint stay unchanged.
    // Keep this function in sync with Height() in TerrainPass.hlsl.
    static float SceneHeight(float x, float z, float height);
    static bool TouchesSceneClearing(const TerrainNode& node);
    void InitializePaint(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, unsigned frameCount);
    bool PickSurface(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction, DirectX::XMFLOAT3& hit) const;
    void PaintAt(const DirectX::XMFLOAT3& hit, bool erase, float dt);
    bool Stamp(float x, float z, bool erase, float opacity);
    DirectX::XMFLOAT3 PaintColor() const;
    void PushUndo();
    static constexpr unsigned PaintSize = 1024;
    std::vector<uint16_t> mHeightCPU;
    std::vector<uint8_t> mPaintCPU;
    std::vector<std::vector<uint8_t>> mPaintUndo, mPaintRedo;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> mPaintUploads;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPaintTexture;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT mPaintLayout = {};
    D3D12_GPU_DESCRIPTOR_HANDLE mPaintSRV = {};
    UINT64 mPaintUploadSize = 0;
    uint64_t mPaintRevision = 1, mPaintGPURevision = 0;
    uint32_t mTerrainFingerprint = 2166136261u;
    bool mBrushHit = false, mStrokeActive = false, mBrushErase = false;
    DirectX::XMFLOAT3 mBrushPosition = {}, mLastPaintPosition = {};
    std::wstring mPaintMessage;
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
