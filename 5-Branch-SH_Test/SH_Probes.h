#pragma once
#include <DirectXMath.h>
#include <vector>
#include <algorithm> // for max/min

using namespace DirectX;
using std::min;
using std::max;

// 简单的 9 系数结构体
struct SH9 
{
    XMFLOAT3 coeffs[9];

    // 初始化为 0
    void Zero() 
    {
        for (int i = 0; i < 9; ++i) coeffs[i] = XMFLOAT3(0, 0, 0);
    }

    // 累加
    void Add(const SH9& other) 
    {
        for (int i = 0; i < 9; ++i) 
        {
            coeffs[i].x += other.coeffs[i].x;
            coeffs[i].y += other.coeffs[i].y;
            coeffs[i].z += other.coeffs[i].z;
        }
    }
};

// 将一点光投影为 SH
// 包含：Basis(形状) + Cosine(漫反射卷积) + Windowing(抗振铃降噪)
SH9 ProjectDirectionalLight(XMFLOAT3 dir, XMFLOAT3 color)
{
    SH9 result;

    // 1. 归一化方向
    XMVECTOR vDir = XMLoadFloat3(&dir);//转换为寄存器形态！
    vDir = XMVector3Normalize(vDir);
    XMStoreFloat3(&dir, vDir);//切回来
    float x = dir.x;
    float y = dir.y;
    float z = dir.z;


    // A. 定义系数 
    
    // [1] SH Basis (Y): 描述形状的数学常数 (基本不变)
    // Band 0
    float Y0 = 0.282095f;
    // Band 1
    float Y1_y = 0.488603f * y;
    float Y1_z = 0.488603f * z;
    float Y1_x = 0.488603f * x;
    // Band 2
    float Y2_xy = 1.092548f * x * y;
    float Y2_yz = 1.092548f * y * z;
    float Y2_z2 = 0.315392f * (3.0f * z * z - 1.0f);
    float Y2_xz = 1.092548f * x * z;
    float Y2_x2y2 = 0.546274f * (x * x - y * y);

    // [2] Cosine Convolution (A): 漫反射积分常数
    // 使得 SH 存储的是 Irradiance (反射) 而不是 Radiance（入射）
    float A0 = 3.141593f; // PI
    float A1 = 2.094395f; // 2PI / 3
    float A2 = 0.785398f; // PI / 4

    // [3] Windowing (W): Anti振铃/吉布斯现象系数 (可调参数)
    // 这一步是为了解决"透光"和"波纹"。
    // 我们人为地压低高频信号(L2)，让光照稍微"糊"一点，没那么锐利
    // 也是 Peter-Pike Sloan 推荐的 Hanning Window 近似值
    float W0 = 1.0f;     // L0 不变
    float W1 = 0.9f;     // L1 稍微压一点点
    float W2 = 0.6f;     // L2 狠狠压住！



    // B. 合成计算

    // Lambda: 最终系数 = 颜色 * 形状(Y) * 漫反射(A) * 降噪(W)
    auto SetCoeff = [&](int idx, float basis, float cosineFactor, float window)
        {
            // 这一行是整个 SH 烘焙的精髓：SH系数大小
            float scale = basis * cosineFactor * window;

            result.coeffs[idx].x = color.x * scale;
            result.coeffs[idx].y = color.y * scale;
            result.coeffs[idx].z = color.z * scale;
        };

    // Band 0 
    SetCoeff(0, Y0, A0, W0);

    // Band 1
    SetCoeff(1, Y1_y, A1, W1);
    SetCoeff(2, Y1_z, A1, W1);
    SetCoeff(3, Y1_x, A1, W1);

    // Band 2 
    SetCoeff(4, Y2_xy, A2, W2);
    SetCoeff(5, Y2_yz, A2, W2);
    SetCoeff(6, Y2_z2, A2, W2);
    SetCoeff(7, Y2_xz, A2, W2);
    SetCoeff(8, Y2_x2y2, A2, W2);

    return result;
}

// 定义场景数据 
struct MyLight {
    XMFLOAT3 pos;    // 光源位置 (不再环境贴图那样无穷远了)
    XMFLOAT3 color;  // 颜色强度
    float radius;    // 照射半径 (超过这个范围光就衰减没了)
};

struct MyProbe {
    XMFLOAT3 pos;    // 世界坐标
    SH9 shData;      // 烘焙后的数据
};

