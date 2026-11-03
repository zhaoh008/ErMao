// ErMao Engine - 前向渲染器（GAMES104 第 4/5/6/7 讲）
//
// Pass 顺序：Shadow → Sky → Opaque(实例化) → Debug → Post
// 输出可以是后备缓冲（无编辑器时），也可以是离屏 RT（编辑器视口用）。
//
// 相比 v0.1 的修正（对应审核报告）：
//   * 实例数据带「逆转置法线矩阵」，非均匀缩放不再让光照变形（P1-4）
//   * 统计只在 BuildBatches 里累加，实例数不再翻倍（P2-2）
//   * 批处理按资源 id 排序，结果可复现（P2-3）
//   * 阴影 focus 对齐到光源 texel，消除阴影游动（P2-4）
//   * 阴影光栅化状态只创建一次（P3-8）
//   * PostPass 不再借用 SetOpaque，状态语义清晰（P2-6）
//   * 提供 GPU 时间戳查询，避免"CPU 提交时间"冒充 GPU 时间（P2-2）
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "renderer/Camera.h"
#include "renderer/DebugDraw.h"
#include "renderer/RenderScene.h"
#include "renderer/SkinnedMesh.h"
#include "rhi/Buffer.h"
#include "rhi/Device.h"
#include "rhi/RenderTarget.h"
#include "rhi/Shader.h"
#include "rhi/Texture.h"

namespace erm {

class Mesh;

class Renderer {
public:
    struct Stats {
        u32 drawCalls = 0;
        u32 instances = 0;
        u32 triangles = 0;
        u32 culled = 0;
        float cpuShadowMs = 0.0f;
        float cpuOpaqueMs = 0.0f;
        float cpuPostMs = 0.0f;
        // 两个口径（审核 P2-21）：
        // gpuRenderMs = Shadow→Post 的 GPU 时间，量的是渲染成本；
        // gpuFrameMs  = 整帧 GPU 时间戳跨度，vsync 开时会包含等待翻转的空转。
        float gpuRenderMs = 0.0f;
        float gpuFrameMs = 0.0f;
        bool  gpuRenderValid = false;
        bool  gpuTimingValid = false;
        // GPU Driven（第 22 讲）：compute 剔除后的可见实例数（每 30 帧回读一次）
        u32   gpuVisibleInstances = 0;
        bool  gpuCullEnabled = false;
    };

    bool Init(Device* device);
    void Shutdown();

    // 渲染到后备缓冲（整窗）
    void Render(const Camera& camera, const RenderScene& scene, float time, bool wireframe = false);
    // 渲染到离屏视口纹理（编辑器视口）；返回后可通过 ViewportTexture() 取 SRV
    void RenderToViewport(const Camera& camera, const RenderScene& scene, float time, u32 width, u32 height,
                          bool wireframe = false);

    ID3D11ShaderResourceView* ViewportTexture() const { return m_viewportTarget.ColorSRV(); }
    // 创建（并由渲染器持有）一个蒙皮网格：关卡代码只提供 CPU 数据
    const SkinnedMesh* CreateSkinnedMesh(const SkinnedMeshData& data);
    // 资源浏览器缩略图：首次调用时把网格渲染到 128² RT 并缓存
    ID3D11ShaderResourceView* GetMeshThumbnail(const Mesh* mesh);
    ID3D11ShaderResourceView* GetTextureThumbnail(const Texture* texture) {
        return texture ? texture->SRV() : nullptr;
    }
    u32 ViewportWidth() const { return m_viewportWidth; }
    u32 ViewportHeight() const { return m_viewportHeight; }
    void SetWindowSize(u32 width, u32 height) {
        m_windowWidth = width;
        m_windowHeight = height;
    }

    DebugDraw&   Debug() { return m_debug; }
    const Stats& GetStats() const { return m_stats; }

    float& Exposure() { return m_exposure; }
    bool&  ShadowsEnabled() { return m_shadowsEnabled; }
    // GPU Driven 开关（第 22 讲）；关闭后走 CPU 批处理
    void SetGpuCullEnabled(bool enabled) { m_gpuCullWanted = enabled; }
    bool GpuCullEnabled() const { return m_gpuCullReady && m_gpuCullWanted; }
    bool&  Wireframe() { return m_wireframe; }
    int&   DebugView() { return m_debugView; }

private:
    struct PerFrameCB {
        Mat4 viewProj;
        Mat4 lightViewProj;
        Mat4 invViewProj;
        Vec4 cameraPosTime;
        Vec4 sunDirIntensity;
        Vec4 sunColorTexel;
        Vec4 ambientUpCount;
        Vec4 ambientDownExposure;
        Vec4 shadowParams;
        Vec4 misc;
    };

