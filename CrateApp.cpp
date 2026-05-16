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
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void RebuildDemoLights();

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;
 
	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	// Мои поля
	TextureManager  mTexMgr;
	TextureAnimator mUVAnim;
	ObjMesh         mObjMesh;

	RenderingSystem mRenderSys;

	ComPtr<ID3D12Resource> mUVAnimCBUpload;
	UVAnimCB* mUVAnimCBMapped = nullptr;
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.3f*XM_PI;
	float mPhi = 0.4f*XM_PI;
	float mRadius = 2.5f;

    POINT mLastMousePos;

	int mSelectedDemoLight = 1;

	// Directional light
	XMFLOAT3 mDirLightDir = { 0.577f, -0.577f, 0.577f };
	XMFLOAT3 mDirLightStrength = { 0.35f, 0.35f, 0.35f };

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
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

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
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void CrateApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Сброс allocator'а текущего frame resource.
	ThrowIfFailed(cmdListAlloc->Reset());

	// PSO можно передать nullptr, потому что нужные PSO выставит RenderingSystem.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

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
	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

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
		XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f)
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
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.05f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
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

	if (selectedPos != nullptr)
	{
		// Движение по сцене.
		if (GetAsyncKeyState('A') & 0x8000)
			selectedPos->x -= moveSpeed;

		if (GetAsyncKeyState('D') & 0x8000)
			selectedPos->x += moveSpeed;

		if (GetAsyncKeyState('W') & 0x8000)
			selectedPos->z += moveSpeed;

		if (GetAsyncKeyState('S') & 0x8000)
			selectedPos->z -= moveSpeed;

		if (GetAsyncKeyState('Q') & 0x8000)
			selectedPos->y -= moveSpeed;

		if (GetAsyncKeyState('E') & 0x8000)
			selectedPos->y += moveSpeed;
	}

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

	// После изменения параметров пересобираем список источников света.
	RebuildDemoLights();
}
 

void CrateApp::RebuildDemoLights()
{
	mRenderSys.ClearLights();

	// Общий направленный свет.
	mRenderSys.AddDirectionalLight(
		mDirLightDir,
		mDirLightStrength
	);

	// Первый точечный источник.
	mRenderSys.AddPointLight(
		mPoint1Pos,
		mPoint1Range,
		mPoint1Strength
	);

	// Второй точечный источник.
	mRenderSys.AddPointLight(
		mPoint2Pos,
		mPoint2Range,
		mPoint2Strength
	);

	// Spot light всегда направлен в mSpotTarget.
	XMVECTOR spotPos = XMLoadFloat3(&mSpotPos);
	XMVECTOR spotTarget = XMLoadFloat3(&mSpotTarget);
	XMVECTOR spotDirV = XMVector3Normalize(spotTarget - spotPos);

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
}


void CrateApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
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

	UINT totalTex = 2 + mtlTexCount;

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

