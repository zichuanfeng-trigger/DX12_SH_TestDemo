// ============================================================================
// DX12 Deferred Renderer - MVP Demo
// ============================================================================
// Deferred Renderer single-file implementation
// Two-stage rendering pipeline:
// 1. Geometry Pass: Write geometry info to G-Buffer
// 2. Lighting Pass: Compute lighting based on G-Buffer
// ============================================================================

#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <fstream>
#include <vector>
#include <strsafe.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <d3dcompiler.h>
#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

using namespace std;
using namespace Microsoft::WRL;
using namespace DirectX;

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ============================================================================
// Constants
// ============================================================================

#define GRS_WND_CLASS_NAME _T("DeferredRendererWindowClass")
#define GRS_WND_TITLE _T("DX12 Deferred Renderer")
#define GRS_THROW_IF_FAILED(hr) { HRESULT _hr = (hr); if (FAILED(_hr)) { throw CGRSCOMException(_hr); } }
#define GRS_UPPER(A,B) ((UINT)(((A)+((B)-1))&~(B - 1)))

// G-Buffer configuration
static constexpr UINT GBUFFER_COUNT = 3;
static constexpr DXGI_FORMAT GBUFFER_FORMATS[GBUFFER_COUNT] = {
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT
};

// ============================================================================
// Exception handling
// ============================================================================

class CGRSCOMException
{
public:
    CGRSCOMException(HRESULT hr) : m_hrError(hr) {}
    HRESULT Error() const { return m_hrError; }
private:
    const HRESULT m_hrError;
};

// ============================================================================
// Vertex structures
// ============================================================================

struct VertexGeometry
{
    XMFLOAT4 Position;
    XMFLOAT3 Normal;
    XMFLOAT2 UV;
};

struct VertexFullscreen
{
    XMFLOAT4 Position;
};

// ============================================================================
// Constant buffer structures
// ============================================================================

struct GeometryConstants
{
    XMFLOAT4X4 WorldViewProj;
    XMFLOAT4X4 WorldView;
};

struct PointLight
{
    XMFLOAT3 Position;
    float Radius;
    XMFLOAT3 Color;
    float Intensity;
};

struct LightingConstants
{
    XMFLOAT4 CameraPos;
    PointLight Lights[4];
    UINT LightCount;
    float Pad[3];
};

struct SkyboxConstants
{
    XMFLOAT4X4 InvViewProj;
};

// ============================================================================
// Global state
// ============================================================================

UINT g_WindowWidth = 1024;
UINT g_WindowHeight = 768;
XMFLOAT3 g_CameraPos = XMFLOAT3(0.0f, 2.0f, -8.0f);
float g_CameraYaw = 0.0f;
float g_CameraPitch = 0.0f;
POINT g_LastMousePos = { 0, 0 };
bool g_RightMouseDown = false;

PointLight g_SceneLights[3] = {
    { XMFLOAT3(-3.0f, 3.0f, 3.0f), 10.0f, XMFLOAT3(1.0f, 0.3f, 0.3f), 2.0f },
    { XMFLOAT3(3.0f, 3.0f, 3.0f), 10.0f, XMFLOAT3(0.3f, 0.3f, 1.0f), 2.0f },
    { XMFLOAT3(0.0f, 5.0f, -3.0f), 15.0f, XMFLOAT3(1.0f, 1.0f, 0.8f), 1.5f }
};

// D3D12 global objects
ComPtr<IDXGIFactory5> g_Factory;
ComPtr<ID3D12Device4> g_Device;
ComPtr<IDXGISwapChain3> g_SwapChain;
ComPtr<ID3D12CommandQueue> g_CommandQueue;

ComPtr<ID3D12CommandAllocator> g_CommandAllocator;
ComPtr<ID3D12GraphicsCommandList> g_CommandList;
ComPtr<ID3D12CommandAllocator> g_BundleAllocatorGeometry;
ComPtr<ID3D12GraphicsCommandList> g_BundleGeometry;
ComPtr<ID3D12CommandAllocator> g_BundleAllocatorLighting;
ComPtr<ID3D12GraphicsCommandList> g_BundleLighting;
ComPtr<ID3D12CommandAllocator> g_BundleAllocatorSkybox;
ComPtr<ID3D12GraphicsCommandList> g_BundleSkybox;

