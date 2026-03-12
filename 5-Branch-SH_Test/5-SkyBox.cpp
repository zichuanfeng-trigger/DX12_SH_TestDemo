#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN // �� Windows ͷ���ų�����ʹ�õ�����
#include <windows.h>
#include <tchar.h>
#include <fstream>  //for ifstream
#include <strsafe.h>
#include <atlconv.h> //for T2A
#include <atlcoll.h>
#include <wrl.h> //����WTL֧�� ����ʹ��COM
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3d12.h>//for d3d12
#include <d3dcompiler.h>
#if defined(_DEBUG)
#include <dxgidebug.h>
#endif
#include <wincodec.h> //for WIC

#include "..\WindowsCommons\DDSTextureLoader12.h"

// Deferred Renderer Constants
static constexpr UINT GBUFFER_COUNT = 3;
static constexpr DXGI_FORMAT GBUFFER_FORMATS[GBUFFER_COUNT] = {
    DXGI_FORMAT_R8G8B8A8_UNORM,       // RT0: Albedo
    DXGI_FORMAT_R16G16B16A16_FLOAT,   // RT1: Normal (View Space)
    DXGI_FORMAT_R32G32B32A32_FLOAT    // RT2: Position (View Space)
};

// Light structure for deferred rendering
struct PointLight
{
    XMFLOAT3 Position;
    float Radius;
    XMFLOAT3 Color;
    float Intensity;
};

// Lighting constants for deferred rendering
struct LightingConstants
{
    XMFLOAT4 CameraPos;
    PointLight Lights[4];
    UINT LightCount;
    float Pad[3];
};

using namespace std;
using namespace Microsoft;
using namespace Microsoft::WRL;
using namespace DirectX;

//linker
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef GRS_BLOCK

#define GRS_WND_CLASS_NAME _T("GRS Game Window Class")
#define GRS_WND_TITLE	_T("GRS DirectX12 SkyBox Sample")

#define GRS_THROW_IF_FAILED(hr) {HRESULT _hr = (hr);if (FAILED(_hr)){ throw CGRSCOMException(_hr); }}

//������ȡ������
#define GRS_UPPER_DIV(A,B) ((UINT)(((A)+((B)-1))/(B)))

//���������ϱ߽�����㷨 �ڴ�����г��� ���ס
#define GRS_UPPER(A,B) ((UINT)(((A)+((B)-1))&~(B - 1)))

// �ڴ����ĺ궨��
#define GRS_ALLOC(sz)		::HeapAlloc(GetProcessHeap(),0,(sz))
#define GRS_CALLOC(sz)		::HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(sz))
#define GRS_CREALLOC(p,sz)	::HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(p),(sz))
#define GRS_SAFE_FREE(p)		if( nullptr != (p) ){ ::HeapFree( ::GetProcessHeap(),0,(p) ); (p) = nullptr; }

class CGRSCOMException
{
public:
	CGRSCOMException(HRESULT hr) : m_hrError(hr)
	{
	}
	HRESULT Error() const
	{
		return m_hrError;
	}
private:
	const HRESULT m_hrError;
};

struct WICTranslate
{
	GUID wic;
	DXGI_FORMAT format;
};

static WICTranslate g_WICFormats[] =
{//WIC��ʽ��DXGI���ظ�ʽ�Ķ�Ӧ�����ñ��еĸ�ʽΪ��֧�ֵĸ�ʽ
	{ GUID_WICPixelFormat128bppRGBAFloat,       DXGI_FORMAT_R32G32B32A32_FLOAT },

	{ GUID_WICPixelFormat64bppRGBAHalf,         DXGI_FORMAT_R16G16B16A16_FLOAT },
	{ GUID_WICPixelFormat64bppRGBA,             DXGI_FORMAT_R16G16B16A16_UNORM },

	{ GUID_WICPixelFormat32bppRGBA,             DXGI_FORMAT_R8G8B8A8_UNORM },
	{ GUID_WICPixelFormat32bppBGRA,             DXGI_FORMAT_B8G8R8A8_UNORM }, // DXGI 1.1
	{ GUID_WICPixelFormat32bppBGR,              DXGI_FORMAT_B8G8R8X8_UNORM }, // DXGI 1.1

	{ GUID_WICPixelFormat32bppRGBA1010102XR,    DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM }, // DXGI 1.1
	{ GUID_WICPixelFormat32bppRGBA1010102,      DXGI_FORMAT_R10G10B10A2_UNORM },

	{ GUID_WICPixelFormat16bppBGRA5551,         DXGI_FORMAT_B5G5R5A1_UNORM },
	{ GUID_WICPixelFormat16bppBGR565,           DXGI_FORMAT_B5G6R5_UNORM },

	{ GUID_WICPixelFormat32bppGrayFloat,        DXGI_FORMAT_R32_FLOAT },
	{ GUID_WICPixelFormat16bppGrayHalf,         DXGI_FORMAT_R16_FLOAT },
	{ GUID_WICPixelFormat16bppGray,             DXGI_FORMAT_R16_UNORM },
	{ GUID_WICPixelFormat8bppGray,              DXGI_FORMAT_R8_UNORM },

	{ GUID_WICPixelFormat8bppAlpha,             DXGI_FORMAT_A8_UNORM },
};

// WIC ���ظ�ʽת����.
struct WICConvert
{
	GUID source;
	GUID target;
};

