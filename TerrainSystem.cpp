#include "TerrainSystem.h"
#include "GBuffer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    struct Header
    {
        char Magic[4];
        unsigned Version, Nodes, Grid, Depth, TileSize, SourceSize;
        float WorldSize, HeightScale;
    };
    static_assert(sizeof(Header) == 36, "Terrain header layout");
    struct GridVertex { float X, Z, Skirt; };
    struct DrawConstants { XMFLOAT4X4 VP; XMFLOAT4 Tile, Options, Brush, BrushColor, PaintMap; };
    static_assert(sizeof(DrawConstants) == 144, "Terrain shader layout");
}

void TerrainSystem::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, DXGI_FORMAT depthFormat, unsigned frameCount)
{
    std::ifstream file("Assets/Terrain/terrain.bin", std::ios::binary);
    Header header = {};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
        std::memcmp(header.Magic, "TRN1", 4) || header.Version != 1 ||
        header.Grid != 32 || header.Depth > 5 || header.TileSize != header.Grid + 3 ||
        header.Nodes != ((1u << (2*(header.Depth+1)))-1)/3 ||
        header.SourceSize != header.Grid * (1u << header.Depth)+1 ||
        !std::isfinite(header.WorldSize) || header.WorldSize <= 0 ||
        !std::isfinite(header.HeightScale) || header.HeightScale <= 0)
        throw std::runtime_error("Missing/invalid Assets/Terrain/terrain.bin. Run from the Crate folder; see TERRAIN_LAB.md.");
    mGridCells = header.Grid; mMaxDepth = header.Depth; mTileSize = header.TileSize;
    mWorldSize = header.WorldSize; mHeightScale = header.HeightScale;
    mNodes.resize(header.Nodes);
    if (!file.read(reinterpret_cast<char*>(mNodes.data()), mNodes.size()*sizeof(TerrainNode)))
        throw std::runtime_error("Truncated terrain node table");
    for (unsigned i=0; i<mNodes.size(); ++i)
    {
        const auto& n=mNodes[i];
        if (n.Level>mMaxDepth || !std::isfinite(n.Error) || n.Error<0 ||
            n.MinHeight<0 || n.MaxHeight>mHeightScale+0.01f || n.MinHeight>n.MaxHeight ||
            n.Size<=0 || n.SkirtDepth<0)
            throw std::runtime_error("Invalid terrain node metadata");
        for (int child : n.Children)
            if ((n.Level==mMaxDepth && child!=-1) ||
                (n.Level<mMaxDepth && (child<=static_cast<int>(i) || child>=static_cast<int>(mNodes.size()) || mNodes[child].Level!=n.Level+1)))
                throw std::runtime_error("Invalid terrain quadtree child");
    }
    const size_t layerElements = mTileSize*mTileSize;
    std::vector<uint16_t> heights(layerElements*mNodes.size());
    if (!file.read(reinterpret_cast<char*>(heights.data()), heights.size()*sizeof(uint16_t)))
        throw std::runtime_error("Truncated terrain height tiles");
    mHeightCPU = heights; // Exact rendered LOD heights for cursor ray/triangle intersection.
    for (uint16_t value : heights)
    {
        mTerrainFingerprint = (mTerrainFingerprint ^ (value & 255u))*16777619u;
        mTerrainFingerprint = (mTerrainFingerprint ^ (value >> 8))*16777619u;
    }

    auto heapDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto heapUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_UNORM,mTileSize,mTileSize,
        static_cast<UINT16>(mNodes.size()),1);
    ThrowIfFailed(device->CreateCommittedResource(&heapDefault,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&mHeightTiles)));
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(mHeightTiles.Get(),0,header.Nodes));
    ThrowIfFailed(device->CreateCommittedResource(&heapUpload,D3D12_HEAP_FLAG_NONE,&uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&mHeightUpload)));
    std::vector<D3D12_SUBRESOURCE_DATA> subresources(header.Nodes);
    for (unsigned i=0;i<header.Nodes;++i)
        subresources[i] = { heights.data()+i*layerElements, static_cast<LONG_PTR>(mTileSize*2), static_cast<LONG_PTR>(layerElements*2) };
    UpdateSubresources(cmd,mHeightTiles.Get(),mHeightUpload.Get(),0,0,header.Nodes,subresources.data());
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mHeightTiles.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1,&barrier);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors=2; heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&mHeap)));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format=DXGI_FORMAT_R16_UNORM; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2DArray.ArraySize=header.Nodes; srv.Texture2DArray.MipLevels=1;
    device->CreateShaderResourceView(mHeightTiles.Get(),&srv,mHeap->GetCPUDescriptorHandleForHeapStart());
    InitializePaint(device,cmd,frameCount);

    std::vector<GridVertex> vertices;
    std::vector<uint32_t> indices;
    const unsigned side=mGridCells+1;
    for (unsigned z=0;z<side;++z)
        for (unsigned x=0;x<side;++x)
            vertices.push_back({float(x)/mGridCells,float(z)/mGridCells,0});
    for (unsigned z=0;z<mGridCells;++z)
        for (unsigned x=0;x<mGridCells;++x)
        {
            unsigned a=z*side+x,b=a+1,c=a+side,d=c+1;
            indices.insert(indices.end(),{a,c,d,a,d,b});
        }
    mSurfaceIndexCount=static_cast<unsigned>(indices.size());
    for (unsigned edge=0;edge<4;++edge)
    {
        unsigned base=static_cast<unsigned>(vertices.size());
        for (unsigned i=0;i<side;++i)
        {
            unsigned id=edge==0?i:edge==1?mGridCells*side+i:edge==2?i*side:i*side+mGridCells;
            auto v=vertices[id]; vertices.push_back(v); v.Skirt=1; vertices.push_back(v);
        }
        for (unsigned i=0;i<mGridCells;++i)
        {
            unsigned a=base+i*2;
            indices.insert(indices.end(),{a,a+1,a+3,a,a+3,a+2});
        }
    }
    mIndexCount=static_cast<unsigned>(indices.size());
    UINT vbBytes=static_cast<UINT>(vertices.size()*sizeof(GridVertex)),ibBytes=mIndexCount*sizeof(uint32_t);
    mVB=d3dUtil::CreateDefaultBuffer(device,cmd,vertices.data(),vbBytes,mVBUpload);
    mIB=d3dUtil::CreateDefaultBuffer(device,cmd,indices.data(),ibBytes,mIBUpload);
    mVBView={mVB->GetGPUVirtualAddress(),vbBytes,sizeof(GridVertex)};
    mIBView={mIB->GetGPUVirtualAddress(),ibBytes,DXGI_FORMAT_R32_UINT};

    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0);
    CD3DX12_DESCRIPTOR_RANGE paintRange;
    paintRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,1);
    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsDescriptorTable(1,&range,D3D12_SHADER_VISIBILITY_VERTEX);
    params[1].InitAsConstants(sizeof(DrawConstants)/4,0);
    params[2].InitAsDescriptorTable(1,&paintRange,D3D12_SHADER_VISIBILITY_PIXEL);
    CD3DX12_STATIC_SAMPLER_DESC paintSampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    paintSampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    CD3DX12_ROOT_SIGNATURE_DESC rootDesc(3,params,1,&paintSampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serialized,errors;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
    ThrowIfFailed(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&mRoot)));
    auto vs=d3dUtil::CompileShader(L"Shaders\\TerrainPass.hlsl",nullptr,"VS","vs_5_0");
    auto ps=d3dUtil::CompileShader(L"Shaders\\TerrainPass.hlsl",nullptr,"PS","ps_5_0");
    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout={layout,1}; pso.pRootSignature=mRoot.Get();
    pso.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; pso.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pso.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask=UINT_MAX; pso.SampleDesc.Count=1;
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets=GBUFFER_COUNT;
    for (unsigned i=0;i<GBUFFER_COUNT;++i) pso.RTVFormats[i]=GBUFFER_FORMATS[i];
    pso.DSVFormat=depthFormat;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&mPSO)));
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&mWirePSO)));
}