static constexpr UINT FRAME_COUNT = 3;
ComPtr<ID3D12Resource> g_RenderTargets[FRAME_COUNT];
ComPtr<ID3D12DescriptorHeap> g_RTVHeap;
UINT g_RTVDescriptorSize = 0;
UINT g_CurrentFrameIndex = 0;

ComPtr<ID3D12Resource> g_DepthBuffer;
ComPtr<ID3D12DescriptorHeap> g_DSVHeap;

ComPtr<ID3D12Resource> g_GBuffer[GBUFFER_COUNT];
ComPtr<ID3D12DescriptorHeap> g_GBufferRTVHeap;
ComPtr<ID3D12DescriptorHeap> g_GBufferSRVHeap;

ComPtr<ID3D12RootSignature> g_RootSignatureGeometry;
ComPtr<ID3D12RootSignature> g_RootSignatureLighting;
ComPtr<ID3D12RootSignature> g_RootSignatureSkybox;

ComPtr<ID3D12PipelineState> g_PSOGeometry;
ComPtr<ID3D12PipelineState> g_PSOLighting;
ComPtr<ID3D12PipelineState> g_PSOSkybox;

ComPtr<ID3D12Resource> g_SphereVB;
ComPtr<ID3D12Resource> g_SphereIB;
D3D12_VERTEX_BUFFER_VIEW g_SphereVBV;
D3D12_INDEX_BUFFER_VIEW g_SphereIBV;
UINT g_SphereIndexCount = 0;

ComPtr<ID3D12Resource> g_SkyboxVB;
D3D12_VERTEX_BUFFER_VIEW g_SkyboxVBV;

ComPtr<ID3D12Resource> g_GeometryCB;
ComPtr<ID3D12Resource> g_LightingCB;
ComPtr<ID3D12Resource> g_SkyboxCB;
UINT8* g_pGeometryCBData = nullptr;
UINT8* g_pLightingCBData = nullptr;
UINT8* g_pSkyboxCBData = nullptr;

ComPtr<ID3D12Fence> g_Fence;
UINT64 g_FenceValue = 0;
HANDLE g_FenceEvent = nullptr;

UINT g_SRVDescriptorSize = 0;
UINT g_SamplerDescriptorSize = 0;

// ============================================================================
// Function declarations
// ============================================================================

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void InitD3D(HWND hWnd);
void CreateGBuffer();
void CreateRootSignatures();
void CreatePSOs(const WCHAR* shaderPath);
void CreateGeometry();
void CreateSkyboxGeometry();
void CreateConstantBuffers();
void CreateBundleGeometry();
void CreateBundleLighting();
void CreateBundleSkybox();
void LoadPipeline(HWND hWnd);
void LoadAssets(const WCHAR* shaderPath);
void PopulateCommandList();
void WaitForGPU();
void OnUpdate();
void OnRender();
void Cleanup();

// ============================================================================
// Entry point
// ============================================================================

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    ::CoInitialize(nullptr);

    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_GLOBALCLASS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcex.lpszClassName = GRS_WND_CLASS_NAME;
    RegisterClassEx(&wcex);

    DWORD dwStyle = WS_OVERLAPPED | WS_SYSMENU;
    RECT rtWnd = { 0, 0, (LONG)g_WindowWidth, (LONG)g_WindowHeight };
    AdjustWindowRect(&rtWnd, dwStyle, FALSE);
    
    INT posX = (GetSystemMetrics(SM_CXSCREEN) - rtWnd.right - rtWnd.left) / 2;
    INT posY = (GetSystemMetrics(SM_CYSCREEN) - rtWnd.bottom - rtWnd.top) / 2;

    HWND hWnd = CreateWindowW(GRS_WND_CLASS_NAME, GRS_WND_TITLE, dwStyle,
        posX, posY, rtWnd.right - rtWnd.left, rtWnd.bottom - rtWnd.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return FALSE;

    try
    {
        WCHAR appPath[MAX_PATH] = {};
        GetModuleFileName(nullptr, appPath, MAX_PATH);
        WCHAR* lastSlash = wcsrchr(appPath, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';

        InitD3D(hWnd);
        LoadPipeline(hWnd);
        LoadAssets(appPath);
        CreateBundleGeometry();
        CreateBundleLighting();
        CreateBundleSkybox();

        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);

        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                OnUpdate();
                OnRender();
            }
        }
    }
    catch (CGRSCOMException& e)
    {
        e;
    }

    Cleanup();
    ::CoUninitialize();
    return 0;
}