// 全局变量管理场景
std::vector<MyLight> g_SceneLights;
std::vector<MyProbe> g_Probes; // 8 个探针

// 初始化场景数据（灯光、probes）
void InitSceneData() 
{
    // 注：左手系，手势像是”我有个点子“的 MAX 版
    // 1. 红灯：放在左-前-上
    // 位置 (-5, 5, 5)，颜色红，半径 15米
    g_SceneLights.push_back({ XMFLOAT3(-5.0f, 5.0f, 5.0f), XMFLOAT3(3.0f, 0.0f, 0.0f), 15.0f });

    // 2. 青灯：放在右-前-上
    // 位置 (5, 5, 5)，颜色青，半径 15米
    g_SceneLights.push_back({ XMFLOAT3(5.0f, 5.0f, 5.0f), XMFLOAT3(0.0f, 3.0f, 3.0f), 15.0f });

    // 3. 顶光：放在正中心的天花板上
    // 位置 (0, 8, 0)，颜色白，半径 20米 (覆盖全场)
    g_SceneLights.push_back({ XMFLOAT3(0.0f, 8.0f, 0.0f), XMFLOAT3(0.8f, 0.8f, 0.8f), 20.0f });

    // 2. 定义 8 个探针的位置 (2x2x2 网格)
    // 假设场景范围是 -10 到 10
    float minP = -10.0f;
    float maxP = 10.0f;

    g_Probes.resize(8);

    /* 
    //// 极简的 0~7 索引构建位置
    //// 写的真高级，一点也不简单，给我看力竭了，我的话直接把probe的位置写死得了，哪敢写这种for循环定义炫技。
    //// 想了解这段看我下面二进制注释
    //for (int i = 0; i < 8; i++) {
    //    float x = (i & 1) ? maxP : minP;
    //    float y = (i & 2) ? maxP : minP; // 注意 Y 轴
    //    float z = (i & 4) ? maxP : minP;
    //    g_Probes[i].pos = XMFLOAT3(x, y, z);
    //    g_Probes[i].shData.Zero(); // 清空数据等待烘焙
    //}
    */

    
    // --- 底层 (Y = minP) ---

    // 探针0: 左-下-后
    // 二进制 000 (X=min, Y=min, Z=min)
    g_Probes[0].pos = XMFLOAT3(minP, minP, minP);
    g_Probes[0].shData.Zero();

    // 探针1: 右-下-后
    // 二进制 001 (X=max, Y=min, Z=min)
    g_Probes[1].pos = XMFLOAT3(maxP, minP, minP);
    g_Probes[1].shData.Zero();

    // 探针4: 左-下-前
    // 二进制 100 (X=min, Y=min, Z=max)
    g_Probes[4].pos = XMFLOAT3(minP, minP, maxP);
    g_Probes[4].shData.Zero();

    // 探针5: 右-下-前
    // 二进制 101 (X=max, Y=min, Z=max)
    g_Probes[5].pos = XMFLOAT3(maxP, minP, maxP);
    g_Probes[5].shData.Zero();


    // --- 顶层 (Y = maxP) ---

    // 探针2: 左-上-后
    // 二进制 010 (X=min, Y=max, Z=min)
    g_Probes[2].pos = XMFLOAT3(minP, maxP, minP);
    g_Probes[2].shData.Zero();

    // 探针3: 右-上-后
    // 二进制 011 (X=max, Y=max, Z=min)
    g_Probes[3].pos = XMFLOAT3(maxP, maxP, minP);
    g_Probes[3].shData.Zero();

    // 探针6: 左-上-前
    // 二进制 110 (X=min, Y=max, Z=max)
    g_Probes[6].pos = XMFLOAT3(minP, maxP, maxP);
    g_Probes[6].shData.Zero();

    // 探针7: 右-上-前
    // 二进制 111 (X=max, Y=max, Z=max)
    g_Probes[7].pos = XMFLOAT3(maxP, maxP, maxP);
    g_Probes[7].shData.Zero();
}

