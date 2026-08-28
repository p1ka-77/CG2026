//***************************************************************************************
// CrateApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "TextureManager.h"
#include "ObjLoader.h"
#include "TextureAnimation.h"
#include "RenderingSystem.h"
#include "ParticleSystem.h"
#include <cfloat>
#include <algorithm>
#include <DirectXCollision.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;

	int AnimType = 0;

	bool EnableSinusoidalAnimation = false;
	float BaseY = 0.0f;
	float AnimationAmplitude = 0.25f;
	float AnimationAngularSpeed = 1.5f;
	float AnimationPhase = 0.0f;

	bool EnableFrustumCulling = false;
	BoundingBox LocalBounds;

};

struct PaintLight
{
	XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	float    Range = 4.0f;

	XMFLOAT3 Strength = { 2.0f, 0.4f, 0.2f };
	float    Pad = 0.0f;
};

struct OctreeNode
{
	BoundingBox Bounds;

	std::vector<RenderItem*> Objects;

	std::unique_ptr<OctreeNode> Children[8];

	bool IsLeaf() const
	{
		for (int i = 0; i < 8; ++i)
		{
			if (Children[i] != nullptr)
				return false;
		}

		return true;
	}
};

struct TessellationConstants
{
	float MinTessFactor = 1.0f;
	float MaxTessFactor = 8.0f;
	float MinTessDistance = 3.0f;
	float MaxTessDistance = 25.0f;

	float DisplacementScale = 0.25f;

	// Параметры волн
	float WaveAmplitude = 0.08f;   // высота волны
	float WaveFrequency = 2.5f;    // частота волн
	float WaveSpeed = 2.0f;        // скорость движения волн
};