    struct PointLightsCB {
        Vec4 posRange[8];
        Vec4 colorIntensity[8];
    };

    struct PostCB {
        Vec4 params;
        Vec4 screen;
    };

    // 实例数据：世界矩阵 4 列 + 法线矩阵 3 列 + 材质，共 144 字节（16 字节对齐）
    struct InstanceVertex {
        Vec4 col0, col1, col2, col3;
        Vec4 nrm0, nrm1, nrm2;
        Vec4 albedo;
        Vec4 params;
    };

    struct Batch {
        const Mesh*    mesh = nullptr;
        const Texture* texture = nullptr;
        u32            firstInstance = 0;
        u32            count = 0;
    };

    bool CreateShaders();
    bool CreateConstantBuffers();
    bool CreateInstanceBuffer(u32 maxInstances);
    bool EnsureSceneTarget(u32 width, u32 height);
    bool EnsureViewportTarget(u32 width, u32 height);
    bool CreateTimingQueries();

    Mat4 ComputeLightMatrix(const Vec3& sunDirToSun, const Vec3& focus, float radius) const;
    Vec3 SnapShadowFocus(const Vec3& focus, const Vec3& sunDir, float radius) const;

    void BuildBatches(const Camera& camera, const RenderScene& scene, bool cull);
    void UploadInstances(ID3D11DeviceContext* ctx);
    void DrawBatches(ID3D11DeviceContext* ctx);

    void RenderInternal(const Camera& camera, const RenderScene& scene, float time, bool wireframe,
                        ID3D11RenderTargetView* outputRtv, u32 outputWidth, u32 outputHeight);

    void ShadowPass(ID3D11DeviceContext* ctx);
    void SkyPass(ID3D11DeviceContext* ctx);
    void OpaquePass(ID3D11DeviceContext* ctx, bool wireframe);
    void SkinnedPass(ID3D11DeviceContext* ctx);
    void ParticlePass(ID3D11DeviceContext* ctx);
    // 2D：正交 + 精灵（第 23 讲）
    void SpritePass(ID3D11DeviceContext* ctx, const RenderScene& scene, float aspect);
    // 离屏视口 -> 后备缓冲（统一两条渲染路径的口径）
    void CopyPass(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* target, u32 width, u32 height);
    // GPU Driven：创建资源 / compute 剔除 + 间接绘制
    bool CreateGpuCullResources();
    void GpuCulledDraw(ID3D11DeviceContext* ctx);
    void ReadbackGpuCullStats(ID3D11DeviceContext* ctx);
    void DebugPass(ID3D11DeviceContext* ctx);
    void PostPass(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* target, u32 width, u32 height);

    void SetViewport(u32 width, u32 height);
    void UpdateGpuTiming(ID3D11DeviceContext* ctx, bool begin);
    // 只包 Shadow→Post 的查询，得到真实渲染成本（与 vsync 无关）
    void UpdateRenderTiming(ID3D11DeviceContext* ctx, bool begin);

    Device* m_device = nullptr;
    u32     m_windowWidth = 0;
    u32     m_windowHeight = 0;

    Shader m_pbrShader;
    Shader m_shadowShader;
    Shader m_skyShader;
    Shader m_debugShader;
    Shader m_postShader;
    Shader m_skinnedShader;
    Shader m_particleShader;
    Shader m_copyShader;
    Shader m_spriteShader;

    ConstantBuffer<PerFrameCB>    m_perFrame;
    ConstantBuffer<PointLightsCB> m_pointLights;
    ConstantBuffer<PostCB>        m_postParams;

    // 蒙皮：骨骼调色板（b2）+ 每角色常量（b3）
    struct BonesCB {
        Mat4 bones[64];
    };
    struct SkinnedObjectCB {
        Mat4 world;
        Vec4 albedo;
        Vec4 params;
    };
    ConstantBuffer<BonesCB>        m_bones;
    ConstantBuffer<SkinnedObjectCB> m_skinnedObject;

