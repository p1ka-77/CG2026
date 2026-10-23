#include "TerrainSystem.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    struct PaintHeader
    {
        char Magic[4];
        uint32_t Version, Size, TerrainFingerprint;
        float WorldSize;
    };
    static_assert(sizeof(PaintHeader)==20,"Paint file layout");
    constexpr size_t HistoryLimit=8;
    float Clamp01(float x) {return (std::max)(0.0f,(std::min)(1.0f,x));}
}

XMFLOAT3 TerrainSystem::PaintColor() const
{
    static const XMFLOAT3 colors[]={
        {0.85f,0.035f,0.025f},{0.025f,0.16f,0.95f},{0.035f,0.65f,0.08f},
        {0.95f,0.65f,0.02f},{0.9f,0.9f,0.9f},{0.012f,0.012f,0.012f}};
    return colors[BrushColor%6];
}

void TerrainSystem::InitializePaint(ID3D12Device* device,ID3D12GraphicsCommandList* cmd,unsigned frameCount)
{
    if(frameCount==0) throw std::runtime_error("Painting requires frame upload buffers");
    mPaintCPU.assign(PaintSize*PaintSize*4,0);
    if(GetFileAttributesA("Assets/Terrain/terrain_paint.bin")!=INVALID_FILE_ATTRIBUTES)
        LoadPaint();
    mPaintUndo.clear(); mPaintRedo.clear();
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,PaintSize,PaintSize,1,1);
    ThrowIfFailed(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&mPaintTexture)));
    device->GetCopyableFootprints(&desc,0,1,0,&mPaintLayout,nullptr,nullptr,&mPaintUploadSize);
    auto uploadHeap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc=CD3DX12_RESOURCE_DESC::Buffer(mPaintUploadSize);
    mPaintUploads.resize(frameCount);
    for(auto& buffer:mPaintUploads)
        ThrowIfFailed(device->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&buffer)));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv={};
    srv.Format=desc.Format; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels=1;
    auto handle=mHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CreateShaderResourceView(mPaintTexture.Get(),&srv,handle);
    mPaintSRV=mHeap->GetGPUDescriptorHandleForHeapStart();
    mPaintSRV.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UploadPaint(cmd,0);
}