static WICConvert g_WICConvert[] =
{
	// Ŀ���ʽһ������ӽ��ı�֧�ֵĸ�ʽ
	{ GUID_WICPixelFormatBlackWhite,            GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM

	{ GUID_WICPixelFormat1bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat2bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat4bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat8bppIndexed,           GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM

	{ GUID_WICPixelFormat2bppGray,              GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM
	{ GUID_WICPixelFormat4bppGray,              GUID_WICPixelFormat8bppGray }, // DXGI_FORMAT_R8_UNORM

	{ GUID_WICPixelFormat16bppGrayFixedPoint,   GUID_WICPixelFormat16bppGrayHalf }, // DXGI_FORMAT_R16_FLOAT
	{ GUID_WICPixelFormat32bppGrayFixedPoint,   GUID_WICPixelFormat32bppGrayFloat }, // DXGI_FORMAT_R32_FLOAT

	{ GUID_WICPixelFormat16bppBGR555,           GUID_WICPixelFormat16bppBGRA5551 }, // DXGI_FORMAT_B5G5R5A1_UNORM

	{ GUID_WICPixelFormat32bppBGR101010,        GUID_WICPixelFormat32bppRGBA1010102 }, // DXGI_FORMAT_R10G10B10A2_UNORM

	{ GUID_WICPixelFormat24bppBGR,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat24bppRGB,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat32bppPBGRA,            GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat32bppPRGBA,            GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM

	{ GUID_WICPixelFormat48bppRGB,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat48bppBGR,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppBGRA,             GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPRGBA,            GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPBGRA,            GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM

	{ GUID_WICPixelFormat48bppRGBFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat48bppBGRFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat64bppRGBAFixedPoint,   GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat64bppBGRAFixedPoint,   GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat64bppRGBFixedPoint,    GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat48bppRGBHalf,          GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
	{ GUID_WICPixelFormat64bppRGBHalf,          GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT

	{ GUID_WICPixelFormat128bppPRGBAFloat,      GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	{ GUID_WICPixelFormat128bppRGBFloat,        GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	{ GUID_WICPixelFormat128bppRGBAFixedPoint,  GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	{ GUID_WICPixelFormat128bppRGBFixedPoint,   GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT
	{ GUID_WICPixelFormat32bppRGBE,             GUID_WICPixelFormat128bppRGBAFloat }, // DXGI_FORMAT_R32G32B32A32_FLOAT

	{ GUID_WICPixelFormat32bppCMYK,             GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat64bppCMYK,             GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat40bppCMYKAlpha,        GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat80bppCMYKAlpha,        GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM

	{ GUID_WICPixelFormat32bppRGB,              GUID_WICPixelFormat32bppRGBA }, // DXGI_FORMAT_R8G8B8A8_UNORM
	{ GUID_WICPixelFormat64bppRGB,              GUID_WICPixelFormat64bppRGBA }, // DXGI_FORMAT_R16G16B16A16_UNORM
	{ GUID_WICPixelFormat64bppPRGBAHalf,        GUID_WICPixelFormat64bppRGBAHalf }, // DXGI_FORMAT_R16G16B16A16_FLOAT
};

bool GetTargetPixelFormat(const GUID* pSourceFormat, GUID* pTargetFormat)
{//���ȷ�����ݵ���ӽ���ʽ���ĸ�
	*pTargetFormat = *pSourceFormat;
	for (size_t i = 0; i < _countof(g_WICConvert); ++i)
	{
		if (InlineIsEqualGUID(g_WICConvert[i].source, *pSourceFormat))
		{
			*pTargetFormat = g_WICConvert[i].target;
			return true;
		}
	}
	return false;
}

DXGI_FORMAT GetDXGIFormatFromPixelFormat(const GUID* pPixelFormat)
{//���ȷ�����ն�Ӧ��DXGI��ʽ����һ��
	for (size_t i = 0; i < _countof(g_WICFormats); ++i)
	{
		if (InlineIsEqualGUID(g_WICFormats[i].wic, *pPixelFormat))
		{
			return g_WICFormats[i].format;
		}
	}
	return DXGI_FORMAT_UNKNOWN;
}



#endif // !GRS_BLOCK

struct ST_GRS_VERTEX
{
	XMFLOAT4 m_v4Position;	//Position
	XMFLOAT2 m_vTex;		//Texcoord
	XMFLOAT3 m_vNor;		//Normal
};

struct ST_GRS_SKYBOX_VERTEX
{
public: // ��ʽ������������Ȩ�ޣ���ʵ struct Ĭ�Ͼ��� public��д������Ϊ��������

	// ������꣬�����õ�4ά
	XMFLOAT4 m_v4Position;

	ST_GRS_SKYBOX_VERTEX()
		: m_v4Position() // ���� XMFLOAT4 ��Ĭ�Ϲ��죬����
	{
	} 

	ST_GRS_SKYBOX_VERTEX(float x, float y, float z)
		: m_v4Position(x, y, z, 1.0f) // �� w ������Ϊ 1.0f
	{
	}
	// �� 3D ͼ��ѧ�У�'��'�� w ���������� 1.0��'����'�� w ����ͨ���� 0.0��
	// ��պеĶ�����λ�õ㣬��������Ϊ 1.0f��

	// ���ظ�ֵ�����
	ST_GRS_SKYBOX_VERTEX& operator = (const ST_GRS_SKYBOX_VERTEX& vt)
	{
		// �Ѵ������ (vt) ��λ�����ݣ��������Լ� (m_v4Position)
		m_v4Position = vt.m_v4Position;
		return *this; 
	}
};

struct ST_GRS_FRAME_MVP_BUFFER
{
	XMFLOAT4X4 m_MVP;
	XMFLOAT4X4 m_mWorld;
	XMFLOAT4   m_v4EyePos;
};

UINT g_nCurrentSamplerNO = 1;
UINT g_nSampleMaxCnt = 5;

XMFLOAT3 g_f3EyePos = XMFLOAT3(0.0f, 2.0f, -8.0f);
XMFLOAT3 g_f3LockAt = XMFLOAT3(0.0f, 0.0f, 1.0f);
XMFLOAT3 g_f3HeapUp = XMFLOAT3(0.0f, 1.0f, 0.0f);

float g_fYaw = 0.0f;
float g_fPitch = 0.0f;

double g_fPalstance = 10.0f * XM_PI / 180.0f;

XMFLOAT3 g_SpherePos = XMFLOAT3(0.0f, 0.0f, 0.0f);

POINT g_LastMousePos = { 0, 0 };
bool  g_bRightMouseDown = false;

// Deferred Rendering - Scene Lights
PointLight g_SceneLights[3] = {
    { XMFLOAT3(-3.0f, 3.0f, 3.0f), 10.0f, XMFLOAT3(1.0f, 0.3f, 0.3f), 2.0f },
    { XMFLOAT3(3.0f, 3.0f, 3.0f), 10.0f, XMFLOAT3(0.3f, 0.3f, 1.0f), 2.0f },
    { XMFLOAT3(0.0f, 5.0f, -3.0f), 15.0f, XMFLOAT3(1.0f, 1.0f, 0.8f), 1.5f }
};

LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR    lpCmdLine, int nCmdShow)
{
	::CoInitialize(nullptr);  //for WIC & COM

	const UINT							nFrameBackBufCount = 3u;
	int									iWndWidth = 1024;
	int									iWndHeight = 768;
	UINT								nCurrentFrameIndex = 0;
	UINT								nDXGIFactoryFlags = 0U;
	

	HWND								hWnd = nullptr;
	MSG									msg = {};
	TCHAR								pszAppPath[MAX_PATH] = {};

	ST_GRS_FRAME_MVP_BUFFER*			pMVPBufEarth = nullptr;

	ST_GRS_FRAME_MVP_BUFFER*			pMVPBufSkybox = nullptr;
	//������������С�϶��뵽256Bytes�߽�
	SIZE_T								szMVPBuf = GRS_UPPER(sizeof(ST_GRS_FRAME_MVP_BUFFER), 256);

	float								fSphereSize = 1.0f;

	D3D12_VERTEX_BUFFER_VIEW			stVBVEarth = {};
	D3D12_INDEX_BUFFER_VIEW				stIBVEarth = {};

	D3D12_VERTEX_BUFFER_VIEW			stVBVSkybox = {};
	D3D12_INDEX_BUFFER_VIEW				stIBVSkybox = {};

	UINT64								n64FenceValue = 0ui64;
	HANDLE								hEventFence = nullptr;

	UINT								nTxtWEarth = 0u;
	UINT								nTxtHEarth = 0u;
	UINT								nTxtWSkybox = 0u;
	UINT								nTxtHSkybox = 0u;
	UINT								nBPPEarth = 0u;
	UINT								nBPPSkybox = 0u;
	UINT								nRowPitchEarth = 0;
	UINT								nRowPitchSkybox = 0;
	UINT64								n64szUploadBufEarth = 0;
	UINT64								n64szUploadBufSkybox = 0;

	DXGI_FORMAT							emRTFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT							emDSFormat = DXGI_FORMAT_D32_FLOAT;// DXGI_FORMAT_D32_FLOAT
	DXGI_FORMAT							emTxtFmtEarth = DXGI_FORMAT_UNKNOWN;
	const float							faClearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT	stTxtLayoutsEarth = {};
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT	stTxtLayoutsSkybox = {};
	D3D12_RESOURCE_DESC					stTextureDesc = {};
	D3D12_RESOURCE_DESC					stDestDesc = {};

	UINT								nRTVDescriptorSize = 0;
	UINT								nSRVDescriptorSize = 0;
	UINT								nSamplerDescriptorSize = 0; //��������С
	

	D3D12_VIEWPORT						stViewPort = { 0.0f, 0.0f, static_cast<float>(iWndWidth), static_cast<float>(iWndHeight), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
	D3D12_RECT							stScissorRect = { 0, 0, static_cast<LONG>(iWndWidth), static_cast<LONG>(iWndHeight) };

	//�������������
	ST_GRS_VERTEX*						pstSphereVertices = nullptr;
	UINT								nSphereVertexCnt = 0;
	UINT*								pSphereIndices = nullptr;
	UINT								nSphereIndexCnt = 0;

	//Sky Box����������
	UINT								nSkyboxIndexCnt = 0;

	
	//����Skybox��Cube Map��Ҫ�ı���
	std::unique_ptr<uint8_t[]>			ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> arSubResources;
	DDS_ALPHA_MODE						emAlphaMode = DDS_ALPHA_MODE_UNKNOWN;
	bool								bIsCube = false;
	

	ComPtr<IDXGIFactory5>				pIDXGIFactory5;
	ComPtr<IDXGIAdapter1>				pIAdapter1;

	ComPtr<ID3D12Device4>				pID3D12Device4;
	ComPtr<ID3D12CommandQueue>			pICMDQueue;

	ComPtr<ID3D12CommandAllocator>		pICmdAllocDirect;
	ComPtr<ID3D12CommandAllocator>		pICmdAllocSkybox;
	ComPtr<ID3D12CommandAllocator>		pICmdAllocEarth;
	ComPtr<ID3D12GraphicsCommandList>	pICmdListDirect;
	ComPtr<ID3D12GraphicsCommandList>   pIBundlesSkybox;
	ComPtr<ID3D12GraphicsCommandList>   pIBundlesEarth;

	ComPtr<IDXGISwapChain1>				pISwapChain1;
	ComPtr<IDXGISwapChain3>				pISwapChain3;
	ComPtr<ID3D12Resource>				pIARenderTargets[nFrameBackBufCount];
	ComPtr<ID3D12DescriptorHeap>		pIRTVHeap;
	ComPtr<ID3D12DescriptorHeap>		pIDSVHeap;			//��Ȼ�����������
	ComPtr<ID3D12Resource>				pIDepthStencilBuffer; //������建����

	ComPtr<ID3D12Heap>					pIRESHeapEarth;
	ComPtr<ID3D12Heap>					pIUploadHeapEarth;
	ComPtr<ID3D12Heap>					pIRESHeapSkybox;
	ComPtr<ID3D12Heap>					pIUploadHeapSkybox;

	ComPtr<ID3D12Resource>				pITextureEarth;
	ComPtr<ID3D12Resource>				pITextureUploadEarth;
	ComPtr<ID3D12Resource>			    pICBUploadEarth;
	ComPtr<ID3D12Resource>				pIVBEarth;
	ComPtr<ID3D12Resource>				pIIBEarth;

	ComPtr<ID3D12Resource>				pITextureSkybox;
	ComPtr<ID3D12Resource>				pITextureUploadSkybox;
	ComPtr<ID3D12Resource>			    pICBUploadSkybox;
	ComPtr<ID3D12Resource>				pIVBSkybox;

	ComPtr<ID3D12DescriptorHeap>		pISRVHpEarth;
	ComPtr<ID3D12DescriptorHeap>		pISampleHpEarth;
	ComPtr<ID3D12DescriptorHeap>		pISRVHpSkybox;
	ComPtr<ID3D12DescriptorHeap>		pISampleHpSkybox;

	ComPtr<ID3D12Fence>					pIFence;

	ComPtr<ID3D12RootSignature>			pIRootSignature;
	ComPtr<ID3D12PipelineState>			pIPSOEarth;
	ComPtr<ID3D12PipelineState>			pIPSOSkyBox;

	// Deferred Rendering - G-Buffer Resources
	ComPtr<ID3D12Resource>				pIGBuffer[GBUFFER_COUNT];
	ComPtr<ID3D12DescriptorHeap>		pIGBufferRTVHeap;
	ComPtr<ID3D12DescriptorHeap>		pIGBufferSRVHeap;
	ComPtr<ID3D12CommandAllocator>		pICmdAllocLighting;
	ComPtr<ID3D12GraphicsCommandList>	pIBundlesLighting;
	ComPtr<ID3D12Resource>				pICBUploadLighting;
	LightingConstants*					pLightingCBData = nullptr;
	ComPtr<ID3D12RootSignature>			pIRootSignatureLighting;
	ComPtr<ID3D12PipelineState>			pIPSOLighting;

	ComPtr<IWICImagingFactory>			pIWICFactory;
	ComPtr<IWICBitmapDecoder>			pIWICDecoder;
	ComPtr<IWICBitmapFrameDecode>		pIWICFrame;
	ComPtr<IWICBitmapSource>			pIBMPEarth;

	

	try
	{
			// �õ���ǰ�Ĺ���Ŀ¼����������ʹ�����·�������ʸ�����Դ�ļ�
			{
				if (0 == ::GetModuleFileName(nullptr, pszAppPath, MAX_PATH))
				{
					GRS_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
				}

				WCHAR* lastSlash = _tcsrchr(pszAppPath, _T('\\'));
				if (lastSlash)
				{//ɾ��Exe�ļ���
					*(lastSlash) = _T('\0');
				}

				lastSlash = _tcsrchr(pszAppPath, _T('\\'));
				if (lastSlash)
				{//ɾ��x64·��
					*(lastSlash) = _T('\0');
				}

				lastSlash = _tcsrchr(pszAppPath, _T('\\'));
				if (lastSlash)
				{//ɾ��Debug·��
					*(lastSlash + 1) = _T('\0');
				}
			}

		// ��������
		{
			WNDCLASSEX wcex = {};
			wcex.cbSize = sizeof(WNDCLASSEX);
			wcex.style = CS_GLOBALCLASS;
			wcex.lpfnWndProc = WndProc;
			wcex.cbClsExtra = 0;
			wcex.cbWndExtra = 0;
			wcex.hInstance = hInstance;
			wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);		//��ֹ���ĵı����ػ�
			wcex.lpszClassName = GRS_WND_CLASS_NAME;
			RegisterClassEx(&wcex);

			DWORD dwWndStyle = WS_OVERLAPPED | WS_SYSMENU;
			RECT rtWnd = { 0, 0, iWndWidth, iWndHeight };
			AdjustWindowRect(&rtWnd, dwWndStyle, FALSE);

			// ���㴰�ھ��е���Ļ����
			INT posX = (GetSystemMetrics(SM_CXSCREEN) - rtWnd.right - rtWnd.left) / 2;
			INT posY = (GetSystemMetrics(SM_CYSCREEN) - rtWnd.bottom - rtWnd.top) / 2;

			hWnd = CreateWindowW(GRS_WND_CLASS_NAME
				, GRS_WND_TITLE
				, dwWndStyle
				, posX
				, posY
				, rtWnd.right - rtWnd.left
				, rtWnd.bottom - rtWnd.top
				, nullptr
				, nullptr
				, hInstance
				, nullptr);

			if (!hWnd)
			{
				return FALSE;
			}
		}

		// ʹ��WIC����ͼƬ����ת��ΪDXGI���ݵĸ�ʽ
		{
			ComPtr<IWICFormatConverter> pIConverter;
			ComPtr<IWICComponentInfo> pIWICmntinfo;
			WICPixelFormatGUID wpf = {};
			GUID tgFormat = {};
			WICComponentType type;
			ComPtr<IWICPixelFormatInfo> pIWICPixelinfo;

			
			//ʹ�ô�COM��ʽ����WIC�೧����Ҳ�ǵ���WIC��һ��Ҫ��������
			GRS_THROW_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pIWICFactory)));

			//ʹ��WIC�೧����ӿڼ�������ͼƬ�����õ�һ��WIC����������ӿڣ�ͼƬ��Ϣ��������ӿڴ����Ķ�������
			WCHAR pszTexcuteFileName[MAX_PATH] = {};
			StringCchPrintfW(pszTexcuteFileName, MAX_PATH, _T("%sAssets\\����.jpg"), pszAppPath);

			GRS_THROW_IF_FAILED(pIWICFactory->CreateDecoderFromFilename(
				pszTexcuteFileName,              // �ļ���
				nullptr,                            // ��ָ����������ʹ��Ĭ��
				GENERIC_READ,                    // ����Ȩ��
				WICDecodeMetadataCacheOnDemand,  // ����Ҫ�ͻ������� 
				&pIWICDecoder                    // ����������
			));
			// ��ȡ��һ֡ͼƬ(��ΪGIF�ȸ�ʽ�ļ����ܻ��ж�֡ͼƬ�������ĸ�ʽһ��ֻ��һ֡ͼƬ)
			// ʵ�ʽ���������������λͼ��ʽ����
			GRS_THROW_IF_FAILED(pIWICDecoder->GetFrame(0, &pIWICFrame));
			//��ȡWICͼƬ��ʽ
			GRS_THROW_IF_FAILED(pIWICFrame->GetPixelFormat(&wpf));
			//ͨ����һ��ת��֮���ȡDXGI�ĵȼ۸�ʽ
			if (GetTargetPixelFormat(&wpf, &tgFormat))//Ѱ���ʺϵĸ�ʽ��wpf�������ʺϾͲ��ܡ���Ҫת��Ϊ�ʺϵľ͸�ֵ��tgFormat
			{
				emTxtFmtEarth = GetDXGIFormatFromPixelFormat(&tgFormat);
			}

			if (DXGI_FORMAT_UNKNOWN == emTxtFmtEarth)
			{// ��֧�ֵ�ͼƬ��ʽ Ŀǰ�˳����� 
			 // һ�� ��ʵ�ʵ����浱�ж����ṩ������ʽת�����ߣ�
			 // ͼƬ����Ҫ��ǰת���ã����Բ�����ֲ�֧�ֵ�����
				throw CGRSCOMException(S_FALSE);
			}


			//��ʽת��
			if (!InlineIsEqualGUID(wpf, tgFormat))
			{// ����жϺ���Ҫ�����ԭWIC��ʽ����ֱ����ת��ΪDXGI��ʽ��ͼƬʱ
			 // ������Ҫ���ľ���ת��ͼƬ��ʽλ�ʺϵĸ�ʽ��Ϊ�ܹ�ֱ�Ӷ�ӦDXGI��ʽ����ʽ��׼��
				//����ͼƬ��ʽת����
				GRS_THROW_IF_FAILED(pIWICFactory->CreateFormatConverter(&pIConverter));
				//��ʼ��һ��ͼƬת������ʵ��Ҳ���ǽ�ͼƬ���ݽ����˸�ʽת��
				GRS_THROW_IF_FAILED(pIConverter->Initialize(//����ת����
					pIWICFrame.Get(),                // ����ԭͼƬ����
					tgFormat,						 // ָ����ת����Ŀ���ʽ
					WICBitmapDitherTypeNone,         // ָ��λͼ�Ƿ��е�ɫ�壬�ִ��������λͼ�����õ�ɫ�壬����ΪNone
					nullptr,                            // ָ����ɫ��ָ��
					0.f,                             // ָ��Alpha��ֵ
					WICBitmapPaletteTypeCustom       // ��ɫ�����ͣ�ʵ��û��ʹ�ã�����ָ��ΪCustom
				));
				// ����QueryInterface������ö����λͼ����Դ�ӿ�
				GRS_THROW_IF_FAILED(pIConverter.As(&pIBMPEarth));
			}
			else
			{
				//ͼƬ���ݸ�ʽ����Ҫת����ֱ�ӻ�ȡ��λͼ����Դ�ӿ�
				GRS_THROW_IF_FAILED(pIWICFrame.As(&pIBMPEarth));
			}
			//���ͼƬ��С����λ�����أ�
			GRS_THROW_IF_FAILED(pIBMPEarth->GetSize(&nTxtWEarth, &nTxtHEarth));
			//��ȡͼƬ���ص�λ��С��BPP��Bits Per Pixel����Ϣ�����Լ���ͼƬ�����ݵ���ʵ��С����λ���ֽڣ�
			GRS_THROW_IF_FAILED(pIWICFactory->CreateComponentInfo(tgFormat, pIWICmntinfo.GetAddressOf()));//�����Ϣ�����������Ƶõ�ָ��pIWICmntinfo��ֻ��&����һ����ʽ


			GRS_THROW_IF_FAILED(pIWICmntinfo->GetComponentType(&type));
			if (type != WICPixelFormat)
			{
				throw CGRSCOMException(S_FALSE);
			}
			GRS_THROW_IF_FAILED(pIWICmntinfo.As(&pIWICPixelinfo));
			// ���������ڿ��Եõ�BPP�ˣ���Ҳ���ҿ��ıȽ���Ѫ�ĵط���Ϊ��BPP��Ȼ������ô�໷��
			GRS_THROW_IF_FAILED(pIWICPixelinfo->GetBitsPerPixel(&nBPPEarth));
			// ����ͼƬʵ�ʵ��д�С����λ���ֽڣ�������ʹ����һ����ȡ����������A+B-1��/B ��
			// ����������˵��΢����������,ϣ�����Ѿ���������ָ��
			nRowPitchEarth = GRS_UPPER_DIV(uint64_t(nTxtWEarth) * uint64_t(nBPPEarth), 8);
		}

		// ����ʾ��ϵͳ�ĵ���֧��
		{
#if defined(_DEBUG)
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();
				// �򿪸��ӵĵ���֧��
				nDXGIFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
#endif
		}

		// ����DXGI Factory����
		{
			GRS_THROW_IF_FAILED(CreateDXGIFactory2(nDXGIFactoryFlags, IID_PPV_ARGS(&pIDXGIFactory5)));
			
		}

		// ö�������������豸
		{//ѡ��NUMA�ܹ��Ķ���������3D�豸����,��ʱ�Ȳ�֧�ּ����ˣ���Ȼ������޸���Щ��Ϊ
			DXGI_ADAPTER_DESC1 stAdapterDesc1 = {};
			D3D12_FEATURE_DATA_ARCHITECTURE stArchitecture = {};
			for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pIDXGIFactory5->EnumAdapters1(adapterIndex, &pIAdapter1); ++adapterIndex)//��ȡö���豸����ָ��
			{
				pIAdapter1->GetDesc1(&stAdapterDesc1);//�����Ϣ����ָ�룬����stAdapterDesc1

				if (stAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{//�������������������豸
					continue;
				}

				GRS_THROW_IF_FAILED(D3D12CreateDevice(pIAdapter1.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&pID3D12Device4)));
				GRS_THROW_IF_FAILED(pID3D12Device4->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE
					, &stArchitecture, sizeof(D3D12_FEATURE_DATA_ARCHITECTURE)));

				if (!stArchitecture.UMA)
				{
					break;
				}

				pID3D12Device4.Reset();
			}

			
			if (nullptr == pID3D12Device4.Get())
			{// �����Ļ����Ͼ�Ȼû�ж��� �������˳����� 
				throw CGRSCOMException(E_FAIL);
			}

			TCHAR pszWndTitle[MAX_PATH] = {};
			GRS_THROW_IF_FAILED(pIAdapter1->GetDesc1(&stAdapterDesc1));
			::GetWindowText(hWnd, pszWndTitle, MAX_PATH);
			StringCchPrintf(pszWndTitle
				, MAX_PATH
				, _T("%s (GPU:%s)")
				, pszWndTitle
				, stAdapterDesc1.Description);
			::SetWindowText(hWnd, pszWndTitle);
			
			nRTVDescriptorSize = pID3D12Device4->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			nSRVDescriptorSize = pID3D12Device4->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			nSamplerDescriptorSize = pID3D12Device4->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		}

		// ����ֱ���������
		{
			D3D12_COMMAND_QUEUE_DESC stQueueDesc = {};
			stQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandQueue(&stQueueDesc, IID_PPV_ARGS(&pICMDQueue)));
		}

		// ����ֱ�������б��������
		{
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT
				, IID_PPV_ARGS(&pICmdAllocDirect)));//AllocatorҲ�������

			//����ֱ�������б��������Ͽ���ִ�м������е��������3Dͼ�����桢�������桢��������ȣ�
			//ע���ʼʱ��û��ʹ��PSO���󣬴�ʱ��ʵ��������б���Ȼ���Լ�¼����
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT
				, pICmdAllocDirect.Get(), nullptr, IID_PPV_ARGS(&pICmdListDirect)));
			//����3��list��һ��direct������bundles
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE
				, IID_PPV_ARGS(&pICmdAllocEarth)));
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE
				, pICmdAllocEarth.Get(), nullptr, IID_PPV_ARGS(&pIBundlesEarth)));

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE
				, IID_PPV_ARGS(&pICmdAllocSkybox)));
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE
				, pICmdAllocSkybox.Get(), nullptr, IID_PPV_ARGS(&pIBundlesSkybox)));
		}

		// ����������
		{
			DXGI_SWAP_CHAIN_DESC1 stSwapChainDesc = {};
			stSwapChainDesc.BufferCount = nFrameBackBufCount;
			stSwapChainDesc.Width = iWndWidth;
			stSwapChainDesc.Height = iWndHeight;
			stSwapChainDesc.Format = emRTFormat;
			stSwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			stSwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			stSwapChainDesc.SampleDesc.Count = 1;

			GRS_THROW_IF_FAILED(pIDXGIFactory5->CreateSwapChainForHwnd(
				pICMDQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
				hWnd,
				&stSwapChainDesc,
				nullptr,
				nullptr,
				&pISwapChain1
			));

			//ע��˴�ʹ���˸߰汾��SwapChain�ӿڵĺ���
			GRS_THROW_IF_FAILED(pISwapChain1.As(&pISwapChain3));
			nCurrentFrameIndex = pISwapChain3->GetCurrentBackBufferIndex();

			//����RTV(��ȾĿ����ͼ)��������(����ѵĺ���Ӧ������Ϊ������߹̶���СԪ�صĹ̶���С�Դ��)
			D3D12_DESCRIPTOR_HEAP_DESC stRTVHeapDesc = {};
			stRTVHeapDesc.NumDescriptors = nFrameBackBufCount;
			stRTVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			stRTVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stRTVHeapDesc, IID_PPV_ARGS(&pIRTVHeap)));
			
			//stRTVHandle����װ��һ�Ѳ�ͬ��RTV��
			D3D12_CPU_DESCRIPTOR_HANDLE stRTVHandle = { pIRTVHeap->GetCPUDescriptorHandleForHeapStart() };//RTV�������ṹ�壬����ֻ��pIRTVHeapһ��
			for (UINT i = 0; i < nFrameBackBufCount; i++)
			{//���ѭ����©����������ʵ�����Ǹ�����ı���
				GRS_THROW_IF_FAILED(pISwapChain3->GetBuffer(i, IID_PPV_ARGS(&pIARenderTargets[i])));
				pID3D12Device4->CreateRenderTargetView(pIARenderTargets[i].Get(), nullptr, stRTVHandle);
				stRTVHandle.ptr += nRTVDescriptorSize;
			}

			// �ر�ALT+ENTER���л�ȫ���Ĺ��ܣ���Ϊ����û��ʵ��OnSize�����������ȹر�
			GRS_THROW_IF_FAILED(pIDXGIFactory5->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));
		}

		// ������Ȼ��弰��Ȼ�����������
		{
			// 1. ���������
			//��������Դ��������Ȼ�������� GPU �Դ��У�CPU ����Ҫֱ�Ӷ�д��
			D3D12_HEAP_PROPERTIES stDSBufHeapDesc = {};
			stDSBufHeapDesc.Type = D3D12_HEAP_TYPE_DEFAULT;
			stDSBufHeapDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stDSBufHeapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			stDSBufHeapDesc.CreationNodeMask = 0;
			stDSBufHeapDesc.VisibleNodeMask = 0;


			// 2. ���������ͼ������
			D3D12_DEPTH_STENCIL_VIEW_DESC stDepthStencilDesc = {};
			stDepthStencilDesc.Format = emDSFormat;// ���˱�������ĸ�ʽ
			stDepthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // ����һ�� 2D ����
			stDepthStencilDesc.Flags = D3D12_DSV_FLAG_NONE; // �������־��ֻ���ȣ�


			// 3. �����Ż������ֵ 
			D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
			depthOptimizedClearValue.Format = emDSFormat;
			depthOptimizedClearValue.DepthStencil.Depth = 1.0f; // 1.0 = ����Զ��0.0 = �۾���
			depthOptimizedClearValue.DepthStencil.Stencil = 0;


			// 4. ������Դ����
			// �������š�ͼ���ĳ����������͡�
			D3D12_RESOURCE_DESC stDSResDesc = {};
			stDSResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2D ����
			stDSResDesc.Alignment = 0; // Ĭ�϶��루64KB��
			stDSResDesc.Width = iWndWidth;  // ���ȣ��������Ⱦ����/������һ�£�
			stDSResDesc.Height = iWndHeight; // �߶�
			stDSResDesc.DepthOrArraySize = 1; // ֻ��һ�㣬������������
			stDSResDesc.MipLevels = 0; // ���ͼ����Ҫ Mipmap (0 �� 1)
			stDSResDesc.Format = emDSFormat; // ��ʽ����ƥ��
			stDSResDesc.SampleDesc.Count = 1; // ������ 1 (�������� MSAA �����)
			stDSResDesc.SampleDesc.Quality = 0;
			stDSResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // ��Ӳ���Լ�������õ��ڴ��Ų�
			// ���ؼ���־��������Ϊ���ģ��Ŀ�ꡣ��������������ֻ�ܵ���ͨͼƬ�ã����ܵ���Ȼ�����
			stDSResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;


			// 5. �����ύ��Դ
			// ��һ��ͬʱ���������£�1.�����Դ�� 2.�ڶ��ϴ�����Դ��
			// ��ʵ�����͵���ˣ���ֱ�۲��������
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommittedResource(
				&stDSBufHeapDesc                // ������ (GPU Default)
				, D3D12_HEAP_FLAG_NONE
				, &stDSResDesc                   // ��Դ����
				, D3D12_RESOURCE_STATE_DEPTH_WRITE // ��ʼ״̬��׼����д�����
				, &depthOptimizedClearValue      // �Ż����ֵ (���봫��������ܻᱨ���������½�)
				, IID_PPV_ARGS(&pIDepthStencilBuffer) // �����ComPtr<ID3D12Resource> ����
			));


			// 6. ������������
			// ����һ�������ӡ���������� DSV (�����ͼ)
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.NumDescriptors = 1; // ����ֻ��Ҫ�� 1 �������ͼ
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; // ���ͣ�DSV ר�ö�
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // ����Ҫ Shader �ɼ� (DSV ���� Shader ��ֱ�Ӷ�ȡ)
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&pIDSVHeap)));

			// 7. ������ͼ
			// ������Դ(pIDepthStencilBuffer)���͡�����(pIDSVHeap)����ϵ����
			pID3D12Device4->CreateDepthStencilView(
				pIDepthStencilBuffer.Get() // ԭʼ��Դ
				, &stDepthStencilDesc        // ��ͼ����
				, pIDSVHeap->GetCPUDescriptorHandleForHeapStart() // �����ϵ�λ�� (��0�Ų�λ)
			);
		}

		// ���� SRV CBV Sample��
		{

			//���ǽ�SRV������ͼ����������ͼView����CBV����������һ������������
			D3D12_DESCRIPTOR_HEAP_DESC stSRVHeapDesc = {};
			stSRVHeapDesc.NumDescriptors = 2; //1 SRV + 1 CBV
			stSRVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			stSRVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stSRVHeapDesc, IID_PPV_ARGS(&pISRVHpEarth)));

			D3D12_DESCRIPTOR_HEAP_DESC stSamplerHeapDesc = {};
			stSamplerHeapDesc.NumDescriptors = g_nSampleMaxCnt;
			stSamplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			stSamplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stSamplerHeapDesc, IID_PPV_ARGS(&pISampleHpEarth)));


			//Skybox �� SRV CBV Sample ��
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stSRVHeapDesc, IID_PPV_ARGS(&pISRVHpSkybox)));
			stSamplerHeapDesc.NumDescriptors = 1; //��պ��Ӿ�һ��������
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stSamplerHeapDesc, IID_PPV_ARGS(&pISampleHpSkybox)));

			
		}

		// ������ǩ��
		{//��������У������Skyboxʹ����ͬ�ĸ�ǩ������Ϊ��Ⱦ��������Ҫ�Ĳ�����һ����
			D3D12_FEATURE_DATA_ROOT_SIGNATURE stFeatureData = {};
			// ����Ƿ�֧��V1.1�汾�ĸ�ǩ��
			stFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			if (FAILED(pID3D12Device4->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &stFeatureData, sizeof(stFeatureData))))
			{// 1.0�� ֱ�Ӷ��쳣�˳���
				GRS_THROW_IF_FAILED(E_NOTIMPL);
			}
			// ��GPU��ִ��SetGraphicsRootDescriptorTable�����ǲ��޸������б��е�SRV��������ǿ���ʹ��Ĭ��Rang��Ϊ:
			// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
			D3D12_DESCRIPTOR_RANGE1 stDSPRanges[3] = {};//һ����range����ζ���������ã����Ǹ���������ֻ������һ��
			// ����װ����SRV/CBV/SAMPLE

			stDSPRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			stDSPRanges[0].NumDescriptors = 1;
			stDSPRanges[0].BaseShaderRegister = 0;
			stDSPRanges[0].RegisterSpace = 0;
			stDSPRanges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
			stDSPRanges[0].OffsetInDescriptorsFromTableStart = 0;

			stDSPRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
			stDSPRanges[1].NumDescriptors = 1;
			stDSPRanges[1].BaseShaderRegister = 0;
			stDSPRanges[1].RegisterSpace = 0;
			stDSPRanges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
			stDSPRanges[1].OffsetInDescriptorsFromTableStart = 0;

			stDSPRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			stDSPRanges[2].NumDescriptors = 1;
			stDSPRanges[2].BaseShaderRegister = 0;
			stDSPRanges[2].RegisterSpace = 0;
			stDSPRanges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
			stDSPRanges[2].OffsetInDescriptorsFromTableStart = 0;

			D3D12_ROOT_PARAMETER1 stRootParameters[3] = {};

			stRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			stRootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			stRootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
			stRootParameters[0].DescriptorTable.pDescriptorRanges = &stDSPRanges[0];

			stRootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			stRootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			stRootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
			stRootParameters[1].DescriptorTable.pDescriptorRanges = &stDSPRanges[1];

			stRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			stRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			stRootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
			stRootParameters[2].DescriptorTable.pDescriptorRanges = &stDSPRanges[2];

			
			D3D12_VERSIONED_ROOT_SIGNATURE_DESC stRootSignatureDesc = {};
			stRootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
			stRootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			stRootSignatureDesc.Desc_1_1.NumParameters = _countof(stRootParameters);
			stRootSignatureDesc.Desc_1_1.pParameters = stRootParameters;
			stRootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
			stRootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;

			
			ComPtr<ID3DBlob> pISignatureBlob;
			ComPtr<ID3DBlob> pIErrorBlob;
			GRS_THROW_IF_FAILED(D3D12SerializeVersionedRootSignature(&stRootSignatureDesc
				, &pISignatureBlob
				, &pIErrorBlob));

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateRootSignature(0
				, pISignatureBlob->GetBufferPointer()
				, pISignatureBlob->GetBufferSize()
				, IID_PPV_ARGS(&pIRootSignature)));

		}

		// ����Shader������Ⱦ����״̬����
		{

#if defined(_DEBUG)
			// Enable better shader debugging with the graphics debugging tools.
			UINT nCompileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
			UINT nCompileFlags = 0;
#endif
			ComPtr<ID3DBlob>					pIVSEarth;
			ComPtr<ID3DBlob>					pIPSEarth;

			//����Ϊ�о�����ʽ	   
			nCompileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

			TCHAR pszShaderFileName[MAX_PATH] = {};
			StringCchPrintf(pszShaderFileName, MAX_PATH, _T("%sShader\\SphereSH.hlsl"), pszAppPath);	

			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszShaderFileName, nullptr, nullptr
				, "VSMain", "vs_5_0", nCompileFlags, 0, &pIVSEarth, nullptr));

			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszShaderFileName, nullptr, nullptr
				, "PSMain", "ps_5_0", nCompileFlags, 0, &pIPSEarth, nullptr));

			// ���Ƕ�������һ�����ߵĶ��壬��ĿǰShader�����ǲ�û��ʹ��
			D3D12_INPUT_ELEMENT_DESC stIALayoutEarth[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};

			// ���� graphics pipeline state object (PSO)����
			D3D12_GRAPHICS_PIPELINE_STATE_DESC stPSODesc = {};
			stPSODesc.InputLayout = { stIALayoutEarth, _countof(stIALayoutEarth) };
			stPSODesc.pRootSignature = pIRootSignature.Get();
			stPSODesc.VS.BytecodeLength = pIVSEarth->GetBufferSize();
			stPSODesc.VS.pShaderBytecode = pIVSEarth->GetBufferPointer();
			stPSODesc.PS.BytecodeLength = pIPSEarth->GetBufferSize();
			stPSODesc.PS.pShaderBytecode = pIPSEarth->GetBufferPointer();

			stPSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
			stPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

			stPSODesc.BlendState.AlphaToCoverageEnable = FALSE;
			stPSODesc.BlendState.IndependentBlendEnable = FALSE;
			stPSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

			stPSODesc.SampleMask = UINT_MAX;
			stPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			stPSODesc.NumRenderTargets = 1;
			stPSODesc.RTVFormats[0] = emRTFormat;
			stPSODesc.DSVFormat = emDSFormat;
			stPSODesc.DepthStencilState.DepthEnable = TRUE;
			stPSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;//������Ȼ���д�빦��
			stPSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;     //��Ȳ��Ժ�������ֵΪ��ͨ����Ȳ��ԣ�
			stPSODesc.DepthStencilState.StencilEnable = FALSE;//ģ�������ﱻ������
			stPSODesc.SampleDesc.Count = 1;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateGraphicsPipelineState(&stPSODesc
				, IID_PPV_ARGS(&pIPSOEarth)));//����Eateh��PSOָ��


			//�����ڶ���PSO��ר������պ�
			//����Ϊ�о�����ʽ	   
			nCompileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

			TCHAR pszSMFileSkybox[MAX_PATH] = {};

			// ע�⣺·����׼�ˣ�������ļ��������˾ͻ����ˣ���debug�����вżǵ�
			StringCchPrintf(pszSMFileSkybox, MAX_PATH, _T("%sShader\\SkyBox.hlsl"), pszAppPath);

			ComPtr<ID3DBlob>					pIVSSkybox;
			ComPtr<ID3DBlob>					pIPSSkybox;

			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszSMFileSkybox, nullptr, nullptr
				, "SkyboxVS", "vs_5_0", nCompileFlags, 0, &pIVSSkybox, nullptr));
			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszSMFileSkybox, nullptr, nullptr
				, "SkyboxPS", "ps_5_0", nCompileFlags, 0, &pIPSSkybox, nullptr));

			// ��պ���ֻ�ж���ֻ��λ�ò���
			D3D12_INPUT_ELEMENT_DESC stIALayoutSkybox[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};

			// ����Skybox��(PSO)���� 
			stPSODesc.InputLayout = { stIALayoutSkybox, _countof(stIALayoutSkybox) };
			//stPSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
			stPSODesc.DepthStencilState.DepthEnable = FALSE;//��ȱ�����
			stPSODesc.DepthStencilState.StencilEnable = FALSE;
			stPSODesc.VS.BytecodeLength = pIVSSkybox->GetBufferSize();
			stPSODesc.VS.pShaderBytecode = pIVSSkybox->GetBufferPointer();
			stPSODesc.PS.BytecodeLength = pIPSSkybox->GetBufferSize();
			stPSODesc.PS.pShaderBytecode = pIPSSkybox->GetBufferPointer();

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateGraphicsPipelineState(&stPSODesc
				, IID_PPV_ARGS(&pIPSOSkyBox)));//����PSO���������أ��Ǹ�д������߼�
		}

		// ����������Ĭ�϶ѡ��ϴ��Ѳ���������
		{
			D3D12_HEAP_DESC stTextureHeapDesc = {};//�������Ĭ�϶ѣ��㴴����ʱ��������ǿյģ���ȫ��û�Ѷ������ȥ��
			//Ϊ��ָ������ͼƬ����2����С�Ŀռ䣬����û����ϸȥ�����ˣ�ֻ��ָ����һ���㹻��Ŀռ䣬������������
			//ʵ��Ӧ����Ҳ��Ҫ�ۺϿ��Ƿ���ѵĴ�С���Ա�������ö�
			stTextureHeapDesc.SizeInBytes = GRS_UPPER(2 * nRowPitchEarth * nTxtHEarth, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
			//ָ���ѵĶ��뷽ʽ������ʹ����Ĭ�ϵ�64K�߽���룬��Ϊ������ʱ����ҪMSAA֧��
			stTextureHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			stTextureHeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;		//Ĭ�϶�����
			stTextureHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stTextureHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			//�ܾ���ȾĿ���������ܾ��������������ʵ�ʾ�ֻ�������ڷ���ͨ����
			stTextureHeapDesc.Flags = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_BUFFERS;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateHeap(&stTextureHeapDesc, IID_PPV_ARGS(&pIRESHeapEarth)));

			// ����2D����		
			stTextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			stTextureDesc.MipLevels = 1;
			stTextureDesc.Format = emTxtFmtEarth; //DXGI_FORMAT_R8G8B8A8_UNORM;
			stTextureDesc.Width = nTxtWEarth;
			stTextureDesc.Height = nTxtHEarth;
			stTextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			stTextureDesc.DepthOrArraySize = 1;
			stTextureDesc.SampleDesc.Count = 1;
			stTextureDesc.SampleDesc.Quality = 0;
			
			//ʹ�á���λ��ʽ��������������ע��������������ڲ�ʵ���Ѿ�û�д洢������ͷŵ�ʵ�ʲ����ˣ��������ܸܺ�
			//ͬʱ������������Ϸ�������CreatePlacedResource��������ͬ����������Ȼǰ�������ǲ��ڱ�ʹ�õ�ʱ�򣬲ſ���
			//���ö�
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIRESHeapEarth.Get()
				, 0
				, &stTextureDesc				//����ʹ��CD3DX12_RESOURCE_DESC::Tex2D���򻯽ṹ��ĳ�ʼ��
				, D3D12_RESOURCE_STATE_COPY_DEST//�����ʼ���趨��ʱ��˵�����Ǳ�������ȥ�Ķ���
				, nullptr
				, IID_PPV_ARGS(&pITextureEarth)));
			
			//��ȡ�ϴ�����Դ����Ĵ�С������ߴ�ͨ������ʵ��ͼƬ�ĳߴ�
			D3D12_RESOURCE_DESC stCopyDstDesc = pITextureEarth->GetDesc();
			pID3D12Device4->GetCopyableFootprints(&stCopyDstDesc, 0, 1, 0, nullptr, nullptr, nullptr, &n64szUploadBufEarth);

			
			// �����ϴ���
			D3D12_HEAP_DESC stUploadHeapDesc = {  };
			//�ߴ���Ȼ��ʵ���������ݴ�С��2����64K�߽�����С
			stUploadHeapDesc.SizeInBytes = GRS_UPPER(2 * n64szUploadBufEarth, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
			//ע���ϴ��ѿ϶���Buffer���ͣ����Բ�ָ�����뷽ʽ����Ĭ����64k�߽����
			stUploadHeapDesc.Alignment = 0;
			stUploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;		//�ϴ�������
			stUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			//�ϴ��Ѿ��ǻ��壬���԰ڷ���������
			stUploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateHeap(&stUploadHeapDesc, IID_PPV_ARGS(&pIUploadHeapEarth)));

			
			// ʹ�á���λ��ʽ�����������ϴ��������ݵĻ�����Դ
			D3D12_RESOURCE_DESC stUploadBufDesc = {};
			stUploadBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			stUploadBufDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			stUploadBufDesc.Width = n64szUploadBufEarth;
			stUploadBufDesc.Height = 1;
			stUploadBufDesc.DepthOrArraySize = 1;
			stUploadBufDesc.MipLevels = 1;
			stUploadBufDesc.Format = DXGI_FORMAT_UNKNOWN;
			stUploadBufDesc.SampleDesc.Count = 1;
			stUploadBufDesc.SampleDesc.Quality = 0;
			stUploadBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			stUploadBufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(pIUploadHeapEarth.Get()
				, 0
				, &stUploadBufDesc
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&pITextureUploadEarth)));

			// ����ͼƬ�������ϴ��ѣ�����ɵ�һ��Copy��������memcpy������֪������CPU��ɵ�
			//������Դ�����С������ʵ��ͼƬ���ݴ洢���ڴ��С
			void* pbPicData = GRS_CALLOC(n64szUploadBufEarth);
			if (nullptr == pbPicData)
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}

			//��ͼƬ�ж�ȡ�����ݣ�������ʵ�о������������ݣ��õ�pbPicData�����ͱ���
			GRS_THROW_IF_FAILED(pIBMPEarth->CopyPixels(nullptr//��Զ�ĵط���������λͼԴ�ӿڣ�WIC���һ��
				, nRowPitchEarth
				, static_cast<UINT>(nRowPitchEarth * nTxtHEarth)   //ע���������ͼƬ������ʵ�Ĵ�С�����ֵͨ��С�ڻ���Ĵ�С//ʵ����Ҫռλ��С*tex����
				, reinterpret_cast<BYTE*>(pbPicData)));//�޷�������ǿתBYTE��������������ָ��

			//{//������δ�������DX12��ʾ����ֱ��ͨ����仺�������һ���ڰ׷��������
			// //��ԭ��δ��룬Ȼ��ע�������CopyPixels���ÿ��Կ����ڰ׷���������Ч��
			//	const UINT rowPitch = nRowPitchEarth; //nTxtWEarth * 4; //static_cast<UINT>(n64szUploadBufEarth / nTxtHEarth);
			//	const UINT cellPitch = rowPitch >> 3;		// The width of a cell in the checkboard texture.
			//	const UINT cellHeight = nTxtWEarth >> 3;	// The height of a cell in the checkerboard texture.
			//	const UINT textureSize = static_cast<UINT>(n64szUploadBufEarth);
			//	UINT nTexturePixelSize = static_cast<UINT>(n64szUploadBufEarth / nTxtHEarth / nTxtWEarth);

			//	UINT8* pData = reinterpret_cast<UINT8*>(pbPicData);

			//	for (UINT n = 0; n < textureSize; n += nTexturePixelSize)
			//	{
			//		UINT x = n % rowPitch;
			//		UINT y = n / rowPitch;
			//		UINT i = x / cellPitch;
			//		UINT j = y / cellHeight;

			//		if (i % 2 == j % 2)
			//		{
			//			pData[n] = 0x00;		// R
			//			pData[n + 1] = 0x00;	// G
			//			pData[n + 2] = 0x00;	// B
			//			pData[n + 3] = 0xff;	// A
			//		}
			//		else
			//		{
			//			pData[n] = 0xff;		// R
			//			pData[n + 1] = 0xff;	// G
			//			pData[n + 2] = 0xff;	// B
			//			pData[n + 3] = 0xff;	// A
			//		}
			//	}
			//}

			//��ȡ���ϴ��ѿ����������ݵ�һЩ����ת���ߴ���Ϣ
			//���ڸ��ӵ�DDS�������Ƿǳ���Ҫ�Ĺ���

			UINT   nNumSubresources = 1u;  //����ֻ��һ��ͼƬ��������Դ����Ϊ1
			UINT   nTextureRowNum = 0u;
			UINT64 n64TextureRowSizes = 0u;
			UINT64 n64RequiredSize = 0u;

			stDestDesc = pITextureEarth->GetDesc();

			pID3D12Device4->GetCopyableFootprints(&stDestDesc//����//�����������������ȡ��Ϣ�ġ�
				, 0
				, nNumSubresources
				, 0
				, &stTxtLayoutsEarth//���в������ݡ�ÿ��ƫ�����ݵȣ���n64TextureRowSizesƫ�ƶ�����
				, &nTextureRowNum//������
				, &n64TextureRowSizes//ÿ��ʵ�����ݴ�С
				, &n64RequiredSize);//����ƫ�ƺ�ȫ���ܹ������С���������õ�ȫ�ǽ��������DX��ĺ�ϲ��������ֵ��&�����޸ķ���

			//��Ϊ�ϴ���ʵ�ʾ���CPU�������ݵ�GPU���н�
			//�������ǿ���ʹ����Ϥ��Map����������ӳ�䵽CPU�ڴ��ַ��
			//Ȼ�����ǰ��н����ݸ��Ƶ��ϴ�����
			//��Ҫע�����֮���԰��п�������ΪGPU��Դ���д�С
			//��ʵ��ͼƬ���д�С���в����,���ߵ��ڴ�߽����Ҫ���ǲ�һ����
			BYTE* pData = nullptr;
			GRS_THROW_IF_FAILED(pITextureUploadEarth->Map(0, nullptr, reinterpret_cast<void**>(&pData)));//map��ָ���ţ�����Σ�����pData������������

			BYTE* pDestSlice = reinterpret_cast<BYTE*>(pData) + stTxtLayoutsEarth.Offset;//Ŀ����Ƭ������̤������������ Offset ��Ϊ��ͨ����(��Ȼ����Offset��0)��
			BYTE* pSrcSlice = reinterpret_cast<BYTE*>(pbPicData);//����������΢ת���˸����Ͷ���
			for (UINT y = 0; y < nTextureRowNum; ++y)
			{	// Ŀ���ַ: �ϴ��ѵ���ʼ + (�к� * �Կ�Ҫ����п� 2816)
				// ע�⣺����˵��� 2816������ζ��ÿ����һ�У�ָ������� 16 �ֽڵĿ�϶��
				memcpy(pDestSlice + static_cast<SIZE_T>(stTxtLayoutsEarth.Footprint.RowPitch)* y
					// Դ��ַ: ϵͳ�ڴ����ʼ + (�к� * ��ʵ�п� 2800)
					// �����ǽ������еġ�
					, pSrcSlice + static_cast<SIZE_T>(nRowPitchEarth)* y
					// ��������: ֻ������ʵ���ݳ��� (2800)
					, nRowPitchEarth);
			}
			//ȡ��ӳ�� �����ױ��������ÿ֡�ı任��������ݣ�������������Unmap�ˣ�
			//������פ�ڴ�,������������ܣ���Ϊÿ��Map��Unmap�Ǻܺ�ʱ�Ĳ���
			//��Ϊ�������붼��64λϵͳ��Ӧ���ˣ���ַ�ռ����㹻�ģ�������ռ�ò���Ӱ��ʲô
			pITextureUploadEarth->Unmap(0, nullptr);

			//�ͷ�ͼƬ���ݣ���һ���ɾ��ĳ���Ա
			GRS_SAFE_FREE(pbPicData);
		}


		// ʹ��DDSLoader������������Skybox������
		{
			TCHAR pszSkyboxTextureFile[MAX_PATH] = {};
			StringCchPrintf(pszSkyboxTextureFile, MAX_PATH, _T("%sAssets\\Sky_cube_1024.dds"), pszAppPath);

			ID3D12Resource* pIResSkyBox = nullptr;

			// LoadDDSTextureFromFile �Ǹ����⺯����DirectXTex �����ƿ��ṩ��
			// ���ܣ���ȡ�ļ�ͷ��������ʽ������ D3D12 ��Դ���󣬲����ļ����ݵĶ��������ݶ����ڴ�
			GRS_THROW_IF_FAILED(LoadDDSTextureFromFile(
				pID3D12Device4.Get(),       // D3D12 �豸ָ�룬���ڴ�����Դ
				pszSkyboxTextureFile,       // �������ļ�·��
				&pIResSkyBox,               // [���] �����õ�������Դ�ӿ�ָ�루ͨ���� Default Heap �ϣ�
				ddsData,                    // [���] unique_ptr���������ص��ڴ��ԭʼ�ļ�����������
				arSubResources,             // [���] vector��������������Դ��Mipmap���������棩������ָ����о���Ϣ
				SIZE_MAX,                   // ����������صĴ�С��SIZE_MAX ��ʾ������
				&emAlphaMode,               // [���] ���������� Alpha ���ģʽ��Ϣ
				&bIsCube));                 // [���] ���ز���ֵ��ȷ�ϸ� DDS �Ƿ�Ϊ��������ͼ (CubeMap)

			// ����ָ�� pIResSkyBox ���ӵ� ComPtr ����ָ����й���
			// ��ʱ pITextureSkybox ָ�����Դ�� GPU Ĭ�϶��ϣ���������δ��ȷ��ʼ������Ҫ�� Upload Heap ������
			pITextureSkybox.Attach(pIResSkyBox);//�����ģ������Ŀ�ָ�룬û�о���earth�����Ĵ���

			// ��ȡ�ղŴ�����������Դ�������������ߡ���ʽ�ȣ�
			// stCopyDstDesc���������ͬ���ģ�������ͬ�������������������м����
			D3D12_RESOURCE_DESC stCopyDstDesc = pITextureSkybox->GetDesc();//nb����Դ�����������趨�õģ���ֱ�ӻ�ȡ��
			
			pID3D12Device4->GetCopyableFootprints(
				&stCopyDstDesc, 
				0, 
				static_cast<UINT>(arSubResources.size()), //   - 0, arSubResources.size(): �ӵ�0������Դ��ʼ��������������Դ
				0, 
				nullptr, 
				nullptr, 
				nullptr, 
				&n64szUploadBufSkybox);//[���] ����������ϴ��ѻ������ܴ�С���ֽڣ��������õ���skybox��

			// �����ϴ��� (Upload Heap) ������
			D3D12_HEAP_DESC stUploadHeapDesc = {};

			// ���öѵĴ�С��
			// GRS_UPPER ��һ���꣬���ڽ����ڴ���루ͨ�����뵽 64KB �� 4KB��
			// ���������� "������Ĵ�С * 2" �Ŀռ䡣
			// ���� 2 ��һ�ֱ��ص�������Buffer����ȷ�����㹻�Ŀռ䴦��������䣬����Ϊ����������������
			stUploadHeapDesc.SizeInBytes = GRS_UPPER(2 * n64szUploadBufSkybox, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// ��ϸ���öѵĲ���
			stUploadHeapDesc.Alignment = 0;// Alignment = 0 ��ʾʹ��Ĭ�϶��루��ͨ���� 64KB ���룩
			stUploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
			stUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			stUploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;// ��־��ֻ�������� Buffer (����) ���͵���Դ

			// �����ѣ��� GPU �Դ棨�����Դ棩��ʵ�ʷ����ڴ�
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateHeap(&stUploadHeapDesc, IID_PPV_ARGS(&pIUploadHeapSkybox)));//skybox���Լ����ϴ�������



			// �����ϴ����ϵ���Դ����
			D3D12_RESOURCE_DESC stUploadBufDesc = {};
			stUploadBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // �����ǻ���
			stUploadBufDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; // Ĭ�϶���(64KB)
			stUploadBufDesc.Width = n64szUploadBufSkybox; // ��һ����������ܴ�С
			stUploadBufDesc.Height = 1; // Buffer �߶ȹ̶�Ϊ 1
			stUploadBufDesc.DepthOrArraySize = 1;
			stUploadBufDesc.MipLevels = 1;
			stUploadBufDesc.Format = DXGI_FORMAT_UNKNOWN; // Buffer û�����ظ�ʽ
			stUploadBufDesc.SampleDesc.Count = 1;
			stUploadBufDesc.SampleDesc.Quality = 0;
			stUploadBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // ���Բ���
			stUploadBufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			// �����е� Upload Heap �ϴ�����Դ����
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapSkybox.Get(),            // ָ���ղŴ������ϴ���
				0,                                   // ���ڵ�ƫ�����������ͷ��ʼ
				&stUploadBufDesc,                    // ��Դ����
				D3D12_RESOURCE_STATE_GENERIC_READ,   // ��ʼ״̬��Upload Heap ������ CPU �ɶ�
				nullptr,                             // ClearValue��Buffer ����Ҫ
				IID_PPV_ARGS(&pITextureUploadSkybox) // ������ϴ���Դ�Ľӿ�ָ��
			));

			// ׼����ȡ���ӵ�����Դ������Ϣ
			UINT nFirstSubresource = 0;

			// arSubResources �� LoadDDSTextureFromFile ���������ģ���������������Դ����
			UINT nNumSubresources = static_cast<UINT>(arSubResources.size());
			
			// �ϴ�������
			D3D12_RESOURCE_DESC stUploadResDesc = pITextureUploadSkybox->GetDesc();
			
			// Ĭ�϶�����
			D3D12_RESOURCE_DESC stDefaultResDesc = pITextureSkybox->GetDesc();

			UINT64 n64RequiredSize = 0;

			// ============================================================================
			// Ϊ������Ϣ��������ڴ�
			// GetCopyableFootprints ��Ҫ������������ÿһ������Դ����Ϣ��
			// ��Ϊ����Դ������ȷ����ȡ���� Mipmap �㼶����������Ҫ��̬�����ڴ档
			// ������Ҫ����������飺
			//   1. pLayouts: ���ÿ������Դ��ƫ�ƺ� footprint (D3D12_PLACED_SUBRESOURCE_FOOTPRINT)
			//   2. pNumRows: ���ÿ������Դ������ (UINT)
			//   3. pRowSizesInBytes: ���ÿ������Դ���д�С (UINT64)
			// ============================================================================
			SIZE_T szMemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT)
				+ sizeof(UINT)
				+ sizeof(UINT64))
				* nNumSubresources; // ��������Դ����

			// GRS_CALLOC ���Զ���꣬ͨ������ HeapAlloc �����㡣
			// ������������ϵͳ����һ���ڴ棬���ҷ��ص���void*��������ָ�룩��
			void* pMem = GRS_CALLOC(static_cast<SIZE_T>(szMemToAlloc));

			if (nullptr == pMem)
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}

			// ָ�����㣺�������һ����ڴ��зָ���������ʹ��
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);//����˼�ǣ��� pMem ��������͵��ڴ��ַ��ǿ�н���Ϊһ�� D3D12_PLACED_SUBRESOURCE_FOOTPRINT �ṹ��������׵�ַ

			// ע��������footprint��С
			UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + nNumSubresources);

			// pNumRows ������ pRowSizesInBytes �������
			UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + nNumSubresources);

		
			// ��һ�ε���ֻ��Ϊ�����ܴ�С����ε�����Ϊ������������������
			pID3D12Device4->GetCopyableFootprints(
				&stDefaultResDesc,   // Ŀ����������
				nFirstSubresource,   // ��ʼ����
				nNumSubresources,    // ����Դ����
				0,                   // Buffer �е���ʼƫ�� (Base Offset)
				pLayouts,            // [���] ������Ϣ����
				pNumRows,            // [���] ��������//Ŷţ�ƣ����Ҳ�Ǹ����飬ȫ����s�ġ���Ӧ��ͬ�ĳߴ�����ȫ��ͬ��ֵ������ͬ��
				pRowSizesInBytes,    // [���] �д�С����
				&n64RequiredSize     // [���] �ܴ�С
			);

			
			BYTE* pData = nullptr;

			// 0 ��ʾ����ȡ��ֻд�룬�����Ż�����
			HRESULT hr = pITextureUploadSkybox->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			if (FAILED(hr))
			{
				return 0;
			}
			
			// ��һ��ѭ����������������Դ
			for (UINT i = 0; i < nNumSubresources; ++i)
			{
				// �����Լ�飺ȷ������Ҫ�������д�Сû����� SIZE_T �����ֵ�������һ�еĴ�С������ף�������ϵͳ�ܱ�ʾ������ڴ棩���ͱ���
				if (pRowSizesInBytes[i] > (SIZE_T)-1)
				{
					throw CGRSCOMException(E_FAIL);
				}

				// ����� pLayouts[i] ��������֮ǰ�Ѿ���������׵�ַ
				D3D12_MEMCPY_DEST stCopyDestData = {
					// 1. pData: Ŀ����ʼ��ַ = Upload Heap����ַ + ����Сͼ��ƫ����(Offset)
					pData + pLayouts[i].Offset,//pData����ַ����playouts����60���ı���

					// 2. RowPitch: Ŀ���о� (�Կ�Ҫ��Ķ�����ȣ����� 256)
					pLayouts[i].Footprint.RowPitch,//�Կ�Ҫ��Ķ���Ŀ���о࣬������һ�е��о�

					// 3. SlicePitch: Ŀ����Ƭ��С (�о� * ����)
					pLayouts[i].Footprint.RowPitch * pNumRows[i]
				};


				// �ڶ���ѭ�������������Ƭ
				// ���� Skybox (Cube Map) ����ͨ 2D ������Depth ͨ���� 1
				// ���ѭ��ò����Ϊ�˼��� 3D ����
				// ������ԭ����ע��д�� "Mipmap" ��ʵ��̫׼ȷ��Mipmap �������ѭ�� i ���Ƶ�
				// ���� z ���Ƶ��ǡ����������ĺ��
				for (UINT z = 0; z < pLayouts[i].Footprint.Depth; ++z)
				{
					// ��ʼλ�� + (��Ƭ���� * ÿһ����Ƭ�Ĵ�С)
					BYTE* pDestSlice = reinterpret_cast<BYTE*>(stCopyDestData.pData) + stCopyDestData.SlicePitch * z;//zΪ0ʱ���������Դ��ַ

					// arSubResources �� LoadDDSTextureFromFile ���ؽ�����ԭʼ����
					// ԭʼ�����ǽ��յģ�SlicePitch Ҳ�ǽ��յ�
					const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(arSubResources[i].pData) + arSubResources[i].SlicePitch * z;

					// ������ѭ���������� (Rows)����
					// pNumRows[i] ������Сͼ�ĸ߶�
					for (UINT y = 0; y < pNumRows[i]; ++y)
					{
						// memcpy(Ŀ��, Դ, ��С)
						memcpy(
							// Ŀ���ַ����ǰ��Ƭ��� + (�к� * �Դ�����о� 256)
							// ע�⣺�����õ��� stCopyDestData.RowPitch����������϶
							pDestSlice + stCopyDestData.RowPitch * y,//y�����б��

							// Դ��ַ����ǰ��Ƭ��� + (�к� * ԭʼ�����о� 128)
							// ע�⣺�����õ��� arSubResources[i].RowPitch��û�п�϶
							pSrcSlice + arSubResources[i].RowPitch * y,

							// ������С��ֻ������Ч���ݳ��� (pRowSizesInBytes[i])
							// ����ֻ�� 128 �ֽڡ�
							// �Դ���ʣ�µ� (256 - 128 = 128) �ֽ� padding �ᱣ��δ��ʼ��״̬��GPU ����ȥ������
							(SIZE_T)pRowSizesInBytes[i]
						);
					}
				}
			}
			// ����ӳ��
			pITextureUploadSkybox->Unmap(0, nullptr);

			
			// ����һ��ͨ�õ��ϴ�����ģ�塣
			// ��Ȼ����� Skybox �����������ȷ���� Texture�����Կ϶��� else��
			if (stDefaultResDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				// Buffer �����Եģ�û�ж�ά�ṹ��Ҳû�и��ӵĶ���Ҫ���оࣩ���Բ���Ҫ���С�����Ƭ��ֱ��һ�����ƹ�ȥ����

				pICmdListDirect->CopyBufferRegion(
					pITextureSkybox.Get(),        // Ŀ����Դ (Default Heap)
					0,                            // Ŀ��ƫ�� (��ͷ��ʼд)
					pITextureUploadSkybox.Get(),  // Դ��Դ (Upload Heap)
					pLayouts[0].Offset,           // Դƫ�� (ͨ�� Buffer ��һ������Դ��Offset������0)
					pLayouts[0].Footprint.Width   // ���ƵĴ�С (���� Buffer��Width �����ֽ��ܳ���)
				);
			}
			else
			{

				// ����ÿһ������Դ (6���� * Mip�ȼ���)
				for (UINT i = 0; i < nNumSubresources; ++i)
				{
					// ����Ŀ�ĵ�
					D3D12_TEXTURE_COPY_LOCATION stDstCopyLocation = {};

					// Ŀ����Դ���Ǹ������� Default Heap �ϵ� Cube Map ����
					stDstCopyLocation.pResource = pITextureSkybox.Get();
					stDstCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

					// ָ������Դ����������� 0 ���� +X ��Ĵ�ͼ���� 1 ���� +X �����ͼ...
					stDstCopyLocation.SubresourceIndex = i;

					// ����������Դ
					D3D12_TEXTURE_COPY_LOCATION stSrcCopyLocation = {};

					// �����ϴ���
					stSrcCopyLocation.pResource = pITextureUploadSkybox.Get();
					stSrcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					stSrcCopyLocation.PlacedFootprint = pLayouts[i];//����ֱ�ӷ�ÿ������Դ��Ӧ�Ĳ��ָ�ʽ��

					// ������
					pICmdListDirect->CopyTextureRegion(
						&stDstCopyLocation, // Ŀ�ĵ�����
						0, 0, 0,            // Ŀ���ڵ���ʼ���� (X, Y, Z)��ͨ�������Ͻ�(0,0,0)��ʼд
						&stSrcCopyLocation, // Դͷ����
						nullptr             // Դ�����nullptr ��ʾ�������� Footprint ���������
					);
				}
			}

			// ʹ��Barrierͬ��һ��
			D3D12_RESOURCE_BARRIER stTransResBarrier = {};
			stTransResBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stTransResBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stTransResBarrier.Transition.pResource = pITextureSkybox.Get();
			stTransResBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			stTransResBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			// ָ������Դ
			stTransResBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			// ��ָ����������б�
			pICmdListDirect->ResourceBarrier(1, &stTransResBarrier);
		}

		{}
		//��ֱ�������б��������ϴ��Ѹ����������ݵ�Ĭ�϶ѵ����ִ�в�ͬ���ȴ�������ɵڶ���Copy��������GPU�ϵĸ����������
		//ע���ʱֱ�������б���û�а�PSO���������Ҳ�ǲ���ִ��3Dͼ������ģ����ǿ���ִ�и��������Ϊ�������治��Ҫʲô
		//�����״̬����֮��Ĳ���
		//����Ŀ�������������ֱ�򵥵���ը�أ���Ϊ����earth��벿�ֵ����ݣ�ǰ��upload���ӵ�memcpy�Ѿ������ˣ������ֻ��upload����default����
		{
			D3D12_TEXTURE_COPY_LOCATION stDstCopyLocation = {};
			stDstCopyLocation.pResource = pITextureEarth.Get();
			stDstCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			stDstCopyLocation.SubresourceIndex = 0;

			D3D12_TEXTURE_COPY_LOCATION stSrcCopyLocation = {};
			stSrcCopyLocation.pResource = pITextureUploadEarth.Get();
			stSrcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			stSrcCopyLocation.PlacedFootprint = stTxtLayoutsEarth;

			pICmdListDirect->CopyTextureRegion(&stDstCopyLocation, 0, 0, 0, &stSrcCopyLocation, nullptr);

			//����һ����Դ���ϣ�ͬ����ȷ�ϸ��Ʋ������
			//ֱ��ʹ�ýṹ��Ȼ����õ���ʽ
			D3D12_RESOURCE_BARRIER stResBar = {};
			stResBar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stResBar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stResBar.Transition.pResource = pITextureEarth.Get();
			stResBar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			stResBar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			stResBar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			pICmdListDirect->ResourceBarrier(1, &stResBar);

		}

		// ִ�еڶ���Copy���ȷ�����е��������ϴ�����Ĭ�϶���
		{
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pIFence)));
			n64FenceValue = 1;
			//����һ��Eventͬ���������ڵȴ�Χ���¼�֪ͨ
			hEventFence = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (hEventFence == nullptr)
			{
				GRS_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
			}

			// ִ�������б����ȴ�������Դ�ϴ���ɣ���һ���Ǳ����
			GRS_THROW_IF_FAILED(pICmdListDirect->Close());

			ID3D12CommandList* ppCommandLists[] = { pICmdListDirect.Get() };
			pICMDQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

			// �ȴ�������Դ��ʽ���������
			const UINT64 fence = n64FenceValue;
			GRS_THROW_IF_FAILED(pICMDQueue->Signal(pIFence.Get(), fence));
			n64FenceValue++;
			GRS_THROW_IF_FAILED(pIFence->SetEventOnCompletion(fence, hEventFence));
		}

		// �����������������
		{
			ifstream fin;//����ʹ�� C++ ��׼�� ifstream ��ȡ�ı��ļ�
			char input;
			USES_CONVERSION;
			char pModuleFileName[MAX_PATH] = {};
			StringCchPrintfA(pModuleFileName, MAX_PATH, "%sAssets\\sphere.txt", T2A(pszAppPath));
			
			// ���ļ�
			fin.open(pModuleFileName);
			// �����飺����ļ�û�ҵ���򲻿�
			if (fin.fail())
			{
				throw CGRSCOMException(E_FAIL);//�쳣
			}
			// �߼�����ȡ�ַ�ֱ������ð�� ':'��ͨ������������ǩ��Vertices Count:��
			fin.get(input);
			while (input != ':')
			{
				fin.get(input);
			}
			// ��ȡð�ź������������������
			fin >> nSphereVertexCnt;
			// ����򵥴ֱ��������������ڶ�����
			nSphereIndexCnt = nSphereVertexCnt;

			// �߼����ٴζ�ȡֱ��������һ��ð�ţ�Data Start:��
			fin.get(input);
			while (input != ':')
			{
				fin.get(input);
			}
			fin.get(input);
			fin.get(input);

			//����ʹ����ǰ�涨���GRS_CALLOC�꣬������������ϵͳ����һ���ڴ棬���ҷ��ص���void*��������ָ�룩��
			//���������ţ����������ܹ���Ҫ�����ֽڣ�ǰ������ǿ������ת��
			pstSphereVertices = (ST_GRS_VERTEX*)GRS_CALLOC(nSphereVertexCnt * sizeof(ST_GRS_VERTEX));
			pSphereIndices = (UINT*)GRS_CALLOC(nSphereVertexCnt * sizeof(UINT));//�������¡����ﴴ������ָ��ķ����Ƚ��ϣ�Ҳ������ʹ���ִ���ָ������

			for (UINT i = 0; i < nSphereVertexCnt; i++)
			{
				// ���ζ�ȡ��λ�� x, y, z
				fin >> pstSphereVertices[i].m_v4Position.x
					>> pstSphereVertices[i].m_v4Position.y
					>> pstSphereVertices[i].m_v4Position.z;
				// ���� w ����Ϊ 1.0 (�������ϵҪ�󣬱�ʾ����һ����)
				pstSphereVertices[i].m_v4Position.w = 1.0f;
				// ���ζ�ȡ���������� u, v
				fin >> pstSphereVertices[i].m_vTex.x
					>> pstSphereVertices[i].m_vTex.y;
				// ���ζ�ȡ������ nx, ny, nz
				fin >> pstSphereVertices[i].m_vNor.x
					>> pstSphereVertices[i].m_vNor.y
					>> pstSphereVertices[i].m_vNor.z;
				// ����������0, 1, 2, ...
				pSphereIndices[i] = i;
			}
		}

		// �������㻺�塢�������塢��������
		{
			
			UINT64 n64BufferOffset = GRS_UPPER(n64szUploadBufEarth, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// ������Դ�����ṹ��
			D3D12_RESOURCE_DESC stBufResDesc = {};
			stBufResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // ����һ�����壬��������
			stBufResDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; // 64KB ����
			stBufResDesc.Width = nSphereVertexCnt * sizeof(ST_GRS_VERTEX); // ���� = �������ֽ���
			stBufResDesc.Height = 1;
			stBufResDesc.DepthOrArraySize = 1;
			stBufResDesc.MipLevels = 1;
			stBufResDesc.Format = DXGI_FORMAT_UNKNOWN; // Buffer ͨ������Ҫָ����ʽ
			stBufResDesc.SampleDesc.Count = 1;
			stBufResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // Buffer ������ Row Major
			stBufResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			// �������㻺��
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapEarth.Get(),         // ָ�������Ѿ�����õĶ� (Upload Heap)
				// �ڶ����ˣ�ǰ��earth��������Ҳ�õ����
				n64BufferOffset,                 // ָ�����ڵ�ƫ����
				&stBufResDesc,                   // ��Դ����
				D3D12_RESOURCE_STATE_GENERIC_READ, // ��ʼ״̬ (Upload Heap ������ Generic Read)
				nullptr,
				IID_PPV_ARGS(&pIVBEarth)));      // ����ӿ�ָ��

			// �����ϴ� (Map -> Memcpy -> Unmap)
			UINT8* pVertexDataBegin = nullptr;
			D3D12_RANGE stReadRange = { 0, 0 }; // ���ǲ������ CPU ��ȡ����Դ棬���Է�Χ��Ϊ 0

			GRS_THROW_IF_FAILED(pIVBEarth->Map(0, &stReadRange, reinterpret_cast<void**>(&pVertexDataBegin)));

			memcpy(pVertexDataBegin, pstSphereVertices, nSphereVertexCnt * sizeof(ST_GRS_VERTEX));

			pIVBEarth->Unmap(0, nullptr);

			// �ͷ� CPU �˵���ʱ�ڴ�
			GRS_SAFE_FREE(pstSphereVertices);

			// �������㻺����ͼ
			stVBVEarth.BufferLocation = pIVBEarth->GetGPUVirtualAddress(); // GPU �Դ��ַ
			stVBVEarth.StrideInBytes = sizeof(ST_GRS_VERTEX);              // ÿ������Ĳ��� (�ֽ�)
			stVBVEarth.SizeInBytes = nSphereVertexCnt * sizeof(ST_GRS_VERTEX); // �ܴ�С

			// ������������ (Index Buffer)
			n64BufferOffset = GRS_UPPER(n64BufferOffset + nSphereVertexCnt * sizeof(ST_GRS_VERTEX), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// ������Դ�����Ŀ���Ϊ��������Ĵ�С
			stBufResDesc.Width = nSphereIndexCnt * sizeof(UINT);

			// �ڶѵġ���ƫ��λ�á���������������Դ pIIBEarth
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapEarth.Get(),
				n64BufferOffset,
				&stBufResDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&pIIBEarth)));

			// �����ϴ� (Map -> Copy -> Unmap)
			UINT8* pIndexDataBegin = nullptr;
			GRS_THROW_IF_FAILED(pIIBEarth->Map(0, &stReadRange, reinterpret_cast<void**>(&pIndexDataBegin)));
			memcpy(pIndexDataBegin, pSphereIndices, nSphereIndexCnt * sizeof(UINT));
			pIIBEarth->Unmap(0, nullptr);

			GRS_SAFE_FREE(pSphereIndices);

			// ��������������ͼ
			stIBVEarth.BufferLocation = pIIBEarth->GetGPUVirtualAddress();
			stIBVEarth.Format = DXGI_FORMAT_R32_UINT; // ָ��������ʽΪ 32λ �޷�������
			stIBVEarth.SizeInBytes = nSphereIndexCnt * sizeof(UINT);

			// ������������
			// ������һ��ƫ���� (��������֮���ٴζ���)
			n64BufferOffset = GRS_UPPER(n64BufferOffset + nSphereIndexCnt * sizeof(UINT), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// ���¿���Ϊ��������Ĵ�С (szMVPBuf Ӧ���Ѿ��� 256�ֽڶ���Ĵ�С)
			stBufResDesc.Width = szMVPBuf;//�������buffer��С��������Զ���˸�ȫ�ֱ������ţ�ʲô�����Ա�̣�ֱ����st���size������

			// �ڶѵġ���ƫ��λ�á���������������Դ pICBUploadEarth
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapEarth.Get(),
				n64BufferOffset,
				&stBufResDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&pICBUploadEarth)));

			// Map ��û�� Unmap��Ҳû��memcpy����Ϊд��constant����Ⱦѭ���н���
			GRS_THROW_IF_FAILED(pICBUploadEarth->Map(0, nullptr, reinterpret_cast<void**>(&pMVPBufEarth)));
		}

		// ������պУ�Զƽ���ͣ�
		{
			//�Խ�ndc�����ͦ����˼�ģ�����ϸ��
			float fHighW = -1.0f - (1.0f / (float)iWndWidth);
			float fHighH = -1.0f - (1.0f / (float)iWndHeight);
			float fLowW = 1.0f + (1.0f / (float)iWndWidth);
			float fLowH = 1.0f + (1.0f / (float)iWndHeight);

			ST_GRS_SKYBOX_VERTEX stSkyboxVertices[4] = {};

			stSkyboxVertices[0].m_v4Position = XMFLOAT4(fLowW, fLowH, 1.0f, 1.0f);
			stSkyboxVertices[1].m_v4Position = XMFLOAT4(fLowW, fHighH, 1.0f, 1.0f);
			stSkyboxVertices[2].m_v4Position = XMFLOAT4(fHighW, fLowH, 1.0f, 1.0f);
			stSkyboxVertices[3].m_v4Position = XMFLOAT4(fHighW, fHighH, 1.0f, 1.0f);

			nSkyboxIndexCnt = 4;
			

			//������պ��ӵ�����
			D3D12_RESOURCE_DESC stBufResDesc = {};
			stBufResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			stBufResDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			stBufResDesc.Width = nSkyboxIndexCnt * sizeof(ST_GRS_SKYBOX_VERTEX);
			stBufResDesc.Height = 1;
			stBufResDesc.DepthOrArraySize = 1;
			stBufResDesc.MipLevels = 1;
			stBufResDesc.Format = DXGI_FORMAT_UNKNOWN;
			stBufResDesc.SampleDesc.Count = 1;
			stBufResDesc.SampleDesc.Quality = 0;
			stBufResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			stBufResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			UINT64 n64BufferOffset = GRS_UPPER(n64szUploadBufSkybox, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapSkybox.Get()
				, n64BufferOffset
				, &stBufResDesc
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&pIVBSkybox)));

			//ʹ��map-memcpy-unmap�󷨽����ݴ������㻺�����
			ST_GRS_SKYBOX_VERTEX* pVertexDataBegin = nullptr;

			GRS_THROW_IF_FAILED(pIVBSkybox->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin)));
			memcpy(pVertexDataBegin, stSkyboxVertices, nSkyboxIndexCnt * sizeof(ST_GRS_SKYBOX_VERTEX));
			pIVBSkybox->Unmap(0, nullptr);

			//������Դ��ͼ��ʵ�ʿ��Լ�����Ϊָ�򶥵㻺����Դ�ָ��
			stVBVSkybox.BufferLocation = pIVBSkybox->GetGPUVirtualAddress();
			stVBVSkybox.StrideInBytes = sizeof(ST_GRS_SKYBOX_VERTEX);
			stVBVSkybox.SizeInBytes = nSkyboxIndexCnt * sizeof(ST_GRS_SKYBOX_VERTEX);

			//����߽�������ȷ��ƫ��λ��
			n64BufferOffset = GRS_UPPER(n64BufferOffset + nSkyboxIndexCnt * sizeof(ST_GRS_SKYBOX_VERTEX), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// ������������ ע�⻺��ߴ�����Ϊ256�߽�����С
			stBufResDesc.Width = szMVPBuf;
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapSkybox.Get()
				, n64BufferOffset
				, &stBufResDesc
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&pICBUploadSkybox)));

			// Map ֮��Ͳ���Unmap�� ֱ�Ӹ������ݽ�ȥ ����ÿ֡������map-copy-unmap�˷�ʱ����
			GRS_THROW_IF_FAILED(pICBUploadSkybox->Map(0, nullptr, reinterpret_cast<void**>(&pMVPBufSkybox)));
		}

		// ����SRV������
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC stSRVDesc = {};
			stSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			stSRVDesc.Format = emTxtFmtEarth;
			stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			stSRVDesc.Texture2D.MipLevels = 1;
			pID3D12Device4->CreateShaderResourceView(pITextureEarth.Get(), &stSRVDesc, pISRVHpEarth->GetCPUDescriptorHandleForHeapStart());

			D3D12_RESOURCE_DESC stDescSkybox = pITextureSkybox->GetDesc();
			stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			stSRVDesc.Format = stDescSkybox.Format;
			stSRVDesc.TextureCube.MipLevels = stDescSkybox.MipLevels;
			pID3D12Device4->CreateShaderResourceView(pITextureSkybox.Get(), &stSRVDesc, pISRVHpSkybox->GetCPUDescriptorHandleForHeapStart());//�ŷ��ְ���View�Ĵ����Ѿ������ٳ���ĳ��ָ���ˣ��������������Ȼ���ֱ�Ӵ�pISRVHpSkybox��������view���ýṹ��󶨾�����
		}

		// ����CBV������
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = pICBUploadEarth->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = static_cast<UINT>(szMVPBuf);

			D3D12_CPU_DESCRIPTOR_HANDLE stSRVCBVHandle = pISRVHpEarth->GetCPUDescriptorHandleForHeapStart();//ԭ����д�ľ��ǹ��죬�������ֵ����������handle�����������
			stSRVCBVHandle.ptr += nSRVDescriptorSize;

			pID3D12Device4->CreateConstantBufferView(&cbvDesc, stSRVCBVHandle);//�Ҳ��޵��ˣ��������ⳤ�û���һ���ĺ�������Ҫ��õĶ�����ʽ����һ���£��Ҿ�˵��ô��Ҫ���cbvDesc�Ӹ��㣡

			cbvDesc.BufferLocation = pICBUploadSkybox->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = static_cast<UINT>(szMVPBuf);

			D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvHandleSkybox = { pISRVHpSkybox->GetCPUDescriptorHandleForHeapStart() };//�����ֲ�����Ŷ��view����������������ɶ��˼��ǰ׺�����ˣ��о���Щ����
			cbvSrvHandleSkybox.ptr += nSRVDescriptorSize;

			pID3D12Device4->CreateConstantBufferView(&cbvDesc, cbvSrvHandleSkybox);

		}

		// �������ֲ�����//���������ã�Ȼ��д���������������
		{
			D3D12_CPU_DESCRIPTOR_HANDLE hSamplerHeap = pISampleHpEarth->GetCPUDescriptorHandleForHeapStart();

			D3D12_SAMPLER_DESC stSamplerDesc = {};
			stSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

			stSamplerDesc.MinLOD = 0;
			stSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
			stSamplerDesc.MipLODBias = 0.0f;
			stSamplerDesc.MaxAnisotropy = 1;
			stSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;

			// Sampler 1
			stSamplerDesc.BorderColor[0] = 1.0f;
			stSamplerDesc.BorderColor[1] = 0.0f;
			stSamplerDesc.BorderColor[2] = 1.0f;
			stSamplerDesc.BorderColor[3] = 1.0f;
			stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			pID3D12Device4->CreateSampler(&stSamplerDesc, hSamplerHeap);

			hSamplerHeap.ptr += nSamplerDescriptorSize;

			// Sampler 2
			stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			pID3D12Device4->CreateSampler(&stSamplerDesc, hSamplerHeap);

			hSamplerHeap.ptr += nSamplerDescriptorSize;

			// Sampler 3
			stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			pID3D12Device4->CreateSampler(&stSamplerDesc, hSamplerHeap);

			hSamplerHeap.ptr += nSamplerDescriptorSize;

			// Sampler 4
			stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			pID3D12Device4->CreateSampler(&stSamplerDesc, hSamplerHeap);

			hSamplerHeap.ptr += nSamplerDescriptorSize;

			// Sampler 5
			stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
			stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
			stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
			pID3D12Device4->CreateSampler(&stSamplerDesc, hSamplerHeap);

			
			//����Skybox�Ĳ�����//����������
			stSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;//����һ���Թ���

			stSamplerDesc.MinLOD = 0;
			stSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
			stSamplerDesc.MipLODBias = 0.0f;
			stSamplerDesc.MaxAnisotropy = 1;
			stSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;//�������Ը�û��
			stSamplerDesc.BorderColor[0] = 0.0f;
			stSamplerDesc.BorderColor[1] = 0.0f;
			stSamplerDesc.BorderColor[2] = 0.0f;
			stSamplerDesc.BorderColor[3] = 0.0f;
			stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;//WRAP (����)

			pID3D12Device4->CreateSampler(&stSamplerDesc, pISampleHpSkybox->GetCPUDescriptorHandleForHeapStart());
			//---------------------------------------------------------------------------------------------

		}

		// ���������¼�̻�������
		{
			//����������
			pIBundlesEarth->SetGraphicsRootSignature(pIRootSignature.Get());
			pIBundlesEarth->SetPipelineState(pIPSOEarth.Get());

			ID3D12DescriptorHeap* ppHeapsEarth[] = { pISRVHpEarth.Get(),pISampleHpEarth.Get() };
			pIBundlesEarth->SetDescriptorHeaps(_countof(ppHeapsEarth), ppHeapsEarth);
			//����SRV
			pIBundlesEarth->SetGraphicsRootDescriptorTable(0, pISRVHpEarth->GetGPUDescriptorHandleForHeapStart());

			D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleEarth = pISRVHpEarth->GetGPUDescriptorHandleForHeapStart();
			stGPUCBVHandleEarth.ptr += nSRVDescriptorSize;

			//����CBV
			pIBundlesEarth->SetGraphicsRootDescriptorTable(1, stGPUCBVHandleEarth);

			D3D12_GPU_DESCRIPTOR_HANDLE hGPUSamplerEarth = pISampleHpEarth->GetGPUDescriptorHandleForHeapStart();
			hGPUSamplerEarth.ptr += (g_nCurrentSamplerNO * nSamplerDescriptorSize);

			//����Sample
			pIBundlesEarth->SetGraphicsRootDescriptorTable(2, hGPUSamplerEarth);
			//ע������ʹ�õ���Ⱦ�ַ����������б���Ҳ����ͨ����Mesh����
			pIBundlesEarth->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pIBundlesEarth->IASetVertexBuffers(0, 1, &stVBVEarth);
			pIBundlesEarth->IASetIndexBuffer(&stIBVEarth);

			//Draw Call������
			pIBundlesEarth->DrawIndexedInstanced(nSphereIndexCnt, 1, 0, 0, 0);
			pIBundlesEarth->Close();



			//Skybox�������
			pIBundlesSkybox->SetPipelineState(pIPSOSkyBox.Get());
			pIBundlesSkybox->SetGraphicsRootSignature(pIRootSignature.Get());
			ID3D12DescriptorHeap* ppHeaps[] = { pISRVHpSkybox.Get(),pISampleHpSkybox.Get() };
			pIBundlesSkybox->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			//����SRV
			pIBundlesSkybox->SetGraphicsRootDescriptorTable(0, pISRVHpSkybox->GetGPUDescriptorHandleForHeapStart());

			D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleSkybox = pISRVHpSkybox->GetGPUDescriptorHandleForHeapStart();
			stGPUCBVHandleSkybox.ptr += nSRVDescriptorSize;
			//����CBV
			pIBundlesSkybox->SetGraphicsRootDescriptorTable(1, stGPUCBVHandleSkybox);
			pIBundlesSkybox->SetGraphicsRootDescriptorTable(2, pISampleHpSkybox->GetGPUDescriptorHandleForHeapStart());
			pIBundlesSkybox->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			pIBundlesSkybox->IASetVertexBuffers(0, 1, &stVBVSkybox);

			//Draw Call������
			pIBundlesSkybox->DrawInstanced(4, 1, 0, 0);
			pIBundlesSkybox->Close();
		}

		D3D12_RESOURCE_BARRIER stBeginResBarrier = {};
		D3D12_RESOURCE_BARRIER stEndResBarrier = {};
		{
			stBeginResBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stBeginResBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stBeginResBarrier.Transition.pResource = pIARenderTargets[nCurrentFrameIndex].Get();
			stBeginResBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			stBeginResBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			stBeginResBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;


			stEndResBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stEndResBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stEndResBarrier.Transition.pResource = pIARenderTargets[nCurrentFrameIndex].Get();
			stEndResBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			stEndResBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			stEndResBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		}
		
		// ��Ⱦ��ؿ�ʼ
		// ��¼֡��ʼʱ�䣬�͵�ǰʱ�䣬��ѭ������Ϊ��
		ULONGLONG n64tmFrameStart = ::GetTickCount64();
		ULONGLONG n64tmCurrent = n64tmFrameStart;
		//������ת�Ƕ���Ҫ�ı���
		double dModelRotationYAngle = 0.0f;

		DWORD dwRet = 0;
		BOOL bExit = FALSE;

		ShowWindow(hWnd, nCmdShow);
		UpdateWindow(hWnd);
		while (!bExit)
		{//ע���������ǵ�������Ϣѭ�������ȴ�ʱ������Ϊ0��ͬʱ����ʱ�Ե���Ⱦ���ĳ���ÿ��ѭ������Ⱦ
		 //���ⲻ��ʾ˵MsgWait������ûɶ���ˣ����ʹ��������Ϊ������������������߳̿��ƾͷǳ�����
			dwRet = ::MsgWaitForMultipleObjects(1, &hEventFence, FALSE, INFINITE, QS_ALLINPUT);
			switch (dwRet - WAIT_OBJECT_0)
			{
			case 0:
			{
				//OnUpdate()
				{// ׼��һ���򵥵���תMVP���� �÷���ת����
					n64tmCurrent = ::GetTickCount();
					//������ת�ĽǶȣ���ת�Ƕ�(����) = ʱ��(��) * ���ٶ�(����/��)
					dModelRotationYAngle += ((n64tmCurrent - n64tmFrameStart) / 1000.0f) * g_fPalstance;//��λ�Ǻ��룬��֮�㶨һ��ת�١�g_fPalstance���ٶ�(����/��)

					n64tmFrameStart = n64tmCurrent;

					//��ת�Ƕ���2PI���ڵı�����ȥ����������ֻ�������0���ȿ�ʼ��С��2PI�Ļ��ȼ���
					if (dModelRotationYAngle > XM_2PI)
					{
						dModelRotationYAngle = fmod(dModelRotationYAngle, XM_2PI);//ȡ�࣬��Ҫ�ýǶȶ�̫�ർ�¾���ƫ��
					}

					// 1. ��������п��� (WASD + QE)
					
					// A. ���㵱ǰ�����߷��� (Forward) �� �ҷ��� (Right)
					// ---------------------------------------------------
					// �������ѧԭ����
					// Forward = (sinY*cosP, sinP, cosY*cosP) -> ������ת�ѿ���
					// Right   = (cosY, 0, -sinY)             -> �� Forward ��ֱ��ˮƽ����
					float r = cosf(g_fPitch);
					XMFLOAT3 f3Forward;
					f3Forward.x = r * sinf(g_fYaw);
					f3Forward.y = sinf(g_fPitch);
					f3Forward.z = r * cosf(g_fYaw);

					XMFLOAT3 f3Right;
					f3Right.x = cosf(g_fYaw);
					f3Right.y = 0.0f;
					f3Right.z = -sinf(g_fYaw);

					// �� Float3 תΪ Vector �Ա����
					XMVECTOR vForward = XMLoadFloat3(&f3Forward);
					XMVECTOR vRight = XMLoadFloat3(&f3Right);
					XMVECTOR vUpWorld = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
					XMVECTOR vEye = XMLoadFloat3(&g_f3EyePos);

					// B. ���������� (�첽������˿������)
					float moveSpeed = 0.1f; // �����ٶ�

					// ��ס Shift ����
					if (GetAsyncKeyState(VK_SHIFT) & 0x8000) moveSpeed *= 3.0f;

					// W / S : ǰ����� (�����߷���)
					if (GetAsyncKeyState('W') & 0x8000) vEye += vForward * moveSpeed;
					if (GetAsyncKeyState('S') & 0x8000) vEye -= vForward * moveSpeed;

					// A / D : ����ƽ�� (������������)
					if (GetAsyncKeyState('D') & 0x8000) vEye += vRight * moveSpeed;
					if (GetAsyncKeyState('A') & 0x8000) vEye -= vRight * moveSpeed;

					// Q / E : ��ֱ���� (������ Y ��)
					if (GetAsyncKeyState('Q') & 0x8000) vEye += vUpWorld * moveSpeed; // ��
					if (GetAsyncKeyState('E') & 0x8000) vEye -= vUpWorld * moveSpeed; // ��

					// C. ���������λ��
					XMStoreFloat3(&g_f3EyePos, vEye);

					// 2. ������� (�����)
					// ���������һ�߷ɣ�һ��΢�����λ��
					float sphereSpeed = 0.05f;
					if (GetAsyncKeyState(VK_UP) & 0x8000) g_SpherePos.z += sphereSpeed;
					if (GetAsyncKeyState(VK_DOWN) & 0x8000) g_SpherePos.z -= sphereSpeed;
					if (GetAsyncKeyState(VK_LEFT) & 0x8000) g_SpherePos.x -= sphereSpeed;
					if (GetAsyncKeyState(VK_RIGHT) & 0x8000) g_SpherePos.x += sphereSpeed;

					// 3. ���� View ����
					// Ŀ��� = �۾� + ���߷���
					XMVECTOR vFocus = vEye + vForward;
					XMMATRIX xmView = XMMatrixLookAtLH(vEye, vFocus, vUpWorld);

					//ͶӰ�������ӳ�����
					XMMATRIX xmProj = XMMatrixPerspectiveFovLH(XM_PIDIV4//�ķ�֮��
						, (FLOAT)iWndWidth / (FLOAT)iWndHeight, 1.0f, 2000.0f);//���߱ȣ�
					
					// ���������ã�������skybox��view����earth��
					XMMATRIX xmSkyBox = xmView;
					
					
					xmSkyBox = XMMatrixMultiply(xmSkyBox, xmProj);//��xmView��xmProj��
					// ��ȡ�����
					xmSkyBox = XMMatrixInverse(nullptr, xmSkyBox);//���棬V��P������󣬼���M����������MVP�������

					//����Skybox��MVP
					XMStoreFloat4x4(&pMVPBufSkybox->m_MVP, xmSkyBox);//�Ҿ�˵�������ǰ�xmSkyBox����ָ��pMVPBufSkybox��������⸨��������DX��ϰ���ֵ������ˡ����ﻹ�Ӽ�����תΪ�洢���

					pMVPBufSkybox->m_v4EyePos = XMFLOAT4(g_f3EyePos.x, g_f3EyePos.y, g_f3EyePos.z, 1.0f);//���ֵڶ��Σ��趨view��ʱ���������������۾�λ��

					// ����λ�õ��������Բ�ֵ
					SH9 finalSH = InterpolateProbeVolume(g_SpherePos);

					// ���볣�����������򿪵�map������Ž���д�룬Ҳ����CBVָ�����ǿ�Uploadһֱ�������д��
					for (int i = 0; i < 9; i++) {
						pMVPBufEarth->m_SHCoeffs[i] = XMFLOAT4(
							finalSH.coeffs[i].x, finalSH.coeffs[i].y, finalSH.coeffs[i].z, 1.0f);
					}

					// A. ׼���任����
					// ����
					XMMATRIX xmScale = XMMatrixScaling(fSphereSize, fSphereSize, fSphereSize);
					// ��ת (����֮ǰ����ת����)
					XMMATRIX xmRot = XMMatrixRotationY(static_cast<float>(dModelRotationYAngle));
					// λ�� (ʹ�� WASD ���Ƶ� g_SpherePos)
					XMMATRIX xmTrans = XMMatrixTranslation(g_SpherePos.x, g_SpherePos.y, g_SpherePos.z);

					// B. ���������� (World Matrix)
					// ˳������ -> ��ת -> ƽ��
					// ע�⣺XMMatrixMultiply ����˹��򣬻�������������۳ˣ��߼����� Scale * Rot * Trans
					XMMATRIX xmWorld = XMMatrixMultiply(xmScale, xmRot);
					xmWorld = XMMatrixMultiply(xmWorld, xmTrans);

					// C. ���� World ���� (�� Shader �㷨����)
					XMStoreFloat4x4(&pMVPBufEarth->m_mWorld, xmWorld);

					// D. ���� View * Projection
					XMMATRIX xmVP = XMMatrixMultiply(xmView, xmProj);

					// E. �������� MVP (World * View * Projection)
					XMMATRIX xmFinalMVP = XMMatrixMultiply(xmWorld, xmVP);

					// F. ���� MVP ��������
					XMStoreFloat4x4(&pMVPBufEarth->m_MVP, xmFinalMVP);

				}

				// ���޸���ԭ����û���ϵ�5��samplers�����������Ƿ��л�������л��ˣ�������¼ Bundle
				UINT nLastSamplerNO = 0;
				if (nLastSamplerNO != g_nCurrentSamplerNO)
				{
					// ������������������������б�
					GRS_THROW_IF_FAILED(pICmdAllocEarth->Reset());
					GRS_THROW_IF_FAILED(pIBundlesEarth->Reset(pICmdAllocEarth.Get(), pIPSOEarth.Get()));

					// ��ʼ��¼
					pIBundlesEarth->SetGraphicsRootSignature(pIRootSignature.Get());
					pIBundlesEarth->SetPipelineState(pIPSOEarth.Get());

					ID3D12DescriptorHeap* ppHeapsEarth[] = { pISRVHpEarth.Get(), pISampleHpEarth.Get() };
					pIBundlesEarth->SetDescriptorHeaps(_countof(ppHeapsEarth), ppHeapsEarth);

					// ���� SRV
					pIBundlesEarth->SetGraphicsRootDescriptorTable(0, pISRVHpEarth->GetGPUDescriptorHandleForHeapStart());

					// ���� CBV
					D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleEarth = pISRVHpEarth->GetGPUDescriptorHandleForHeapStart();
					stGPUCBVHandleEarth.ptr += nSRVDescriptorSize;
					pIBundlesEarth->SetGraphicsRootDescriptorTable(1, stGPUCBVHandleEarth);

					// ���ؼ��㡿�����ʹ���µ� g_nCurrentSamplerNO �����µĵ�ַ
					D3D12_GPU_DESCRIPTOR_HANDLE hGPUSamplerEarth = pISampleHpEarth->GetGPUDescriptorHandleForHeapStart();
					hGPUSamplerEarth.ptr += (g_nCurrentSamplerNO * nSamplerDescriptorSize);
					pIBundlesEarth->SetGraphicsRootDescriptorTable(2, hGPUSamplerEarth);

					pIBundlesEarth->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					pIBundlesEarth->IASetVertexBuffers(0, 1, &stVBVEarth);
					pIBundlesEarth->IASetIndexBuffer(&stIBVEarth);

					pIBundlesEarth->DrawIndexedInstanced(nSphereIndexCnt, 1, 0, 0, 0);

					// 5. ���
					pIBundlesEarth->Close();

					// 6. ����״̬����ֹ��һ֡�ظ���¼
					nLastSamplerNO = g_nCurrentSamplerNO;
				}

				//��ȡ�µĺ󻺳���ţ���ΪPresent�������ʱ�󻺳����ž͸�����
				nCurrentFrameIndex = pISwapChain3->GetCurrentBackBufferIndex();
				//�����������Resetһ��
				GRS_THROW_IF_FAILED(pICmdAllocDirect->Reset());
				//Reset�����б���������ָ�������������PSO����
				GRS_THROW_IF_FAILED(pICmdListDirect->Reset(pICmdAllocDirect.Get(), pIPSOEarth.Get()));

				// ͨ����Դ�����ж��󻺳��Ѿ��л���Ͽ��Կ�ʼ��Ⱦ��
				stBeginResBarrier.Transition.pResource = pIARenderTargets[nCurrentFrameIndex].Get();
				pICmdListDirect->ResourceBarrier(1, &stBeginResBarrier);

				//ƫ��������ָ�뵽ָ��֡������ͼλ��
				D3D12_CPU_DESCRIPTOR_HANDLE stRTVHandle = pIRTVHeap->GetCPUDescriptorHandleForHeapStart();
				stRTVHandle.ptr += (nCurrentFrameIndex * nRTVDescriptorSize);
				D3D12_CPU_DESCRIPTOR_HANDLE stDSVHandle = pIDSVHeap->GetCPUDescriptorHandleForHeapStart();
				//������ȾĿ��
				pICmdListDirect->OMSetRenderTargets(1, &stRTVHandle, FALSE, &stDSVHandle);
				pICmdListDirect->RSSetViewports(1, &stViewPort);
				pICmdListDirect->RSSetScissorRects(1, &stScissorRect);

				// ������¼�����������ʼ��һ֡����Ⱦ
				pICmdListDirect->ClearRenderTargetView(stRTVHandle, faClearColor, 0, nullptr);
				pICmdListDirect->ClearDepthStencilView(pIDSVHeap->GetCPUDescriptorHandleForHeapStart()
					, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

				//31��ִ��Skybox�������
				ID3D12DescriptorHeap* ppHeapsSkybox[] = { pISRVHpSkybox.Get(),pISampleHpSkybox.Get() };
				pICmdListDirect->SetDescriptorHeaps(_countof(ppHeapsSkybox), ppHeapsSkybox);
				pICmdListDirect->ExecuteBundle(pIBundlesSkybox.Get());//ʵ�������PSO�������ȥ�ˡ����Ƕ�Ӧ��PSO

				//32��ִ������������
				ID3D12DescriptorHeap* ppHeapsEarth[] = { pISRVHpEarth.Get(),pISampleHpEarth.Get() };
				pICmdListDirect->SetDescriptorHeaps(_countof(ppHeapsEarth), ppHeapsEarth);
				pICmdListDirect->ExecuteBundle(pIBundlesEarth.Get());
				

				//��һ����Դ���ϣ�����ȷ����Ⱦ�Ѿ����������ύ����ȥ��ʾ��
				stEndResBarrier.Transition.pResource = pIARenderTargets[nCurrentFrameIndex].Get();
				pICmdListDirect->ResourceBarrier(1, &stEndResBarrier);
				//�ر������б�������ȥִ����
				GRS_THROW_IF_FAILED(pICmdListDirect->Close());

				//ִ�������б�
				ID3D12CommandList* ppCommandLists[] = { pICmdListDirect.Get() };
				pICMDQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);


				//�ύ����
				GRS_THROW_IF_FAILED(pISwapChain3->Present(1, 0));


				//��ʼͬ��GPU��CPU��ִ�У��ȼ�¼Χ�����ֵ
				const UINT64 fence = n64FenceValue;
				GRS_THROW_IF_FAILED(pICMDQueue->Signal(pIFence.Get(), fence));
				n64FenceValue++;
				GRS_THROW_IF_FAILED(pIFence->SetEventOnCompletion(fence, hEventFence));
			}
			break;
			case 1:
			{//������Ϣ
				while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
				{
					if (WM_QUIT != msg.message)
					{
						::TranslateMessage(&msg);
						::DispatchMessage(&msg);
					}
					else
					{
						bExit = TRUE;
					}
				}
			}
			break;
			case WAIT_TIMEOUT:
			{
			}
			break;
			default:
				break;
			}


		}
		//::CoUninitialize();
	}
	catch (CGRSCOMException & e)
	{//������COM�쳣
		e;
	}
	return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;

		// �������Ҽ�����
	case WM_RBUTTONDOWN:
	{
		g_bRightMouseDown = true;
		// ��¼����ʱ������
		g_LastMousePos.x = LOWORD(lParam);
		g_LastMousePos.y = HIWORD(lParam);
		// ������꣬��ֹ�ϳ�������ʧЧ
		SetCapture(hWnd);
		// ���ع��
		ShowCursor(FALSE);
	}
	break;

	// ����Ҽ�̧��
	case WM_RBUTTONUP:
	{
		g_bRightMouseDown = false;
		ReleaseCapture();
		ShowCursor(TRUE);
	}
	break;

	// ����ƶ���������ת�Ƕ� (Yaw/Pitch)
	case WM_MOUSEMOVE:
	{
		if (g_bRightMouseDown)
		{
			// ��ȡ��ǰ���λ��
			int xPos = (short)LOWORD(lParam);
			int yPos = (short)HIWORD(lParam);

			// ����λ���� (Delta)
			int dx = xPos - g_LastMousePos.x;
			int dy = yPos - g_LastMousePos.y;

			// ������ (Sensitivity)
			float fSens = 0.005f;

			// ���½Ƕ�
			// ע�⣺dx ��Ӧ Yaw (����ת)��dy ��Ӧ Pitch (���¿�)
			g_fYaw += dx * fSens;

			// ���޸����: �� += ��Ϊ -= 
			// ��Ϊ��Ļ���� Y �����Ǽ�С����������Ҫ Pitch ����������(̧ͷ)
			g_fPitch -= dy * fSens;

			// ���� Pitch �Ƕȣ���ֹ����ͷ (������ +/- 85��)
			g_fPitch = max(-XM_PIDIV2 + 0.1f, min(XM_PIDIV2 - 0.1f, g_fPitch));

			// ���¡���һ֡λ�á�
			g_LastMousePos.x = xPos;
			g_LastMousePos.y = yPos;
		}
	}
	break;

	case WM_KEYDOWN:
	{
		USHORT n16KeyCode = (wParam & 0xFF);

		// �ո���л�������
		if (VK_SPACE == n16KeyCode)
		{
			++g_nCurrentSamplerNO;
			g_nCurrentSamplerNO %= g_nSampleMaxCnt;
			TCHAR szTitle[MAX_PATH] = {};
			StringCchPrintf(szTitle, MAX_PATH, _T("Current Sampler Index: %d"), g_nCurrentSamplerNO);
			SetWindowText(hWnd, szTitle);
		}

		// ��λ���� (TAB)
		if (VK_TAB == n16KeyCode)
		{
			g_SpherePos = XMFLOAT3(0.0f, 0.0f, 0.0f);
			g_f3EyePos = XMFLOAT3(0.0f, 2.0f, -10.0f);
			g_f3LockAt = XMFLOAT3(0.0f, 0.0f, 1.0f);
			g_fYaw = 0.0f;
			g_fPitch = 0.0f;
		}
		// ע�⣺WASD �� ��������߼������Ƶ� OnUpdate ��ȥ������
		// ��Ϊ WndProc �������������ӳٺͿ��٣����ʺ�˿�����С�
	}
	break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

