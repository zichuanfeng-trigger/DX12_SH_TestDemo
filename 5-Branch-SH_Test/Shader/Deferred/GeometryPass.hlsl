// ============================================================================
// Geometry Pass Shader
// ============================================================================
// 延迟渲染第一阶段：将几何信息写入 G-Buffer
// 输出：
//   SV_Target0: Albedo (RGB)
//   SV_Target1: Normal View Space (RGB)
//   SV_Target2: Position View Space (RGB)
// ============================================================================

// ============================================================================
// 常量缓冲区
// ============================================================================
cbuffer GeometryConstants : register(b0)
{
    row_major float4x4 WorldViewProj;  // 世界-视图-投影矩阵
    row_major float4x4 WorldView;      // 世界-视图矩阵（用于法线变换）
};

// ============================================================================
// 输入输出结构
// ============================================================================
struct VSInput
{
    float4 Position : POSITION;  // xyz: position, w: 1.0
    float3 Normal   : NORMAL;    // normal
    float2 UV       : TEXCOORD;  // texture coordinate
};

struct PSInput
{
    float4 Position     : SV_POSITION;  // 裁剪空间位置
    float3 NormalVS     : NORMAL;       // 视图空间法线
    float3 PositionVS   : TEXCOORD0;    // 视图空间位置
};

struct PSOutput
{
    float4 Albedo    : SV_Target0;  // RT0: Albedo
    float4 Normal    : SV_Target1;  // RT1: Normal View Space
    float4 Position  : SV_Target2;  // RT2: Position View Space
};

// ============================================================================
// 顶点着色器
// ============================================================================
PSInput VSMain(VSInput input)
{
    PSInput output;
    
    // 变换到裁剪空间
    output.Position = mul(input.Position, WorldViewProj);
    
    // 变换到视图空间（用于法线）
    float3 normalVS = mul(input.Normal, (float3x3)WorldView);
    output.NormalVS = normalize(normalVS);
    
    // 计算视图空间位置
    float4 positionVS = mul(input.Position, WorldView);
    output.PositionVS = positionVS.xyz;
    
    return output;
}

// ============================================================================
// 像素着色器
// ============================================================================
PSOutput PSMain(PSInput input)
{
    PSOutput output;
    
    // Albedo: 简单的灰白棋盘格纹理（ procedural ）
    float2 uv = input.PositionVS.xy * 0.5 + 0.5;
    float checker = fmod(floor(uv.x * 8) + floor(uv.y * 8), 2.0);
    float3 albedo = lerp(float3(0.8, 0.8, 0.8), float3(0.3, 0.3, 0.3), checker);
    output.Albedo = float4(albedo, 1.0);
    
    // Normal: 视图空间法线，编码到 [0,1] 范围
    float3 normalEncoded = input.NormalVS * 0.5 + 0.5;
    output.Normal = float4(normalEncoded, 1.0);
    
    // Position: 视图空间位置
    output.Position = float4(input.PositionVS, 1.0);
    
    return output;
}
