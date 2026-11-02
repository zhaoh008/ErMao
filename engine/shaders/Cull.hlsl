// ErMao Engine - GPU 实例剔除（GAMES104 第 22 讲）
//
// 思路（最小闭环）：
//   1. CPU 把实例数据（144B）上传到 StructuredBuffer；
//   2. 每个线程处理一个实例：用世界矩阵把 8 个角点变换到世界空间，测试 6 个平面；
//   3. 可见实例的 **整块 144B 数据** 拷进压缩缓冲（UAV + VERTEX_BUFFER）；
//   4. 用 InterlockedAdd 直接改间接绘制参数里的 InstanceCount；
//   5. CPU 只发 DrawIndexedInstancedIndirect，不再逐实例处理。
//
// 这样顶点着色器完全不用改：压缩缓冲的布局与原实例缓冲一致，直接当 instance data 绑定。

cbuffer CullParams : register(b0)
{
    float4 gPlanes[6];      // xyz=法线, w=d；平面内测试 dot(n,p)+d >= 0
    float4 gLocalMin;       // 网格局部 AABB
    float4 gLocalMax;
    uint   gSourceOffset;   // 本批次在源缓冲里的起始实例
    uint   gInstanceCount;  // 本批次实例数
    uint   gPad0;
    uint   gPad1;
};

struct Instance
{
    float4 c0, c1, c2, c3; // 世界矩阵 4 列
    float4 n0, n1, n2;     // 法线矩阵 3 列
    float4 albedo;
    float4 params;
};

StructuredBuffer<Instance> gSource : register(t0);
RWStructuredBuffer<Instance> gCompact : register(u0);
RWByteAddressBuffer gArgs : register(u1); // { IndexCount, InstanceCount, StartIndex, BaseVertex, StartInstance }

void TestCorner(inout float3 mn, inout float3 mx, float4x4 world, float3 corner)
{
    // 与 PBR.hlsl 一致：实例的 c0..c3 按「列拼行向量」使用，行向量左乘才是正确的世界位置
    const float3 p = mul(float4(corner, 1.0), world).xyz;
    mn = min(mn, p);
    mx = max(mx, p);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gInstanceCount) return;

    const Instance inst = gSource[gSourceOffset + id.x];
    const float4x4 world = float4x4(inst.c0, inst.c1, inst.c2, inst.c3);

    // 用网格的局部 AABB 变换 8 个角点，得到世界包围盒（与 CPU 剔除一致）
    float3 mn = float3(1e30, 1e30, 1e30);
    float3 mx = float3(-1e30, -1e30, -1e30);
    const float3 lmin = gLocalMin.xyz;
    const float3 lmax = gLocalMax.xyz;
    TestCorner(mn, mx, world, float3(lmin.x, lmin.y, lmin.z));
    TestCorner(mn, mx, world, float3(lmax.x, lmin.y, lmin.z));
    TestCorner(mn, mx, world, float3(lmax.x, lmax.y, lmin.z));
    TestCorner(mn, mx, world, float3(lmin.x, lmax.y, lmin.z));
    TestCorner(mn, mx, world, float3(lmin.x, lmin.y, lmax.z));
    TestCorner(mn, mx, world, float3(lmax.x, lmin.y, lmax.z));
    TestCorner(mn, mx, world, float3(lmax.x, lmax.y, lmax.z));
    TestCorner(mn, mx, world, float3(lmin.x, lmax.y, lmax.z));

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        const float3 n = gPlanes[i].xyz;
        // p-vertex：沿法线最远的角点
        const float3 p = float3(n.x >= 0.0 ? mx.x : mn.x,
                                n.y >= 0.0 ? mx.y : mn.y,
                                n.z >= 0.0 ? mx.z : mn.z);
        if (dot(n, p) + gPlanes[i].w < 0.0) return; // 整个盒子在平面外侧 -> 剔除
    }

    // 可见：追加到压缩缓冲，并把间接参数的 InstanceCount +1
    uint slot = 0;
    gArgs.InterlockedAdd(4, 1, slot);
    gCompact[slot] = inst;
}