void TerrainSystem::ReleaseUploadBuffers()
{
    mHeightUpload.Reset(); mVBUpload.Reset(); mIBUpload.Reset();
}

BoundingBox TerrainSystem::Bounds(const TerrainNode& n) const
{
    float bottom=n.MinHeight-n.SkirtDepth;
    if (TouchesSceneClearing(n)) bottom=(std::min)(bottom,-2.0f-n.SkirtDepth);
    return BoundingBox(XMFLOAT3(n.X+n.Size*0.5f,(bottom+n.MaxHeight)*0.5f,n.Z+n.Size*0.5f),
        XMFLOAT3(n.Size*0.5f,(n.MaxHeight-bottom)*0.5f,n.Size*0.5f));
}

float TerrainSystem::SceneHeight(float x,float z,float height)
{
    // Flat ground below the entire original room (lowest vertex is Y=-1.74).
    float t=(std::max)(0.0f,(std::min)(1.0f,(sqrtf(x*x+z*z)-24.0f)/156.0f));
    t=t*t*(3.0f-2.0f*t);
    return -2.0f+(height+2.0f)*t;
}

bool TerrainSystem::TouchesSceneClearing(const TerrainNode& n)
{
    return n.X<=180.0f && n.X+n.Size>=-180.0f &&
        n.Z<=180.0f && n.Z+n.Size>=-180.0f;
}