// ============================================================================
// D3D12 initialization
// ============================================================================

void InitD3D(HWND hWnd)
{
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }
#endif

    UINT factoryFlags = 0;
#if defined(_DEBUG)
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    GRS_THROW_IF_FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&g_Factory)));

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; DXGI_ERROR_NOT_FOUND != g_Factory->EnumAdapters1(i, &adapter); ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_Device)))) break;
    }

    if (!g_Device) throw CGRSCOMException(E_FAIL);

    g_RTVDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    g_SRVDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_SamplerDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
}

void LoadPipeline(HWND hWnd)
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    GRS_THROW_IF_FAILED(g_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_CommandQueue)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FRAME_COUNT;
    swapChainDesc.Width = g_WindowWidth;
    swapChainDesc.Height = g_WindowHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    GRS_THROW_IF_FAILED(g_Factory->CreateSwapChainForHwnd(
        g_CommandQueue.Get(), hWnd, &swapChainDesc, nullptr, nullptr, &swapChain));
    GRS_THROW_IF_FAILED(swapChain.As(&g_SwapChain));
    g_CurrentFrameIndex = g_SwapChain->GetCurrentBackBufferIndex();

    g_Factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    GRS_THROW_IF_FAILED(g_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_RTVHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_RTVHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < FRAME_COUNT; i++)
    {
        GRS_THROW_IF_FAILED(g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&g_RenderTargets[i])));
        g_Device->CreateRenderTargetView(g_RenderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, g_RTVDescriptorSize);
    }

    D3D12_HEAP_PROPERTIES depthHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, g_WindowWidth, g_WindowHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    GRS_THROW_IF_FAILED(g_Device->CreateCommittedResource(
        &depthHeapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&g_DepthBuffer)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    GRS_THROW_IF_FAILED(g_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&g_DSVHeap)));
    g_Device->CreateDepthStencilView(g_DepthBuffer.Get(), nullptr, g_DSVHeap->GetCPUDescriptorHandleForHeapStart());

    CreateGBuffer();

    GRS_THROW_IF_FAILED(g_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_CommandAllocator)));
    GRS_THROW_IF_FAILED(g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        g_CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&g_CommandList)));
    GRS_THROW_IF_FAILED(g_CommandList->Close());

    GRS_THROW_IF_FAILED(g_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&g_BundleAllocatorGeometry)));
    GRS_THROW_IF_FAILED(g_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&g_BundleAllocatorLighting)));
    GRS_THROW_IF_FAILED(g_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&g_BundleAllocatorSkybox)));

    GRS_THROW_IF_FAILED(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_Fence)));
    g_FenceValue = 1;
    g_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!g_FenceEvent) throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
}

void CreateGBuffer()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    GRS_THROW_IF_FAILED(g_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_GBufferRTVHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    GRS_THROW_IF_FAILED(g_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&g_GBufferSRVHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_GBufferRTVHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(g_GBufferSRVHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < GBUFFER_COUNT; i++)
    {
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            GBUFFER_FORMATS[i], g_WindowWidth, g_WindowHeight, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = GBUFFER_FORMATS[i];
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        GRS_THROW_IF_FAILED(g_Device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&g_GBuffer[i])));

        g_Device->CreateRenderTargetView(g_GBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, g_RTVDescriptorSize);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = GBUFFER_FORMATS[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        g_Device->CreateShaderResourceView(g_GBuffer[i].Get(), &srvDesc, srvHandle);
        srvHandle.Offset(1, g_SRVDescriptorSize);
    }
}

