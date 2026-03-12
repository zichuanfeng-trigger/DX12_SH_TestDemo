// ============================================================================
// Skybox Shader
// ============================================================================
// 全屏天空盒渲染，使用逆视投影矩阵重建世界方向
// ============================================================================

// ============================================================================
// 常量缓冲区
// ============================================================================
cbuffer SkyboxConstants : register(b0)
{
    row_major float4x4 InvViewProj;  // 逆视图-投影矩阵
};

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
    float3 Direction : TEXCOORD0;   // 世界空间方向
};

// ============================================================================
// 顶点着色器
// ============================================================================
PSInput VSMain(VSInput input)
{
    PSInput output;
    output.Position = input.Position;
    
    // 将 NDC 坐标通过逆矩阵变换到世界空间方向
    float4 worldPos = mul(input.Position, InvViewProj);
    output.Direction = normalize(worldPos.xyz / worldPos.w);
    
    return output;
}

// ============================================================================
// 噪声函数（ procedural sky ）
// ============================================================================
float Hash(float3 p)
{
    p = float3(dot(p, float3(127.1, 311.7, 74.7)),
               dot(p, float3(269.5, 183.3, 246.1)),
               dot(p, float3(113.5, 271.9, 124.6)));
    return frac(sin(dot(p, float3(1.0, 1.0, 1.0))) * 43758.5453);
}

float Noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float n = lerp(
        lerp(
            lerp(Hash(i + float3(0,0,0)), Hash(i + float3(1,0,0)), f.x),
            lerp(Hash(i + float3(0,1,0)), Hash(i + float3(1,1,0)), f.x),
            f.y
        ),
        lerp(
            lerp(Hash(i + float3(0,0,1)), Hash(i + float3(1,0,1)), f.x),
            lerp(Hash(i + float3(0,1,1)), Hash(i + float3(1,1,1)), f.x),
            f.y
        ),
        f.z
    );
    return n;
}

float FBM(float3 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for (int i = 0; i < 4; i++)
    {
        value += amplitude * Noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    
    return value;
}

// ============================================================================
// 像素着色器
// ============================================================================
float4 PSMain(PSInput input) : SV_Target
{
    float3 dir = normalize(input.Direction);
    
    // 天空渐变
    float3 skyColorTop = float3(0.2, 0.4, 0.8);    // 天顶颜色
    float3 skyColorBottom = float3(0.6, 0.8, 1.0); // 地平线颜色
    
    float t = saturate(dir.y * 0.5 + 0.5);
    float3 skyColor = lerp(skyColorBottom, skyColorTop, t);
    
    // 添加云层
    float cloudNoise = FBM(dir * 3.0 + float3(0.0, 1.0, 0.0));
    float cloudMask = smoothstep(0.4, 0.6, cloudNoise) * saturate(dir.y + 0.2);
    float3 cloudColor = float3(1.0, 1.0, 1.0);
    
    skyColor = lerp(skyColor, cloudColor, cloudMask * 0.5);
    
    // 添加太阳
    float3 sunDir = normalize(float3(0.3, 0.8, -0.5));
    float sunDot = dot(dir, sunDir);
    float sunMask = smoothstep(0.995, 1.0, sunDot);
    skyColor = lerp(skyColor, float3(1.0, 0.95, 0.8), sunMask);
    
    // 太阳周围的光晕
    float glowMask = smoothstep(0.9, 1.0, sunDot) * 0.3;
    skyColor += float3(1.0, 0.9, 0.7) * glowMask;
    
    return float4(skyColor, 1.0);
}