void TerrainSystem::Select(CXMMATRIX view,CXMMATRIX projection,float viewportHeight)
{
    if (FreezeSelection && mSelectionValid) return;
    XMMATRIX inverse=XMMatrixInverse(nullptr,view);
    BoundingFrustum camera;
    BoundingFrustum::CreateFromMatrix(camera,projection);
    camera.Transform(mFrustum,inverse);
    XMStoreFloat3(&mSelectionEye,inverse.r[3]);
    XMStoreFloat3(&mSelectionForward,XMVector3Normalize(inverse.r[2]));
    mProjectionScale=viewportHeight*XMVectorGetY(projection.r[1])*0.5f;
    mSelected.clear(); mLevels.fill(0); mVisited=mRejected=mHiddenLeaves=0;
    Visit(0); mSelectionValid=true;
}

void TerrainSystem::Visit(unsigned index)
{
    const auto& node=mNodes[index];
    ++mVisited;
    const auto box=Bounds(node);
    if (EnableCulling && mFrustum.Contains(box)==DISJOINT)
    {
        ++mRejected;
        mHiddenLeaves+=1u<<(2*(mMaxDepth-node.Level));
        return;
    }
    // Conservative near depth of the AABB along the camera's viewing direction.
    // The projection formula uses vertical FOV and viewport HEIGHT consistently.
    const auto& f=mSelectionForward;
    float depth=(box.Center.x-mSelectionEye.x)*f.x+(box.Center.y-mSelectionEye.y)*f.y+(box.Center.z-mSelectionEye.z)*f.z;
    depth-=std::abs(f.x)*box.Extents.x+std::abs(f.y)*box.Extents.y+std::abs(f.z)*box.Extents.z;
    float pixelError=node.Error*mProjectionScale/(std::max)(1.0f,depth);
    // Original error bounds describe the unmodified heightmap. Keep the small
    // clearing at full detail so its new profile never buries the room at coarse LOD.
    if (node.Level<mMaxDepth && (!EnableLOD || TouchesSceneClearing(node) || pixelError>PixelError))
    {
        for (int child:node.Children) Visit(static_cast<unsigned>(child));
    }
    else
    {
        mSelected.push_back(index);
        ++mLevels[node.Level];
    }
}