void CreateRootSignatures()
{
    // Geometry pass root signature
    {
        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParams[1];
        rootParams[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sigBlob, errorBlob;
        GRS_THROW_IF_FAILED(D3D12SerializeVersionedRootSignature(&rootSigDesc, &sigBlob, &errorBlob));
        GRS_THROW_IF_FAILED(g_Device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
            sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_RootSignatureGeometry)));
    }

    // Lighting pass root signature
    {
        CD3DX12_DESCRIPTOR_RANGE1 ranges[4];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParams[4];
        rootParams[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[3].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sigBlob, errorBlob;
        GRS_THROW_IF_FAILED(D3D12SerializeVersionedRootSignature(&rootSigDesc, &sigBlob, &errorBlob));
        GRS_THROW_IF_FAILED(g_Device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
            sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_RootSignatureLighting)));
    }

    // Skybox root signature
    {
        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 rootParams[1];
        rootParams[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sigBlob, errorBlob;
        GRS_THROW_IF_FAILED(D3D12SerializeVersionedRootSignature(&rootSigDesc, &sigBlob, &errorBlob));
        GRS_THROW_IF_FAILED(g_Device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
            sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_RootSignatureSkybox)));
    }
}

void CreatePSOs(const WCHAR* shaderPath)
{
    UINT compileFlags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Geometry pass PSO
    {
        WCHAR shaderFile[MAX_PATH];
        StringCchPrintfW(shaderFile, MAX_PATH, L"%sShader\\Deferred\\GeometryPass.hlsl", shaderPath);

        ComPtr<ID3DBlob> vsBlob, psBlob;
        GRS_THROW_IF_FAILED(D3DCompileFromFile(shaderFile, nullptr, nullptr, "VSMain", "vs_5_0",
            compileFlags, 0, &vsBlob, nullptr));
        GRS_THROW_IF_FAILED(D3DCompileFromFile(shaderFile, nullptr, nullptr, "PSMain", "ps_5_0",
            compileFlags, 0, &psBlob, nullptr));

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        psoDesc.pRootSignature = g_RootSignatureGeometry.Get();
        psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = GBUFFER_COUNT;
        for (UINT i = 0; i < GBUFFER_COUNT; i++)
            psoDesc.RTVFormats[i] = GBUFFER_FORMATS[i];
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        GRS_THROW_IF_FAILED(g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_PSOGeometry)));
    }

    // Lighting pass PSO
    {
        WCHAR shaderFile[MAX_PATH];
        StringCchPrintfW(shaderFile, MAX_PATH, L"%sShader\\Deferred\\LightingPass.hlsl", shaderPath);

        ComPtr<ID3DBlob> vsBlob, psBlob;
        GRS_THROW_IF_FAILED(D3DCompileFromFile(shaderFile, nullptr, nullptr, "VSMain", "vs_5_0",
            compileFlags, 0, &vsBlob, nullptr));
        GRS_THROW_IF_FAILED(D3DCompileFromFile(shaderFile, nullptr, nullptr, "PSMain", "ps_5_0",
            compileFlags, 0, &psBlob, nullptr));

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        psoDesc.pRootSignature = g_RootSignatureLighting.Get();
        psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        GRS_THROW_IF_FAILED(g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_PSOLighting)));
    }

    // Skybox PSO
    {
        WCHAR shaderFile[MAX_PATH];
        StringCchPrintfW(shaderFile, MAX_PATH, L"%sShader\\Deferred\\Skybox.hlsl", shaderPath);

        ComPtr<ID3DBlob> vsBlob, psBlob;
        GRS_THROW_IF_FAILED(D3DCompileFromFile(shaderFile, nullptr, nullptr, "VSMain", "vs_5_0",
            compileFlags, 0, &vsBlob, nullptr));
        GRS_THROW_IF_FAILED(D3DCompileFromFile(shaderFile, nullptr, nullptr, "PSMain", "ps_5_0",
            compileFlags, 0, &psBlob, nullptr));

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        psoDesc.pRootSignature = g_RootSignatureSkybox.Get();
        psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        GRS_THROW_IF_FAILED(g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_PSOSkybox)));
    }
}