class CrateApp : public D3DApp
{
public:
    CrateApp(HINSTANCE hInstance);
    CrateApp(const CrateApp& rhs) = delete;
    CrateApp& operator=(const CrateApp& rhs) = delete;
    ~CrateApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateSinusoidalAnimations(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateCascadedShadowMaps(const GameTimer& gt);
	void BuildScatterObjects();
	void UpdateVisibleObjects();
	void BuildOctree();
	void InsertObjectToOctree(OctreeNode* node, RenderItem* ri, const BoundingBox& worldBounds, int depth);
	void QueryOctree(OctreeNode* node, const BoundingFrustum& frustum);
	void SubdivideOctreeNode(OctreeNode* node);
	bool BoxContainsBox(const BoundingBox& outer, const BoundingBox& inner);
	void LoadTextures();
    void BuildRootSignature();
	void BuildTessellationRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
	void BuildTessellatedPatchGeometry();
    void BuildPSOs();
	void BuildTessellationPSO();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawShadowCasters(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawTessellatedPatch(ID3D12GraphicsCommandList* cmdList);
	void RebuildDemoLights();
	void FirePaintLight();
	bool RaycastBreakfastRoom(XMFLOAT3& hitPos, XMFLOAT3& hitNormal);
	bool RayTriangleIntersect(
		FXMVECTOR rayOrigin,
		FXMVECTOR rayDir,
		FXMVECTOR v0,
		FXMVECTOR v1,
		FXMVECTOR v2,
		float& t,
		XMFLOAT3& outNormal);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
	void BuildTessellationCB();
	void UpdateTessellationCB(const GameTimer& gt);

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;
	UINT mTessDiffuseSrvIndex = 0;
	UINT mTessNormalSrvIndex = 0;
	UINT mTessDisplacementSrvIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;

	ComPtr<ID3D12RootSignature> mTessRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> mTessPSO = nullptr;
 
	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	RenderItem* mTessPatchRitem = nullptr;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	// Мои поля
	TextureManager  mTexMgr;
	TextureAnimator mUVAnim;
	ObjMesh         mObjMesh;

	RenderingSystem mRenderSys;
	ParticleSystem mParticleSystem;

	ComPtr<ID3D12Resource> mUVAnimCBUpload;
	UVAnimCB* mUVAnimCBMapped = nullptr;
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.3f*XM_PI;
	float mPhi = 0.4f*XM_PI;
	float mRadius = 2.5f;

	// Free-fly camera
	XMFLOAT3 mCameraPos = { 0.0f, 2.0f, -8.0f };

	float mYaw = 0.0f;
	float mPitch = 0.0f;

	float mCameraMoveSpeed = 6.0f;
	float mMouseSensitivity = 0.003f;

    POINT mLastMousePos;

	int mSelectedDemoLight = 1;

	// Внешний направленный свет (солнце) для каскадных теней.
	XMFLOAT3 mDirLightDir = { 0.45f, -0.80f, 0.35f };
	XMFLOAT3 mDirLightStrength = { 2.0f, 1.85f, 1.55f };

	// Point light 1
	XMFLOAT3 mPoint1Pos = { -3.0f, 2.2f, 1.5f };
	float    mPoint1Range = 10.0f;
	XMFLOAT3 mPoint1Strength = { 2.0f, 0.4f, 0.2f };

	// Point light 2
	XMFLOAT3 mPoint2Pos = { 3.0f, 2.2f, -1.5f };
	float    mPoint2Range = 10.0f;
	XMFLOAT3 mPoint2Strength = { 0.2f, 0.5f, 2.0f };

	// Spot light
	XMFLOAT3 mSpotPos = { 0.0f, 4.0f, -4.0f };
	XMFLOAT3 mSpotTarget = { 0.0f, 1.0f, 0.0f };
	float    mSpotRange = 16.0f;
	XMFLOAT3 mSpotStrength = { 2.0f, 1.7f, 0.8f };

	std::vector<PaintLight> mPaintLights;

	XMFLOAT3 mPaintColor = { 2.0f, 0.4f, 0.2f };
	float    mPaintRange = 4.0f;

	bool mFireKeyWasDown = false;

	ComPtr<ID3D12Resource> mTessCBUpload;
	TessellationConstants* mTessCBMapped = nullptr;

	TessellationConstants mTessCB;

	std::vector<RenderItem*> mVisibleRitems;

	bool mEnableFrustumCulling = true;
	bool mCullingToggleKeyPressed = false;

	int mNumVisibleObjects = 0;
	int mNumCulledObjects = 0;

	std::unique_ptr<OctreeNode> mOctreeRoot = nullptr;

	bool mUseOctreeCulling = false;
	bool mOctreeToggleKeyPressed = false;
	bool mAnimationLodDemoMode = true;
	bool mAnimationLodToggleKeyPressed = false;
	bool mShadowsEnabled = true;
	bool mShadowsToggleKeyPressed = false;
	bool mVisualizeShadowCascades = false;
	bool mCascadeDebugToggleKeyPressed = false;
	bool mParticlesEnabled = true;
	bool mParticlesToggleKeyPressed = false;

	std::array<ShadowPassConstants, SHADOW_CASCADE_COUNT> mShadowPassConstants = {};
	std::array<XMFLOAT4X4, SHADOW_CASCADE_COUNT> mShadowTransforms = {};
	XMFLOAT4 mCascadeSplits = {};

	int mNumOctreeNodesVisited = 0;
	int mOctreeMaxDepth = 5;
	int mOctreeMaxObjectsPerNode = 16;
	UINT64 mAnimationFrameIndex = 0;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        CrateApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

CrateApp::CrateApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

CrateApp::~CrateApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool CrateApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Инициализация TextureManager
	mTexMgr.Init(md3dDevice.Get(), mCbvSrvDescriptorSize);
	mTexMgr.CreateSrvHeap(64);

	// Загрузка OBJ сцены
	mObjMesh = ObjLoader::Load("breakfast_room.obj");

	//
	// Старый forward root signature пока оставляем.
	// Он может быть полезен, если захочешь быстро откатиться или сравнить.
	//
	LoadTextures();
	BuildRootSignature();

	//
	// Deferred Rendering System
	//
	mRenderSys.Init(
		md3dDevice.Get(),
		mClientWidth,
		mClientHeight,
		mBackBufferFormat,
		mDepthStencilFormat
	);

	mRenderSys.BuildGeometryRootSignature(md3dDevice.Get());
	mRenderSys.BuildLightingRootSignature(md3dDevice.Get());
	mRenderSys.BuildShadowRootSignature(md3dDevice.Get());

	//
	// Источники света теперь создаются через demo-поля:
	// mPoint1Pos, mPoint1Strength, mSpotPos и т.д.
	//
	RebuildDemoLights();

	//
	// Обычная сборка ресурсов сцены
	//
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildTessellatedPatchGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildScatterObjects();
	BuildOctree();
	BuildFrameResources();

	BuildTessellationRootSignature();

	BuildPSOs();
	BuildTessellationPSO();

	BuildTessellationCB();

	mParticleSystem.Initialize(
		md3dDevice.Get(),
		mCommandList.Get(),
		1024,
		gNumFrameResources,
		mDepthStencilFormat);
	//
	// Fullscreen quad для Lighting Pass.
	// Важно: command list ещё открыт, поэтому CopyResource внутри BuildFullscreenQuad сработает.
	//
	mRenderSys.BuildFullscreenQuad(
		md3dDevice.Get(),
		mCommandList.Get()
	);

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	mTexMgr.ReleaseUploadHeaps();
	mParticleSystem.ReleaseInitializationUpload();

	return true;
}
 
void CrateApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(
		0.25f * MathHelper::Pi,
		AspectRatio(),
		1.0f,
		1000.0f);

	XMStoreFloat4x4(&mProj, P);

	if (md3dDevice != nullptr)
	{
		mRenderSys.OnResize(
			md3dDevice.Get(),
			mClientWidth,
			mClientHeight
		);
	}
}

void CrateApp::Update(const GameTimer& gt)
{
	// Обновить UV анимацию
	//mUVAnim.Update(gt.DeltaTime());
	//UVAnimCB data = mUVAnim.GetCBData("diablo");
	//memcpy(mUVAnimCBMapped, &data, sizeof(UVAnimCB));

	OnKeyboardInput(gt);
	UpdateCamera(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateSinusoidalAnimations(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateCascadedShadowMaps(gt);
	UpdateTessellationCB(gt);
	UpdateVisibleObjects();
}

void CrateApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Сброс allocator'а текущего frame resource.
	ThrowIfFailed(cmdListAlloc->Reset());

	// PSO можно передать nullptr, потому что нужные PSO выставит RenderingSystem.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

	if (mParticlesEnabled)
	{
		mParticleSystem.Update(
			mCommandList.Get(),
			gt.DeltaTime(),
			gt.TotalTime(),
			XMLoadFloat4x4(&mView),
			XMLoadFloat4x4(&mProj),
			mEyePos,
			mCurrFrameResourceIndex);
	}

	//
	// 0. CASCADED SHADOW PASS
	// Каждый срез Texture2DArray получает depth направленного света.
	//
	if (mShadowsEnabled)
	{
		mRenderSys.BeginShadowPass(mCommandList.Get());

		for (UINT cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade)
		{
			mRenderSys.SetShadowCascade(
				mCommandList.Get(),
				cascade,
				mShadowPassConstants[cascade]);

			// Используем все opaque-объекты, а не только camera-visible:
			// объект вне кадра тоже может отбрасывать тень внутрь кадра.
			DrawShadowCasters(mCommandList.Get(), mOpaqueRitems);
		}

		mRenderSys.EndShadowPass(mCommandList.Get());
	}

	//
	// ─────────────────────────────────────────────
	// 1. GEOMETRY PASS
	// Рисуем всю сцену не в back buffer, а в GBuffer.
	// ─────────────────────────────────────────────
	//

	mRenderSys.BeginGeometryPass(
		mCommandList.Get(),
		DepthStencilView(),
		mScreenViewport,
		mScissorRect
	);

	// Depth buffer нужно очистить перед geometry pass.
	mCommandList->ClearDepthStencilView(
		DepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr
	);

	// Для GeometryPass.hlsl нужна куча с обычными текстурами сцены.
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// Geometry root signature использует:
	// slot 0 — texture SRV
	// slot 1 — object CBV
	// slot 2 — pass CBV
	// slot 3 — material CBV
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(
		2,
		passCB->GetGPUVirtualAddress()
	);

	// DrawRenderItems уже выставляет:
	// slot 0 — texture
	// slot 1 — object CB
	// slot 3 — material CB
	DrawRenderItems(mCommandList.Get(), mVisibleRitems);

	// Рисуем tessellated patch в тот же GBuffer.
	DrawTessellatedPatch(mCommandList.Get());

	// Непрозрачные billboards тоже записываются в GBuffer.
	if (mParticlesEnabled)
	{
		mParticleSystem.Draw(mCommandList.Get());
	}

	// Переводим GBuffer из RENDER_TARGET в PIXEL_SHADER_RESOURCE.
	mRenderSys.EndGeometryPass(mCommandList.Get());

	//
	// ─────────────────────────────────────────────
	// 2. LIGHTING PASS
	// Рисуем fullscreen quad в back buffer.
	// LightingPass.hlsl читает GBuffer и считает свет.
	// ─────────────────────────────────────────────
	//

	auto barrierToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);

	mCommandList->ResourceBarrier(1, &barrierToRenderTarget);

	mCommandList->ClearRenderTargetView(
		CurrentBackBufferView(),
		Colors::Black,
		0,
		nullptr
	);

	mRenderSys.LightingPass(
		mCommandList.Get(),
		CurrentBackBufferView(),
		mScreenViewport,
		mScissorRect,
		mEyePos,
		XMFLOAT4(0.05f, 0.05f, 0.05f, 1.0f)
	);

	auto barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	mCommandList->ResourceBarrier(1, &barrierToPresent);

	//
	// ─────────────────────────────────────────────
	// 3. EXECUTE + PRESENT
	// ─────────────────────────────────────────────
	//

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CrateApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void CrateApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void CrateApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = static_cast<float>(x - mLastMousePos.x);
		float dy = static_cast<float>(y - mLastMousePos.y);

		mYaw += dx * mMouseSensitivity;
		mPitch -= dy * mMouseSensitivity;

		float limit = XM_PIDIV2 - 0.05f;

		if (mPitch > limit)
			mPitch = limit;

		if (mPitch < -limit)
			mPitch = -limit;
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}
 
void CrateApp::BuildScatterObjects()
{
	//
	// Добавим много небольших коробок на пол.
	// Если ноутбук слабый — уменьшишь gridX/gridZ.
	//

	if (mGeometries.count("boxGeo") == 0)
		return;

	if (mMaterials.count("woodCrate") == 0)
		return;

	if (mMaterials.count("shadowFloorMat") == 0)
		return;

	// Почти вдвое больше объектов, но на той же площади сцены.
	const int gridX = 28;
	const int gridZ = 28;
	const float spacing = 0.56f;
	const float baseY = 0.55f;

	const float startX = -((gridX - 1) * spacing) * 0.5f;
	const float startZ = -((gridZ - 1) * spacing) * 0.5f;

	UINT nextObjIndex = (UINT)mAllRitems.size();

	// Отдельная матовая платформа непосредственно под ящиками.
	// Верхняя грань находится на Y=0.15 — в нижней точке анимации
	// ящики почти касаются пола, поэтому контактные тени хорошо видны.
	{
		auto floorRitem = std::make_unique<RenderItem>();

		XMMATRIX floorScale = XMMatrixScaling(18.0f, 0.1f, 18.0f);
		XMMATRIX floorTranslation = XMMatrixTranslation(0.0f, 0.10f, 0.0f);
		XMStoreFloat4x4(
			&floorRitem->World,
			floorScale * floorTranslation);
		XMStoreFloat4x4(
			&floorRitem->TexTransform,
			XMMatrixIdentity());

		floorRitem->ObjCBIndex = nextObjIndex++;
		floorRitem->Mat = mMaterials["shadowFloorMat"].get();
		floorRitem->Geo = mGeometries["boxGeo"].get();
		floorRitem->PrimitiveType =
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		auto floorSubmesh = floorRitem->Geo->DrawArgs["box"];
		floorRitem->IndexCount = floorSubmesh.IndexCount;
		floorRitem->StartIndexLocation =
			floorSubmesh.StartIndexLocation;
		floorRitem->BaseVertexLocation =
			floorSubmesh.BaseVertexLocation;
		floorRitem->NumFramesDirty = gNumFrameResources;
		floorRitem->LocalBounds.Center =
			XMFLOAT3(0.0f, 0.0f, 0.0f);
		floorRitem->LocalBounds.Extents =
			XMFLOAT3(0.5f, 0.5f, 0.5f);
		floorRitem->EnableFrustumCulling = true;

		mOpaqueRitems.push_back(floorRitem.get());
		mAllRitems.push_back(std::move(floorRitem));
	}

	for (int z = 0; z < gridZ; ++z)
	{
		for (int x = 0; x < gridX; ++x)
		{
			auto ritem = std::make_unique<RenderItem>();

			float px = startX + x * spacing;
			float pz = startZ + z * spacing;

			// Небольшие коробки на полу.
			XMMATRIX S = XMMatrixScaling(0.3f, 0.3f, 0.3f);
			XMMATRIX T = XMMatrixTranslation(px, baseY, pz);
			XMMATRIX W = S * T;

			XMStoreFloat4x4(&ritem->World, W);
			XMStoreFloat4x4(&ritem->TexTransform, XMMatrixIdentity());

			ritem->ObjCBIndex = nextObjIndex++;
			ritem->Mat = mMaterials["woodCrate"].get();
			ritem->Geo = mGeometries["boxGeo"].get();
			ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

			auto submesh = ritem->Geo->DrawArgs["box"];
			ritem->IndexCount = submesh.IndexCount;
			ritem->StartIndexLocation = submesh.StartIndexLocation;
			ritem->BaseVertexLocation = submesh.BaseVertexLocation;

			ritem->NumFramesDirty = gNumFrameResources;
			ritem->EnableSinusoidalAnimation = true;
			ritem->BaseY = baseY;
			ritem->AnimationAmplitude = 0.25f;
			// Быстрое синхронное движение подчёркивает разную частоту
			// обновления Animation LOD.
			ritem->AnimationAngularSpeed = 6.0f;
			ritem->AnimationPhase = 0.0f;

			// Для unit-box в локальных координатах.
			ritem->LocalBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
			ritem->LocalBounds.Extents = XMFLOAT3(0.5f, 0.5f, 0.5f);

			// Эти коробки будут участвовать во frustum culling.
			ritem->EnableFrustumCulling = true;

			mOpaqueRitems.push_back(ritem.get());
			mAllRitems.push_back(std::move(ritem));
		}
	}
}


void CrateApp::BuildOctree()
{
	std::vector<RenderItem*> cullableObjects;

	for (auto ri : mOpaqueRitems)
	{
		if (ri->EnableFrustumCulling)
		{
			cullableObjects.push_back(ri);
		}
	}

	if (cullableObjects.empty())
		return;

	XMFLOAT3 minP(FLT_MAX, FLT_MAX, FLT_MAX);
	XMFLOAT3 maxP(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	std::vector<BoundingBox> worldBoundsList;
	worldBoundsList.reserve(cullableObjects.size());

	for (auto ri : cullableObjects)
	{
		BoundingBox worldBounds;
		XMMATRIX world = XMLoadFloat4x4(&ri->World);
		ri->LocalBounds.Transform(worldBounds, world);

		worldBoundsList.push_back(worldBounds);

		XMFLOAT3 bMin(
			worldBounds.Center.x - worldBounds.Extents.x,
			worldBounds.Center.y - worldBounds.Extents.y,
			worldBounds.Center.z - worldBounds.Extents.z);

		XMFLOAT3 bMax(
			worldBounds.Center.x + worldBounds.Extents.x,
			worldBounds.Center.y + worldBounds.Extents.y,
			worldBounds.Center.z + worldBounds.Extents.z);

		if (bMin.x < minP.x) minP.x = bMin.x;
		if (bMin.y < minP.y) minP.y = bMin.y;
		if (bMin.z < minP.z) minP.z = bMin.z;

		if (bMax.x > maxP.x) maxP.x = bMax.x;
		if (bMax.y > maxP.y) maxP.y = bMax.y;
		if (bMax.z > maxP.z) maxP.z = bMax.z;
	}

	XMFLOAT3 center(
		(minP.x + maxP.x) * 0.5f,
		(minP.y + maxP.y) * 0.5f,
		(minP.z + maxP.z) * 0.5f);

	XMFLOAT3 extents(
		(maxP.x - minP.x) * 0.5f,
		(maxP.y - minP.y) * 0.5f,
		(maxP.z - minP.z) * 0.5f);

	// Делаем корневой bounding box чуть больше, чтобы объекты не лежали ровно на границе.
	extents.x += 1.0f;
	extents.y += 1.0f;
	extents.z += 1.0f;

	// Лучше сделать root примерно кубическим, чтобы octree был нормальным.
	float maxExtent = extents.x;
	if (extents.y > maxExtent) maxExtent = extents.y;
	if (extents.z > maxExtent) maxExtent = extents.z;

	extents = XMFLOAT3(maxExtent, maxExtent, maxExtent);

	mOctreeRoot = std::make_unique<OctreeNode>();
	mOctreeRoot->Bounds.Center = center;
	mOctreeRoot->Bounds.Extents = extents;

	for (size_t i = 0; i < cullableObjects.size(); ++i)
	{
		InsertObjectToOctree(
			mOctreeRoot.get(),
			cullableObjects[i],
			worldBoundsList[i],
			0);
	}
}

void CrateApp::SubdivideOctreeNode(OctreeNode* node)
{
	XMFLOAT3 c = node->Bounds.Center;
	XMFLOAT3 e = node->Bounds.Extents;

	XMFLOAT3 childExtents(
		e.x * 0.5f,
		e.y * 0.5f,
		e.z * 0.5f);

	int index = 0;

	for (int z = -1; z <= 1; z += 2)
	{
		for (int y = -1; y <= 1; y += 2)
		{
			for (int x = -1; x <= 1; x += 2)
			{
				auto child = std::make_unique<OctreeNode>();

				child->Bounds.Center = XMFLOAT3(
					c.x + x * childExtents.x,
					c.y + y * childExtents.y,
					c.z + z * childExtents.z);

				child->Bounds.Extents = childExtents;

				node->Children[index++] = std::move(child);
			}
		}
	}
}

bool CrateApp::BoxContainsBox(const BoundingBox& outer, const BoundingBox& inner)
{
	float outerMinX = outer.Center.x - outer.Extents.x;
	float outerMinY = outer.Center.y - outer.Extents.y;
	float outerMinZ = outer.Center.z - outer.Extents.z;

	float outerMaxX = outer.Center.x + outer.Extents.x;
	float outerMaxY = outer.Center.y + outer.Extents.y;
	float outerMaxZ = outer.Center.z + outer.Extents.z;

	float innerMinX = inner.Center.x - inner.Extents.x;
	float innerMinY = inner.Center.y - inner.Extents.y;
	float innerMinZ = inner.Center.z - inner.Extents.z;

	float innerMaxX = inner.Center.x + inner.Extents.x;
	float innerMaxY = inner.Center.y + inner.Extents.y;
	float innerMaxZ = inner.Center.z + inner.Extents.z;

	return
		innerMinX >= outerMinX &&
		innerMinY >= outerMinY &&
		innerMinZ >= outerMinZ &&
		innerMaxX <= outerMaxX &&
		innerMaxY <= outerMaxY &&
		innerMaxZ <= outerMaxZ;
}

void CrateApp::InsertObjectToOctree(
	OctreeNode* node,
	RenderItem* ri,
	const BoundingBox& worldBounds,
	int depth)
{
	if (depth >= mOctreeMaxDepth)
	{
		node->Objects.push_back(ri);
		return;
	}

	// Если лист ещё не переполнен — кладём объект сюда.
	if (node->IsLeaf() && (int)node->Objects.size() < mOctreeMaxObjectsPerNode)
	{
		node->Objects.push_back(ri);
		return;
	}

	// Если лист переполнен — делим.
	if (node->IsLeaf())
	{
		std::vector<RenderItem*> oldObjects = node->Objects;
		node->Objects.clear();

		SubdivideOctreeNode(node);

		// Старые объекты у нас уже без worldBounds, поэтому безопаснее оставить их в родителе.
		// Это проще и надёжнее для учебной реализации.
		for (auto oldRi : oldObjects)
		{
			node->Objects.push_back(oldRi);
		}
	}

	// Пытаемся положить новый объект в одного из детей.
	for (int i = 0; i < 8; ++i)
	{
		if (BoxContainsBox(node->Children[i]->Bounds, worldBounds))
		{
			InsertObjectToOctree(
				node->Children[i].get(),
				ri,
				worldBounds,
				depth + 1);

			return;
		}
	}

	// Если объект пересекает границы нескольких детей — оставляем в текущем узле.
	node->Objects.push_back(ri);
}

void CrateApp::QueryOctree(OctreeNode* node, const BoundingFrustum& frustum)
{
	if (node == nullptr)
		return;

	++mNumOctreeNodesVisited;

	auto relation = frustum.Contains(node->Bounds);

	if (relation == DirectX::DISJOINT)
	{
		// Весь узел вне камеры — сразу отбрасываем все его объекты и детей.
		mNumCulledObjects += (int)node->Objects.size();
		return;
	}

	// Объекты, лежащие в этом узле.
	for (auto ri : node->Objects)
	{
		BoundingBox worldBounds;
		XMMATRIX world = XMLoadFloat4x4(&ri->World);
		ri->LocalBounds.Transform(worldBounds, world);

		if (frustum.Contains(worldBounds) != DirectX::DISJOINT)
		{
			mVisibleRitems.push_back(ri);
			++mNumVisibleObjects;
		}
		else
		{
			++mNumCulledObjects;
		}
	}

	for (int i = 0; i < 8; ++i)
	{
		QueryOctree(node->Children[i].get(), frustum);
	}
}

void CrateApp::UpdateVisibleObjects()
{
	mVisibleRitems.clear();

	mNumVisibleObjects = 0;
	mNumCulledObjects = 0;
	mNumOctreeNodesVisited = 0;

	// Если culling выключен — рисуем всё.
	if (!mEnableFrustumCulling)
	{
		for (auto ri : mOpaqueRitems)
			mVisibleRitems.push_back(ri);

		mNumVisibleObjects = (int)mVisibleRitems.size();
		mNumCulledObjects = 0;

		std::wstring title = L"d3d App | Frustum Culling: OFF";
		title += L" | Visible: " + std::to_wstring(mNumVisibleObjects);
		title += L" | Culled: " + std::to_wstring(mNumCulledObjects);
		title += mAnimationLodDemoMode
			? L" | AnimLOD: DEMO 1/8/30 [8]"
			: L" | AnimLOD: NORMAL 1/2/4 [8]";
		title += mShadowsEnabled
			? L" | CSM: ON [9]"
			: L" | CSM: OFF [9]";
		if (mVisualizeShadowCascades)
			title += L" | Cascades: DEBUG [0]";
		title += mParticlesEnabled
			? L" | Particles: ON [4]"
			: L" | Particles: OFF [4]";

		SetWindowText(mhMainWnd, title.c_str());
		return;
	}

	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	BoundingFrustum localFrustum;
	BoundingFrustum::CreateFromMatrix(localFrustum, proj);

	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);

	BoundingFrustum worldFrustum;
	localFrustum.Transform(worldFrustum, invView);

	// Объекты без culling всегда рисуем.
	for (auto ri : mOpaqueRitems)
	{
		if (!ri->EnableFrustumCulling)
		{
			mVisibleRitems.push_back(ri);
			++mNumVisibleObjects;
		}
	}

	if (mUseOctreeCulling && mOctreeRoot != nullptr)
	{
		QueryOctree(mOctreeRoot.get(), worldFrustum);
	}
	else
	{
		for (auto ri : mOpaqueRitems)
		{
			if (!ri->EnableFrustumCulling)
				continue;

			BoundingBox worldBounds;
			XMMATRIX world = XMLoadFloat4x4(&ri->World);
			ri->LocalBounds.Transform(worldBounds, world);

			if (worldFrustum.Contains(worldBounds) != DirectX::DISJOINT)
			{
				mVisibleRitems.push_back(ri);
				++mNumVisibleObjects;
			}
			else
			{
				++mNumCulledObjects;
			}
		}
	}

	std::wstring title = L"d3d App | Frustum Culling: ON";

	title += mUseOctreeCulling
		? L" | Mode: Octree"
		: L" | Mode: Linear";

	title += L" | Visible: " + std::to_wstring(mNumVisibleObjects);
	title += L" | Culled: " + std::to_wstring(mNumCulledObjects);

	if (mUseOctreeCulling)
	{
		title += L" | Nodes: " + std::to_wstring(mNumOctreeNodesVisited);
	}

	title += mAnimationLodDemoMode
		? L" | AnimLOD: DEMO 1/8/30 [8]"
		: L" | AnimLOD: NORMAL 1/2/4 [8]";
	title += mShadowsEnabled
		? L" | CSM: ON [9]"
		: L" | CSM: OFF [9]";
	if (mVisualizeShadowCascades)
		title += L" | Cascades: DEBUG [0]";
	title += mParticlesEnabled
		? L" | Particles: ON [4]"
		: L" | Particles: OFF [4]";

	SetWindowText(mhMainWnd, title.c_str());
}

void CrateApp::OnKeyboardInput(const GameTimer& gt)
{
	const float moveSpeed = 4.0f * gt.DeltaTime();
	const float rangeSpeed = 10.0f * gt.DeltaTime();

	// Выбор источника света.
	if (GetAsyncKeyState('1') & 0x8000)
		mSelectedDemoLight = 1;

	if (GetAsyncKeyState('2') & 0x8000)
		mSelectedDemoLight = 2;

	if (GetAsyncKeyState('3') & 0x8000)
		mSelectedDemoLight = 3;

	XMFLOAT3* selectedPos = nullptr;
	XMFLOAT3* selectedStrength = nullptr;
	float* selectedRange = nullptr;

	if (mSelectedDemoLight == 1)
	{
		selectedPos = &mPoint1Pos;
		selectedStrength = &mPoint1Strength;
		selectedRange = &mPoint1Range;
	}
	else if (mSelectedDemoLight == 2)
	{
		selectedPos = &mPoint2Pos;
		selectedStrength = &mPoint2Strength;
		selectedRange = &mPoint2Range;
	}
	else if (mSelectedDemoLight == 3)
	{
		selectedPos = &mSpotPos;
		selectedStrength = &mSpotStrength;
		selectedRange = &mSpotRange;
	}

	float dt = gt.DeltaTime();

	float cameraMoveSpeed = mCameraMoveSpeed * dt;

	if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
		cameraMoveSpeed *= 3.0f;

	if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
		cameraMoveSpeed *= 0.35f;

	float cosPitch = cosf(mPitch);
	float sinPitch = sinf(mPitch);
	float cosYaw = cosf(mYaw);
	float sinYaw = sinf(mYaw);

	XMVECTOR forward = XMVector3Normalize(XMVectorSet(
		sinYaw * cosPitch,
		sinPitch,
		cosYaw * cosPitch,
		0.0f
	));

	XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));

	XMVECTOR pos = XMLoadFloat3(&mCameraPos);

	if (GetAsyncKeyState('W') & 0x8000)
		pos = XMVectorAdd(pos, XMVectorScale(forward, moveSpeed));

	if (GetAsyncKeyState('S') & 0x8000)
		pos = XMVectorSubtract(pos, XMVectorScale(forward, moveSpeed));

	if (GetAsyncKeyState('D') & 0x8000)
		pos = XMVectorAdd(pos, XMVectorScale(right, moveSpeed));

	if (GetAsyncKeyState('A') & 0x8000)
		pos = XMVectorSubtract(pos, XMVectorScale(right, moveSpeed));

	if (GetAsyncKeyState('E') & 0x8000)
		pos = XMVectorAdd(pos, XMVectorScale(worldUp, moveSpeed));

	if (GetAsyncKeyState('Q') & 0x8000)
		pos = XMVectorSubtract(pos, XMVectorScale(worldUp, moveSpeed));

	XMStoreFloat3(&mCameraPos, pos);

	if (selectedStrength != nullptr)
	{
		// Быстрая смена цвета.
		if (GetAsyncKeyState('Z') & 0x8000)
			*selectedStrength = XMFLOAT3(2.0f, 0.4f, 0.2f); // красно-оранжевый

		if (GetAsyncKeyState('X') & 0x8000)
			*selectedStrength = XMFLOAT3(0.2f, 0.5f, 2.0f); // синий

		if (GetAsyncKeyState('C') & 0x8000)
			*selectedStrength = XMFLOAT3(0.3f, 2.0f, 0.4f); // зелёный

		if (GetAsyncKeyState('V') & 0x8000)
			*selectedStrength = XMFLOAT3(2.0f, 1.7f, 0.8f); // жёлто-белый
	}

	if (selectedRange != nullptr)
	{
		if (GetAsyncKeyState('O') & 0x8000)
			*selectedRange -= rangeSpeed;

		if (GetAsyncKeyState('P') & 0x8000)
			*selectedRange += rangeSpeed;

		if (*selectedRange < 1.0f)
			*selectedRange = 1.0f;
	}

	// Цвет будущего "выстрела".
	if (GetAsyncKeyState('Z') & 0x8000)
		mPaintColor = XMFLOAT3(2.0f, 0.4f, 0.2f); // красно-оранжевый

	if (GetAsyncKeyState('X') & 0x8000)
		mPaintColor = XMFLOAT3(0.2f, 0.5f, 2.0f); // синий

	if (GetAsyncKeyState('C') & 0x8000)
		mPaintColor = XMFLOAT3(0.3f, 2.0f, 0.4f); // зелёный

	if (GetAsyncKeyState('V') & 0x8000)
		mPaintColor = XMFLOAT3(2.0f, 1.7f, 0.8f); // жёлто-белый

	// Радиус будущего пятна.
	if (GetAsyncKeyState('O') & 0x8000)
		mPaintRange -= rangeSpeed;

	if (GetAsyncKeyState('P') & 0x8000)
		mPaintRange += rangeSpeed;

	if (mPaintRange < 1.0f)
		mPaintRange = 1.0f;

	if (mPaintRange > 20.0f)
		mPaintRange = 20.0f;

	// 6 — включить/выключить frustum culling.
	bool key6Down = (GetAsyncKeyState('6') & 0x8000) != 0;

	if (key6Down && !mCullingToggleKeyPressed)
	{
		mEnableFrustumCulling = !mEnableFrustumCulling;
	}

	mCullingToggleKeyPressed = key6Down;


	// 7 — переключить режим culling: Linear / Octree.
	bool key7Down = (GetAsyncKeyState('7') & 0x8000) != 0;

	if (key7Down && !mOctreeToggleKeyPressed)
	{
		mUseOctreeCulling = !mUseOctreeCulling;
	}

	mOctreeToggleKeyPressed = key7Down;

	// 8 — обычный Animation LOD или усиленный режим для демонстрации.
	bool key8Down = (GetAsyncKeyState('8') & 0x8000) != 0;

	if (key8Down && !mAnimationLodToggleKeyPressed)
	{
		mAnimationLodDemoMode = !mAnimationLodDemoMode;
	}

	mAnimationLodToggleKeyPressed = key8Down;

	// 9 — включить/выключить каскадные тени.
	bool key9Down = (GetAsyncKeyState('9') & 0x8000) != 0;

	if (key9Down && !mShadowsToggleKeyPressed)
	{
		mShadowsEnabled = !mShadowsEnabled;
	}

	mShadowsToggleKeyPressed = key9Down;

	// 0 — цветная визуализация границ каскадов.
	bool key0Down = (GetAsyncKeyState('0') & 0x8000) != 0;

	if (key0Down && !mCascadeDebugToggleKeyPressed)
	{
		mVisualizeShadowCascades = !mVisualizeShadowCascades;
	}

	mCascadeDebugToggleKeyPressed = key0Down;

	// 4 — включить/выключить GPU particle system.
	bool key4Down = (GetAsyncKeyState('4') & 0x8000) != 0;

	if (key4Down && !mParticlesToggleKeyPressed)
	{
		mParticlesEnabled = !mParticlesEnabled;
	}

	mParticlesToggleKeyPressed = key4Down;

	// После изменения параметров пересобираем список источников света.
	RebuildDemoLights();
}
 