void TerrainSystem::Draw(ID3D12GraphicsCommandList* cmd,CXMMATRIX viewProjection)
{
    cmd->SetPipelineState(DebugMode==2?mWirePSO.Get():mPSO.Get());
    cmd->SetGraphicsRootSignature(mRoot.Get());
    ID3D12DescriptorHeap* heaps[]={mHeap.Get()};
    cmd->SetDescriptorHeaps(1,heaps);
    cmd->SetGraphicsRootDescriptorTable(0,mHeap->GetGPUDescriptorHandleForHeapStart());
    cmd->SetGraphicsRootDescriptorTable(2,mPaintSRV);
    cmd->IASetVertexBuffers(0,1,&mVBView); cmd->IASetIndexBuffer(&mIBView);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    DrawConstants cb;
    XMStoreFloat4x4(&cb.VP,XMMatrixTranspose(viewProjection));
    const auto color=PaintColor();
    cb.Brush=XMFLOAT4(mBrushPosition.x,mBrushPosition.z,BrushRadius,BrushEnabled && mBrushHit?1.0f:0.0f);
    cb.BrushColor=mBrushErase?XMFLOAT4(1,1,1,1):XMFLOAT4(color.x,color.y,color.z,1);
    cb.PaintMap=XMFLOAT4(1.0f/mWorldSize,PaintVisible?1.0f:0.0f,0,0);
    for (unsigned index:mSelected)
    {
        const auto& n=mNodes[index];
        cb.Tile=XMFLOAT4(n.X,n.Z,n.Size,static_cast<float>(index));
        cb.Options=XMFLOAT4(mHeightScale,n.SkirtDepth,static_cast<float>(n.Level),static_cast<float>(DebugMode));
        cmd->SetGraphicsRoot32BitConstants(1,sizeof(cb)/4,&cb,0);
        cmd->DrawIndexedInstanced(EnableSkirts?mIndexCount:mSurfaceIndexCount,1,0,0,0);
    }
}

std::wstring TerrainSystem::Status() const
{
    std::wostringstream s;
    const wchar_t* names[]={L"Red",L"Blue",L"Green",L"Yellow",L"White",L"Black"};
    s<<L"Brush "<<(BrushEnabled?L"ON":L"OFF")<<L" [P] "<<names[BrushColor%6]<<L" [1-6] R="<<int(BrushRadius)
        <<L"m [[]] | LMB paint / Ctrl erase / RMB look | F2 save F3 load | "
        <<(PaintVisible?L"Paint ON":L"Paint OFF")<<L" [F4] | ";
    if(!mPaintMessage.empty()) s<<mPaintMessage<<L" | ";
    s<<L"TERRAIN [F7] | LOD "<<(EnableLOD?L"ON":L"FULL")<<L" [F8] | Cull "<<(EnableCulling?L"ON":L"OFF")
        <<L" [F9] | Tiles/Draws "<<mSelected.size()<<L" | Tris "<<mSelected.size()*(EnableSkirts?mIndexCount:mSurfaceIndexCount)/3
        <<L" | Nodes "<<mVisited<<L" | Rejected "<<mRejected<<L" | HiddenLeaves "<<mHiddenLeaves
        <<L" | SSE "<<static_cast<int>(PixelError)<<L"px [-/+] | View [F11] | Debug [F10] | "
        <<(FreezeSelection?L"FROZEN":L"LIVE")<<L" [F12] | Levels ";
    for(unsigned i=0;i<=mMaxDepth;++i) s<<i<<L":"<<mLevels[i]<<L" ";
    return s.str();
}