void CreateGeometry()
{
    ifstream fin;
    WCHAR appPath[MAX_PATH];
    GetModuleFileName(nullptr, appPath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(appPath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    char filePath[MAX_PATH];
    sprintf_s(filePath, "%lsAssets\\sphere.txt", appPath);
    fin.open(filePath);
    if (fin.fail()) throw CGRSCOMException(E_FAIL);

    char input;
    fin.get(input);
    while (input != ':') fin.get(input);
    UINT vertexCount;
    fin >> vertexCount;
    g_SphereIndexCount = vertexCount;

    fin.get(input);
    while (input != ':') fin.get(input);
    fin.get(input);
    fin.get(input);

    vector<VertexGeometry> vertices(vertexCount);
    vector<UINT> indices(vertexCount);

    for (UINT i = 0; i < vertexCount; i++)
    {
        fin >> vertices[i].Position.x >> vertices[i].Position.y >> vertices[i].Position.z;
        vertices[i].Position.w = 1.0f;
        fin >> vertices[i].UV.x >> vertices[i].UV.y;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
        indices[i] = i;
    }
    fin.close();

    const UINT vbSize = vertexCount * sizeof(VertexGeometry);
    D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    GRS_THROW_IF_FAILED(g_Device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_SphereVB)));

    void* pData;
    GRS_THROW_IF_FAILED(g_SphereVB->Map(0, nullptr, &pData));
    memcpy(pData, vertices.data(), vbSize);
    g_SphereVB->Unmap(0, nullptr);

    g_SphereVBV.BufferLocation = g_SphereVB->GetGPUVirtualAddress();
    g_SphereVBV.StrideInBytes = sizeof(VertexGeometry);
    g_SphereVBV.SizeInBytes = vbSize;

    const UINT ibSize = vertexCount * sizeof(UINT);
    D3D12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);
    GRS_THROW_IF_FAILED(g_Device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_SphereIB)));

    GRS_THROW_IF_FAILED(g_SphereIB->Map(0, nullptr, &pData));
    memcpy(pData, indices.data(), ibSize);
    g_SphereIB->Unmap(0, nullptr);

    g_SphereIBV.BufferLocation = g_SphereIB->GetGPUVirtualAddress();
    g_SphereIBV.Format = DXGI_FORMAT_R32_UINT;
    g_SphereIBV.SizeInBytes = ibSize;
}

void CreateSkyboxGeometry()
{
    VertexFullscreen vertices[] = {
        { XMFLOAT4(-1.0f, -1.0f, 1.0f, 1.0f) },
        { XMFLOAT4(-1.0f,  1.0f, 1.0f, 1.0f) },
        { XMFLOAT4( 1.0f, -1.0f, 1.0f, 1.0f) },
        { XMFLOAT4( 1.0f,  1.0f, 1.0f, 1.0f) }
    };

    const UINT vbSize = sizeof(vertices);
    D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    GRS_THROW_IF_FAILED(g_Device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_SkyboxVB)));

    void* pData;
    GRS_THROW_IF_FAILED(g_SkyboxVB->Map(0, nullptr, &pData));
    memcpy(pData, vertices, vbSize);
    g_SkyboxVB->Unmap(0, nullptr);

    g_SkyboxVBV.BufferLocation = g_SkyboxVB->GetGPUVirtualAddress();
    g_SkyboxVBV.StrideInBytes = sizeof(VertexFullscreen);
    g_SkyboxVBV.SizeInBytes = vbSize;
}

void CreateConstantBuffers()
{
    D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(GRS_UPPER(sizeof(GeometryConstants), 256));
    GRS_THROW_IF_FAILED(g_Device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_GeometryCB)));
    g_GeometryCB->Map(0, nullptr, reinterpret_cast<void**>(&g_pGeometryCBData));

    cbDesc = CD3DX12_RESOURCE_DESC::Buffer(GRS_UPPER(sizeof(LightingConstants), 256));
    GRS_THROW_IF_FAILED(g_Device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_LightingCB)));
    g_LightingCB->Map(0, nullptr, reinterpret_cast<void**>(&g_pLightingCBData));

    cbDesc = CD3DX12_RESOURCE_DESC::Buffer(GRS_UPPER(sizeof(SkyboxConstants), 256));
    GRS_THROW_IF_FAILED(g_Device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_SkyboxCB)));
    g_SkyboxCB->Map(0, nullptr, reinterpret_cast<void**>(&g_pSkyboxCBData));
}