void CrateApp::RebuildDemoLights()
{
	mRenderSys.ClearLights();

	mRenderSys.AddDirectionalLight(
		mDirLightDir,
		mDirLightStrength
	);

	mRenderSys.AddPointLight(
		mPoint1Pos,
		mPoint1Range,
		mPoint1Strength
	);

	mRenderSys.AddPointLight(
		mPoint2Pos,
		mPoint2Range,
		mPoint2Strength
	);

	XMVECTOR spotPos = XMLoadFloat3(&mSpotPos);
	XMVECTOR spotTarget = XMLoadFloat3(&mSpotTarget);
	XMVECTOR spotDirV = XMVector3Normalize(XMVectorSubtract(spotTarget, spotPos));

	XMFLOAT3 spotDir;
	XMStoreFloat3(&spotDir, spotDirV);

	mRenderSys.AddSpotLight(
		mSpotPos,
		spotDir,
		mSpotRange,
		XM_PIDIV4,
		8.0f,
		mSpotStrength
	);

	// Постоянные цветные пятна, которые ты "выстрелил" из камеры.
	for (const auto& p : mPaintLights)
	{
		mRenderSys.AddPointLight(
			p.Position,
			p.Range,
			p.Strength
		);
	}

}

void CrateApp::FirePaintLight()
{
	XMFLOAT3 hitPos;
	XMFLOAT3 hitNormal;

	if (!RaycastBreakfastRoom(hitPos, hitNormal))
		return;

	XMVECTOR pos = XMLoadFloat3(&hitPos);
	XMVECTOR normal = XMLoadFloat3(&hitNormal);

	// Чуть выносим свет от поверхности, чтобы он не оказался прямо внутри стены.
	pos = XMVectorAdd(pos, XMVectorScale(normal, 0.08f));

	PaintLight p;
	XMStoreFloat3(&p.Position, pos);
	p.Range = mPaintRange;
	p.Strength = mPaintColor;

	mPaintLights.push_back(p);

	// Чтобы не превысить MAX_LIGHTS = 16.
	// У нас примерно 4 базовых источника, оставим 10 пятен.
	const size_t maxPaintLights = 10;

	if (mPaintLights.size() > maxPaintLights)
	{
		mPaintLights.erase(mPaintLights.begin());
	}

	RebuildDemoLights();
}