void TerrainSystem::RunSelectionChecks()
{
    std::ofstream log("terrain_selection_checks.txt");
    auto require=[](bool test,const char* reason){if(!test) throw std::runtime_error(reason);};
    auto projection=XMMatrixPerspectiveFovLH(XM_PIDIV4,16.0f/9.0f,0.5f,12000.0f);
    auto view=XMMatrixLookAtLH(XMVectorSet(0,500,-1800,1),XMVectorSet(0,100,0,1),XMVectorSet(0,1,0,0));
    EnableCulling=false; EnableLOD=false; FreezeSelection=false;
    Select(view,projection,720);
    const size_t full=mSelected.size();
    require(full==(size_t{1}<<(2*mMaxDepth)),"Full detail must visit all leaf tiles");
    log<<"Full detail, culling off: "<<full<<" tiles\n";
    EnableLOD=true; Select(view,projection,720);
    require(mSelected.size()<full,"LOD must reduce draw count at overview distance");
    log<<"Adaptive LOD, culling off: "<<mSelected.size()<<" tiles\n";
    // Rasterize selected node footprints into leaf cells: no gaps, no ancestor overlap.
    const unsigned side=1u<<mMaxDepth;
    std::vector<unsigned> coverage(side*side,0);
    for(unsigned index:mSelected)
    {
        const auto& n=mNodes[index];
        require(!TouchesSceneClearing(n) || n.Level==mMaxDepth,
            "Central clearing must retain full detail around Breakfast Room");
        unsigned x=static_cast<unsigned>(std::lround((n.X/mWorldSize+0.5f)*side));
        unsigned z=static_cast<unsigned>(std::lround((n.Z/mWorldSize+0.5f)*side));
        unsigned span=1u<<(mMaxDepth-n.Level);
        for(unsigned j=z;j<z+span;++j) for(unsigned i=x;i<x+span;++i) ++coverage[j*side+i];
    }
    for(unsigned count:coverage) require(count==1,"LOD cover has a gap or parent-child overlap");
    log<<"Adaptive cover: each leaf cell covered exactly once\n";
    for(unsigned index=0;index<mNodes.size();++index)
    {
        const auto& n=mNodes[index];
        const auto box=Bounds(n);
        for(unsigned z=0;z<=mGridCells;++z) for(unsigned x=0;x<=mGridCells;++x)
        {
            float h=mHeightCPU[index*mTileSize*mTileSize+(z+1)*mTileSize+x+1]/65535.0f*mHeightScale;
            h=SceneHeight(n.X+x*n.Size/mGridCells,n.Z+z*n.Size/mGridCells,h);
            require(h-n.SkirtDepth>=box.Center.y-box.Extents.y-0.01f &&
                h<=box.Center.y+box.Extents.y+0.01f,"Clearing exceeds culling bounds");
        }
    }
    log<<"Central clearing: full detail and conservative height bounds PASS\n";
    const size_t overview=mSelected.size();
    auto distant=XMMatrixLookAtLH(XMVectorSet(0,1500,-6000,1),XMVectorSet(0,100,0,1),XMVectorSet(0,1,0,0));
    Select(distant,projection,720);
    require(mSelected.size()<overview,"Distant camera must reduce LOD detail");
    log<<"Distant adaptive view: "<<mSelected.size()<<" tiles\n";
    const float savedError=PixelError;
    PixelError=40; Select(view,projection,720);
    require(mSelected.size()<overview,"Larger error tolerance must reduce detail");
    log<<"Relaxed error tolerance: "<<mSelected.size()<<" tiles\n";
    PixelError=savedError;
    EnableLOD=false; EnableCulling=true; Select(view,projection,720);
    require(mSelected.size()<=full,"Culling increased full-detail count");
    auto away=XMMatrixLookAtLH(XMVectorSet(0,500,-1800,1),XMVectorSet(0,500,-3000,1),XMVectorSet(0,1,0,0));
    Select(away,projection,720);
    require(mSelected.empty(),"Away-facing frustum should reject all terrain");
    log<<"Away-facing camera: zero tiles\n";
    EnableLOD=true; Select(view,projection,720);
    auto saved=mSelected; FreezeSelection=true; Select(away,projection,720);
    require(saved==mSelected,"Frozen selection changed");
    FreezeSelection=false; Select(away,projection,720);
    require(mSelected.empty(),"Unfreezing did not restore live culling");
    log<<"Freeze/unfreeze: PASS\nAll selection checks passed\n";
    mSelectionValid=false;
}