void CreateBundleGeometry()
{
    GRS_THROW_IF_FAILED(g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
        g_BundleAllocatorGeometry.Get(), g_PSOGeometry.Get(), IID_PPV_ARGS(&g_BundleGeometry)));

    g_BundleGeometry->SetGraphicsRootSignature(g_RootSignatureGeometry.Get());
    g_BundleGeometry->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_BundleGeometry->IASetVertexBuffers(0, 1, &g_SphereVBV);
    g_BundleGeometry->IASetIndexBuffer(&g_SphereIBV);
    g_BundleGeometry->DrawIndexedInstanced(g_SphereIndexCount, 1, 0, 0, 0);

    GRS_THROW_IF_FAILED(g_BundleGeometry->Close());
}

void CreateBundleLighting()
{
    GRS_THROW_IF_FAILED(g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
        g_BundleAllocatorLighting.Get(), g_PSOLighting.Get(), IID_PPV_ARGS(&g_BundleLighting)));

    g_BundleLighting->SetGraphicsRootSignature(g_RootSignatureLighting.Get());
    g_BundleLighting->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_BundleLighting->IASetVertexBuffers(0, 1, &g_SkyboxVBV);
    g_BundleLighting->DrawInstanced(4, 1, 0, 0);

    GRS_THROW_IF_FAILED(g_BundleLighting->Close());
}

void CreateBundleSkybox()
{
    GRS_THROW_IF_FAILED(g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
        g_BundleAllocatorSkybox.Get(), g_PSOSkybox.Get(), IID_PPV_ARGS(&g_BundleSkybox)));

    g_BundleSkybox->SetGraphicsRootSignature(g_RootSignatureSkybox.Get());
    g_BundleSkybox->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_BundleSkybox->IASetVertexBuffers(0, 1, &g_SkyboxVBV);
    g_BundleSkybox->DrawInstanced(4, 1, 0, 0);

    GRS_THROW_IF_FAILED(g_BundleSkybox->Close());
}

void LoadAssets(const WCHAR* shaderPath)
{
    CreateRootSignatures();
    CreatePSOs(shaderPath);
    CreateGeometry();
    CreateSkyboxGeometry();
    CreateConstantBuffers();
}

// ============================================================================
// Render loop
// ============================================================================

void OnUpdate()
{
    float r = cosf(g_CameraPitch);
    XMFLOAT3 forward = XMFLOAT3(r * sinf(g_CameraYaw), sinf(g_CameraPitch), r * cosf(g_CameraYaw));
    XMFLOAT3 right = XMFLOAT3(cosf(g_CameraYaw), 0.0f, -sinf(g_CameraYaw));
    XMVECTOR vForward = XMLoadFloat3(&forward);
    XMVECTOR vRight = XMLoadFloat3(&right);
    XMVECTOR vUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR vEye = XMLoadFloat3(&g_CameraPos);

    float moveSpeed = 0.1f;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) moveSpeed *= 3.0f;
    if (GetAsyncKeyState('W') & 0x8000) vEye += vForward * moveSpeed;
    if (GetAsyncKeyState('S') & 0x8000) vEye -= vForward * moveSpeed;
    if (GetAsyncKeyState('D') & 0x8000) vEye += vRight * moveSpeed;
    if (GetAsyncKeyState('A') & 0x8000) vEye -= vRight * moveSpeed;
    if (GetAsyncKeyState('Q') & 0x8000) vEye += vUp * moveSpeed;
    if (GetAsyncKeyState('E') & 0x8000) vEye -= vUp * moveSpeed;
    XMStoreFloat3(&g_CameraPos, vEye);

    XMVECTOR vFocus = vEye + vForward;
    XMMATRIX view = XMMatrixLookAtLH(vEye, vFocus, vUp);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)g_WindowWidth / (float)g_WindowHeight, 0.1f, 1000.0f);
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX worldView = XMMatrixMultiply(world, view);
    XMMATRIX worldViewProj = XMMatrixMultiply(world, viewProj);

    GeometryConstants geoConsts;
    XMStoreFloat4x4(&geoConsts.WorldViewProj, XMMatrixTranspose(worldViewProj));
    XMStoreFloat4x4(&geoConsts.WorldView, XMMatrixTranspose(worldView));
    memcpy(g_pGeometryCBData, &geoConsts, sizeof(geoConsts));

    LightingConstants lightConsts;
    lightConsts.CameraPos = XMFLOAT4(g_CameraPos.x, g_CameraPos.y, g_CameraPos.z, 1.0f);
    lightConsts.LightCount = 3;
    for (int i = 0; i < 3; i++)
    {
        lightConsts.Lights[i] = g_SceneLights[i];
    }
    memcpy(g_pLightingCBData, &lightConsts, sizeof(lightConsts));

    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);
    SkyboxConstants skyboxConsts;
    XMStoreFloat4x4(&skyboxConsts.InvViewProj, XMMatrixTranspose(invViewProj));
    memcpy(g_pSkyboxCBData, &skyboxConsts, sizeof(skyboxConsts));
}