void TerrainSystem::UploadPaint(ID3D12GraphicsCommandList* cmd,unsigned frameIndex)
{
    if(mPaintRevision==mPaintGPURevision) return;
    if(frameIndex>=mPaintUploads.size()) throw std::runtime_error("Invalid paint frame index");
    // Caller has waited on this frame resource's fence, so its upload buffer is no longer in use.
    auto* upload=mPaintUploads[frameIndex].Get();
    uint8_t* mapped=nullptr;
    D3D12_RANGE noRead={0,0};
    ThrowIfFailed(upload->Map(0,&noRead,reinterpret_cast<void**>(&mapped)));
    for(unsigned y=0;y<PaintSize;++y)
        std::memcpy(mapped+mPaintLayout.Offset+y*mPaintLayout.Footprint.RowPitch,
            mPaintCPU.data()+y*PaintSize*4,PaintSize*4);
    D3D12_RANGE written={0,static_cast<SIZE_T>(mPaintUploadSize)};
    upload->Unmap(0,&written);
    auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(mPaintTexture.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->ResourceBarrier(1,&barrier);
    CD3DX12_TEXTURE_COPY_LOCATION dst(mPaintTexture.Get(),0),src(upload,mPaintLayout);
    cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    barrier=CD3DX12_RESOURCE_BARRIER::Transition(mPaintTexture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1,&barrier);
    mPaintGPURevision=mPaintRevision;
}

bool TerrainSystem::PickSurface(FXMVECTOR origin,FXMVECTOR direction,XMFLOAT3& hit) const
{
    float nearest=(std::numeric_limits<float>::max)();
    bool found=false;
    // Test only selected, actually rendered top surfaces (not artificial skirts).
    for(unsigned index:mSelected)
    {
        const auto& node=mNodes[index];
        float entry;
        if(!Bounds(node).Intersects(origin,direction,entry) || entry>nearest) continue;
        auto vertex=[&](unsigned x,unsigned z)
        {
            float h=mHeightCPU[index*mTileSize*mTileSize+(z+1)*mTileSize+x+1]/65535.0f*mHeightScale;
            float wx=node.X+x*node.Size/mGridCells,wz=node.Z+z*node.Size/mGridCells;
            return XMVectorSet(wx,SceneHeight(wx,wz,h),wz,1);
        };
        for(unsigned z=0;z<mGridCells;++z) for(unsigned x=0;x<mGridCells;++x)
        {
            auto a=vertex(x,z),b=vertex(x+1,z),c=vertex(x,z+1),d=vertex(x+1,z+1);
            float distance;
            if(TriangleTests::Intersects(origin,direction,a,c,d,distance) && distance<nearest)
                {nearest=distance; found=true;}
            if(TriangleTests::Intersects(origin,direction,a,d,b,distance) && distance<nearest)
                {nearest=distance; found=true;}
        }
    }
    if(found) XMStoreFloat3(&hit,XMVectorMultiplyAdd(direction,XMVectorReplicate(nearest),origin));
    return found;
}

void TerrainSystem::UpdateBrush(CXMMATRIX view,CXMMATRIX projection,int width,int height,
    int mouseX,int mouseY,bool paint,bool erase,float dt)
{
    mBrushHit=false; mBrushErase=erase;
    if(!BrushEnabled || width<=0 || height<=0 || mouseX<0 || mouseY<0 || mouseX>=width || mouseY>=height)
        {EndStroke(); return;}
    auto nearPoint=XMVector3Unproject(XMVectorSet(float(mouseX),float(mouseY),0,1),
        0,0,float(width),float(height),0,1,projection,view,XMMatrixIdentity());
    auto farPoint=XMVector3Unproject(XMVectorSet(float(mouseX),float(mouseY),1,1),
        0,0,float(width),float(height),0,1,projection,view,XMMatrixIdentity());
    mBrushHit=PickSurface(nearPoint,XMVector3Normalize(farPoint-nearPoint),mBrushPosition);
    if(paint && mBrushHit)
    {
        DebugMode=0; PaintVisible=true;
        PaintAt(mBrushPosition,erase,dt);
    }
    else EndStroke();
}

void TerrainSystem::PushUndo()
{
    if(mPaintUndo.size()==HistoryLimit) mPaintUndo.erase(mPaintUndo.begin());
    mPaintUndo.push_back(mPaintCPU);
    mPaintRedo.clear();
}

void TerrainSystem::EndStroke() {mStrokeActive=false;}

bool TerrainSystem::Stamp(float x,float z,bool erase,float opacity)
{
    const float scale=PaintSize/mWorldSize;
    const float cx=(x/mWorldSize+0.5f)*PaintSize-0.5f;
    const float cz=(z/mWorldSize+0.5f)*PaintSize-0.5f;
    const float radius=(std::max)(1.0f,BrushRadius*scale);
    int x0=(std::max)(0,int(std::floor(cx-radius))),x1=(std::min)(int(PaintSize)-1,int(std::ceil(cx+radius)));
    int z0=(std::max)(0,int(std::floor(cz-radius))),z1=(std::min)(int(PaintSize)-1,int(std::ceil(cz+radius)));
    const auto color=PaintColor(); const float rgb[]={color.x,color.y,color.z};
    bool changed=false;
    for(int j=z0;j<=z1;++j) for(int i=x0;i<=x1;++i)
    {
        float dx=i-cx,dz=j-cz;
        float distance=std::sqrt(dx*dx+dz*dz)/radius;
        if(distance>=1) continue;
        float edge=Clamp01((1-distance)/0.45f);
        float amount=opacity*edge*edge*(3-2*edge);
        auto* pixel=mPaintCPU.data()+(j*PaintSize+i)*4;
        for(unsigned c=0;c<4;++c)
        {
            float target=erase?0.0f:(c==3?1.0f:rgb[c]);
            uint8_t value=static_cast<uint8_t>(std::lround(Clamp01(pixel[c]/255.0f*(1-amount)+target*amount)*255));
            changed|=value!=pixel[c]; pixel[c]=value;
        }
    }
    return changed;
}

void TerrainSystem::PaintAt(const XMFLOAT3& hit,bool erase,float dt)
{
    bool first=!mStrokeActive;
    if(first) {PushUndo(); mLastPaintPosition=hit; mStrokeActive=true;}
    float dx=hit.x-mLastPaintPosition.x,dz=hit.z-mLastPaintPosition.z;
    unsigned steps=(std::max)(1u,static_cast<unsigned>(std::ceil(std::sqrt(dx*dx+dz*dz)/(BrushRadius*0.18f))));
    steps=(std::min)(steps,512u);
    float opacity=first?0.65f:1.0f-std::exp(-18.0f*(std::min)((std::max)(dt,0.0f),0.1f));
    bool changed=false;
    for(unsigned i=1;i<=steps;++i)
    {
        float t=float(i)/steps;
        changed|=Stamp(mLastPaintPosition.x+dx*t,mLastPaintPosition.z+dz*t,erase,opacity);
    }
    if(changed) {++mPaintRevision; mPaintMessage=L"Unsaved";}
    mLastPaintPosition=hit;
}

void TerrainSystem::UndoPaint()
{
    EndStroke();
    if(mPaintUndo.empty()) {mPaintMessage=L"Nothing to undo"; return;}
    mPaintRedo.push_back(std::move(mPaintCPU));
    mPaintCPU=std::move(mPaintUndo.back()); mPaintUndo.pop_back();
    ++mPaintRevision; mPaintMessage=L"Undo (unsaved)";
}

void TerrainSystem::RedoPaint()
{
    EndStroke();
    if(mPaintRedo.empty()) {mPaintMessage=L"Nothing to redo"; return;}
    mPaintUndo.push_back(std::move(mPaintCPU));
    mPaintCPU=std::move(mPaintRedo.back()); mPaintRedo.pop_back();
    ++mPaintRevision; mPaintMessage=L"Redo (unsaved)";
}

bool TerrainSystem::SavePaint(const std::string& path)
{
    EndStroke();
    const PaintHeader header={{'T','P','T','1'},1,PaintSize,mTerrainFingerprint,mWorldSize};
    const std::string temporary=path+".tmp";
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&header),sizeof(header));
    out.write(reinterpret_cast<const char*>(mPaintCPU.data()),mPaintCPU.size());
    out.close();
    if(!out || !MoveFileExA(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        {mPaintMessage=L"SAVE FAILED (check folder permissions)"; return false;}
    mPaintMessage=L"Saved"; return true;
}

bool TerrainSystem::LoadPaint(const std::string& path)
{
    EndStroke();
    std::ifstream in(path,std::ios::binary);
    PaintHeader header={};
    if(!in) {mPaintMessage=L"No saved paint"; return false;}
    if(!in.read(reinterpret_cast<char*>(&header),sizeof(header)) || std::memcmp(header.Magic,"TPT1",4) ||
        header.Version!=1 || header.Size!=PaintSize || header.TerrainFingerprint!=mTerrainFingerprint || header.WorldSize!=mWorldSize)
        {mPaintMessage=L"LOAD FAILED: incompatible paint file"; return false;}
    std::vector<uint8_t> pixels(PaintSize*PaintSize*4);
    if(!in.read(reinterpret_cast<char*>(pixels.data()),pixels.size()) || in.peek()!=std::char_traits<char>::eof())
        {mPaintMessage=L"LOAD FAILED: invalid file size"; return false;}
    for(size_t i=0;i<pixels.size();i+=4)
        if(pixels[i]>pixels[i+3] || pixels[i+1]>pixels[i+3] || pixels[i+2]>pixels[i+3])
            {mPaintMessage=L"LOAD FAILED: invalid premultiplied color"; return false;}
    PushUndo(); mPaintCPU=std::move(pixels); ++mPaintRevision;
    mPaintMessage=L"Loaded"; return true;
}

void TerrainSystem::MakePaintDemo()
{
    // Only diagnostic launches call this. Preserve the on-disk user painting.
    std::fill(mPaintCPU.begin(),mPaintCPU.end(),0);
    BrushRadius=105;
    for(unsigned color=0;color<3;++color)
    {
        BrushColor=color;
        for(int i=0;i<14;++i) Stamp(-560.0f+i*85,-650.0f+color*350.0f+70*std::sin(float(i)*0.5f),false,1);
    }
    ++mPaintRevision; mBrushHit=false;
    mPaintUndo.clear(); mPaintRedo.clear();
}

void TerrainSystem::RunBrushChecks()
{
    auto require=[](bool condition,const char* reason){if(!condition) throw std::runtime_error(reason);};
    std::ofstream log("terrain_brush_checks.txt");
    const auto saved=mPaintCPU;
    std::fill(mPaintCPU.begin(),mPaintCPU.end(),0);
    auto clean=mPaintCPU;
    BrushRadius=65; BrushColor=0;
    PaintAt(XMFLOAT3(0,0,0),false,1.0f/60); EndStroke();
    const size_t center=(PaintSize/2*PaintSize+PaintSize/2)*4;
    require(mPaintCPU[center]>100 && mPaintCPU[center+3]>100,"Brush center not painted");
    require(mPaintCPU[3]==0,"Brush leaked outside radius");
    const auto painted=mPaintCPU;
    UndoPaint(); require(mPaintCPU==clean,"Undo failed");
    RedoPaint(); require(mPaintCPU==painted,"Redo failed");
    PaintAt(XMFLOAT3(0,0,0),true,1.0f/60); EndStroke();
    require(mPaintCPU[center+3]<painted[center+3],"Eraser did not reduce coverage");
    UndoPaint(); require(mPaintCPU==painted,"Eraser undo failed");
    log<<"Paint, radius clipping, erase, undo and redo: PASS\n";
    auto projection=XMMatrixPerspectiveFovLH(XM_PIDIV4,16.0f/9.0f,0.5f,12000);
    auto view=XMMatrixLookAtLH(XMVectorSet(0,500,-1800,1),XMVectorSet(0,100,0,1),XMVectorSet(0,1,0,0));
    EnableCulling=false; EnableLOD=true; FreezeSelection=false; Select(view,projection,720);
    XMFLOAT3 hit;
    require(PickSurface(XMVectorSet(0,800,0,1),XMVectorSet(0,-1,0,0),hit),"Top-down pick missed");
    require(std::abs(hit.y+2.0f)<0.01f,"Picking does not match the central scene clearing");
    require(SceneHeight(200,0,123)==123,"Clearing changed terrain outside its radius");
    require(!PickSurface(XMVectorSet(0,800,0,1),XMVectorSet(0,1,0,0),hit),"Sky ray hit terrain");
    require(mPaintCPU==painted,"LOD selection modified paint");
    // Project a known surface point and check the actual screen-space picking path.
    PickSurface(XMVectorSet(0,800,0,1),XMVectorSet(0,-1,0,0),hit);
    auto eye=XMVectorSet(0,800,0,1);
    auto topView=XMMatrixLookAtLH(eye,XMLoadFloat3(&hit),XMVectorSet(0,0,1,0));
    Select(topView,projection,720);
    UpdateBrush(topView,projection,1280,720,640,360,false,false,1.0f/60);
    require(mBrushHit && std::abs(mBrushPosition.x)<0.1f && std::abs(mBrushPosition.z)<0.1f,"Screen ray is not aligned with cursor");
    UpdateBrush(topView,projection,1280,720,-1,360,true,false,1.0f/60);
    require(!mBrushHit && !mStrokeActive && mPaintCPU==painted,"Outside-window input painted");
    log<<"Ray picking, sky rejection, screen ray, outside input, LOD-independent paint: PASS\n";
    std::string path="terrain_brush_check_"+std::to_string(GetCurrentProcessId())+".bin";
    require(SavePaint(path),"Paint save failed");
    std::fill(mPaintCPU.begin(),mPaintCPU.end(),0);
    require(LoadPaint(path) && mPaintCPU==painted,"Paint save/load roundtrip failed");
    DeleteFileA(path.c_str()); // Only the process-specific fixture created just above.
    log<<"Save/load roundtrip: PASS\nAll brush checks passed\n";
    mPaintCPU=saved; ++mPaintRevision;
    mPaintUndo.clear(); mPaintRedo.clear(); EndStroke(); mBrushHit=false;
}
