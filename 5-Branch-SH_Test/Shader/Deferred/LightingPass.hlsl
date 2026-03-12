// ============================================================================
// Lighting Pass Shader
// ============================================================================
// 延迟渲染第二阶段：基于 G-Buffer 计算光照
// 输入：全屏四边形 (NDC 空间)
// 输出：最终颜色
// ============================================================================

// ============================================================================
// 常量缓冲区
// ============================================================================
struct PointLight
{
    float3 Position;
    float Radius;
    float3 Color;
    float Intensity;
};

cbuffer LightingConstants : register(b0)
{
    float4 CameraPos;        // 相机位置
    PointLight Lights[4];    // 最多4个点光源
    uint LightCount;         // 实际光源数量
    float3 Pad;              // 对齐填充
};

// ============================================================================
// G-Buffer 纹理
// ============================================================================
Texture2D<float4> GBufferAlbedo   : register(t0);  // RT0: Albedo
Texture2D<float4> GBufferNormal   : register(t1);  // RT1: Normal View Space
Texture2D<float4> GBufferPosition : register(t2);  // RT2: Position View Space

SamplerState PointClamp : register(s0);

// ============================================================================
// 输入输出结构
// ============================================================================
struct VSInput
{
    float4 Position : POSITION;  // NDC 坐标
};

struct PSInput
{
    float4 Position : SV_POSITION;  // 屏幕空间位置
    float2 UV       : TEXCOORD0;    // 纹理坐标 [0,1]
};

// ============================================================================
// 顶点着色器
// ============================================================================
PSInput VSMain(VSInput input)
{
    PSInput output;
    output.Position = input.Position;
    
    // 从 NDC [-1,1] 转换到 UV [0,1]
    output.UV = input.Position.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    
    return output;
}

// ============================================================================
// 光照计算
// ============================================================================
float3 ComputePointLight(PointLight light, float3 positionVS, float3 normalVS, float3 albedo)
{
    // 光源到片元的向量（视图空间）
    float3 lightDir = light.Position - positionVS;
    float distance = length(lightDir);
    
    // 超出范围则返回黑色
    if (distance > light.Radius)
        return float3(0.0, 0.0, 0.0);
    
    lightDir = normalize(lightDir);
    
    // Lambert 漫反射
    float NdotL = max(dot(normalVS, lightDir), 0.0);
    
    // 距离衰减
    float attenuation = 1.0 - saturate(distance / light.Radius);
    attenuation = attenuation * attenuation;
    
    // 最终光照贡献
    return albedo * light.Color * NdotL * attenuation * light.Intensity;
}

// ============================================================================
// 像素着色器
// ============================================================================
float4 PSMain(PSInput input) : SV_Target
{
    // 采样 G-Buffer
    float4 albedoData = GBufferAlbedo.SampleLevel(PointClamp, input.UV, 0);
    float4 normalData = GBufferNormal.SampleLevel(PointClamp, input.UV, 0);
    float4 positionData = GBufferPosition.SampleLevel(PointClamp, input.UV, 0);
    
    // 解码数据
    float3 albedo = albedoData.rgb;
    float3 normalVS = normalData.rgb * 2.0 - 1.0;  // 从 [0,1] 解码到 [-1,1]
    float3 positionVS = positionData.rgb;
    
    // 背景检测（没有几何体的像素）
    if (length(normalVS) < 0.01)
    {
        return float4(0.05, 0.05, 0.1, 1.0);  // 深色背景
    }
    
    normalVS = normalize(normalVS);
    
    // 累加所有光源的贡献
    float3 finalColor = float3(0.0, 0.0, 0.0);
    
    for (uint i = 0; i < LightCount; i++)
    {
        finalColor += ComputePointLight(Lights[i], positionVS, normalVS, albedo);
    }
    
    // 添加环境光
    float3 ambient = albedo * 0.1;
    finalColor += ambient;
    
    // 色调映射 (Reinhard)
    finalColor = finalColor / (finalColor + 1.0);
    
    // Gamma 校正
    finalColor = pow(max(finalColor, 0.001), 1.0 / 2.2);
    
    return float4(finalColor, 1.0);
}