void OnRender()
{
    GRS_THROW_IF_FAILED(g_CommandAllocator->Reset());
    GRS_THROW_IF_FAILED(g_CommandList->Reset(g_CommandAllocator.Get(), nullptr));

    // Phase 1: Geometry Pass - Render geometry to G-Buffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE gBufferRTVs[GBUFFER_COUNT];
    for (UINT i = 0; i < GBUFFER_COUNT; i++)
    {
        gBufferRTVs[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(g_GBufferRTVHeap->GetCPUDescriptorHandleForHeapStart(), i, g_RTVDescriptorSize);
    }
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(g_DSVHeap->GetCPUDescriptorHandleForHeapStart());

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (UINT i = 0; i < GBUFFER_COUNT; i++)
    {
        g_CommandList->ClearRenderTargetView(gBufferRTVs[i], clearColor, 0, nullptr);
    }
    g_CommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    g_CommandList->OMSetRenderTargets(GBUFFER_COUNT, gBufferRTVs, FALSE, &dsvHandle);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)g_WindowWidth, (float)g_WindowHeight, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)g_WindowWidth, (LONG)g_WindowHeight };
    g_CommandList->RSSetViewports(1, &viewport);
    g_CommandList->RSSetScissorRects(1, &scissorRect);

    g_CommandList->SetGraphicsRootSignature(g_RootSignatureGeometry.Get());
    g_CommandList->SetPipelineState(g_PSOGeometry.Get());
    g_CommandList->SetGraphicsRootConstantBufferView(0, g_GeometryCB->GetGPUVirtualAddress());
    g_CommandList->ExecuteBundle(g_BundleGeometry.Get());

    // G-Buffer state transition: RENDER_TARGET -> PIXEL_SHADER_RESOURCE
    CD3DX12_RESOURCE_BARRIER barriers[GBUFFER_COUNT];
    for (UINT i = 0; i < GBUFFER_COUNT; i++)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(g_GBuffer[i].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    g_CommandList->ResourceBarrier(GBUFFER_COUNT, barriers);

    // Phase 2: Lighting Pass - Compute lighting based on G-Buffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE backBufferRTV(
        g_RTVHeap->GetCPUDescriptorHandleForHeapStart(), g_CurrentFrameIndex, g_RTVDescriptorSize);

    CD3DX12_RESOURCE_BARRIER presentToRT = CD3DX12_RESOURCE_BARRIER::Transition(
        g_RenderTargets[g_CurrentFrameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    g_CommandList->ResourceBarrier(1, &presentToRT);

    const float bgColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    g_CommandList->ClearRenderTargetView(backBufferRTV, bgColor, 0, nullptr);

    g_CommandList->OMSetRenderTargets(1, &backBufferRTV, FALSE, nullptr);

    g_CommandList->SetGraphicsRootSignature(g_RootSignatureLighting.Get());
    g_CommandList->SetPipelineState(g_PSOLighting.Get());

    ID3D12DescriptorHeap* srvHeaps[] = { g_GBufferSRVHeap.Get() };
    g_CommandList->SetDescriptorHeaps(_countof(srvHeaps), srvHeaps);

    g_CommandList->SetGraphicsRootConstantBufferView(0, g_LightingCB->GetGPUVirtualAddress());

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(g_GBufferSRVHeap->GetGPUDescriptorHandleForHeapStart());
    g_CommandList->SetGraphicsRootDescriptorTable(1, srvHandle);
    srvHandle.Offset(1, g_SRVDescriptorSize);
    g_CommandList->SetGraphicsRootDescriptorTable(2, srvHandle);
    srvHandle.Offset(1, g_SRVDescriptorSize);
    g_CommandList->SetGraphicsRootDescriptorTable(3, srvHandle);

    g_CommandList->ExecuteBundle(g_BundleLighting.Get());

    // BackBuffer state transition: RENDER_TARGET -> PRESENT
    CD3DX12_RESOURCE_BARRIER rtToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        g_RenderTargets[g_CurrentFrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    g_CommandList->ResourceBarrier(1, &rtToPresent);

    GRS_THROW_IF_FAILED(g_CommandList->Close());
    ID3D12CommandList* ppCommandLists[] = { g_CommandList.Get() };
    g_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    GRS_THROW_IF_FAILED(g_SwapChain->Present(1, 0));

    WaitForGPU();

    g_CurrentFrameIndex = g_SwapChain->GetCurrentBackBufferIndex();

    // Reset G-Buffer state for next frame
    GRS_THROW_IF_FAILED(g_CommandAllocator->Reset());
    GRS_THROW_IF_FAILED(g_CommandList->Reset(g_CommandAllocator.Get(), nullptr));
    for (UINT i = 0; i < GBUFFER_COUNT; i++)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(g_GBuffer[i].Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    g_CommandList->ResourceBarrier(GBUFFER_COUNT, barriers);
    GRS_THROW_IF_FAILED(g_CommandList->Close());
    g_CommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(g_CommandList.GetAddressOf()));
    WaitForGPU();
}

void WaitForGPU()
{
    GRS_THROW_IF_FAILED(g_CommandQueue->Signal(g_Fence.Get(), g_FenceValue));
    GRS_THROW_IF_FAILED(g_Fence->SetEventOnCompletion(g_FenceValue, g_FenceEvent));
    WaitForSingleObject(g_FenceEvent, INFINITE);
    g_FenceValue++;
}

void Cleanup()
{
    WaitForGPU();
    CloseHandle(g_FenceEvent);
}

// ============================================================================
// Window message handling
// ============================================================================

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_RBUTTONDOWN:
        g_RightMouseDown = true;
        g_LastMousePos.x = LOWORD(lParam);
        g_LastMousePos.y = HIWORD(lParam);
        SetCapture(hWnd);
        ShowCursor(FALSE);
        return 0;

    case WM_RBUTTONUP:
        g_RightMouseDown = false;
        ReleaseCapture();
        ShowCursor(TRUE);
        return 0;

    case WM_MOUSEMOVE:
        if (g_RightMouseDown)
        {
            int xPos = (short)LOWORD(lParam);
            int yPos = (short)HIWORD(lParam);
            int dx = xPos - g_LastMousePos.x;
            int dy = yPos - g_LastMousePos.y;

            float sensitivity = 0.005f;
            g_CameraYaw += dx * sensitivity;
            g_CameraPitch -= dy * sensitivity;
            g_CameraPitch = max(-XM_PIDIV2 + 0.1f, min(XM_PIDIV2 - 0.1f, g_CameraPitch));

            g_LastMousePos.x = xPos;
            g_LastMousePos.y = yPos;
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_TAB)
        {
            g_CameraPos = XMFLOAT3(0.0f, 2.0f, -8.0f);
            g_CameraYaw = 0.0f;
            g_CameraPitch = 0.0f;
        }
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