void CrateApp::BuildTessellationCB()
{
	UINT64 cbSize = d3dUtil::CalcConstantBufferByteSize(sizeof(TessellationConstants));

	D3D12_HEAP_PROPERTIES uploadHeap = {};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	uploadHeap.CreationNodeMask = 1;
	uploadHeap.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Alignment = 0;
	bufferDesc.Width = cbSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.SampleDesc.Quality = 0;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mTessCBUpload)));

	ThrowIfFailed(mTessCBUpload->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&mTessCBMapped)));

	memcpy(mTessCBMapped, &mTessCB, sizeof(TessellationConstants));
}

void CrateApp::BuildTessellationRootSignature()
{
	//
	// Tessellation pass root signature:
	//
	// slot 0: SRV table t0-t2
	//         t0 = diffuse
	//         t1 = normal map
	//         t2 = displacement map
	//
	// slot 1: Object CBV b0
	// slot 2: Pass CBV b1
	// slot 3: Material CBV b2
	// slot 4: Tessellation CBV b3
	//

	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	slotRootParameter[0].InitAsDescriptorTable(
		1,
		&texTable,
		D3D12_SHADER_VISIBILITY_ALL);

	slotRootParameter[1].InitAsConstantBufferView(0); // b0 Object
	slotRootParameter[2].InitAsConstantBufferView(1); // b1 Pass
	slotRootParameter[3].InitAsConstantBufferView(2); // b2 Material
	slotRootParameter[4].InitAsConstantBufferView(3); // b3 Tessellation

	CD3DX12_STATIC_SAMPLER_DESC sampler(
		0,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		5,
		slotRootParameter,
		1,
		&sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mTessRootSignature.GetAddressOf())));
}

void CrateApp::BuildTessellationPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC tessPsoDesc;
	ZeroMemory(&tessPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	tessPsoDesc.InputLayout = {
		mInputLayout.data(),
		(UINT)mInputLayout.size()
	};

	tessPsoDesc.pRootSignature = mTessRootSignature.Get();

	tessPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["tessVS"]->GetBufferPointer()),
		mShaders["tessVS"]->GetBufferSize()
	};

	tessPsoDesc.HS =
	{
		reinterpret_cast<BYTE*>(mShaders["tessHS"]->GetBufferPointer()),
		mShaders["tessHS"]->GetBufferSize()
	};

	tessPsoDesc.DS =
	{
		reinterpret_cast<BYTE*>(mShaders["tessDS"]->GetBufferPointer()),
		mShaders["tessDS"]->GetBufferSize()
	};

	tessPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["tessPS"]->GetBufferPointer()),
		mShaders["tessPS"]->GetBufferSize()
	};

	tessPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	tessPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	tessPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	tessPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	tessPsoDesc.SampleMask = UINT_MAX;

	// Важно: для tessellation нужен PATCH, не TRIANGLE.
	tessPsoDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

	// Пишем в тот же GBuffer, что и обычный Geometry Pass.
	tessPsoDesc.NumRenderTargets = GBUFFER_COUNT;

	for (UINT i = 0; i < GBUFFER_COUNT; ++i)
	{
		tessPsoDesc.RTVFormats[i] = GBUFFER_FORMATS[i];
	}

	tessPsoDesc.DSVFormat = mDepthStencilFormat;

	// GBuffer у нас создаётся без MSAA, поэтому Count = 1.
	tessPsoDesc.SampleDesc.Count = 1;
	tessPsoDesc.SampleDesc.Quality = 0;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&tessPsoDesc,
		IID_PPV_ARGS(&mTessPSO)));
}

