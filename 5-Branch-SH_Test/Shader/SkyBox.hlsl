
cbuffer cbPerObject : register(b0)
{
    // 这个矩阵是 "Inverse View-Projection" (逆视投影矩阵)
    row_major matrix g_mWorldViewProjection;
}


// 天空盒是一张 TextureCube (立方体纹理)，由6张图组成
TextureCube g_EnvironmentTexture : register(t0);

SamplerState g_sam : register(s0);


// 输入输出结构体
struct SkyboxVS_Input
{
    // 输入的顶点位置
    // 在全屏四边形技术中，这里通常是 NDC 坐标 (x: -1~1, y: -1~1, z: 1.0)
    float4 Pos : POSITION;
};

struct SkyboxVS_Output
{
    float4 Pos : SV_POSITION;
    float3 Tex : TEXCOORD0;
};


SkyboxVS_Output SkyboxVS(SkyboxVS_Input Input)
{
    SkyboxVS_Output Output;

    // 输入的模型是一个铺满屏幕的四边形，这里直接把坐标传给光栅化器，
    // 确保画出来的东西覆盖整个屏幕
    Output.Pos = Input.Pos;

    
    // Input.Pos 是屏幕上的点
    // 乘以存入的逆矩阵后，我们希望得到 "在这个屏幕像素点，摄像机看向世界的方向"
    Output.Tex = normalize(mul(Input.Pos, g_mWorldViewProjection));

    return Output;
}


float4 SkyboxPS(SkyboxVS_Output Input) : SV_TARGET
{
    // 使用 3D 向量 (Input.Tex) 对立方体纹理 (TextureCube) 进行采样
    return g_EnvironmentTexture.Sample(g_sam, Input.Tex);
}