    RenderTarget m_sceneTarget;   // HDR RGBA16F + 深度（与当前输出同尺寸）
    std::unordered_map<u32, Scope<RenderTarget>> m_thumbnails; // meshId -> 128² 缩略图
    std::vector<Scope<SkinnedMesh>> m_skinnedMeshes;
    RenderTarget m_viewportTarget; // LDR RGBA8，供编辑器 ImGui::Image 使用
    DepthTarget  m_shadowMap;

    ID3D11RasterizerState* m_shadowRaster = nullptr;

    GpuBuffer m_instanceBuffer;
    u32       m_maxInstances = 0;
    u32       m_instanceCount = 0;

    std::vector<Batch>          m_batches;
    std::vector<InstanceVertex> m_instances;
    // 粒子：动态顶点缓冲（每粒子 6 顶点）
    struct ParticleVertex {
        Vec3 position;
        Vec4 color;
        Vec2 uv;
    };
    std::vector<ParticleVertex> m_particleVertices;
    GpuBuffer                   m_particleBuffer;
    bool                        m_particleCapacityWarned = false;
    std::vector<RenderItem>     m_visibleItems;
    std::vector<u8>             m_visibility;

    DebugDraw m_debug;

    // GPU 时间戳（双缓冲查询，读取上一帧结果）
    ID3D11Query* m_queryDisjoint = nullptr;
    ID3D11Query* m_queryStart = nullptr;
    ID3D11Query* m_queryEnd = nullptr;
    ID3D11Query* m_renderDisjoint = nullptr;
    ID3D11Query* m_renderStart = nullptr;
    ID3D11Query* m_renderEnd = nullptr;
    bool         m_renderPending = false;
    bool         m_renderActive = false;
    bool         m_queryPending = false;     // 数据已写入、等下一帧读取
    bool         m_queryActiveThisFrame = false; // 本帧 Begin 过，End 必须成对

    u32   m_shadowSize = 2048;
    bool  m_shadowsEnabled = true;
    bool  m_wireframe = false;
    int   m_debugView = 0;
    float m_exposure = 1.0f;
    u32   m_viewportWidth = 0;
    u32   m_viewportHeight = 0;

    Stats m_stats;
    const RenderScene* m_currentScene = nullptr; // OpaquePass 里画蒙皮网格用

    // ---------------- GPU Driven（第 22 讲）
    static constexpr u32 kMaxCullBatches = 32;
    struct GpuCullCB {
        Vec4 planes[6];
        Vec4 localMin;   // 网格局部 AABB（让 GPU 用和 CPU 一样的包围盒）
        Vec4 localMax;
        u32  sourceOffset;
        u32  instanceCount;
        u32  pad0;
        u32  pad1;
    };
    Shader                          m_cullShader;
    ConstantBuffer<GpuCullCB>       m_cullParams;

    // 2D 精灵（第 23 讲）
    static constexpr u32 kMaxSpriteVertices = 65536;
    static constexpr u32 kMaxParticleVertices = 4096;
    struct SpriteVertex {
        Vec2 position;
        Vec2 uv;
        Vec4 color;
    };
    struct ParticleParamsCB {
        Vec4 params;   // x=useTexture
    };
    ConstantBuffer<Mat4> m_spriteVP;
    ConstantBuffer<ParticleParamsCB> m_particleParams;
    std::vector<SpriteVertex> m_spriteVertices;
    GpuBuffer                 m_spriteBuffer;
    bool                      m_spriteCapacityWarned = false;
    ComPtr<ID3D11Buffer>            m_gpuInstanceSource;
    ComPtr<ID3D11ShaderResourceView> m_gpuInstanceSRV;
    ComPtr<ID3D11Buffer>            m_gpuCompact;
    ComPtr<ID3D11UnorderedAccessView> m_gpuCompactUAV;
    ComPtr<ID3D11Buffer>            m_gpuArgs[kMaxCullBatches];
    ComPtr<ID3D11UnorderedAccessView> m_gpuArgsUAV[kMaxCullBatches];
    ComPtr<ID3D11Buffer>            m_gpuCompactVB; // UAV-only 时的中转顶点缓冲
    ComPtr<ID3D11Buffer>            m_gpuArgsStaging;
    u32                             m_gpuBatchCount = 0;
    u32                             m_gpuReadbackTimer = 0;
    bool                            m_gpuCullReady = false;
    bool                            m_gpuCompactIsUavOnly = false;
    bool                            m_gpuCullWanted = true;
    bool                            m_gpuBatchOverflowWarned = false;
    const Camera*      m_currentCamera = nullptr;
};

} // namespace erm