void CrateApp::UpdateTessellationCB(const GameTimer& gt)
{
	// Клавиши для демонстрации.
	// I / K — увеличить / уменьшить силу displacement.
	if (GetAsyncKeyState('I') & 0x8000)
		mTessCB.DisplacementScale += 0.5f * gt.DeltaTime();

	if (GetAsyncKeyState('K') & 0x8000)
		mTessCB.DisplacementScale -= 0.5f * gt.DeltaTime();

	if (mTessCB.DisplacementScale < 0.0f)
		mTessCB.DisplacementScale = 0.0f;

	if (mTessCB.DisplacementScale > 2.0f)
		mTessCB.DisplacementScale = 2.0f;

	// U / J — увеличить / уменьшить максимальный уровень тесселяции.
	if (GetAsyncKeyState('U') & 0x8000)
		mTessCB.MaxTessFactor += 8.0f * gt.DeltaTime();

	if (GetAsyncKeyState('J') & 0x8000)
		mTessCB.MaxTessFactor -= 8.0f * gt.DeltaTime();

	if (mTessCB.MaxTessFactor < 1.0f)
		mTessCB.MaxTessFactor = 1.0f;

	if (mTessCB.MaxTessFactor > 16.0f)
		mTessCB.MaxTessFactor = 16.0f;

	// Y / H — амплитуда волн.
	if (GetAsyncKeyState('Y') & 0x8000)
		mTessCB.WaveAmplitude += 0.2f * gt.DeltaTime();

	if (GetAsyncKeyState('H') & 0x8000)
		mTessCB.WaveAmplitude -= 0.2f * gt.DeltaTime();

	if (mTessCB.WaveAmplitude < 0.0f)
		mTessCB.WaveAmplitude = 0.0f;

	if (mTessCB.WaveAmplitude > 1.0f)
		mTessCB.WaveAmplitude = 1.0f;


	// T / G — частота волн.
	if (GetAsyncKeyState('T') & 0x8000)
		mTessCB.WaveFrequency += 3.0f * gt.DeltaTime();

	if (GetAsyncKeyState('G') & 0x8000)
		mTessCB.WaveFrequency -= 3.0f * gt.DeltaTime();

	if (mTessCB.WaveFrequency < 0.1f)
		mTessCB.WaveFrequency = 0.1f;

	if (mTessCB.WaveFrequency > 20.0f)
		mTessCB.WaveFrequency = 20.0f;


	// R / F — скорость волн.
	if (GetAsyncKeyState('R') & 0x8000)
		mTessCB.WaveSpeed += 3.0f * gt.DeltaTime();

	if (GetAsyncKeyState('F') & 0x8000)
		mTessCB.WaveSpeed -= 3.0f * gt.DeltaTime();

	if (mTessCB.WaveSpeed < 0.0f)
		mTessCB.WaveSpeed = 0.0f;

	if (mTessCB.WaveSpeed > 20.0f)
		mTessCB.WaveSpeed = 20.0f;


	// F2 — переключить режим culling: linear / octree.
	bool f2Down = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;

	if (f2Down && !mOctreeToggleKeyPressed)
	{
		mUseOctreeCulling = !mUseOctreeCulling;
	}

	mOctreeToggleKeyPressed = f2Down;


	memcpy(mTessCBMapped, &mTessCB, sizeof(TessellationConstants));
}

bool CrateApp::RaycastBreakfastRoom(XMFLOAT3& hitPos, XMFLOAT3& hitNormal)
{
	if (mObjMesh.Vertices.empty())
		return false;

	XMVECTOR rayOrigin = XMLoadFloat3(&mEyePos);

	// В твоей камере взгляд всегда направлен в центр сцены.
	// Поэтому "выстрел" идёт из камеры в точку (0,0,0).
	XMVECTOR target = XMVectorZero();
	XMVECTOR rayDir = XMVector3Normalize(XMVectorSubtract(target, rayOrigin));

	float closestT = FLT_MAX;
	bool hit = false;

	XMFLOAT3 bestNormal = { 0.0f, 1.0f, 0.0f };

	for (const auto& sub : mObjMesh.SubMeshes)
	{
		const auto& indices = sub.Indices;

		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			UINT i0 = indices[i + 0];
			UINT i1 = indices[i + 1];
			UINT i2 = indices[i + 2];

			if (i0 >= mObjMesh.Vertices.size() ||
				i1 >= mObjMesh.Vertices.size() ||
				i2 >= mObjMesh.Vertices.size())
			{
				continue;
			}

			XMVECTOR v0 = XMLoadFloat3(&mObjMesh.Vertices[i0].Position);
			XMVECTOR v1 = XMLoadFloat3(&mObjMesh.Vertices[i1].Position);
			XMVECTOR v2 = XMLoadFloat3(&mObjMesh.Vertices[i2].Position);

			float t = 0.0f;
			XMFLOAT3 n;

			if (RayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t, n))
			{
				if (t > 0.001f && t < closestT)
				{
					closestT = t;
					bestNormal = n;
					hit = true;
				}
			}
		}
	}

	if (!hit)
		return false;

	XMVECTOR hitV = XMVectorAdd(rayOrigin, XMVectorScale(rayDir, closestT));

	XMStoreFloat3(&hitPos, hitV);
	hitNormal = bestNormal;

	return true;
}

bool CrateApp::RayTriangleIntersect(
	FXMVECTOR rayOrigin,
	FXMVECTOR rayDir,
	FXMVECTOR v0,
	FXMVECTOR v1,
	FXMVECTOR v2,
	float& t,
	XMFLOAT3& outNormal)
{
	const float EPSILON = 0.000001f;

	XMVECTOR edge1 = XMVectorSubtract(v1, v0);
	XMVECTOR edge2 = XMVectorSubtract(v2, v0);

	XMVECTOR h = XMVector3Cross(rayDir, edge2);
	float a = XMVectorGetX(XMVector3Dot(edge1, h));

	if (fabsf(a) < EPSILON)
		return false;

	float f = 1.0f / a;

	XMVECTOR s = XMVectorSubtract(rayOrigin, v0);
	float u = f * XMVectorGetX(XMVector3Dot(s, h));

	if (u < 0.0f || u > 1.0f)
		return false;

	XMVECTOR q = XMVector3Cross(s, edge1);
	float v = f * XMVectorGetX(XMVector3Dot(rayDir, q));

	if (v < 0.0f || u + v > 1.0f)
		return false;

	t = f * XMVectorGetX(XMVector3Dot(edge2, q));

	if (t <= EPSILON)
		return false;

	XMVECTOR n = XMVector3Normalize(XMVector3Cross(edge1, edge2));
	XMStoreFloat3(&outNormal, n);

	return true;
}

void CrateApp::UpdateCamera(const GameTimer& gt)
{
	XMVECTOR pos = XMLoadFloat3(&mCameraPos);

	float cosPitch = cosf(mPitch);
	float sinPitch = sinf(mPitch);
	float cosYaw = cosf(mYaw);
	float sinYaw = sinf(mYaw);

	// Направление взгляда камеры.
	XMVECTOR forward = XMVector3Normalize(XMVectorSet(
		sinYaw * cosPitch,
		sinPitch,
		cosYaw * cosPitch,
		0.0f
	));

	// Правый вектор камеры.
	XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));

	// Настоящий up камеры.
	XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));

	XMVECTOR target = XMVectorAdd(pos, forward);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);

	XMStoreFloat4x4(&mView, view);
	XMStoreFloat3(&mEyePos, pos);
}

void CrateApp::UpdateCascadedShadowMaps(const GameTimer& gt)
{
	constexpr float cameraNear = 1.0f;
	constexpr float shadowDistance = 60.0f;
	constexpr float splitLambda = 0.75f;
	constexpr float casterDepthPadding = 50.0f;
	const float cameraFovY = 0.25f * MathHelper::Pi;

	float splitDistances[SHADOW_CASCADE_COUNT] = {};

	for (UINT cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade)
	{
		float fraction =
			static_cast<float>(cascade + 1) /
			static_cast<float>(SHADOW_CASCADE_COUNT);
		float logarithmic =
			cameraNear * powf(shadowDistance / cameraNear, fraction);
		float uniform =
			cameraNear + (shadowDistance - cameraNear) * fraction;

		// Practical split scheme: nonlinear logarithmic detail near the camera,
		// blended with a uniform distribution for stable cascade sizes.
		splitDistances[cascade] =
			splitLambda * logarithmic +
			(1.0f - splitLambda) * uniform;
	}

	mCascadeSplits = XMFLOAT4(
		splitDistances[0],
		splitDistances[1],
		splitDistances[2],
		splitDistances[3]);

	XMMATRIX cameraView = XMLoadFloat4x4(&mView);
	XMVECTOR lightDirection = XMVector3Normalize(
		XMLoadFloat3(&mDirLightDir));
	XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	if (fabsf(XMVectorGetX(XMVector3Dot(lightDirection, worldUp))) > 0.95f)
	{
		worldUp = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	}

	float cascadeNear = cameraNear;

	for (UINT cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade)
	{
		float cascadeFar = splitDistances[cascade];
		XMMATRIX cascadeProjection = XMMatrixPerspectiveFovLH(
			cameraFovY,
			AspectRatio(),
			cascadeNear,
			cascadeFar);
		XMMATRIX inverseCascadeViewProjection = XMMatrixInverse(
			nullptr,
			cameraView * cascadeProjection);

		XMVECTOR frustumCorners[8];
		UINT cornerIndex = 0;

		for (UINT z = 0; z < 2; ++z)
		{
			float ndcZ = z == 0 ? 0.0f : 1.0f;

			for (UINT y = 0; y < 2; ++y)
			{
				float ndcY = y == 0 ? -1.0f : 1.0f;

				for (UINT x = 0; x < 2; ++x)
				{
					float ndcX = x == 0 ? -1.0f : 1.0f;
					frustumCorners[cornerIndex++] = XMVector3TransformCoord(
						XMVectorSet(ndcX, ndcY, ndcZ, 1.0f),
						inverseCascadeViewProjection);
				}
			}
		}

		XMVECTOR cascadeCenter = XMVectorZero();
		for (XMVECTOR corner : frustumCorners)
		{
			cascadeCenter = XMVectorAdd(cascadeCenter, corner);
		}
		cascadeCenter = XMVectorScale(cascadeCenter, 1.0f / 8.0f);

		float cascadeRadius = 0.0f;
		for (XMVECTOR corner : frustumCorners)
		{
			float distance = XMVectorGetX(
				XMVector3Length(XMVectorSubtract(corner, cascadeCenter)));
			cascadeRadius = (std::max)(cascadeRadius, distance);
		}

		// Quantizing the radius prevents tiny projection-size changes.
		cascadeRadius = ceilf(cascadeRadius * 16.0f) / 16.0f;

		float lightDistance = cascadeRadius + casterDepthPadding;
		XMVECTOR lightPosition = XMVectorSubtract(
			cascadeCenter,
			XMVectorScale(lightDirection, lightDistance));
		XMMATRIX lightView = XMMatrixLookAtLH(
			lightPosition,
			cascadeCenter,
			worldUp);

		float minLightZ = FLT_MAX;
		float maxLightZ = -FLT_MAX;

		for (XMVECTOR corner : frustumCorners)
		{
			XMVECTOR cornerLightSpace =
				XMVector3TransformCoord(corner, lightView);
			float zValue = XMVectorGetZ(cornerLightSpace);
			minLightZ = (std::min)(minLightZ, zValue);
			maxLightZ = (std::max)(maxLightZ, zValue);
		}

		float nearLightZ = (std::max)(
			0.1f,
			minLightZ - casterDepthPadding);
		float farLightZ = maxLightZ + casterDepthPadding;

		XMMATRIX lightProjection = XMMatrixOrthographicOffCenterLH(
			-cascadeRadius,
			cascadeRadius,
			-cascadeRadius,
			cascadeRadius,
			nearLightZ,
			farLightZ);
		XMMATRIX lightViewProjection = lightView * lightProjection;

		XMStoreFloat4x4(
			&mShadowTransforms[cascade],
			XMMatrixTranspose(lightViewProjection));
		mShadowPassConstants[cascade].LightViewProj =
			mShadowTransforms[cascade];
		mShadowPassConstants[cascade].TotalTime = gt.TotalTime();

		cascadeNear = cascadeFar;
	}

	float cosPitch = cosf(mPitch);
	XMFLOAT3 cameraForward(
		sinf(mYaw) * cosPitch,
		sinf(mPitch),
		cosf(mYaw) * cosPitch);

	mRenderSys.SetShadowData(
		mShadowTransforms,
		mCascadeSplits,
		cameraForward,
		mShadowsEnabled,
		mVisualizeShadowCascades);
}

