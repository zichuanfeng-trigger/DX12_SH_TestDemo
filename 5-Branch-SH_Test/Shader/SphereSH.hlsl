
// SphereSH.hlsl 

// 1. 常量缓冲区
cbuffer MVPBuffer : register(b0)
{
    float4x4 m_MVP;     // 投影矩阵
    float4x4 m_World;   // 世界矩阵
    float4   m_EyePos;  // 眼睛位置
    float4   m_SH[9];   // SH 系数 (CPU 传过来的)
};

// 2. 顶点输入
struct VSInput
{
    float4 position : POSITION;
    float2 uv       : TEXCOORD;
    float3 normal   : NORMAL;   
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;   
};


// 顶点着色器

PSInput VSMain(VSInput input)
{
    PSInput result;

    // 矩阵乘法顺序：mul(向量, 矩阵) Row-Major
    result.position = mul(input.position, m_MVP);

    // 法线变换：同样使用 mul(向量, 矩阵)
    // 截取 m_World 的前 3x3 负责旋转
    result.normal = normalize(mul(input.normal, (float3x3)m_World));
    
    return result;
}

// 像素着色器

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);

    // Band 0: Y0 
    float C0 = 0.282095; 
    
    // Band 1: Y1 
    float C1 = 0.488603; 
    
    // Band 2: 
    float C2 = 1.092548;     // Y2 (对应 xy, yz, xz, x2-y2)
    float C2_Z = 0.315392;   // Y2 (对应 3z^2-1)
    float C2_XY = 0.546274;  // Y2 (对应 x2-y2，注意这里通常是 0.546)

    // 3. 重建光照
    float3 irradiance = float3(0, 0, 0);

    // Band 0
    irradiance += m_SH[0].rgb * C0;

    // Band 1
    irradiance += m_SH[1].rgb * (n.y * C1);
    irradiance += m_SH[2].rgb * (n.z * C1);
    irradiance += m_SH[3].rgb * (n.x * C1);

    // Band 2
    irradiance += m_SH[4].rgb * (n.x * n.y) * C2;
    irradiance += m_SH[5].rgb * (n.y * n.z) * C2;
    irradiance += m_SH[6].rgb * (3.0 * n.z * n.z - 1.0) * C2_Z;
    irradiance += m_SH[7].rgb * (n.x * n.z) * C2;
    irradiance += m_SH[8].rgb * (n.x * n.x - n.y * n.y) * C2_XY;

    // 后处理加强效果
    
    float3 albedo = float3(1.0, 1.0, 1.0);
    float3 ambientBoost = float3(0.02, 0.02, 0.02);
    
    // 6.0 ，但作为一个强风格化的 Demo 是可以接受
    float exposure = 6.0; 

    float3 finalColor = (irradiance + ambientBoost) * albedo * exposure;
    finalColor = finalColor / (finalColor + 1.0);
    finalColor = pow(max(0.001, finalColor), 1.0/2.2);

    return float4(finalColor, 1.0);
}