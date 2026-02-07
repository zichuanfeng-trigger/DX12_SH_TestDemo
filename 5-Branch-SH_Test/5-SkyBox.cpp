#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN // 从 Windows 头中排除极少使用的资料
#include <windows.h>
#include <tchar.h>
#include <fstream>  //for ifstream
#include <strsafe.h>
#include <atlconv.h> //for T2A
#include <atlcoll.h>
#include <wrl.h> //添加WTL支持 方便使用COM
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3d12.h>//for d3d12
#include <d3dcompiler.h>
#if defined(_DEBUG)
#include <dxgidebug.h>
#endif
#include <wincodec.h> //for WIC

#include "..\WindowsCommons\DDSTextureLoader12.h"

#include "SH_Probes.h"

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

//用于上取整除法
#define GRS_UPPER_DIV(A,B) ((UINT)(((A)+((B)-1))/(B)))

//更简洁的向上边界对齐算法 内存管理中常用 请记住
#define GRS_UPPER(A,B) ((UINT)(((A)+((B)-1))&~(B - 1)))

// 内存分配的宏定义
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
{//WIC格式与DXGI像素格式的对应表，该表中的格式为被支持的格式
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

// WIC 像素格式转换表.
struct WICConvert
{
	GUID source;
	GUID target;
};

static WICConvert g_WICConvert[] =
{
	// 目标格式一定是最接近的被支持的格式
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
{//查表确定兼容的最接近格式是哪个
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
{//查表确定最终对应的DXGI格式是哪一个
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
public: // 显式声明公开访问权限（其实 struct 默认就是 public，写出来是为了清晰）

	// 齐次坐标，所以用的4维
	XMFLOAT4 m_v4Position;

	ST_GRS_SKYBOX_VERTEX()
		: m_v4Position() // 调用 XMFLOAT4 的默认构造，清零
	{
	} 

	ST_GRS_SKYBOX_VERTEX(float x, float y, float z)
		: m_v4Position(x, y, z, 1.0f) // 把 w 分量设为 1.0f
	{
	}
	// 在 3D 图形学中，'点'的 w 分量必须是 1.0，'向量'的 w 分量通常是 0.0。
	// 天空盒的顶点是位置点，所以这里为 1.0f。

	// 重载赋值运算符
	ST_GRS_SKYBOX_VERTEX& operator = (const ST_GRS_SKYBOX_VERTEX& vt)
	{
		// 把传入对象 (vt) 的位置数据，拷贝给自己 (m_v4Position)
		m_v4Position = vt.m_v4Position;
		return *this; 
	}
};

struct ST_GRS_FRAME_MVP_BUFFER
{
	XMFLOAT4X4 m_MVP;
	XMFLOAT4X4 m_mWorld;
	XMFLOAT4   m_v4EyePos;

	// SH 系数我们也放到 MVP BUFFER，因为SH 也需要跟着位置变化变化
	XMFLOAT4   m_SHCoeffs[9];
};

UINT g_nCurrentSamplerNO = 1; //当前使用的采样器索引 ，这里默认使用第一个
UINT g_nSampleMaxCnt = 5;		//创建五个典型的采样器

//初始的默认摄像机的位置。以及摄像机相关变量
XMFLOAT3 g_f3EyePos = XMFLOAT3(0.0f, 5.0f, -10.0f); //眼睛位置
XMFLOAT3 g_f3LockAt = XMFLOAT3(0.0f, 0.0f, 1.0f);    //眼睛所盯的位置
XMFLOAT3 g_f3HeapUp = XMFLOAT3(0.0f, 1.0f, 0.0f);    //头部正上方位置

float g_fYaw = 0.0f;			// 绕正Z轴的旋转量.
float g_fPitch = 0.0f;			// 绕XZ平面的旋转量

double g_fPalstance = 10.0f * XM_PI / 180.0f;	//物体旋转的角速度，单位：弧度/秒

// 球体的世界坐标 (初始在原点)
XMFLOAT3 g_SpherePos = XMFLOAT3(0.0f, 0.0f, 0.0f);

// 【新增】鼠标控制相关变量
POINT g_LastMousePos = { 0, 0 };    // 上一帧的鼠标位置
bool  g_bRightMouseDown = false;    // 右键是否按下

LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR    lpCmdLine, int nCmdShow)
{
	::CoInitialize(nullptr);  //for WIC & COM

	// 初始化场景数据和烘焙探针
	InitSceneData(); // 1. 分配 g_Probes 空间，设置灯光位置
	BakeProbes();    // 2. 预计算每个探针的 SH 数据

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
	//常量缓冲区大小上对齐到256Bytes边界
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
	UINT								nSamplerDescriptorSize = 0; //采样器大小
	

	D3D12_VIEWPORT						stViewPort = { 0.0f, 0.0f, static_cast<float>(iWndWidth), static_cast<float>(iWndHeight), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
	D3D12_RECT							stScissorRect = { 0, 0, static_cast<LONG>(iWndWidth), static_cast<LONG>(iWndHeight) };

	//球体的网格数据
	ST_GRS_VERTEX*						pstSphereVertices = nullptr;
	UINT								nSphereVertexCnt = 0;
	UINT*								pSphereIndices = nullptr;
	UINT								nSphereIndexCnt = 0;

	//Sky Box的网格数据
	UINT								nSkyboxIndexCnt = 0;

	
	//加载Skybox的Cube Map需要的变量
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
	ComPtr<ID3D12DescriptorHeap>		pIDSVHeap;			//深度缓冲描述符堆
	ComPtr<ID3D12Resource>				pIDepthStencilBuffer; //深度蜡板缓冲区

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

	ComPtr<IWICImagingFactory>			pIWICFactory;
	ComPtr<IWICBitmapDecoder>			pIWICDecoder;
	ComPtr<IWICBitmapFrameDecode>		pIWICFrame;
	ComPtr<IWICBitmapSource>			pIBMPEarth;

	

	try
	{
			// 得到当前的工作目录，方便我们使用相对路径来访问各种资源文件
			{
				if (0 == ::GetModuleFileName(nullptr, pszAppPath, MAX_PATH))
				{
					GRS_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
				}

				WCHAR* lastSlash = _tcsrchr(pszAppPath, _T('\\'));
				if (lastSlash)
				{//删除Exe文件名
					*(lastSlash) = _T('\0');
				}

				lastSlash = _tcsrchr(pszAppPath, _T('\\'));
				if (lastSlash)
				{//删除x64路径
					*(lastSlash) = _T('\0');
				}

				lastSlash = _tcsrchr(pszAppPath, _T('\\'));
				if (lastSlash)
				{//删除Debug路径
					*(lastSlash + 1) = _T('\0');
				}
			}

		// 创建窗口
		{
			WNDCLASSEX wcex = {};
			wcex.cbSize = sizeof(WNDCLASSEX);
			wcex.style = CS_GLOBALCLASS;
			wcex.lpfnWndProc = WndProc;
			wcex.cbClsExtra = 0;
			wcex.cbWndExtra = 0;
			wcex.hInstance = hInstance;
			wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);		//防止无聊的背景重绘
			wcex.lpszClassName = GRS_WND_CLASS_NAME;
			RegisterClassEx(&wcex);

			DWORD dwWndStyle = WS_OVERLAPPED | WS_SYSMENU;
			RECT rtWnd = { 0, 0, iWndWidth, iWndHeight };
			AdjustWindowRect(&rtWnd, dwWndStyle, FALSE);

			// 计算窗口居中的屏幕坐标
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

		// 使用WIC加载图片，并转换为DXGI兼容的格式
		{
			ComPtr<IWICFormatConverter> pIConverter;
			ComPtr<IWICComponentInfo> pIWICmntinfo;
			WICPixelFormatGUID wpf = {};
			GUID tgFormat = {};
			WICComponentType type;
			ComPtr<IWICPixelFormatInfo> pIWICPixelinfo;

			
			//使用纯COM方式创建WIC类厂对象，也是调用WIC第一步要做的事情
			GRS_THROW_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pIWICFactory)));

			//使用WIC类厂对象接口加载纹理图片，并得到一个WIC解码器对象接口，图片信息就在这个接口代表的对象中了
			WCHAR pszTexcuteFileName[MAX_PATH] = {};
			StringCchPrintfW(pszTexcuteFileName, MAX_PATH, _T("%sAssets\\金星.jpg"), pszAppPath);

			GRS_THROW_IF_FAILED(pIWICFactory->CreateDecoderFromFilename(
				pszTexcuteFileName,              // 文件名
				nullptr,                            // 不指定解码器，使用默认
				GENERIC_READ,                    // 访问权限
				WICDecodeMetadataCacheOnDemand,  // 若需要就缓冲数据 
				&pIWICDecoder                    // 解码器对象
			));
			// 获取第一帧图片(因为GIF等格式文件可能会有多帧图片，其他的格式一般只有一帧图片)
			// 实际解析出来的往往是位图格式数据
			GRS_THROW_IF_FAILED(pIWICDecoder->GetFrame(0, &pIWICFrame));
			//获取WIC图片格式
			GRS_THROW_IF_FAILED(pIWICFrame->GetPixelFormat(&wpf));
			//通过第一道转换之后获取DXGI的等价格式
			if (GetTargetPixelFormat(&wpf, &tgFormat))//寻找适合的格式，wpf本身就适合就不管。需要转换为适合的就赋值到tgFormat
			{
				emTxtFmtEarth = GetDXGIFormatFromPixelFormat(&tgFormat);
			}

			if (DXGI_FORMAT_UNKNOWN == emTxtFmtEarth)
			{// 不支持的图片格式 目前退出了事 
			 // 一般 在实际的引擎当中都会提供纹理格式转换工具，
			 // 图片都需要提前转换好，所以不会出现不支持的现象
				throw CGRSCOMException(S_FALSE);
			}


			//格式转换
			if (!InlineIsEqualGUID(wpf, tgFormat))
			{// 这个判断很重要，如果原WIC格式不是直接能转换为DXGI格式的图片时
			 // 我们需要做的就是转换图片格式位适合的格式，为能够直接对应DXGI格式的形式做准备
				//创建图片格式转换器
				GRS_THROW_IF_FAILED(pIWICFactory->CreateFormatConverter(&pIConverter));
				//初始化一个图片转换器，实际也就是将图片数据进行了格式转换
				GRS_THROW_IF_FAILED(pIConverter->Initialize(//配置转换器
					pIWICFrame.Get(),                // 输入原图片数据
					tgFormat,						 // 指定待转换的目标格式
					WICBitmapDitherTypeNone,         // 指定位图是否有调色板，现代都是真彩位图，不用调色板，所以为None
					nullptr,                            // 指定调色板指针
					0.f,                             // 指定Alpha阀值
					WICBitmapPaletteTypeCustom       // 调色板类型，实际没有使用，所以指定为Custom
				));
				// 调用QueryInterface方法获得对象的位图数据源接口
				GRS_THROW_IF_FAILED(pIConverter.As(&pIBMPEarth));
			}
			else
			{
				//图片数据格式不需要转换，直接获取其位图数据源接口
				GRS_THROW_IF_FAILED(pIWICFrame.As(&pIBMPEarth));
			}
			//获得图片大小（单位：像素）
			GRS_THROW_IF_FAILED(pIBMPEarth->GetSize(&nTxtWEarth, &nTxtHEarth));
			//获取图片像素的位大小的BPP（Bits Per Pixel）信息，用以计算图片行数据的真实大小（单位：字节）
			GRS_THROW_IF_FAILED(pIWICFactory->CreateComponentInfo(tgFormat, pIWICmntinfo.GetAddressOf()));//获得信息对象，这里类似得到指针pIWICmntinfo，只是&的另一种形式


			GRS_THROW_IF_FAILED(pIWICmntinfo->GetComponentType(&type));
			if (type != WICPixelFormat)
			{
				throw CGRSCOMException(S_FALSE);
			}
			GRS_THROW_IF_FAILED(pIWICmntinfo.As(&pIWICPixelinfo));
			// 到这里终于可以得到BPP了，这也是我看的比较吐血的地方，为了BPP居然饶了这么多环节
			GRS_THROW_IF_FAILED(pIWICPixelinfo->GetBitsPerPixel(&nBPPEarth));
			// 计算图片实际的行大小（单位：字节），这里使用了一个上取整除法即（A+B-1）/B ，
			// 这曾经被传说是微软的面试题,希望你已经对它了如指掌
			nRowPitchEarth = GRS_UPPER_DIV(uint64_t(nTxtWEarth) * uint64_t(nBPPEarth), 8);
		}

		// 打开显示子系统的调试支持
		{
#if defined(_DEBUG)
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();
				// 打开附加的调试支持
				nDXGIFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
#endif
		}

		// 创建DXGI Factory对象
		{
			GRS_THROW_IF_FAILED(CreateDXGIFactory2(nDXGIFactoryFlags, IID_PPV_ARGS(&pIDXGIFactory5)));
			
		}

		// 枚举适配器创建设备
		{//选择NUMA架构的独显来创建3D设备对象,暂时先不支持集显了，当然你可以修改这些行为
			DXGI_ADAPTER_DESC1 stAdapterDesc1 = {};
			D3D12_FEATURE_DATA_ARCHITECTURE stArchitecture = {};
			for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pIDXGIFactory5->EnumAdapters1(adapterIndex, &pIAdapter1); ++adapterIndex)//获取枚举设备对象指针
			{
				pIAdapter1->GetDesc1(&stAdapterDesc1);//获得信息描述指针，放入stAdapterDesc1

				if (stAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{//跳过软件虚拟适配器设备
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
			{// 可怜的机器上居然没有独显 还是先退出了事 
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

		// 创建直接命令队列
		{
			D3D12_COMMAND_QUEUE_DESC stQueueDesc = {};
			stQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandQueue(&stQueueDesc, IID_PPV_ARGS(&pICMDQueue)));
		}

		// 创建直接命令列表、捆绑包
		{
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT
				, IID_PPV_ARGS(&pICmdAllocDirect)));//Allocator也是这个的

			//创建直接命令列表，在其上可以执行几乎所有的引擎命令（3D图形引擎、计算引擎、复制引擎等）
			//注意初始时并没有使用PSO对象，此时其实这个命令列表依然可以记录命令
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT
				, pICmdAllocDirect.Get(), nullptr, IID_PPV_ARGS(&pICmdListDirect)));
			//后面3个list，一个direct，两个bundles
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE
				, IID_PPV_ARGS(&pICmdAllocEarth)));
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE
				, pICmdAllocEarth.Get(), nullptr, IID_PPV_ARGS(&pIBundlesEarth)));

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE
				, IID_PPV_ARGS(&pICmdAllocSkybox)));
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE
				, pICmdAllocSkybox.Get(), nullptr, IID_PPV_ARGS(&pIBundlesSkybox)));
		}

		// 创建交换链
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

			//注意此处使用了高版本的SwapChain接口的函数
			GRS_THROW_IF_FAILED(pISwapChain1.As(&pISwapChain3));
			nCurrentFrameIndex = pISwapChain3->GetCurrentBackBufferIndex();

			//创建RTV(渲染目标视图)描述符堆(这里堆的含义应当理解为数组或者固定大小元素的固定大小显存池)
			D3D12_DESCRIPTOR_HEAP_DESC stRTVHeapDesc = {};
			stRTVHeapDesc.NumDescriptors = nFrameBackBufCount;
			stRTVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			stRTVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stRTVHeapDesc, IID_PPV_ARGS(&pIRTVHeap)));
			
			//stRTVHandle又能装下一堆不同的RTV堆
			D3D12_CPU_DESCRIPTOR_HANDLE stRTVHandle = { pIRTVHeap->GetCPUDescriptorHandleForHeapStart() };//RTV描述符结构体，里面只有pIRTVHeap一个
			for (UINT i = 0; i < nFrameBackBufCount; i++)
			{//这个循环暴漏了描述符堆实际上是个数组的本质
				GRS_THROW_IF_FAILED(pISwapChain3->GetBuffer(i, IID_PPV_ARGS(&pIARenderTargets[i])));
				pID3D12Device4->CreateRenderTargetView(pIARenderTargets[i].Get(), nullptr, stRTVHandle);
				stRTVHandle.ptr += nRTVDescriptorSize;
			}

			// 关闭ALT+ENTER键切换全屏的功能，因为我们没有实现OnSize处理，所以先关闭
			GRS_THROW_IF_FAILED(pIDXGIFactory5->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));
		}

		// 创建深度缓冲及深度缓冲描述符堆
		{
			// 1. 定义堆属性
			//决定这块显存在哪里。深度缓冲必须在 GPU 显存中，CPU 不需要直接读写它
			D3D12_HEAP_PROPERTIES stDSBufHeapDesc = {};
			stDSBufHeapDesc.Type = D3D12_HEAP_TYPE_DEFAULT;
			stDSBufHeapDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stDSBufHeapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			stDSBufHeapDesc.CreationNodeMask = 0;
			stDSBufHeapDesc.VisibleNodeMask = 0;


			// 2. 定义深度视图描述符
			D3D12_DEPTH_STENCIL_VIEW_DESC stDepthStencilDesc = {};
			stDepthStencilDesc.Format = emDSFormat;// 读了变量定义的格式
			stDepthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 这是一个 2D 纹理
			stDepthStencilDesc.Flags = D3D12_DSV_FLAG_NONE; // 无特殊标志（只读等）


			// 3. 定义优化的清除值 
			D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
			depthOptimizedClearValue.Format = emDSFormat;
			depthOptimizedClearValue.DepthStencil.Depth = 1.0f; // 1.0 = 无穷远，0.0 = 眼睛处
			depthOptimizedClearValue.DepthStencil.Stencil = 0;


			// 4. 定义资源描述
			// 描述这张“图”的长、宽、类型。
			D3D12_RESOURCE_DESC stDSResDesc = {};
			stDSResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2D 纹理
			stDSResDesc.Alignment = 0; // 默认对齐（64KB）
			stDSResDesc.Width = iWndWidth;  // 宽度（必须和渲染窗口/交换链一致）
			stDSResDesc.Height = iWndHeight; // 高度
			stDSResDesc.DepthOrArraySize = 1; // 只有一层，不是纹理数组
			stDSResDesc.MipLevels = 0; // 深度图不需要 Mipmap (0 或 1)
			stDSResDesc.Format = emDSFormat; // 格式必须匹配
			stDSResDesc.SampleDesc.Count = 1; // 采样数 1 (即不开启 MSAA 抗锯齿)
			stDSResDesc.SampleDesc.Quality = 0;
			stDSResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // 让硬件自己决定最好的内存排布
			// 【关键标志】允许作为深度模版目标。如果不加这个，就只能当普通图片用，不能当深度缓冲用
			stDSResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;


			// 5. 创建提交资源
			// 这一步同时做了两件事：1.申请显存堆 2.在堆上创建资源。
			// 其实用这个偷懒了，不直观不便管理。
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommittedResource(
				&stDSBufHeapDesc                // 堆属性 (GPU Default)
				, D3D12_HEAP_FLAG_NONE
				, &stDSResDesc                   // 资源描述
				, D3D12_RESOURCE_STATE_DEPTH_WRITE // 初始状态：准备被写入深度
				, &depthOptimizedClearValue      // 优化清除值 (必须传，否则可能会报错或性能下降)
				, IID_PPV_ARGS(&pIDepthStencilBuffer) // 输出：ComPtr<ID3D12Resource> 对象
			));


			// 6. 创建描述符堆
			// 这是一个“架子”，用来存放 DSV (深度视图)
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.NumDescriptors = 1; // 我们只需要存 1 个深度视图
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; // 类型：DSV 专用堆
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // 不需要 Shader 可见 (DSV 不在 Shader 中直接读取)
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&pIDSVHeap)));

			// 7. 创建视图
			// 将“资源(pIDepthStencilBuffer)”和“架子(pIDSVHeap)”联系起来
			pID3D12Device4->CreateDepthStencilView(
				pIDepthStencilBuffer.Get() // 原始资源
				, &stDepthStencilDesc        // 视图描述
				, pIDSVHeap->GetCPUDescriptorHandleForHeapStart() // 架子上的位置 (第0号槽位)
			);
		}

		// 创建 SRV CBV Sample堆
		{

			//我们将SRV纹理视图描述符（视图View）和CBV描述符放在一个描述符堆上
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


			//Skybox 的 SRV CBV Sample 堆
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stSRVHeapDesc, IID_PPV_ARGS(&pISRVHpSkybox)));
			stSamplerHeapDesc.NumDescriptors = 1; //天空盒子就一个采样器
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stSamplerHeapDesc, IID_PPV_ARGS(&pISampleHpSkybox)));

			
		}

		// 创建根签名
		{//这个例子中，球体和Skybox使用相同的根签名，因为渲染过程中需要的参数是一样的
			D3D12_FEATURE_DATA_ROOT_SIGNATURE stFeatureData = {};
			// 检测是否支持V1.1版本的根签名
			stFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			if (FAILED(pID3D12Device4->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &stFeatureData, sizeof(stFeatureData))))
			{// 1.0版 直接丢异常退出了
				GRS_THROW_IF_FAILED(E_NOTIMPL);
			}
			// 在GPU上执行SetGraphicsRootDescriptorTable后，我们不修改命令列表中的SRV，因此我们可以使用默认Rang行为:
			// D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
			D3D12_DESCRIPTOR_RANGE1 stDSPRanges[3] = {};//一根三range，意味着三套设置，但是根容器还是只申请了一个
			// 这里装下了SRV/CBV/SAMPLE

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

		// 编译Shader创建渲染管线状态对象
		{

#if defined(_DEBUG)
			// Enable better shader debugging with the graphics debugging tools.
			UINT nCompileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
			UINT nCompileFlags = 0;
#endif
			ComPtr<ID3DBlob>					pIVSEarth;
			ComPtr<ID3DBlob>					pIPSEarth;

			//编译为行矩阵形式	   
			nCompileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

			TCHAR pszShaderFileName[MAX_PATH] = {};
			StringCchPrintf(pszShaderFileName, MAX_PATH, _T("%sShader\\SphereSH.hlsl"), pszAppPath);	

			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszShaderFileName, nullptr, nullptr
				, "VSMain", "vs_5_0", nCompileFlags, 0, &pIVSEarth, nullptr));

			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszShaderFileName, nullptr, nullptr
				, "PSMain", "ps_5_0", nCompileFlags, 0, &pIPSEarth, nullptr));

			// 我们多添加了一个法线的定义，但目前Shader中我们并没有使用
			D3D12_INPUT_ELEMENT_DESC stIALayoutEarth[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};

			// 创建 graphics pipeline state object (PSO)对象
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
			stPSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;//启用深度缓存写入功能
			stPSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;     //深度测试函数（该值为普通的深度测试）
			stPSODesc.DepthStencilState.StencilEnable = FALSE;//模板在这里被禁用了
			stPSODesc.SampleDesc.Count = 1;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateGraphicsPipelineState(&stPSODesc
				, IID_PPV_ARGS(&pIPSOEarth)));//创建Eateh的PSO指针


			//创建第二个PSO，专属于天空盒
			//编译为行矩阵形式	   
			nCompileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

			TCHAR pszSMFileSkybox[MAX_PATH] = {};

			// 注意：路径看准了，你改了文件这个配错了就会闪退，别debug到这行才记得
			StringCchPrintf(pszSMFileSkybox, MAX_PATH, _T("%sShader\\SkyBox.hlsl"), pszAppPath);

			ComPtr<ID3DBlob>					pIVSSkybox;
			ComPtr<ID3DBlob>					pIPSSkybox;

			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszSMFileSkybox, nullptr, nullptr
				, "SkyboxVS", "vs_5_0", nCompileFlags, 0, &pIVSSkybox, nullptr));
			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszSMFileSkybox, nullptr, nullptr
				, "SkyboxPS", "ps_5_0", nCompileFlags, 0, &pIPSSkybox, nullptr));

			// 天空盒子只有顶点只有位置参数
			D3D12_INPUT_ELEMENT_DESC stIALayoutSkybox[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};

			// 创建Skybox的(PSO)对象 
			stPSODesc.InputLayout = { stIALayoutSkybox, _countof(stIALayoutSkybox) };
			//stPSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
			stPSODesc.DepthStencilState.DepthEnable = FALSE;//深度被关了
			stPSODesc.DepthStencilState.StencilEnable = FALSE;
			stPSODesc.VS.BytecodeLength = pIVSSkybox->GetBufferSize();
			stPSODesc.VS.pShaderBytecode = pIVSSkybox->GetBufferPointer();
			stPSODesc.PS.BytecodeLength = pIPSSkybox->GetBufferSize();
			stPSODesc.PS.pShaderBytecode = pIPSSkybox->GetBufferPointer();

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateGraphicsPipelineState(&stPSODesc
				, IID_PPV_ARGS(&pIPSOSkyBox)));//这里PSO的设置重载，是覆写打包的逻辑
		}

		// 创建纹理的默认堆、上传堆并加载纹理
		{
			D3D12_HEAP_DESC stTextureHeapDesc = {};//这个就是默认堆，你创建的时候这个就是空的，完全还没把东西搬进去。
			//为堆指定纹理图片至少2倍大小的空间，这里没有详细去计算了，只是指定了一个足够大的空间，够放纹理就行
			//实际应用中也是要综合考虑分配堆的大小，以便可以重用堆
			stTextureHeapDesc.SizeInBytes = GRS_UPPER(2 * nRowPitchEarth * nTxtHEarth, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
			//指定堆的对齐方式，这里使用了默认的64K边界对齐，因为我们暂时不需要MSAA支持
			stTextureHeapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			stTextureHeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;		//默认堆类型
			stTextureHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stTextureHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			//拒绝渲染目标纹理、拒绝深度蜡板纹理，实际就只是用来摆放普通纹理
			stTextureHeapDesc.Flags = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_BUFFERS;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateHeap(&stTextureHeapDesc, IID_PPV_ARGS(&pIRESHeapEarth)));

			// 创建2D纹理		
			stTextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			stTextureDesc.MipLevels = 1;
			stTextureDesc.Format = emTxtFmtEarth; //DXGI_FORMAT_R8G8B8A8_UNORM;
			stTextureDesc.Width = nTxtWEarth;
			stTextureDesc.Height = nTxtHEarth;
			stTextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			stTextureDesc.DepthOrArraySize = 1;
			stTextureDesc.SampleDesc.Count = 1;
			stTextureDesc.SampleDesc.Quality = 0;
			
			//使用“定位方式”来创建纹理，注意下面这个调用内部实际已经没有存储分配和释放的实际操作了，所以性能很高
			//同时可以在这个堆上反复调用CreatePlacedResource来创建不同的纹理，当然前提是它们不在被使用的时候，才考虑
			//重用堆
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIRESHeapEarth.Get()
				, 0
				, &stTextureDesc				//可以使用CD3DX12_RESOURCE_DESC::Tex2D来简化结构体的初始化
				, D3D12_RESOURCE_STATE_COPY_DEST//这里初始化设定的时候，说明他是被拷贝进去的对象
				, nullptr
				, IID_PPV_ARGS(&pITextureEarth)));
			
			//获取上传堆资源缓冲的大小，这个尺寸通常大于实际图片的尺寸
			D3D12_RESOURCE_DESC stCopyDstDesc = pITextureEarth->GetDesc();
			pID3D12Device4->GetCopyableFootprints(&stCopyDstDesc, 0, 1, 0, nullptr, nullptr, nullptr, &n64szUploadBufEarth);

			
			// 创建上传堆
			D3D12_HEAP_DESC stUploadHeapDesc = {  };
			//尺寸依然是实际纹理数据大小的2倍并64K边界对齐大小
			stUploadHeapDesc.SizeInBytes = GRS_UPPER(2 * n64szUploadBufEarth, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
			//注意上传堆肯定是Buffer类型，可以不指定对齐方式，其默认是64k边界对齐
			stUploadHeapDesc.Alignment = 0;
			stUploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;		//上传堆类型
			stUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			//上传堆就是缓冲，可以摆放任意数据
			stUploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateHeap(&stUploadHeapDesc, IID_PPV_ARGS(&pIUploadHeapEarth)));

			
			// 使用“定位方式”创建用于上传纹理数据的缓冲资源
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

			// 加载图片数据至上传堆，即完成第一个Copy动作，从memcpy函数可知这是由CPU完成的
			//按照资源缓冲大小来分配实际图片数据存储的内存大小
			void* pbPicData = GRS_CALLOC(n64szUploadBufEarth);
			if (nullptr == pbPicData)
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}

			//从图片中读取出数据，按照真实行距计算出来的数据，得到pbPicData，类型比特
			GRS_THROW_IF_FAILED(pIBMPEarth->CopyPixels(nullptr//老远的地方，创建的位图源接口，WIC最后一步
				, nRowPitchEarth
				, static_cast<UINT>(nRowPitchEarth * nTxtHEarth)   //注意这里才是图片数据真实的大小，这个值通常小于缓冲的大小//实际需要占位大小*tex张数
				, reinterpret_cast<BYTE*>(pbPicData)));//无符号整数强转BYTE，数据输出到这个指针

			//{//下面这段代码来自DX12的示例，直接通过填充缓冲绘制了一个黑白方格的纹理
			// //还原这段代码，然后注释上面的CopyPixels调用可以看到黑白方格纹理的效果
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

			//获取向上传堆拷贝纹理数据的一些纹理转换尺寸信息
			//对于复杂的DDS纹理这是非常必要的过程

			UINT   nNumSubresources = 1u;  //我们只有一副图片，即子资源个数为1
			UINT   nTextureRowNum = 0u;
			UINT64 n64TextureRowSizes = 0u;
			UINT64 n64RequiredSize = 0u;

			stDestDesc = pITextureEarth->GetDesc();

			pID3D12Device4->GetCopyableFootprints(&stDestDesc//传入//这个函数就是拿来获取信息的。
				, 0
				, nNumSubresources
				, 0
				, &stTxtLayoutsEarth//存有布局数据、每行偏移数据等，由n64TextureRowSizes偏移对齐获得
				, &nTextureRowNum//总行数
				, &n64TextureRowSizes//每行实际数据大小
				, &n64RequiredSize);//对齐偏移后，全行总共申请大小；这里引用的全是接受输出，DX真的很喜欢给接收值用&传入修改返回

			//因为上传堆实际就是CPU传递数据到GPU的中介
			//所以我们可以使用熟悉的Map方法将它先映射到CPU内存地址中
			//然后我们按行将数据复制到上传堆中
			//需要注意的是之所以按行拷贝是因为GPU资源的行大小
			//与实际图片的行大小是有差异的,二者的内存边界对齐要求是不一样的
			BYTE* pData = nullptr;
			GRS_THROW_IF_FAILED(pITextureUploadEarth->Map(0, nullptr, reinterpret_cast<void**>(&pData)));//map是指定门，门如何，门是pData，空数据容器

			BYTE* pDestSlice = reinterpret_cast<BYTE*>(pData) + stTxtLayoutsEarth.Offset;//目标切片，门与踏进步长。加上 Offset 是为了通用性(虽然这里Offset是0)。
			BYTE* pSrcSlice = reinterpret_cast<BYTE*>(pbPicData);//两个东西稍微转换了个类型而已
			for (UINT y = 0; y < nTextureRowNum; ++y)
			{	// 目标地址: 上传堆的起始 + (行号 * 显卡要求的行宽 2816)
				// 注意：这里乘的是 2816！这意味着每拷完一行，指针会跳过 16 字节的空隙。
				memcpy(pDestSlice + static_cast<SIZE_T>(stTxtLayoutsEarth.Footprint.RowPitch)* y
					// 源地址: 系统内存的起始 + (行号 * 真实行宽 2800)
					// 这里是紧凑排列的。
					, pSrcSlice + static_cast<SIZE_T>(nRowPitchEarth)* y
					// 拷贝长度: 只拷贝真实数据长度 (2800)
					, nRowPitchEarth);
			}
			//取消映射 对于易变的数据如每帧的变换矩阵等数据，可以撒懒不用Unmap了，
			//让它常驻内存,以提高整体性能，因为每次Map和Unmap是很耗时的操作
			//因为现在起码都是64位系统和应用了，地址空间是足够的，被长期占用不会影响什么
			pITextureUploadEarth->Unmap(0, nullptr);

			//释放图片数据，做一个干净的程序员
			GRS_SAFE_FREE(pbPicData);
		}


		// 使用DDSLoader辅助函数加载Skybox的纹理
		{
			TCHAR pszSkyboxTextureFile[MAX_PATH] = {};
			StringCchPrintf(pszSkyboxTextureFile, MAX_PATH, _T("%sAssets\\Sky_cube_1024.dds"), pszAppPath);

			ID3D12Resource* pIResSkyBox = nullptr;

			// LoadDDSTextureFromFile 是辅助库函数（DirectXTex 或类似库提供）
			// 功能：读取文件头，解析格式，创建 D3D12 资源对象，并将文件内容的二进制数据读入内存
			GRS_THROW_IF_FAILED(LoadDDSTextureFromFile(
				pID3D12Device4.Get(),       // D3D12 设备指针，用于创建资源
				pszSkyboxTextureFile,       // 完整的文件路径
				&pIResSkyBox,               // [输出] 创建好的纹理资源接口指针（通常在 Default Heap 上）
				ddsData,                    // [输出] unique_ptr，管理加载到内存的原始文件二进制数据
				arSubResources,             // [输出] vector，包含所有子资源（Mipmap、立方体面）的数据指针和行距信息
				SIZE_MAX,                   // 最大允许加载的大小，SIZE_MAX 表示不限制
				&emAlphaMode,               // [输出] 返回纹理的 Alpha 混合模式信息
				&bIsCube));                 // [输出] 返回布尔值，确认该 DDS 是否为立方体贴图 (CubeMap)

			// 将裸指针 pIResSkyBox 附加到 ComPtr 智能指针进行管理
			// 此时 pITextureSkybox 指向的资源在 GPU 默认堆上，但数据尚未正确初始化（需要从 Upload Heap 拷贝）
			pITextureSkybox.Attach(pIResSkyBox);//独立的，创建的空指针，没有经历earth那样的创建

			// 获取刚才创建的纹理资源的描述（宽、高、格式等）
			// stCopyDstDesc这个变量有同名的，基本相同，是那种随用随弃的中间变量
			D3D12_RESOURCE_DESC stCopyDstDesc = pITextureSkybox->GetDesc();//nb，资源描述符不是设定好的，是直接获取的
			
			pID3D12Device4->GetCopyableFootprints(
				&stCopyDstDesc, 
				0, 
				static_cast<UINT>(arSubResources.size()), //   - 0, arSubResources.size(): 从第0个子资源开始，计算所有子资源
				0, 
				nullptr, 
				nullptr, 
				nullptr, 
				&n64szUploadBufSkybox);//[输出] 接收所需的上传堆缓冲区总大小（字节），这里获得的是skybox的

			// 配置上传堆 (Upload Heap) 的属性
			D3D12_HEAP_DESC stUploadHeapDesc = {};

			// 设置堆的大小：
			// GRS_UPPER 是一个宏，用于进行内存对齐（通常对齐到 64KB 或 4KB）
			// 这里申请了 "计算出的大小 * 2" 的空间。
			// 乘以 2 是一种保守的做法（Buffer），确保有足够的空间处理对齐填充，或者为后续操作留余量。
			stUploadHeapDesc.SizeInBytes = GRS_UPPER(2 * n64szUploadBufSkybox, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// 详细设置堆的参数
			stUploadHeapDesc.Alignment = 0;// Alignment = 0 表示使用默认对齐（堆通常是 64KB 对齐）
			stUploadHeapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
			stUploadHeapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stUploadHeapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			stUploadHeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;// 标志：只允许放置 Buffer (缓冲) 类型的资源

			// 创建堆，在 GPU 显存（或共享显存）中实际分配内存
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateHeap(&stUploadHeapDesc, IID_PPV_ARGS(&pIUploadHeapSkybox)));//skybox有自己的上传堆容器



			// 定义上传堆上的资源描述
			D3D12_RESOURCE_DESC stUploadBufDesc = {};
			stUploadBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // 类型是缓冲
			stUploadBufDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; // 默认对齐(64KB)
			stUploadBufDesc.Width = n64szUploadBufSkybox; // 上一步计算出的总大小
			stUploadBufDesc.Height = 1; // Buffer 高度固定为 1
			stUploadBufDesc.DepthOrArraySize = 1;
			stUploadBufDesc.MipLevels = 1;
			stUploadBufDesc.Format = DXGI_FORMAT_UNKNOWN; // Buffer 没有像素格式
			stUploadBufDesc.SampleDesc.Count = 1;
			stUploadBufDesc.SampleDesc.Quality = 0;
			stUploadBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // 线性布局
			stUploadBufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			// 在已有的 Upload Heap 上创建资源对象
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapSkybox.Get(),            // 指定刚才创建的上传堆
				0,                                   // 堆内的偏移量，这里从头开始
				&stUploadBufDesc,                    // 资源描述
				D3D12_RESOURCE_STATE_GENERIC_READ,   // 初始状态：Upload Heap 必须是 CPU 可读
				nullptr,                             // ClearValue，Buffer 不需要
				IID_PPV_ARGS(&pITextureUploadSkybox) // 输出：上传资源的接口指针
			));

			// 准备获取复杂的子资源布局信息
			UINT nFirstSubresource = 0;

			// arSubResources 是 LoadDDSTextureFromFile 解析出来的，包含了所有子资源数据
			UINT nNumSubresources = static_cast<UINT>(arSubResources.size());
			
			// 上传堆描述
			D3D12_RESOURCE_DESC stUploadResDesc = pITextureUploadSkybox->GetDesc();
			
			// 默认堆描述
			D3D12_RESOURCE_DESC stDefaultResDesc = pITextureSkybox->GetDesc();

			UINT64 n64RequiredSize = 0;

			// ============================================================================
			// 为布局信息数组分配内存
			// GetCopyableFootprints 需要输出数组来存放每一个子资源的信息。
			// 因为子资源数量不确定（取决于 Mipmap 层级），所以需要动态分配内存。
			// 我们需要存放三个数组：
			//   1. pLayouts: 存放每个子资源的偏移和 footprint (D3D12_PLACED_SUBRESOURCE_FOOTPRINT)
			//   2. pNumRows: 存放每个子资源的行数 (UINT)
			//   3. pRowSizesInBytes: 存放每个子资源的行大小 (UINT64)
			// ============================================================================
			SIZE_T szMemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT)
				+ sizeof(UINT)
				+ sizeof(UINT64))
				* nNumSubresources; // 乘以子资源总数

			// GRS_CALLOC 是自定义宏，通常调用 HeapAlloc 并清零。
			// 它的作用是向系统申请一块内存，并且返回的是void*（无类型指针）。
			void* pMem = GRS_CALLOC(static_cast<SIZE_T>(szMemToAlloc));

			if (nullptr == pMem)
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}

			// 指针运算：将分配的一大块内存切分给三个数组使用
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);//的意思是：把 pMem 这个无类型的内存地址，强行解释为一个 D3D12_PLACED_SUBRESOURCE_FOOTPRINT 结构体数组的首地址

			// 注意跳的是footprint大小
			UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + nNumSubresources);

			// pNumRows 紧接着 pRowSizesInBytes 数组后面
			UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + nNumSubresources);

		
			// 第一次调用只是为了算总大小，这次调用是为了填满上面分配的数组
			pID3D12Device4->GetCopyableFootprints(
				&stDefaultResDesc,   // 目标纹理描述
				nFirstSubresource,   // 开始索引
				nNumSubresources,    // 子资源总数
				0,                   // Buffer 中的起始偏移 (Base Offset)
				pLayouts,            // [输出] 布局信息数组
				pNumRows,            // [输出] 行数数组//哦牛逼，这个也是个数组，全都带s的。对应不同的尺寸有完全不同的值，下面同理
				pRowSizesInBytes,    // [输出] 行大小数组
				&n64RequiredSize     // [输出] 总大小
			);

			
			BYTE* pData = nullptr;

			// 0 表示不读取，只写入，可以优化性能
			HRESULT hr = pITextureUploadSkybox->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			if (FAILED(hr))
			{
				return 0;
			}
			
			// 第一重循环：遍历所有子资源
			for (UINT i = 0; i < nNumSubresources; ++i)
			{
				// 防御性检查：确保我们要拷贝的行大小没有溢出 SIZE_T 的最大值，如果这一行的大小大得离谱（超过了系统能表示的最大内存），就报错
				if (pRowSizesInBytes[i] > (SIZE_T)-1)
				{
					throw CGRSCOMException(E_FAIL);
				}

				// 这里的 pLayouts[i] 就是我们之前费劲算出来的首地址
				D3D12_MEMCPY_DEST stCopyDestData = {
					// 1. pData: 目标起始地址 = Upload Heap基地址 + 这张小图的偏移量(Offset)
					pData + pLayouts[i].Offset,//pData基地址，加playouts后面60个的遍历

					// 2. RowPitch: 目标行距 (显卡要求的对齐宽度，比如 256)
					pLayouts[i].Footprint.RowPitch,//显卡要求的对齐目标行距，翻到下一行的行距

					// 3. SlicePitch: 目标切片大小 (行距 * 行数)
					pLayouts[i].Footprint.RowPitch * pNumRows[i]
				};


				// 第二重循环：遍历深度切片
				// 对于 Skybox (Cube Map) 或普通 2D 纹理，Depth 通常是 1
				// 这个循环貌似是为了兼容 3D 纹理
				// 那这样原代码注释写的 "Mipmap" 其实不太准确，Mipmap 是由外层循环 i 控制的
				// 这里 z 控制的是“体纹理”的厚度
				for (UINT z = 0; z < pLayouts[i].Footprint.Depth; ++z)
				{
					// 起始位置 + (切片索引 * 每一个切片的大小)
					BYTE* pDestSlice = reinterpret_cast<BYTE*>(stCopyDestData.pData) + stCopyDestData.SlicePitch * z;//z为0时，这个就是源地址

					// arSubResources 是 LoadDDSTextureFromFile 加载进来的原始数据
					// 原始数据是紧凑的，SlicePitch 也是紧凑的
					const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(arSubResources[i].pData) + arSubResources[i].SlicePitch * z;

					// 第三重循环：遍历行 (Rows)拷贝
					// pNumRows[i] 是这张小图的高度
					for (UINT y = 0; y < pNumRows[i]; ++y)
					{
						// memcpy(目标, 源, 大小)
						memcpy(
							// 目标地址：当前切片起点 + (行号 * 显存对齐行距 256)
							// 注意：这里用的是 stCopyDestData.RowPitch，带有填充空隙
							pDestSlice + stCopyDestData.RowPitch * y,//y决定行编号

							// 源地址：当前切片起点 + (行号 * 原始紧凑行距 128)
							// 注意：这里用的是 arSubResources[i].RowPitch，没有空隙
							pSrcSlice + arSubResources[i].RowPitch * y,

							// 拷贝大小：只拷贝有效数据长度 (pRowSizesInBytes[i])
							// 比如只拷 128 字节。
							// 显存里剩下的 (256 - 128 = 128) 字节 padding 会保持未初始化状态，GPU 不会去读它。
							(SIZE_T)pRowSizesInBytes[i]
						);
					}
				}
			}
			// 结束映射
			pITextureUploadSkybox->Unmap(0, nullptr);

			
			// 这是一个通用的上传代码模板。
			// 虽然在这个 Skybox 的例子里，我们确定是 Texture（所以肯定走 else）
			if (stDefaultResDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				// Buffer 是线性的，没有二维结构，也没有复杂的对齐要求（行距）所以不需要分行、分切片，直接一把梭复制过去就行

				pICmdListDirect->CopyBufferRegion(
					pITextureSkybox.Get(),        // 目标资源 (Default Heap)
					0,                            // 目标偏移 (从头开始写)
					pITextureUploadSkybox.Get(),  // 源资源 (Upload Heap)
					pLayouts[0].Offset,           // 源偏移 (通常 Buffer 就一个子资源，Offset往往是0)
					pLayouts[0].Footprint.Width   // 复制的大小 (对于 Buffer，Width 就是字节总长度)
				);
			}
			else
			{

				// 遍历每一个子资源 (6个面 * Mip等级数)
				for (UINT i = 0; i < nNumSubresources; ++i)
				{
					// 定义目的地
					D3D12_TEXTURE_COPY_LOCATION stDstCopyLocation = {};

					// 目标资源：那个最终在 Default Heap 上的 Cube Map 纹理
					stDstCopyLocation.pResource = pITextureSkybox.Get();
					stDstCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

					// 指定子资源索引：比如第 0 个是 +X 面的大图，第 1 个是 +X 面的中图...
					stDstCopyLocation.SubresourceIndex = i;

					// 定义数据来源
					D3D12_TEXTURE_COPY_LOCATION stSrcCopyLocation = {};

					// 来自上传堆
					stSrcCopyLocation.pResource = pITextureUploadSkybox.Get();
					stSrcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					stSrcCopyLocation.PlacedFootprint = pLayouts[i];//这里直接发每个子资源对应的布局格式了

					// 启动！
					pICmdListDirect->CopyTextureRegion(
						&stDstCopyLocation, // 目的地描述
						0, 0, 0,            // 目标内的起始坐标 (X, Y, Z)，通常从左上角(0,0,0)开始写
						&stSrcCopyLocation, // 源头描述
						nullptr             // 源区域框：nullptr 表示复制整个 Footprint 定义的区域
					);
				}
			}

			// 使用Barrier同步一下
			D3D12_RESOURCE_BARRIER stTransResBarrier = {};
			stTransResBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stTransResBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stTransResBarrier.Transition.pResource = pITextureSkybox.Get();
			stTransResBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			stTransResBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			// 指定子资源
			stTransResBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			// 将指令插入命令列表
			pICmdListDirect->ResourceBarrier(1, &stTransResBarrier);
		}

		{}
		//向直接命令列表发出从上传堆复制纹理数据到默认堆的命令，执行并同步等待，即完成第二个Copy动作，由GPU上的复制引擎完成
		//注意此时直接命令列表还没有绑定PSO对象，因此它也是不能执行3D图形命令的，但是可以执行复制命令，因为复制引擎不需要什么
		//额外的状态设置之类的参数
		//地球的拷贝，看起来简直简单到爆炸呢，因为这是earth后半部分的内容，前面upload复杂的memcpy已经做过了，这里就只是upload拷到default而已
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

			//设置一个资源屏障，同步并确认复制操作完成
			//直接使用结构体然后调用的形式
			D3D12_RESOURCE_BARRIER stResBar = {};
			stResBar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stResBar.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stResBar.Transition.pResource = pITextureEarth.Get();
			stResBar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			stResBar.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			stResBar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			pICmdListDirect->ResourceBarrier(1, &stResBar);

		}

		// 执行第二个Copy命令并确定所有的纹理都上传到了默认堆中
		{
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pIFence)));
			n64FenceValue = 1;
			//创建一个Event同步对象，用于等待围栏事件通知
			hEventFence = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (hEventFence == nullptr)
			{
				GRS_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
			}

			// 执行命令列表并等待纹理资源上传完成，这一步是必须的
			GRS_THROW_IF_FAILED(pICmdListDirect->Close());

			ID3D12CommandList* ppCommandLists[] = { pICmdListDirect.Get() };
			pICMDQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

			// 等待纹理资源正式复制完成先
			const UINT64 fence = n64FenceValue;
			GRS_THROW_IF_FAILED(pICMDQueue->Signal(pIFence.Get(), fence));
			n64FenceValue++;
			GRS_THROW_IF_FAILED(pIFence->SetEventOnCompletion(fence, hEventFence));
		}

		// 加载球体的网格数据
		{
			ifstream fin;//代码使用 C++ 标准库 ifstream 读取文本文件
			char input;
			USES_CONVERSION;
			char pModuleFileName[MAX_PATH] = {};
			StringCchPrintfA(pModuleFileName, MAX_PATH, "%sAssets\\sphere.txt", T2A(pszAppPath));
			
			// 打开文件
			fin.open(pModuleFileName);
			// 错误检查：如果文件没找到或打不开
			if (fin.fail())
			{
				throw CGRSCOMException(E_FAIL);//异常
			}
			// 逻辑：读取字符直到遇到冒号 ':'，通常用于跳过标签（Vertices Count:）
			fin.get(input);
			while (input != ':')
			{
				fin.get(input);
			}
			// 读取冒号后的整数，即顶点数量
			fin >> nSphereVertexCnt;
			// 这里简单粗暴地令索引数等于顶点数
			nSphereIndexCnt = nSphereVertexCnt;

			// 逻辑：再次读取直到遇到下一个冒号（Data Start:）
			fin.get(input);
			while (input != ':')
			{
				fin.get(input);
			}
			fin.get(input);
			fin.get(input);

			//这里使用了前面定义的GRS_CALLOC宏，它的作用是向系统申请一块内存，并且返回的是void*（无类型指针）。
			//宏后面的括号，就是输入总共需要多少字节；前括号是强制类型转换
			pstSphereVertices = (ST_GRS_VERTEX*)GRS_CALLOC(nSphereVertexCnt * sizeof(ST_GRS_VERTEX));
			pSphereIndices = (UINT*)GRS_CALLOC(nSphereVertexCnt * sizeof(UINT));//【待更新】这里创建容器指针的方法比较老，也许可以使用现代的指针容器

			for (UINT i = 0; i < nSphereVertexCnt; i++)
			{
				// 依次读取：位置 x, y, z
				fin >> pstSphereVertices[i].m_v4Position.x
					>> pstSphereVertices[i].m_v4Position.y
					>> pstSphereVertices[i].m_v4Position.z;
				// 设置 w 分量为 1.0 (齐次坐标系要求，表示这是一个点)
				pstSphereVertices[i].m_v4Position.w = 1.0f;
				// 依次读取：纹理坐标 u, v
				fin >> pstSphereVertices[i].m_vTex.x
					>> pstSphereVertices[i].m_vTex.y;
				// 依次读取：法线 nx, ny, nz
				fin >> pstSphereVertices[i].m_vNor.x
					>> pstSphereVertices[i].m_vNor.y
					>> pstSphereVertices[i].m_vNor.z;
				// 生成索引：0, 1, 2, ...
				pSphereIndices[i] = i;
			}
		}

		// 创建顶点缓冲、索引缓冲、常量缓冲
		{
			
			UINT64 n64BufferOffset = GRS_UPPER(n64szUploadBufEarth, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// 定义资源描述结构体
			D3D12_RESOURCE_DESC stBufResDesc = {};
			stBufResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // 这是一个缓冲，不是纹理
			stBufResDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; // 64KB 对齐
			stBufResDesc.Width = nSphereVertexCnt * sizeof(ST_GRS_VERTEX); // 宽度 = 顶点总字节数
			stBufResDesc.Height = 1;
			stBufResDesc.DepthOrArraySize = 1;
			stBufResDesc.MipLevels = 1;
			stBufResDesc.Format = DXGI_FORMAT_UNKNOWN; // Buffer 通常不需要指定格式
			stBufResDesc.SampleDesc.Count = 1;
			stBufResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // Buffer 必须是 Row Major
			stBufResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			// 创建顶点缓冲
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapEarth.Get(),         // 指定由于已经分配好的堆 (Upload Heap)
				// 第二个了，前面earth纹理搬运也用的这个
				n64BufferOffset,                 // 指定堆内的偏移量
				&stBufResDesc,                   // 资源描述
				D3D12_RESOURCE_STATE_GENERIC_READ, // 初始状态 (Upload Heap 必须是 Generic Read)
				nullptr,
				IID_PPV_ARGS(&pIVBEarth)));      // 输出接口指针

			// 数据上传 (Map -> Memcpy -> Unmap)
			UINT8* pVertexDataBegin = nullptr;
			D3D12_RANGE stReadRange = { 0, 0 }; // 我们不打算从 CPU 读取这个显存，所以范围设为 0

			GRS_THROW_IF_FAILED(pIVBEarth->Map(0, &stReadRange, reinterpret_cast<void**>(&pVertexDataBegin)));

			memcpy(pVertexDataBegin, pstSphereVertices, nSphereVertexCnt * sizeof(ST_GRS_VERTEX));

			pIVBEarth->Unmap(0, nullptr);

			// 释放 CPU 端的临时内存
			GRS_SAFE_FREE(pstSphereVertices);

			// 创建顶点缓冲视图
			stVBVEarth.BufferLocation = pIVBEarth->GetGPUVirtualAddress(); // GPU 显存地址
			stVBVEarth.StrideInBytes = sizeof(ST_GRS_VERTEX);              // 每个顶点的步长 (字节)
			stVBVEarth.SizeInBytes = nSphereVertexCnt * sizeof(ST_GRS_VERTEX); // 总大小

			// 创建索引缓冲 (Index Buffer)
			n64BufferOffset = GRS_UPPER(n64BufferOffset + nSphereVertexCnt * sizeof(ST_GRS_VERTEX), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// 更新资源描述的宽度为索引缓冲的大小
			stBufResDesc.Width = nSphereIndexCnt * sizeof(UINT);

			// 在堆的“新偏移位置”创建索引缓冲资源 pIIBEarth
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapEarth.Get(),
				n64BufferOffset,
				&stBufResDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&pIIBEarth)));

			// 数据上传 (Map -> Copy -> Unmap)
			UINT8* pIndexDataBegin = nullptr;
			GRS_THROW_IF_FAILED(pIIBEarth->Map(0, &stReadRange, reinterpret_cast<void**>(&pIndexDataBegin)));
			memcpy(pIndexDataBegin, pSphereIndices, nSphereIndexCnt * sizeof(UINT));
			pIIBEarth->Unmap(0, nullptr);

			GRS_SAFE_FREE(pSphereIndices);

			// 创建索引缓冲视图
			stIBVEarth.BufferLocation = pIIBEarth->GetGPUVirtualAddress();
			stIBVEarth.Format = DXGI_FORMAT_R32_UINT; // 指定索引格式为 32位 无符号整数
			stIBVEarth.SizeInBytes = nSphereIndexCnt * sizeof(UINT);

			// 创建常量缓冲
			// 计算下一个偏移量 (索引缓冲之后，再次对齐)
			n64BufferOffset = GRS_UPPER(n64BufferOffset + nSphereIndexCnt * sizeof(UINT), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// 更新宽度为常量缓冲的大小 (szMVPBuf 应该已经是 256字节对齐的大小)
			stBufResDesc.Width = szMVPBuf;//这个就是buffer大小，放了贼远搞了个全局变量存着，什么防御性编程，直接拿st获得size不得了

			// 在堆的“新偏移位置”创建常量缓冲资源 pICBUploadEarth
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapEarth.Get(),
				n64BufferOffset,
				&stBufResDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&pICBUploadEarth)));

			// Map 后没有 Unmap，也没有memcpy，因为写入constant在渲染循环中进行
			GRS_THROW_IF_FAILED(pICBUploadEarth->Map(0, nullptr, reinterpret_cast<void**>(&pMVPBufEarth)));
		}

		// 加载天空盒（远平面型）
		{
			//自建ndc，这个挺有意思的，可以细看
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
			

			//加载天空盒子的数据
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

			//使用map-memcpy-unmap大法将数据传至顶点缓冲对象
			ST_GRS_SKYBOX_VERTEX* pVertexDataBegin = nullptr;

			GRS_THROW_IF_FAILED(pIVBSkybox->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin)));
			memcpy(pVertexDataBegin, stSkyboxVertices, nSkyboxIndexCnt * sizeof(ST_GRS_SKYBOX_VERTEX));
			pIVBSkybox->Unmap(0, nullptr);

			//创建资源视图，实际可以简单理解为指向顶点缓冲的显存指针
			stVBVSkybox.BufferLocation = pIVBSkybox->GetGPUVirtualAddress();
			stVBVSkybox.StrideInBytes = sizeof(ST_GRS_SKYBOX_VERTEX);
			stVBVSkybox.SizeInBytes = nSkyboxIndexCnt * sizeof(ST_GRS_SKYBOX_VERTEX);

			//计算边界对齐的正确的偏移位置
			n64BufferOffset = GRS_UPPER(n64BufferOffset + nSkyboxIndexCnt * sizeof(ST_GRS_SKYBOX_VERTEX), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

			// 创建常量缓冲 注意缓冲尺寸设置为256边界对齐大小
			stBufResDesc.Width = szMVPBuf;
			GRS_THROW_IF_FAILED(pID3D12Device4->CreatePlacedResource(
				pIUploadHeapSkybox.Get()
				, n64BufferOffset
				, &stBufResDesc
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&pICBUploadSkybox)));

			// Map 之后就不再Unmap了 直接复制数据进去 这样每帧都不用map-copy-unmap浪费时间了
			GRS_THROW_IF_FAILED(pICBUploadSkybox->Map(0, nullptr, reinterpret_cast<void**>(&pMVPBufSkybox)));
		}

		// 创建SRV描述符
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
			pID3D12Device4->CreateShaderResourceView(pITextureSkybox.Get(), &stSRVDesc, pISRVHpSkybox->GetCPUDescriptorHandleForHeapStart());//才发现啊，View的创建已经不会再持有某个指针了，就是填好了设置然后就直接从pISRVHpSkybox描述符和view配置结构体绑定就是了
		}

		// 创建CBV描述符
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = pICBUploadEarth->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = static_cast<UINT>(szMVPBuf);

			D3D12_CPU_DESCRIPTOR_HANDLE stSRVCBVHandle = pISRVHpEarth->GetCPUDescriptorHandleForHeapStart();//原作者写的就是诡异，这里你又单独把这个堆handle单独拎出来。
			stSRVCBVHandle.ptr += nSRVDescriptorSize;

			pID3D12Device4->CreateConstantBufferView(&cbvDesc, stSRVCBVHandle);//我草无敌了，合着你这长得基本一样的函数名，要获得的东西格式还不一样呗！我就说怎么还要造个cbvDesc扔给你！

			cbvDesc.BufferLocation = pICBUploadSkybox->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = static_cast<UINT>(szMVPBuf);

			D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvHandleSkybox = { pISRVHpSkybox->GetCPUDescriptorHandleForHeapStart() };//这里又不是想放多点view到堆里。拎出个中括号啥意思，前缀还变了，感觉有些冗余
			cbvSrvHandleSkybox.ptr += nSRVDescriptorSize;

			pID3D12Device4->CreateConstantBufferView(&cbvDesc, cbvSrvHandleSkybox);

		}

		// 创建各种采样器//核心是设置，然后覆写打包做成描述符。
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

			
			//创建Skybox的采样器//独立采样器
			stSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;//三合一线性过滤

			stSamplerDesc.MinLOD = 0;
			stSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
			stSamplerDesc.MipLODBias = 0.0f;
			stSamplerDesc.MaxAnisotropy = 1;
			stSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;//各向异性搞没了
			stSamplerDesc.BorderColor[0] = 0.0f;
			stSamplerDesc.BorderColor[1] = 0.0f;
			stSamplerDesc.BorderColor[2] = 0.0f;
			stSamplerDesc.BorderColor[3] = 0.0f;
			stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;//WRAP (包裹)

			pID3D12Device4->CreateSampler(&stSamplerDesc, pISampleHpSkybox->GetCPUDescriptorHandleForHeapStart());
			//---------------------------------------------------------------------------------------------

		}

		// 用捆绑包记录固化的命令
		{
			//球体的捆绑包
			pIBundlesEarth->SetGraphicsRootSignature(pIRootSignature.Get());
			pIBundlesEarth->SetPipelineState(pIPSOEarth.Get());

			ID3D12DescriptorHeap* ppHeapsEarth[] = { pISRVHpEarth.Get(),pISampleHpEarth.Get() };
			pIBundlesEarth->SetDescriptorHeaps(_countof(ppHeapsEarth), ppHeapsEarth);
			//设置SRV
			pIBundlesEarth->SetGraphicsRootDescriptorTable(0, pISRVHpEarth->GetGPUDescriptorHandleForHeapStart());

			D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleEarth = pISRVHpEarth->GetGPUDescriptorHandleForHeapStart();
			stGPUCBVHandleEarth.ptr += nSRVDescriptorSize;

			//设置CBV
			pIBundlesEarth->SetGraphicsRootDescriptorTable(1, stGPUCBVHandleEarth);

			D3D12_GPU_DESCRIPTOR_HANDLE hGPUSamplerEarth = pISampleHpEarth->GetGPUDescriptorHandleForHeapStart();
			hGPUSamplerEarth.ptr += (g_nCurrentSamplerNO * nSamplerDescriptorSize);

			//设置Sample
			pIBundlesEarth->SetGraphicsRootDescriptorTable(2, hGPUSamplerEarth);
			//注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
			pIBundlesEarth->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			pIBundlesEarth->IASetVertexBuffers(0, 1, &stVBVEarth);
			pIBundlesEarth->IASetIndexBuffer(&stIBVEarth);

			//Draw Call！！！
			pIBundlesEarth->DrawIndexedInstanced(nSphereIndexCnt, 1, 0, 0, 0);
			pIBundlesEarth->Close();



			//Skybox的捆绑包
			pIBundlesSkybox->SetPipelineState(pIPSOSkyBox.Get());
			pIBundlesSkybox->SetGraphicsRootSignature(pIRootSignature.Get());
			ID3D12DescriptorHeap* ppHeaps[] = { pISRVHpSkybox.Get(),pISampleHpSkybox.Get() };
			pIBundlesSkybox->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
			//设置SRV
			pIBundlesSkybox->SetGraphicsRootDescriptorTable(0, pISRVHpSkybox->GetGPUDescriptorHandleForHeapStart());

			D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleSkybox = pISRVHpSkybox->GetGPUDescriptorHandleForHeapStart();
			stGPUCBVHandleSkybox.ptr += nSRVDescriptorSize;
			//设置CBV
			pIBundlesSkybox->SetGraphicsRootDescriptorTable(1, stGPUCBVHandleSkybox);
			pIBundlesSkybox->SetGraphicsRootDescriptorTable(2, pISampleHpSkybox->GetGPUDescriptorHandleForHeapStart());
			pIBundlesSkybox->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			pIBundlesSkybox->IASetVertexBuffers(0, 1, &stVBVSkybox);

			//Draw Call！！！
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
		
		// 渲染相关开始
		// 记录帧开始时间，和当前时间，以循环结束为界
		ULONGLONG n64tmFrameStart = ::GetTickCount64();
		ULONGLONG n64tmCurrent = n64tmFrameStart;
		//计算旋转角度需要的变量
		double dModelRotationYAngle = 0.0f;

		DWORD dwRet = 0;
		BOOL bExit = FALSE;

		ShowWindow(hWnd, nCmdShow);
		UpdateWindow(hWnd);
		while (!bExit)
		{//注意这里我们调整了消息循环，将等待时间设置为0，同时将定时性的渲染，改成了每次循环都渲染
		 //但这不表示说MsgWait函数就没啥用了，坚持使用它是因为后面例子如果想加入多线程控制就非常简单了
			dwRet = ::MsgWaitForMultipleObjects(1, &hEventFence, FALSE, INFINITE, QS_ALLINPUT);
			switch (dwRet - WAIT_OBJECT_0)
			{
			case 0:
			{
				//OnUpdate()
				{// 准备一个简单的旋转MVP矩阵 让方块转起来
					n64tmCurrent = ::GetTickCount();
					//计算旋转的角度：旋转角度(弧度) = 时间(秒) * 角速度(弧度/秒)
					dModelRotationYAngle += ((n64tmCurrent - n64tmFrameStart) / 1000.0f) * g_fPalstance;//单位是毫秒，总之恒定一秒转速。g_fPalstance角速度(弧度/秒)

					n64tmFrameStart = n64tmCurrent;

					//旋转角度是2PI周期的倍数，去掉周期数，只留下相对0弧度开始的小于2PI的弧度即可
					if (dModelRotationYAngle > XM_2PI)
					{
						dModelRotationYAngle = fmod(dModelRotationYAngle, XM_2PI);//取余，不要让角度堆太多导致精度偏差
					}

					// 1. 摄像机飞行控制 (WASD + QE)
					
					// A. 计算当前的视线方向 (Forward) 和 右方向 (Right)
					// ---------------------------------------------------
					// 这里的数学原理：
					// Forward = (sinY*cosP, sinP, cosY*cosP) -> 球坐标转笛卡尔
					// Right   = (cosY, 0, -sinY)             -> 与 Forward 垂直的水平向量
					float r = cosf(g_fPitch);
					XMFLOAT3 f3Forward;
					f3Forward.x = r * sinf(g_fYaw);
					f3Forward.y = sinf(g_fPitch);
					f3Forward.z = r * cosf(g_fYaw);

					XMFLOAT3 f3Right;
					f3Right.x = cosf(g_fYaw);
					f3Right.y = 0.0f;
					f3Right.z = -sinf(g_fYaw);

					// 将 Float3 转为 Vector 以便计算
					XMVECTOR vForward = XMLoadFloat3(&f3Forward);
					XMVECTOR vRight = XMLoadFloat3(&f3Right);
					XMVECTOR vUpWorld = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
					XMVECTOR vEye = XMLoadFloat3(&g_f3EyePos);

					// B. 检测键盘输入 (异步采样，丝滑流畅)
					float moveSpeed = 0.1f; // 飞行速度

					// 按住 Shift 加速
					if (GetAsyncKeyState(VK_SHIFT) & 0x8000) moveSpeed *= 3.0f;

					// W / S : 前后飞行 (沿视线方向)
					if (GetAsyncKeyState('W') & 0x8000) vEye += vForward * moveSpeed;
					if (GetAsyncKeyState('S') & 0x8000) vEye -= vForward * moveSpeed;

					// A / D : 左右平移 (沿右向量方向)
					if (GetAsyncKeyState('D') & 0x8000) vEye += vRight * moveSpeed;
					if (GetAsyncKeyState('A') & 0x8000) vEye -= vRight * moveSpeed;

					// Q / E : 垂直升降 (沿世界 Y 轴)
					if (GetAsyncKeyState('Q') & 0x8000) vEye += vUpWorld * moveSpeed; // 升
					if (GetAsyncKeyState('E') & 0x8000) vEye -= vUpWorld * moveSpeed; // 降

					// C. 更新摄像机位置
					XMStoreFloat3(&g_f3EyePos, vEye);

					// 2. 球体控制 (方向键)
					// 这样你可以一边飞，一边微调球的位置
					float sphereSpeed = 0.05f;
					if (GetAsyncKeyState(VK_UP) & 0x8000) g_SpherePos.z += sphereSpeed;
					if (GetAsyncKeyState(VK_DOWN) & 0x8000) g_SpherePos.z -= sphereSpeed;
					if (GetAsyncKeyState(VK_LEFT) & 0x8000) g_SpherePos.x -= sphereSpeed;
					if (GetAsyncKeyState(VK_RIGHT) & 0x8000) g_SpherePos.x += sphereSpeed;

					// 3. 构建 View 矩阵
					// 目标点 = 眼睛 + 视线方向
					XMVECTOR vFocus = vEye + vForward;
					XMMATRIX xmView = XMMatrixLookAtLH(vEye, vFocus, vUpWorld);

					//投影，根据视场计算
					XMMATRIX xmProj = XMMatrixPerspectiveFovLH(XM_PIDIV4//四分之派
						, (FLOAT)iWndWidth / (FLOAT)iWndHeight, 1.0f, 2000.0f);//宽高比，
					
					// 变量名不好，这里是skybox的view拷贝earth的
					XMMATRIX xmSkyBox = xmView;
					
					
					xmSkyBox = XMMatrixMultiply(xmSkyBox, xmProj);//（xmView，xmProj）
					// 获取逆矩阵
					xmSkyBox = XMMatrixInverse(nullptr, xmSkyBox);//神奇，V和P的逆矩阵，加上M不动，就是MVP的逆矩阵

					//设置Skybox的MVP
					XMStoreFloat4x4(&pMVPBufSkybox->m_MVP, xmSkyBox);//我就说，这里是把xmSkyBox存早指针pMVPBufSkybox的容器里。这辅助函数跟DX的习惯又倒过来了。这里还从计算规格转为存储规格

					pMVPBufSkybox->m_v4EyePos = XMFLOAT4(g_f3EyePos.x, g_f3EyePos.y, g_f3EyePos.z, 1.0f);//出现第二次，设定view的时候用了这个，获得眼睛位置

					// 传入位置调用三线性差值
					SH9 finalSH = InterpolateProbeVolume(g_SpherePos);

					// 填入常量缓冲区。打开的map，这里才进行写入，也就是CBV指定的那块Upload一直在这等着写呢
					for (int i = 0; i < 9; i++) {
						pMVPBufEarth->m_SHCoeffs[i] = XMFLOAT4(
							finalSH.coeffs[i].x, finalSH.coeffs[i].y, finalSH.coeffs[i].z, 1.0f);
					}

					// A. 准备变换矩阵
					// 缩放
					XMMATRIX xmScale = XMMatrixScaling(fSphereSize, fSphereSize, fSphereSize);
					// 自转 (保留之前的旋转动画)
					XMMATRIX xmRot = XMMatrixRotationY(static_cast<float>(dModelRotationYAngle));
					// 位移 (使用 WASD 控制的 g_SpherePos)
					XMMATRIX xmTrans = XMMatrixTranslation(g_SpherePos.x, g_SpherePos.y, g_SpherePos.z);

					// B. 组合世界矩阵 (World Matrix)
					// 顺序：缩放 -> 旋转 -> 平移
					// 注意：XMMatrixMultiply 是左乘规则，或者是行主序的累乘，逻辑上是 Scale * Rot * Trans
					XMMATRIX xmWorld = XMMatrixMultiply(xmScale, xmRot);
					xmWorld = XMMatrixMultiply(xmWorld, xmTrans);

					// C. 存入 World 矩阵 (给 Shader 算法线用)
					XMStoreFloat4x4(&pMVPBufEarth->m_mWorld, xmWorld);

					// D. 计算 View * Projection
					XMMATRIX xmVP = XMMatrixMultiply(xmView, xmProj);

					// E. 计算最终 MVP (World * View * Projection)
					XMMATRIX xmFinalMVP = XMMatrixMultiply(xmWorld, xmVP);

					// F. 存入 MVP 常量缓冲
					XMStoreFloat4x4(&pMVPBufEarth->m_MVP, xmFinalMVP);

				}

				// 【修复】原作者没用上的5个samplers。检测采样器是否切换，如果切换了，必须重录 Bundle
				UINT nLastSamplerNO = 0;
				if (nLastSamplerNO != g_nCurrentSamplerNO)
				{
					// 重置命令分配器、重置命令列表
					GRS_THROW_IF_FAILED(pICmdAllocEarth->Reset());
					GRS_THROW_IF_FAILED(pIBundlesEarth->Reset(pICmdAllocEarth.Get(), pIPSOEarth.Get()));

					// 开始重录
					pIBundlesEarth->SetGraphicsRootSignature(pIRootSignature.Get());
					pIBundlesEarth->SetPipelineState(pIPSOEarth.Get());

					ID3D12DescriptorHeap* ppHeapsEarth[] = { pISRVHpEarth.Get(), pISampleHpEarth.Get() };
					pIBundlesEarth->SetDescriptorHeaps(_countof(ppHeapsEarth), ppHeapsEarth);

					// 设置 SRV
					pIBundlesEarth->SetGraphicsRootDescriptorTable(0, pISRVHpEarth->GetGPUDescriptorHandleForHeapStart());

					// 设置 CBV
					D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleEarth = pISRVHpEarth->GetGPUDescriptorHandleForHeapStart();
					stGPUCBVHandleEarth.ptr += nSRVDescriptorSize;
					pIBundlesEarth->SetGraphicsRootDescriptorTable(1, stGPUCBVHandleEarth);

					// 【关键点】这里会使用新的 g_nCurrentSamplerNO 计算新的地址
					D3D12_GPU_DESCRIPTOR_HANDLE hGPUSamplerEarth = pISampleHpEarth->GetGPUDescriptorHandleForHeapStart();
					hGPUSamplerEarth.ptr += (g_nCurrentSamplerNO * nSamplerDescriptorSize);
					pIBundlesEarth->SetGraphicsRootDescriptorTable(2, hGPUSamplerEarth);

					pIBundlesEarth->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					pIBundlesEarth->IASetVertexBuffers(0, 1, &stVBVEarth);
					pIBundlesEarth->IASetIndexBuffer(&stIBVEarth);

					pIBundlesEarth->DrawIndexedInstanced(nSphereIndexCnt, 1, 0, 0, 0);

					// 5. 封包
					pIBundlesEarth->Close();

					// 6. 更新状态，防止下一帧重复重录
					nLastSamplerNO = g_nCurrentSamplerNO;
				}

				//获取新的后缓冲序号，因为Present真正完成时后缓冲的序号就更新了
				nCurrentFrameIndex = pISwapChain3->GetCurrentBackBufferIndex();
				//命令分配器先Reset一下
				GRS_THROW_IF_FAILED(pICmdAllocDirect->Reset());
				//Reset命令列表，并重新指定命令分配器和PSO对象
				GRS_THROW_IF_FAILED(pICmdListDirect->Reset(pICmdAllocDirect.Get(), pIPSOEarth.Get()));

				// 通过资源屏障判定后缓冲已经切换完毕可以开始渲染了
				stBeginResBarrier.Transition.pResource = pIARenderTargets[nCurrentFrameIndex].Get();
				pICmdListDirect->ResourceBarrier(1, &stBeginResBarrier);

				//偏移描述符指针到指定帧缓冲视图位置
				D3D12_CPU_DESCRIPTOR_HANDLE stRTVHandle = pIRTVHeap->GetCPUDescriptorHandleForHeapStart();
				stRTVHandle.ptr += (nCurrentFrameIndex * nRTVDescriptorSize);
				D3D12_CPU_DESCRIPTOR_HANDLE stDSVHandle = pIDSVHeap->GetCPUDescriptorHandleForHeapStart();
				//设置渲染目标
				pICmdListDirect->OMSetRenderTargets(1, &stRTVHandle, FALSE, &stDSVHandle);
				pICmdListDirect->RSSetViewports(1, &stViewPort);
				pICmdListDirect->RSSetScissorRects(1, &stScissorRect);

				// 继续记录命令，并真正开始新一帧的渲染
				pICmdListDirect->ClearRenderTargetView(stRTVHandle, faClearColor, 0, nullptr);
				pICmdListDirect->ClearDepthStencilView(pIDSVHeap->GetCPUDescriptorHandleForHeapStart()
					, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

				//31、执行Skybox的捆绑包
				ID3D12DescriptorHeap* ppHeapsSkybox[] = { pISRVHpSkybox.Get(),pISampleHpSkybox.Get() };
				pICmdListDirect->SetDescriptorHeaps(_countof(ppHeapsSkybox), ppHeapsSkybox);
				pICmdListDirect->ExecuteBundle(pIBundlesSkybox.Get());//实际这里把PSO给载入进去了。就是对应的PSO

				//32、执行球体的捆绑包
				ID3D12DescriptorHeap* ppHeapsEarth[] = { pISRVHpEarth.Get(),pISampleHpEarth.Get() };
				pICmdListDirect->SetDescriptorHeaps(_countof(ppHeapsEarth), ppHeapsEarth);
				pICmdListDirect->ExecuteBundle(pIBundlesEarth.Get());
				

				//又一个资源屏障，用于确定渲染已经结束可以提交画面去显示了
				stEndResBarrier.Transition.pResource = pIARenderTargets[nCurrentFrameIndex].Get();
				pICmdListDirect->ResourceBarrier(1, &stEndResBarrier);
				//关闭命令列表，可以去执行了
				GRS_THROW_IF_FAILED(pICmdListDirect->Close());

				//执行命令列表
				ID3D12CommandList* ppCommandLists[] = { pICmdListDirect.Get() };
				pICMDQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);


				//提交画面
				GRS_THROW_IF_FAILED(pISwapChain3->Present(1, 0));


				//开始同步GPU与CPU的执行，先记录围栏标记值
				const UINT64 fence = n64FenceValue;
				GRS_THROW_IF_FAILED(pICMDQueue->Signal(pIFence.Get(), fence));
				n64FenceValue++;
				GRS_THROW_IF_FAILED(pIFence->SetEventOnCompletion(fence, hEventFence));
			}
			break;
			case 1:
			{//处理消息
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
	{//发生了COM异常
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

		// 监测鼠标右键按下
	case WM_RBUTTONDOWN:
	{
		g_bRightMouseDown = true;
		// 记录按下时的坐标
		g_LastMousePos.x = LOWORD(lParam);
		g_LastMousePos.y = HIWORD(lParam);
		// 捕获鼠标，防止拖出窗口外失效
		SetCapture(hWnd);
		// 隐藏光标
		ShowCursor(FALSE);
	}
	break;

	// 鼠标右键抬起
	case WM_RBUTTONUP:
	{
		g_bRightMouseDown = false;
		ReleaseCapture();
		ShowCursor(TRUE);
	}
	break;

	// 鼠标移动：计算旋转角度 (Yaw/Pitch)
	case WM_MOUSEMOVE:
	{
		if (g_bRightMouseDown)
		{
			// 获取当前鼠标位置
			int xPos = (short)LOWORD(lParam);
			int yPos = (short)HIWORD(lParam);

			// 计算位移量 (Delta)
			int dx = xPos - g_LastMousePos.x;
			int dy = yPos - g_LastMousePos.y;

			// 灵敏度 (Sensitivity)
			float fSens = 0.005f;

			// 更新角度
			// 注意：dx 对应 Yaw (左右转)，dy 对应 Pitch (上下看)
			g_fYaw += dx * fSens;

			// 【修改这里】: 把 += 改为 -= 
			// 因为屏幕坐标 Y 向上是减小，而我们需要 Pitch 向上是增加(抬头)
			g_fPitch -= dy * fSens;

			// 限制 Pitch 角度，防止翻跟头 (锁死在 +/- 85度)
			g_fPitch = max(-XM_PIDIV2 + 0.1f, min(XM_PIDIV2 - 0.1f, g_fPitch));

			// 更新“上一帧位置”
			g_LastMousePos.x = xPos;
			g_LastMousePos.y = yPos;
		}
	}
	break;

	case WM_KEYDOWN:
	{
		USHORT n16KeyCode = (wParam & 0xFF);

		// 空格键切换采样器
		if (VK_SPACE == n16KeyCode)
		{
			++g_nCurrentSamplerNO;
			g_nCurrentSamplerNO %= g_nSampleMaxCnt;
			TCHAR szTitle[MAX_PATH] = {};
			StringCchPrintf(szTitle, MAX_PATH, _T("Current Sampler Index: %d"), g_nCurrentSamplerNO);
			SetWindowText(hWnd, szTitle);
		}

		// 复位功能 (TAB)
		if (VK_TAB == n16KeyCode)
		{
			g_SpherePos = XMFLOAT3(0.0f, 0.0f, 0.0f);
			g_f3EyePos = XMFLOAT3(0.0f, 2.0f, -10.0f);
			g_f3LockAt = XMFLOAT3(0.0f, 0.0f, 1.0f);
			g_fYaw = 0.0f;
			g_fPitch = 0.0f;
		}
		// 注意：WASD 和 方向键的逻辑我们移到 OnUpdate 里去处理了
		// 因为 WndProc 处理按键会有延迟和卡顿，不适合丝滑飞行。
	}
	break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