void CrateApp::AnimateMaterials(const GameTimer& gt)
{
	/*
	// Анимация UV для Diablo — скролл текстуры
	static float offsetU = 0.0f;
	offsetU += 0.05f * gt.DeltaTime();
	if (offsetU > 1.0f) offsetU -= 1.0f;

	// Находим RenderItem Diablo (ObjCBIndex == 1)
	for (auto& e : mAllRitems)
	{
		if (e->ObjCBIndex == 1)
		{
			// Матрица сдвига UV
			XMMATRIX scrollMat = XMMatrixTranslation(offsetU, 0.0f, 0.0f);
			XMStoreFloat4x4(&e->TexTransform, scrollMat);
			e->NumFramesDirty = gNumFrameResources;
		}
	}
	*/
}

void CrateApp::UpdateSinusoidalAnimations(const GameTimer& gt)
{
	++mAnimationFrameIndex;

	constexpr float nearDistance = 7.0f;
	constexpr float middleDistance = 14.0f;
	constexpr float nearDistanceSq = nearDistance * nearDistance;
	constexpr float middleDistanceSq = middleDistance * middleDistance;
	constexpr UINT normalNearUpdateInterval = 1;
	constexpr UINT normalMiddleUpdateInterval = 2;
	constexpr UINT normalFarUpdateInterval = 4;
	constexpr UINT demoNearUpdateInterval = 1;
	constexpr UINT demoMiddleUpdateInterval = 8;
	constexpr UINT demoFarUpdateInterval = 30;

	const UINT nearUpdateInterval = mAnimationLodDemoMode
		? demoNearUpdateInterval
		: normalNearUpdateInterval;
	const UINT middleUpdateInterval = mAnimationLodDemoMode
		? demoMiddleUpdateInterval
		: normalMiddleUpdateInterval;
	const UINT farUpdateInterval = mAnimationLodDemoMode
		? demoFarUpdateInterval
		: normalFarUpdateInterval;

	for (auto& item : mAllRitems)
	{
		RenderItem* ritem = item.get();
		if (!ritem->EnableSinusoidalAnimation)
			continue;

		const float dx = ritem->World._41 - mCameraPos.x;
		const float dy = ritem->BaseY - mCameraPos.y;
		const float dz = ritem->World._43 - mCameraPos.z;
		const float distanceSq = dx * dx + dy * dy + dz * dz;

		UINT updateInterval = farUpdateInterval;
		if (distanceSq < nearDistanceSq)
			updateInterval = nearUpdateInterval;
		else if (distanceSq < middleDistanceSq)
			updateInterval = middleUpdateInterval;

		// Распределяем редкие обновления между кадрами, чтобы дальние
		// объекты не обновлялись все одновременно.
		if ((mAnimationFrameIndex + ritem->ObjCBIndex) % updateInterval != 0)
			continue;

		const float phase = ritem->AnimationAngularSpeed * gt.TotalTime() +
			ritem->AnimationPhase;
		ritem->World._42 = ritem->BaseY +
			ritem->AnimationAmplitude * sinf(phase);
		ritem->NumFramesDirty = gNumFrameResources;
	}
}

void CrateApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Картина обновляется каждый кадр из-за анимации
		if (e->AnimType == 1)
			e->NumFramesDirty = gNumFrameResources;

		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.AnimType = e->AnimType;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);
			e->NumFramesDirty--;
		}
	}
}

void CrateApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void CrateApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.6f, 0.6f, 0.6f, 1.0f }; // было 0.25

	mMainPassCB.Lights[0].Direction = { 0.0f, -1.0f, 0.0f };
	mMainPassCB.Lights[0].Strength = { 1.0f, 1.0f, 1.0f };

	mMainPassCB.Lights[1].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.5f, 0.5f, 0.5f };

	mMainPassCB.Lights[2].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[2].Strength = { 0.3f, 0.3f, 0.3f };
	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void CrateApp::LoadTextures()
{
	auto woodCrateTex = std::make_unique<Texture>();
	woodCrateTex->Name = "woodCrateTex";
	woodCrateTex->Filename = L"../../Textures/WoodCrate01.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), woodCrateTex->Filename.c_str(),
		woodCrateTex->Resource, woodCrateTex->UploadHeap));
 
	mTextures[woodCrateTex->Name] = std::move(woodCrateTex);
}

void CrateApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0);
    slotRootParameter[2].InitAsConstantBufferView(1);
    slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void CrateApp::BuildDescriptorHeaps()
{
	// Считаем текстуры: 0=woodCrate, 1=white, 2+=MTL текстуры
	UINT mtlTexCount = 0;
	for (auto& pair : mObjMesh.Materials)
		if (!pair.second.DiffuseMapPath.empty())
			mtlTexCount++;

	UINT totalTex = 2 + mtlTexCount + 3;

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = totalTex;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDesc(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// Слот 0 — woodCrate
	auto woodCrateTex = mTextures["woodCrateTex"]->Resource;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = woodCrateTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = woodCrateTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(woodCrateTex.Get(), &srvDesc, hDesc);

	// Слот 1 — белая текстура 1x1 для материалов без текстуры
	hDesc.Offset(1, mCbvSrvDescriptorSize);
	{
		// Создаём белую текстуру 1x1 программно
		D3D12_RESOURCE_DESC whiteDesc = {};
		whiteDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		whiteDesc.Width = 1;
		whiteDesc.Height = 1;
		whiteDesc.DepthOrArraySize = 1;
		whiteDesc.MipLevels = 1;
		whiteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		whiteDesc.SampleDesc = { 1, 0 };
		whiteDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

		ComPtr<ID3D12Resource> whiteResource;
		md3dDevice->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &whiteDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(&whiteResource));

		// Upload буфер
		UINT64 uploadSize = GetRequiredIntermediateSize(whiteResource.Get(), 0, 1);
		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Width = uploadSize;
		bufDesc.Height = 1;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.MipLevels = 1;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.SampleDesc = { 1, 0 };
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> whiteUpload;
		md3dDevice->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&whiteUpload));

		// Белый пиксель RGBA = 0xFFFFFFFF
		UINT32 whitePixel = 0xFFFFFFFF;
		D3D12_SUBRESOURCE_DATA whiteData = {};
		whiteData.pData = &whitePixel;
		whiteData.RowPitch = 4;
		whiteData.SlicePitch = 4;
		UpdateSubresources(mCommandList.Get(),
			whiteResource.Get(), whiteUpload.Get(),
			0, 0, 1, &whiteData);

		D3D12_RESOURCE_BARRIER whiteBarrier = {};
		whiteBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		whiteBarrier.Transition.pResource = whiteResource.Get();
		whiteBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		whiteBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		whiteBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		mCommandList->ResourceBarrier(1, &whiteBarrier);

		// Сохраняем чтобы не удалился
		mTextures["whiteTex"] = std::make_unique<Texture>();
		mTextures["whiteTex"]->Resource = whiteResource;
		mTextures["whiteTex"]->UploadHeap = whiteUpload;

		// SRV в слот 1
		D3D12_SHADER_RESOURCE_VIEW_DESC whiteSrvDesc = {};
		whiteSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		whiteSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		whiteSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		whiteSrvDesc.Texture2D.MostDetailedMip = 0;
		whiteSrvDesc.Texture2D.MipLevels = 1;
		whiteSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		md3dDevice->CreateShaderResourceView(whiteResource.Get(), &whiteSrvDesc, hDesc);
	}

	// Слоты 2+ — текстуры из MTL
	UINT slot = 2;
	for (auto& pair : mObjMesh.Materials)
	{
		auto& mat = pair.second;
		if (mat.DiffuseMapPath.empty()) continue;

		DirectX::TexMetadata  metadata;
		DirectX::ScratchImage image;
		HRESULT hr = DirectX::LoadFromWICFile(
			mat.DiffuseMapPath.c_str(),
			DirectX::WIC_FLAGS_NONE, &metadata, image);
		if (FAILED(hr)) { mat.SrvHeapIndex = 1; continue; }

		ComPtr<ID3D12Resource> texResource;
		ComPtr<ID3D12Resource> uploadResource;

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resDesc.Width = (UINT64)metadata.width;
		resDesc.Height = (UINT)metadata.height;
		resDesc.DepthOrArraySize = 1;
		resDesc.MipLevels = 1;
		resDesc.Format = metadata.format;
		resDesc.SampleDesc = { 1, 0 };
		resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
		md3dDevice->CreateCommittedResource(
			&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(&texResource));

		UINT64 uploadSize = GetRequiredIntermediateSize(texResource.Get(), 0, 1);
		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Width = uploadSize;
		bufDesc.Height = 1;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.MipLevels = 1;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.SampleDesc = { 1, 0 };
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		md3dDevice->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&uploadResource));

		const DirectX::Image* img = image.GetImage(0, 0, 0);
		D3D12_SUBRESOURCE_DATA subData = {};
		subData.pData = img->pixels;
		subData.RowPitch = (LONG_PTR)img->rowPitch;
		subData.SlicePitch = (LONG_PTR)img->slicePitch;
		UpdateSubresources(mCommandList.Get(),
			texResource.Get(), uploadResource.Get(),
			0, 0, 1, &subData);

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texResource.Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		mCommandList->ResourceBarrier(1, &barrier);

		mTextures[mat.Name] = std::make_unique<Texture>();
		mTextures[mat.Name]->Resource = texResource;
		mTextures[mat.Name]->UploadHeap = uploadResource;

		hDesc.Offset(1, mCbvSrvDescriptorSize);
		D3D12_SHADER_RESOURCE_VIEW_DESC matSrvDesc = {};
		matSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		matSrvDesc.Format = metadata.format;
		matSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		matSrvDesc.Texture2D.MostDetailedMip = 0;
		matSrvDesc.Texture2D.MipLevels = 1;
		matSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		md3dDevice->CreateShaderResourceView(texResource.Get(), &matSrvDesc, hDesc);

		mat.SrvHeapIndex = (int)slot++;
	}
	// ─────────────────────────────────────────────────────────────
