# DX12 延迟渲染器 (Deferred Renderer) 改造计划

## 项目概述
将现有的 `5-SkyBox.cpp` 改造为 **单文件 MVP 级延迟渲染器 Demo**。

---

## 延迟渲染核心概念

延迟渲染将光照计算推迟到几何体渲染之后，分为两个阶段：

1. **几何阶段 (Geometry Pass)**：将场景几何信息写入 G-Buffer
2. **光照阶段 (Lighting Pass)**：基于 G-Buffer 计算光照

---

## G-Buffer 布局

| RT | 格式 | 存储内容 |
|----|------|----------|
| RT0 | `R8G8B8A8_UNORM` | Albedo (RGB) |
| RT1 | `R16G16B16A16_FLOAT` | Normal (RGB) |
| RT2 | `R32G32B32A32_FLOAT` | Position (View Space, RGB) |
| Depth | `D32_FLOAT` | Depth Buffer |

---

## 改造步骤

### Phase 1: 资源创建

#### 1.1 G-Buffer 资源
```cpp
ComPtr<ID3D12Resource> g_GBufferAlbedo;      // RT0
ComPtr<ID3D12Resource> g_GBufferNormal;      // RT1
ComPtr<ID3D12Resource> g_GBufferPosition;    // RT2
ComPtr<ID3D12DescriptorHeap> g_GBufferRTVHeap;  // 3个RTV
ComPtr<ID3D12DescriptorHeap> g_GBufferSRVHeap;  // 3个SRV
```

#### 1.2 PSO 对象
```cpp
ComPtr<ID3D12PipelineState> g_PSOGeometry;   // 几何阶段
ComPtr<ID3D12PipelineState> g_PSOLighting;   // 光照阶段
```

#### 1.3 着色器文件
- `GeometryPass.hlsl` - 写入 G-Buffer
- `LightingPass.hlsl` - 读取 G-Buffer 计算光照

---

### Phase 2: 着色器实现

#### 2.1 GeometryPass.hlsl
输入：顶点位置、法线、UV
输出：
- SV_Target0: Albedo
- SV_Target1: Normal (View Space)
- SV_Target2: Position (View Space)

#### 2.2 LightingPass.hlsl
输入：全屏四边形 (NDC 坐标)
输出：最终颜色

逻辑：
- 从 G-Buffer SRV 采样
- 计算 Lambert 漫反射
- 支持 4 个点光源

---

### Phase 3: 渲染流程

```cpp
// 原流程 (前向渲染):
// Clear RT -> Draw Skybox -> Draw Sphere -> Present

// 新流程 (延迟渲染):
// 1. Geometry Pass
//    - Clear G-Buffer
//    - Set G-Buffer as RT
//    - Draw Sphere
//
// 2. Lighting Pass
//    - Set BackBuffer as RT
//    - Bind G-Buffer SRVs
//    - Draw Fullscreen Quad
//
// 3. Present
```

---

### Phase 4: 移除球谐系统

删除以下文件和代码：
- `SH_Probes.h` - 整个文件删除
- `SphereSH.hlsl` - 替换为 `GeometryPass.hlsl`
- 移除 `InitSceneData()` 和 `BakeProbes()` 调用
- 移除 SH 相关的常量缓冲区成员

---

### Phase 5: 数据结构

```cpp
// G-Buffer 配置
static constexpr UINT GBUFFER_COUNT = 3;
static constexpr DXGI_FORMAT GBUFFER_FORMATS[GBUFFER_COUNT] = {
    DXGI_FORMAT_R8G8B8A8_UNORM,      // Albedo
    DXGI_FORMAT_R16G16B16A16_FLOAT,  // Normal
    DXGI_FORMAT_R32G32B32A32_FLOAT   // Position
};

// 光源数据
struct PointLight {
    XMFLOAT3 Position;
    float Radius;
    XMFLOAT3 Color;
    float Intensity;
};

// 光照常量缓冲区
struct LightingConstants {
    XMFLOAT4 CameraPos;
    PointLight Lights[4];
    UINT LightCount;
};

// 几何阶段常量缓冲区
struct GeometryConstants {
    XMFLOAT4X4 WorldViewProj;
    XMFLOAT4X4 World;
    XMFLOAT4X4 WorldView;  // For normal transform
};
```

---

### Phase 6: 根签名

#### 几何阶段根签名
- Slot 0: CBV (GeometryConstants)
- Slot 1: SRV (Albedo Texture)
- Slot 2: Sampler

#### 光照阶段根签名
- Slot 0: CBV (LightingConstants)
- Slot 1: SRV (G-Buffer Albedo)
- Slot 2: SRV (G-Buffer Normal)
- Slot 3: SRV (G-Buffer Position)

---

## 文件变更

```
5-SkyBox.cpp -> DeferredRenderer.cpp (重命名)
Shader/
  ├── SkyBox.hlsl (保留)
  ├── SphereSH.hlsl -> GeometryPass.hlsl (替换)
  └── LightingPass.hlsl (新增)
删除: SH_Probes.h
```

---

## 验证清单

- [ ] G-Buffer 正确创建
- [ ] 几何阶段写入 G-Buffer
- [ ] 光照阶段采样 G-Buffer
- [ ] 最终画面显示光照
- [ ] 相机控制正常