// 简单的烘焙SH
void BakeProbes() 
{
    for (auto& probe : g_Probes)
    /*for (int i = 0; i < g_Probes.size(); i++)  语法糖形式，自动遍历g_Probes数组里的每一个元素
    {
    // 每次都要用 [i] 去访问，而且要定义类型 MyProbe&
    MyProbe& probe = g_Probes[i];  
    // ... 后面的逻辑
    }*/

    {
        // 遍历所有灯光
        for (const auto& light : g_SceneLights) 
        {

            // 1. 算距离 (Distance)
            XMVECTOR vLightPos = XMLoadFloat3(&light.pos);
            XMVECTOR vProbePos = XMLoadFloat3(&probe.pos);
            XMVECTOR vToProbe = vProbePos - vLightPos;

            float dist = XMVectorGetX(XMVector3Length(vToProbe));

            // 2. 算衰减 (Attenuation) - 简单的线性衰减
            // 如果距离超过半径，光照就是0；距离越近越亮
            if (dist >= light.radius) continue; // 太远了照不到

            float attenuation = 1.0f - (dist / light.radius);//
            // 让衰减更平滑一点 (可选，平方衰减)
            attenuation = attenuation * attenuation;

            // 3. 算光的方向 (Direction) - 指向探针
            XMVECTOR vDir = XMVector3Normalize(vToProbe); // 光射向探针的方向
            XMFLOAT3 dir;
            XMStoreFloat3(&dir, vDir);//dir适合存储的类型

            // 4. 算最终打在探针上的颜色
            // 颜色 = 灯光原色 * 衰减系数
            XMFLOAT3 finalColor;
            finalColor.x = light.color.x * attenuation;
            finalColor.y = light.color.y * attenuation;
            finalColor.z = light.color.z * attenuation;

            // 5. 投影进 SH 并累加
            // 这里的 ProjectDirectionalLight 还是用之前的逻辑
            // 哪怕是点光源，对于探针这个“点”来说，也是从某个方向射来的一束光
            SH9 lightSH = ProjectDirectionalLight(dir, finalColor);
            probe.shData.Add(lightSH);
        }
    }
}

// 线性插值辅助函数
SH9 LerpSH(const SH9& a, const SH9& b, float t) 
{
    SH9 res;
    for (int i = 0; i < 9; i++) {
        XMVECTOR va = XMLoadFloat3(&a.coeffs[i]);
        XMVECTOR vb = XMLoadFloat3(&b.coeffs[i]);
        XMVECTOR vres = XMVectorLerp(va, vb, t);
        XMStoreFloat3(&res.coeffs[i], vres);
    }
    return res;
}

// 三线性插值
// 输入：球的位置
// 输出：该位置混合后的 SH 数据
SH9 InterpolateProbeVolume(XMFLOAT3 pos) 
{
    // 1. 定义包围盒范围 (必须与 InitSceneData 里的一致)
    float minP = -10.0f;
    float maxP = 10.0f;
    float range = maxP - minP;

    // 2. 算出归一化坐标 UVW (0.0 ~ 1.0)
    // saturate 保护一下防止出界
    float u = max(0.0f, min(1.0f, (pos.x - minP) / range));
    float v = max(0.0f, min(1.0f, (pos.y - minP) / range));
    float w = max(0.0f, min(1.0f, (pos.z - minP) / range));

    // 3. 提取探针数据 (假设 g_Probes 顺序就是 0~7)
    // 底层 (Y=min)
    const SH9& c000 = g_Probes[0].shData; // 左下后
    const SH9& c100 = g_Probes[1].shData; // 右下后
    const SH9& c001 = g_Probes[4].shData; // 左下前
    const SH9& c101 = g_Probes[5].shData; // 右下前

    // 顶层 (Y=max)
    const SH9& c010 = g_Probes[2].shData; // 左上后
    const SH9& c110 = g_Probes[3].shData; // 右上后
    const SH9& c011 = g_Probes[6].shData; // 左上前
    const SH9& c111 = g_Probes[7].shData; // 右上前

    // 4. X轴插值 (左右混合)
    SH9 c00 = LerpSH(c000, c100, u); // 下后
    SH9 c01 = LerpSH(c001, c101, u); // 下前
    SH9 c10 = LerpSH(c010, c110, u); // 上后
    SH9 c11 = LerpSH(c011, c111, u); // 上前

    // 5. Y轴插值 (上下混合)
    SH9 c0 = LerpSH(c00, c10, v); // 后
    SH9 c1 = LerpSH(c01, c11, v); // 前

    // 6. Z轴插值 (前后混合)
    return LerpSH(c0, c1, w);
}