// Tessellation material textures:
// t0 = Color
// t1 = NormalDX
// t2 = Displacement
// Они должны лежать подряд в descriptor heap.
// ─────────────────────────────────────────────────────────────

	auto CreateTextureSRVFromFile = [&](
		const std::wstring& filename,
		const std::string& textureName,
		UINT srvIndex)
		{
			DirectX::TexMetadata metadata;
			DirectX::ScratchImage image;

			HRESULT hr = DirectX::LoadFromWICFile(
				filename.c_str(),
				DirectX::WIC_FLAGS_NONE,
				&metadata,
				image);

			ThrowIfFailed(hr);

			ComPtr<ID3D12Resource> texResource;
			ComPtr<ID3D12Resource> uploadResource;

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resDesc.Width = (UINT64)metadata.width;
			resDesc.Height = (UINT)metadata.height;
			resDesc.DepthOrArraySize = 1;
			resDesc.MipLevels = 1;
			resDesc.Format = metadata.format;
			resDesc.SampleDesc.Count = 1;
			resDesc.SampleDesc.Quality = 0;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			D3D12_HEAP_PROPERTIES defaultHeap = {};
			defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

			ThrowIfFailed(md3dDevice->CreateCommittedResource(
				&defaultHeap,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&texResource)));

			UINT64 uploadSize = GetRequiredIntermediateSize(texResource.Get(), 0, 1);

			D3D12_HEAP_PROPERTIES uploadHeap = {};
			uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

			D3D12_RESOURCE_DESC bufferDesc = {};
			bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			bufferDesc.Width = uploadSize;
			bufferDesc.Height = 1;
			bufferDesc.DepthOrArraySize = 1;
			bufferDesc.MipLevels = 1;
			bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
			bufferDesc.SampleDesc.Count = 1;
			bufferDesc.SampleDesc.Quality = 0;
			bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			ThrowIfFailed(md3dDevice->CreateCommittedResource(
				&uploadHeap,
				D3D12_HEAP_FLAG_NONE,
				&bufferDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&uploadResource)));

			const DirectX::Image* img = image.GetImage(0, 0, 0);

			D3D12_SUBRESOURCE_DATA subData = {};
			subData.pData = img->pixels;
			subData.RowPitch = (LONG_PTR)img->rowPitch;
			subData.SlicePitch = (LONG_PTR)img->slicePitch;

			UpdateSubresources(
				mCommandList.Get(),
				texResource.Get(),
				uploadResource.Get(),
				0,
				0,
				1,
				&subData);

			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = texResource.Get();
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			mCommandList->ResourceBarrier(1, &barrier);

			CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
				mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

			srvHandle.Offset(srvIndex, mCbvSrvDescriptorSize);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = metadata.format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

			md3dDevice->CreateShaderResourceView(
				texResource.Get(),
				&srvDesc,
				srvHandle);

			auto tex = std::make_unique<Texture>();
			tex->Name = textureName;
			tex->Filename = filename;
			tex->Resource = texResource;
			tex->UploadHeap = uploadResource;

			mTextures[textureName] = std::move(tex);
		};

	// slot после woodCrate, white и всех MTL-текстур
	mTessDiffuseSrvIndex = slot++;
	mTessNormalSrvIndex = slot++;
	mTessDisplacementSrvIndex = slot++;

	// t0 Color, t1 Normal, t2 Displacement
	CreateTextureSRVFromFile(
		L"MyMaterials\\PavingStones141_2K-JPG_Color.jpg",
		"pavingStonesColor",
		mTessDiffuseSrvIndex);

	CreateTextureSRVFromFile(
		L"MyMaterials\\PavingStones141_2K-JPG_NormalDX.jpg",
		"pavingStonesNormalDX",
		mTessNormalSrvIndex);

	CreateTextureSRVFromFile(
		L"MyMaterials\\PavingStones141_2K-JPG_Displacement.jpg",
		"pavingStonesDisplacement",
		mTessDisplacementSrvIndex);
}

void CrateApp::BuildShadersAndInputLayout()
{
	// Старые forward shaders пока оставляем.
	mShaders["standardVS"] = d3dUtil::CompileShader(
		L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");

	mShaders["opaquePS"] = d3dUtil::CompileShader(
		L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");

	// Новые deferred shaders.
	mShaders["deferredGeoVS"] = d3dUtil::CompileShader(
		L"Shaders\\GeometryPass.hlsl", nullptr, "VS", "vs_5_0");

	mShaders["deferredGeoPS"] = d3dUtil::CompileShader(
		L"Shaders\\GeometryPass.hlsl", nullptr, "PS", "ps_5_0");

	mShaders["deferredLightVS"] = d3dUtil::CompileShader(
		L"Shaders\\LightingPass.hlsl", nullptr, "VS", "vs_5_0");

	mShaders["deferredLightPS"] = d3dUtil::CompileShader(
		L"Shaders\\LightingPass.hlsl", nullptr, "PS", "ps_5_0");

	mShaders["shadowVS"] = d3dUtil::CompileShader(
		L"Shaders\\ShadowPass.hlsl", nullptr, "VS", "vs_5_0");

	mShaders["tessVS"] = d3dUtil::CompileShader(
		L"Shaders\\TessellationPass.hlsl", nullptr, "VS", "vs_5_0");

	mShaders["tessHS"] = d3dUtil::CompileShader(
		L"Shaders\\TessellationPass.hlsl", nullptr, "HS", "hs_5_0");

	mShaders["tessDS"] = d3dUtil::CompileShader(
		L"Shaders\\TessellationPass.hlsl", nullptr, "DS", "ds_5_0");

	mShaders["tessPS"] = d3dUtil::CompileShader(
		L"Shaders\\TessellationPass.hlsl", nullptr, "PS", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void CrateApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
 
	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = 0;
	boxSubmesh.BaseVertexLocation = 0;

 
	std::vector<Vertex> vertices(box.Vertices.size());

	for(size_t i = 0; i < box.Vertices.size(); ++i)
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = box.GetIndices16();

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size()  * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;

	mGeometries[geo->Name] = std::move(geo);
	// ── Загрузка breakfast_room ────────────────────────────────────
	if (!mObjMesh.Vertices.empty())
	{
		// Конвертируем ObjVertex -> Vertex (формат Luna)
		std::vector<Vertex> objVerts(mObjMesh.Vertices.size());
		for (size_t i = 0; i < mObjMesh.Vertices.size(); ++i)
		{
			objVerts[i].Pos = mObjMesh.Vertices[i].Position;
			objVerts[i].Normal = mObjMesh.Vertices[i].Normal;
			objVerts[i].TexC = mObjMesh.Vertices[i].TexCoord;
		}

		// Собираем все индексы всех sub-mesh в один буфер
		std::vector<uint32_t> objIndices;
		for (auto& sub : mObjMesh.SubMeshes)
			for (auto idx : sub.Indices)
				objIndices.push_back(idx);

		const UINT objVbSize = (UINT)objVerts.size() * sizeof(Vertex);
		const UINT objIbSize = (UINT)objIndices.size() * sizeof(uint32_t);

		auto objGeo = std::make_unique<MeshGeometry>();
		objGeo->Name = "objGeo";

		ThrowIfFailed(D3DCreateBlob(objVbSize, &objGeo->VertexBufferCPU));
		CopyMemory(objGeo->VertexBufferCPU->GetBufferPointer(), objVerts.data(), objVbSize);
		ThrowIfFailed(D3DCreateBlob(objIbSize, &objGeo->IndexBufferCPU));
		CopyMemory(objGeo->IndexBufferCPU->GetBufferPointer(), objIndices.data(), objIbSize);

		objGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), objVerts.data(), objVbSize, objGeo->VertexBufferUploader);
		objGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), objIndices.data(), objIbSize, objGeo->IndexBufferUploader);

		objGeo->VertexByteStride = sizeof(Vertex);
		objGeo->VertexBufferByteSize = objVbSize;
		objGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
		objGeo->IndexBufferByteSize = objIbSize;

		// Одна запись — весь меш целиком
		SubmeshGeometry objSubmesh;
		objSubmesh.IndexCount = (UINT)objIndices.size();
		objSubmesh.StartIndexLocation = 0;
		objSubmesh.BaseVertexLocation = 0;
		objGeo->DrawArgs["obj"] = objSubmesh;

		mGeometries[objGeo->Name] = std::move(objGeo);
	}
}

void CrateApp::BuildTessellatedPatchGeometry()
{
	float minX = FLT_MAX;
	float maxX = -FLT_MAX;
	float minY = FLT_MAX;
	float minZ = FLT_MAX;
	float maxZ = -FLT_MAX;

	for (const auto& v : mObjMesh.Vertices)
	{
		if (v.Position.x < minX) minX = v.Position.x;
		if (v.Position.x > maxX) maxX = v.Position.x;

		if (v.Position.y < minY) minY = v.Position.y;

		if (v.Position.z < minZ) minZ = v.Position.z;
		if (v.Position.z > maxZ) maxZ = v.Position.z;
	}

	// Чуть выше настоящего пола, чтобы не было мерцания с оригинальным полом.
	float floorY = minY + 0.3f;

	// Небольшой отступ от стен, чтобы patch не залезал в стены.
	float inset = 0.15f;

	minX += inset;
	maxX -= inset;
	minZ += inset;
	maxZ -= inset;

	std::array<Vertex, 4> vertices;

	// Чем больше UV-координаты, тем чаще повторяется текстура.
	// Для пола лучше не растягивать одну картинку на всю комнату,
	// а повторить её несколько раз.
	float uvRepeat = 8.0f;

	vertices[0].Pos = XMFLOAT3(minX, floorY, minZ);
	vertices[0].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
	vertices[0].TexC = XMFLOAT2(0.0f, uvRepeat);

	vertices[1].Pos = XMFLOAT3(maxX, floorY, minZ);
	vertices[1].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
	vertices[1].TexC = XMFLOAT2(uvRepeat, uvRepeat);

	vertices[2].Pos = XMFLOAT3(minX, floorY, maxZ);
	vertices[2].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
	vertices[2].TexC = XMFLOAT2(0.0f, 0.0f);

	vertices[3].Pos = XMFLOAT3(maxX, floorY, maxZ);
	vertices[3].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
	vertices[3].TexC = XMFLOAT2(uvRepeat, 0.0f);

	std::array<std::uint16_t, 4> indices =
	{
		0, 1, 2, 3
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "tessPatchGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(
		geo->VertexBufferCPU->GetBufferPointer(),
		vertices.data(),
		vbByteSize
	);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(
		geo->IndexBufferCPU->GetBufferPointer(),
		indices.data(),
		ibByteSize
	);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
		md3dDevice.Get(),
		mCommandList.Get(),
		vertices.data(),
		vbByteSize,
		geo->VertexBufferUploader
	);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
		md3dDevice.Get(),
		mCommandList.Get(),
		indices.data(),
		ibByteSize,
		geo->IndexBufferUploader
	);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;

	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry patchSubmesh;
	patchSubmesh.IndexCount = 4;
	patchSubmesh.StartIndexLocation = 0;
	patchSubmesh.BaseVertexLocation = 0;

	geo->DrawArgs["tessPatch"] = patchSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void CrateApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));

	// Deferred Geometry Pass PSO.
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc =
	{
		mInputLayout.data(),
		(UINT)mInputLayout.size()
	};

	mRenderSys.BuildGeometryPSO(
		md3dDevice.Get(),
		inputLayoutDesc,
		mShaders["deferredGeoVS"].Get(),
		mShaders["deferredGeoPS"].Get(),
		mDepthStencilFormat
	);

	mRenderSys.BuildShadowPSO(
		md3dDevice.Get(),
		inputLayoutDesc,
		mShaders["shadowVS"].Get()
	);

	// Deferred Lighting Pass PSO.
	mRenderSys.BuildLightingPSO(
		md3dDevice.Get(),
		mShaders["deferredLightVS"].Get(),
		mShaders["deferredLightPS"].Get(),
		mBackBufferFormat
	);
}

void CrateApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void CrateApp::BuildMaterials()
{
	auto woodCrate = std::make_unique<Material>();
	woodCrate->Name = "woodCrate";
	woodCrate->MatCBIndex = 0;
	woodCrate->DiffuseSrvHeapIndex = 0;
	woodCrate->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	woodCrate->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	woodCrate->Roughness = 0.2f;

	mMaterials["woodCrate"] = std::move(woodCrate);

	auto shadowFloorMat = std::make_unique<Material>();
	shadowFloorMat->Name = "shadowFloorMat";
	shadowFloorMat->MatCBIndex = (int)mMaterials.size();
	shadowFloorMat->DiffuseSrvHeapIndex = 1; // белая служебная текстура
	shadowFloorMat->DiffuseAlbedo =
		XMFLOAT4(0.52f, 0.56f, 0.62f, 1.0f);
	shadowFloorMat->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	shadowFloorMat->Roughness = 0.95f;

	mMaterials["shadowFloorMat"] = std::move(shadowFloorMat);

	for (auto& pair : mObjMesh.Materials)
	{
		const auto& objMat = pair.second;
		auto mat = std::make_unique<Material>();
		mat->Name = objMat.Name;
		mat->MatCBIndex = (int)mMaterials.size();
		mat->FresnelR0 = objMat.FresnelR0;
		mat->Roughness = objMat.Roughness;

		if (objMat.SrvHeapIndex >= 0)
		{
			// Есть текстура — используем её, альбедо белый
			mat->DiffuseSrvHeapIndex = objMat.SrvHeapIndex;
			mat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			// Нет текстуры — используем слот 0 (woodCrate),
			// но перебиваем альбедо цветом из MTL
			mat->DiffuseSrvHeapIndex = 1;
			// Берём Kd цвет из MTL и делаем его ярче
			mat->DiffuseAlbedo = XMFLOAT4(
				objMat.DiffuseAlbedo.x,
				objMat.DiffuseAlbedo.y,
				objMat.DiffuseAlbedo.z,
				1.0f);
		}

		mMaterials[objMat.Name] = std::move(mat);
	}
	auto tessPatchMat = std::make_unique<Material>();
	tessPatchMat->Name = "tessPatchMat";
	tessPatchMat->MatCBIndex = (int)mMaterials.size();

	tessPatchMat->DiffuseSrvHeapIndex = mTessDiffuseSrvIndex;

	tessPatchMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tessPatchMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	tessPatchMat->Roughness = 0.45f;

	mMaterials["tessPatchMat"] = std::move(tessPatchMat);
}

void CrateApp::BuildRenderItems()
{
	// Загружаем breakfast_room
	mObjMesh = ObjLoader::Load("breakfast_room.obj");

	if (mGeometries.count("objGeo") > 0)
	{
		UINT submeshStart = 0;
		for (size_t i = 0; i < mObjMesh.SubMeshes.size(); ++i)
		{
			const auto& sub = mObjMesh.SubMeshes[i];
			if (sub.Indices.empty()) continue;

			auto ritem = std::make_unique<RenderItem>();
			// Если это картина — включаем анимацию качания
			if (sub.MaterialName == "breakfast_room:Artwork")
				ritem->AnimType = 1;
			ritem->ObjCBIndex = (UINT)i;
			// Берём материал из MTL или woodCrate если не найден
			auto matIt = mMaterials.find(sub.MaterialName);
			if (matIt != mMaterials.end())
				ritem->Mat = matIt->second.get();
			else
				ritem->Mat = mMaterials["woodCrate"].get();
			ritem->Geo = mGeometries["objGeo"].get();
			ritem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			ritem->IndexCount = (UINT)sub.Indices.size();
			ritem->StartIndexLocation = submeshStart;
			ritem->BaseVertexLocation = 0;

			XMStoreFloat4x4(&ritem->World, XMMatrixIdentity());
			ritem->NumFramesDirty = gNumFrameResources;

			mAllRitems.push_back(std::move(ritem));
			submeshStart += (UINT)sub.Indices.size();
		}
	}

	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());

	if (mGeometries.count("tessPatchGeo") > 0)
	{
		auto tessRitem = std::make_unique<RenderItem>();

		tessRitem->World = MathHelper::Identity4x4();
		tessRitem->TexTransform = MathHelper::Identity4x4();

		tessRitem->ObjCBIndex = (UINT)mAllRitems.size();

		tessRitem->Mat = mMaterials["tessPatchMat"].get();
		tessRitem->Geo = mGeometries["tessPatchGeo"].get();

		// Важно: это patch topology, не triangle list.
		tessRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;

		tessRitem->IndexCount = 4;
		tessRitem->StartIndexLocation = 0;
		tessRitem->BaseVertexLocation = 0;

		tessRitem->NumFramesDirty = gNumFrameResources;

		mTessPatchRitem = tessRitem.get();

		// Не добавляем в mOpaqueRitems.
		// Он должен рисоваться отдельным DrawTessellatedPatch().
		mAllRitems.push_back(std::move(tessRitem));
	}
}

void CrateApp::DrawShadowCasters(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize =
		d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	for (RenderItem* ri : ritems)
	{
		if (ri == nullptr || ri->Geo == nullptr)
			continue;

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress =
			objectCB->GetGPUVirtualAddress() +
			ri->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->DrawIndexedInstanced(
			ri->IndexCount,
			1,
			ri->StartIndexLocation,
			ri->BaseVertexLocation,
			0);
	}
}

void CrateApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// Если нет текстуры — используем слот 0 (woodCrate)
		int srvIndex = ri->Mat->DiffuseSrvHeapIndex;
		if (srvIndex < 0) srvIndex = 0;

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(srvIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void CrateApp::DrawTessellatedPatch(ID3D12GraphicsCommandList* cmdList)
{
	if (mTessPatchRitem == nullptr)
		return;

	auto ri = mTessPatchRitem;

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();
	auto passCB = mCurrFrameResource->PassCB->Resource();

	D3D12_GPU_VIRTUAL_ADDRESS objCBAddress =
		objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

	D3D12_GPU_VIRTUAL_ADDRESS matCBAddress =
		matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

	cmdList->SetPipelineState(mTessPSO.Get());
	cmdList->SetGraphicsRootSignature(mTessRootSignature.Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// Временно:
	// t0 = slot 0 — woodCrate diffuse
	// t1 = slot 1 — white texture
	// t2 = slot 2 — первая OBJ/MTL texture
	CD3DX12_GPU_DESCRIPTOR_HANDLE tex(
		mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	tex.Offset(mTessDiffuseSrvIndex, mCbvSrvDescriptorSize);

	// Descriptor table содержит 3 подряд идущих SRV:
	// t0 = Color
	// t1 = NormalDX
	// t2 = Displacement
	cmdList->SetGraphicsRootDescriptorTable(0, tex);

	cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);
	cmdList->SetGraphicsRootConstantBufferView(4, mTessCBUpload->GetGPUVirtualAddress());

	cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
	cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);

	cmdList->DrawIndexedInstanced(
		ri->IndexCount,
		1,
		ri->StartIndexLocation,
		ri->BaseVertexLocation,
		0);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> CrateApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}
