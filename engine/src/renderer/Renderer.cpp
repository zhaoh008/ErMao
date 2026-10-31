// ErMao Engine - 渲染器实现
#include "renderer/Renderer.h"
#include "renderer/SkinnedMesh.h"
#include "core/JobSystem.h"
#include "core/Log.h"
#include "core/Timer.h"

#include <algorithm>
#include <unordered_map>

namespace erm {

namespace {
constexpr float kClearColor[4] = {0.02f, 0.025f, 0.035f, 1.0f};

// PBR 顶点输入布局：slot0 静态顶点（32B），slot1 实例数据（144B）
const D3D11_INPUT_ELEMENT_DESC kPbrLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"TEXCOORD", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"TEXCOORD", 8, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"TEXCOORD", 9, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 128, D3D11_INPUT_PER_INSTANCE_DATA, 1},
};

// 调试线布局：位置 + 颜色
const D3D11_INPUT_ELEMENT_DESC kDebugLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
} // namespace

bool Renderer::Init(Device* device) {
    m_device = device;
    m_windowWidth = device->GetWidth();
    m_windowHeight = device->GetHeight();

    RenderStates::Init(device->GetDevice());

    if (!CreateShaders()) return false;
    if (!CreateConstantBuffers()) return false;

    if (!EnsureSceneTarget(m_windowWidth, m_windowHeight)) return false;
    if (!EnsureViewportTarget(16, 16)) return false;
    if (!m_shadowMap.Create(device->GetDevice(), m_shadowSize, m_shadowSize)) return false;
    if (!CreateInstanceBuffer(16384)) return false;
    if (!m_debug.Init(device->GetDevice())) return false;
    if (!CreateTimingQueries()) return false;

    // 阴影光栅化状态只创建一次（P3-8）
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.DepthBias = 1000;
    rd.SlopeScaledDepthBias = 1.5f;
    if (FAILED(device->GetDevice()->CreateRasterizerState(&rd, &m_shadowRaster))) {
        LogError("阴影光栅化状态创建失败");
        return false;
    }

    m_batches.reserve(512);
    m_instances.reserve(16384);
    m_visibleItems.reserve(8192);
    LogInfo("渲染器初始化完成（阴影贴图 %u²，HDR 场景缓冲，离屏视口）", m_shadowSize);
    return true;
}

bool Renderer::CreateShaders() {
    ID3D11Device* dev = m_device->GetDevice();

    if (!m_pbrShader.Compile(dev, "PBR.hlsl", "VSMain", "PSMain")) return false;
    if (!m_pbrShader.CreateInputLayout(dev, kPbrLayout, _countof(kPbrLayout))) return false;

    if (!m_shadowShader.Compile(dev, "Shadow.hlsl", "VSMain", "PSMain")) return false;
    if (!m_shadowShader.CreateInputLayout(dev, kPbrLayout, _countof(kPbrLayout))) return false;

    if (!m_skyShader.Compile(dev, "Sky.hlsl", "VSMain", "PSMain")) return false;
    if (!m_postShader.Compile(dev, "Post.hlsl", "VSMain", "PSMain")) return false;

    if (!m_debugShader.Compile(dev, "DebugLine.hlsl", "VSMain", "PSMain")) return false;
    if (!m_debugShader.CreateInputLayout(dev, kDebugLayout, _countof(kDebugLayout))) return false;

    // 蒙皮着色器：POSITION/NORMAL/UV/BLENDINDICES(u32x4)/BLENDWEIGHT(float4)，stride=64
    if (!m_skinnedShader.Compile(dev, "Skinned.hlsl", "VSMain", "PSMain")) return false;
    const D3D11_INPUT_ELEMENT_DESC kSkinnedLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (!m_skinnedShader.CreateInputLayout(dev, kSkinnedLayout, _countof(kSkinnedLayout))) return false;
    return true;
}

bool Renderer::CreateConstantBuffers() {
    ID3D11Device* dev = m_device->GetDevice();
    return m_perFrame.Create(dev) && m_pointLights.Create(dev) && m_postParams.Create(dev) && m_bones.Create(dev) &&
           m_skinnedObject.Create(dev);
}

bool Renderer::CreateInstanceBuffer(u32 maxInstances) {
    m_maxInstances = maxInstances;
    m_instances.resize(maxInstances);
    return m_instanceBuffer.Create(m_device->GetDevice(), nullptr, maxInstances * sizeof(InstanceVertex),
                                   D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC);
}

bool Renderer::CreateTimingQueries() {
    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    if (FAILED(m_device->GetDevice()->CreateQuery(&desc, &m_queryDisjoint))) return false;
    desc.Query = D3D11_QUERY_TIMESTAMP;
    if (FAILED(m_device->GetDevice()->CreateQuery(&desc, &m_queryStart))) return false;
    if (FAILED(m_device->GetDevice()->CreateQuery(&desc, &m_queryEnd))) return false;
    // disjoint 查询必须用 TIMESTAMP_DISJOINT 创建（复用 TIMESTAMP 的 desc 会 GetData E_INVALIDARG）
    D3D11_QUERY_DESC disjointDesc{};
    disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    if (FAILED(m_device->GetDevice()->CreateQuery(&disjointDesc, &m_renderDisjoint))) return false;
    if (FAILED(m_device->GetDevice()->CreateQuery(&desc, &m_renderStart))) return false;
    if (FAILED(m_device->GetDevice()->CreateQuery(&desc, &m_renderEnd))) return false;
    return true;
}

bool Renderer::EnsureSceneTarget(u32 width, u32 height) {
    if (width == 0 || height == 0) return false;
    if (m_sceneTarget.Width() == width && m_sceneTarget.Height() == height) return true;
    m_sceneTarget.Shutdown();
    const bool ok = m_sceneTarget.Create(m_device->GetDevice(), width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    if (!ok) LogError("HDR 场景缓冲创建失败 %ux%u", width, height);
    return ok;
}

bool Renderer::EnsureViewportTarget(u32 width, u32 height) {
    if (width == 0 || height == 0) return false;
    if (m_viewportTarget.Width() == width && m_viewportTarget.Height() == height) return true;
    m_viewportTarget.Shutdown();
    m_viewportWidth = width;
    m_viewportHeight = height;
    const bool ok = m_viewportTarget.Create(m_device->GetDevice(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM, false);
    if (!ok) LogError("视口 RT 创建失败 %ux%u", width, height);
    return ok;
}

void Renderer::Shutdown() {
    m_debug.Shutdown();
    m_sceneTarget.Shutdown();
    m_viewportTarget.Shutdown();
    m_shadowMap.Shutdown();
    if (m_shadowRaster) {
        m_shadowRaster->Release();
        m_shadowRaster = nullptr;
    }
    if (m_renderDisjoint) m_renderDisjoint->Release();
    if (m_renderStart) m_renderStart->Release();
    if (m_renderEnd) m_renderEnd->Release();
    m_renderDisjoint = m_renderStart = m_renderEnd = nullptr;

    if (m_queryDisjoint) m_queryDisjoint->Release();
    if (m_queryStart) m_queryStart->Release();
    if (m_queryEnd) m_queryEnd->Release();
    m_queryDisjoint = m_queryStart = m_queryEnd = nullptr;
    RenderStates::Shutdown();
}

void Renderer::Render(const Camera& camera, const RenderScene& scene, float time, bool wireframe) {
    RenderInternal(camera, scene, time, wireframe, nullptr, m_windowWidth, m_windowHeight);
}

void Renderer::RenderToViewport(const Camera& camera, const RenderScene& scene, float time, u32 width, u32 height,
                                bool wireframe) {
    if (!EnsureViewportTarget(width, height)) return;
    RenderInternal(camera, scene, time, wireframe, m_viewportTarget.RTV(), width, height);
}

void Renderer::SetViewport(u32 width, u32 height) {
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_device->GetContext()->RSSetViewports(1, &vp);
}

Mat4 Renderer::ComputeLightMatrix(const Vec3& sunDirToSun, const Vec3& focus, float radius) const {
    const Vec3 up = std::fabs(sunDirToSun.y) > 0.95f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
    // 光源相机必须放在「太阳那一侧」，否则拍到的是物体背面，阴影全错
    const Vec3 eye = focus + sunDirToSun * (radius * 2.0f);
    const Mat4 view = Mat4::LookAt(eye, focus, up);
    const Mat4 proj = Mat4::Orthographic(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);
    return proj * view;
}

Vec3 Renderer::SnapShadowFocus(const Vec3& focus, const Vec3& sunDir, float radius) const {
    // 把 focus 投影到光源的切平面，量化到 texel 大小，再还原（P2-4：消除阴影游动）
    const Vec3 up = std::fabs(sunDir.y) > 0.95f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
    const Vec3 right = Normalize(Cross(up, sunDir));
    const Vec3 lightUp = Cross(sunDir, right);
    const float texel = (2.0f * radius) / static_cast<float>(m_shadowSize);
    if (texel <= 0.0f) return focus;

    float x = Dot(focus, right);
    float y = Dot(focus, lightUp);
    const float z = Dot(focus, sunDir);
    x = std::floor(x / texel) * texel;
    y = std::floor(y / texel) * texel;
    return right * x + lightUp * y + sunDir * z;
}

void Renderer::BuildBatches(const Camera& camera, const RenderScene& scene, bool cull) {
    m_batches.clear();
    m_visibleItems.clear();
    m_instanceCount = 0;
    // 只重置本帧计数，保留 GPU 时间戳结果（否则 UpdateGpuTiming 填的值会被清掉）
    m_stats.drawCalls = 0;
    m_stats.instances = 0;
    m_stats.triangles = 0;
    m_stats.culled = 0;

    const Frustum frustum = camera.GetFrustum();
    const u32 itemCount = static_cast<u32>(scene.items.size());

    m_visibility.assign(itemCount, 1);
    if (cull && itemCount > 0) {
        JobSystem::ParallelFor(
            0, itemCount,
            [&](u32 i) {
                const RenderItem& item = scene.items[i];
                if (!item.mesh || !item.mesh->IsValid()) {
                    m_visibility[i] = 0;
                    return;
                }
                const AABB& b = item.mesh->Bounds();
                const Vec3 corners[8] = {{b.min.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.min.z},
                                         {b.max.x, b.max.y, b.min.z}, {b.min.x, b.max.y, b.min.z},
                                         {b.min.x, b.min.y, b.max.z}, {b.max.x, b.min.y, b.max.z},
                                         {b.max.x, b.max.y, b.max.z}, {b.min.x, b.max.y, b.max.z}};
                AABB world;
                world.min = item.world.TransformPoint(corners[0]);
                world.max = world.min;
                for (int c = 1; c < 8; ++c) world.Expand(item.world.TransformPoint(corners[c]));
                m_visibility[i] = frustum.IntersectsAABB(world) ? 1 : 0;
            },
            128);
    }

    for (u32 i = 0; i < itemCount; ++i) {
        if (!m_visibility[i]) {
            ++m_stats.culled;
            continue;
        }
        m_visibleItems.push_back(scene.items[i]);
    }

    // 按 (mesh id, texture id) 分组：同组可一次 DrawIndexedInstanced（P2-3：不用指针排序）
    std::sort(m_visibleItems.begin(), m_visibleItems.end(), [](const RenderItem& a, const RenderItem& b) {
        const u32 am = a.mesh ? a.mesh->Id() : 0;
        const u32 bm = b.mesh ? b.mesh->Id() : 0;
        if (am != bm) return am < bm;
        const u32 at = a.baseColor ? a.baseColor->Id() : 0;
        const u32 bt = b.baseColor ? b.baseColor->Id() : 0;
        return at < bt;
    });

    for (const auto& item : m_visibleItems) {
        if (m_batches.empty() || m_batches.back().mesh != item.mesh ||
            m_batches.back().texture != item.baseColor) {
            Batch batch;
            batch.mesh = item.mesh;
            batch.texture = item.baseColor;
            batch.firstInstance = m_instanceCount;
            batch.count = 0;
            m_batches.push_back(batch);
        }
        if (m_instanceCount >= m_maxInstances) {
            LogWarn("实例缓冲已满（%u），后续物体被丢弃", m_maxInstances);
            break;
        }
        InstanceVertex iv;
        iv.col0 = Vec4(item.world.m[0][0], item.world.m[0][1], item.world.m[0][2], item.world.m[0][3]);
        iv.col1 = Vec4(item.world.m[1][0], item.world.m[1][1], item.world.m[1][2], item.world.m[1][3]);
        iv.col2 = Vec4(item.world.m[2][0], item.world.m[2][1], item.world.m[2][2], item.world.m[2][3]);
        iv.col3 = Vec4(item.world.m[3][0], item.world.m[3][1], item.world.m[3][2], item.world.m[3][3]);

        // 法线矩阵 = transpose(inverse(world))，非均匀缩放时法线方向才正确（P1-4）
        const Mat4 normalMatrix = item.world.Inverse().Transposed();
        iv.nrm0 = Vec4(normalMatrix.m[0][0], normalMatrix.m[0][1], normalMatrix.m[0][2], 0.0f);
        iv.nrm1 = Vec4(normalMatrix.m[1][0], normalMatrix.m[1][1], normalMatrix.m[1][2], 0.0f);
        iv.nrm2 = Vec4(normalMatrix.m[2][0], normalMatrix.m[2][1], normalMatrix.m[2][2], 0.0f);

        iv.albedo = item.albedo;
        iv.params = Vec4(item.roughness, item.metallic, item.emissive,
                         item.baseColor ? (item.uvScale > 0.0f ? item.uvScale : 1.0f) : 0.0f);
        m_instances[m_instanceCount++] = iv;
        m_batches.back().count++;

        ++m_stats.instances;
        m_stats.triangles += item.mesh->IndexCount() / 3;
    }
}

void Renderer::UploadInstances(ID3D11DeviceContext* ctx) {
    if (m_instanceCount == 0) return;
    m_instanceBuffer.Update(ctx, m_instances.data(), m_instanceCount * sizeof(InstanceVertex));
}

void Renderer::DrawBatches(ID3D11DeviceContext* ctx) {
    for (const auto& batch : m_batches) {
        if (batch.count == 0) continue;
        ID3D11Buffer* buffers[2] = {batch.mesh->VertexBuffer(), m_instanceBuffer.Get()};
        UINT strides[2] = {sizeof(Vertex), sizeof(InstanceVertex)};
        UINT offsets[2] = {0, batch.firstInstance * sizeof(InstanceVertex)};
        ctx->IASetVertexBuffers(0, 2, buffers, strides, offsets);
        ctx->IASetIndexBuffer(batch.mesh->IndexBuffer(), DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (batch.texture) {
            batch.texture->BindPS(ctx, 1);
        } else {
            ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
            ctx->PSSetShaderResources(1, 1, nullSrv);
        }
        ctx->DrawIndexedInstanced(batch.mesh->IndexCount(), batch.count, 0, 0, 0);
        ++m_stats.drawCalls;
    }
}

const SkinnedMesh* Renderer::CreateSkinnedMesh(const SkinnedMeshData& data) {
    auto mesh = MakeScope<SkinnedMesh>();
    if (!mesh->Create(m_device->GetDevice(), data)) return nullptr;
    m_skinnedMeshes.push_back(std::move(mesh));
    return m_skinnedMeshes.back().get();
}

ID3D11ShaderResourceView* Renderer::GetMeshThumbnail(const Mesh* mesh) {
    if (!mesh || !mesh->IsValid()) return nullptr;

    auto cached = m_thumbnails.find(mesh->Id());
    if (cached != m_thumbnails.end()) return cached->second->ColorSRV();

    auto target = MakeScope<RenderTarget>();
    const u32 size = 128;
    if (!target->Create(m_device->GetDevice(), size, size, DXGI_FORMAT_R8G8B8A8_UNORM, true)) return nullptr;

    // 用网格包围球决定相机距离，保证不同尺寸的资产都填满画面
    const AABB& bounds = mesh->Bounds();
    const float radius = (std::max)(Length(bounds.Extents()), 0.05f);
    const Vec3  center = bounds.Center();
    const float distance = radius * 3.0f;

    Camera camera;
    camera.position = center + Vec3(1.0f, 0.62f, 1.35f) * distance;
    const Vec3 forward = Normalize(center - camera.position);
    camera.pitch = std::asin(Clamp(forward.y, -1.0f, 1.0f));
    camera.yaw = std::atan2(-forward.x, -forward.z);
    camera.fovY = ToRadians(38.0f);
    camera.aspect = 1.0f;
    camera.nearZ = 0.01f;
    camera.farZ = distance * 8.0f;

    RenderScene scene;
    RenderItem  item;
    item.mesh = const_cast<Mesh*>(mesh);
    item.world = Mat4::Translation(-center);
    item.albedo = Vec4(0.72f, 0.74f, 0.80f, 1.0f);
    item.roughness = 0.45f;
    item.metallic = 0.05f;
    scene.items.push_back(item);
    scene.sunDirection = Normalize(Vec3(0.45f, 0.85f, 0.30f));
    scene.sunIntensity = 2.6f;
    scene.ambientUp = Vec3(0.26f, 0.30f, 0.38f);
    scene.ambientDown = Vec3(0.08f, 0.08f, 0.09f);
    scene.ambientIntensity = 0.9f;

    const Stats savedStats = m_stats; // 缩略图渲染不能污染主画面统计
    RenderInternal(camera, scene, 0.0f, false, target->RTV(), size, size);
    m_stats = savedStats;

    ID3D11ShaderResourceView* srv = target->ColorSRV();
    m_thumbnails.emplace(mesh->Id(), std::move(target));
    return srv;
}

void Renderer::UpdateGpuTiming(ID3D11DeviceContext* ctx, bool begin) {
    if (!m_queryDisjoint) return;

    if (begin) {
        if (m_queryPending) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            if (ctx->GetData(m_queryDisjoint, &disjoint, sizeof(disjoint), 0) == S_OK) {
                UINT64 start = 0, end = 0;
                ctx->GetData(m_queryStart, &start, sizeof(start), 0);
                ctx->GetData(m_queryEnd, &end, sizeof(end), 0);
                if (!disjoint.Disjoint && disjoint.Frequency > 0) {
                    m_stats.gpuFrameMs =
                        static_cast<float>(static_cast<double>(end - start) / disjoint.Frequency * 1000.0);
                    m_stats.gpuTimingValid = true;
                }
                m_queryPending = false;
            }
        }
        if (!m_queryPending) {
            ctx->Begin(m_queryDisjoint);
            ctx->End(m_queryStart);
            m_queryActiveThisFrame = true; // 只有本帧 Begin 过，End 才成对出现
        }
    } else if (m_queryActiveThisFrame) {
        ctx->End(m_queryEnd);
        ctx->End(m_queryDisjoint); // 必须闭合 disjoint，否则 GetData 永远 S_FALSE
        m_queryActiveThisFrame = false;
        m_queryPending = true;
    }
}

void Renderer::UpdateRenderTiming(ID3D11DeviceContext* ctx, bool begin) {
    if (!m_renderDisjoint) return;
    if (begin) {
        if (m_renderPending) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            const HRESULT hr = ctx->GetData(m_renderDisjoint, &disjoint, sizeof(disjoint), 0);
            if (hr == S_OK) {
                UINT64 start = 0, end = 0;
                ctx->GetData(m_renderStart, &start, sizeof(start), 0);
                ctx->GetData(m_renderEnd, &end, sizeof(end), 0);
                if (!disjoint.Disjoint && disjoint.Frequency > 0) {
                    m_stats.gpuRenderMs =
                        static_cast<float>(static_cast<double>(end - start) / disjoint.Frequency * 1000.0);
                    m_stats.gpuRenderValid = true;
                }
                m_renderPending = false;
            }
        }
        if (!m_renderPending) {
            ctx->Begin(m_renderDisjoint);
            ctx->End(m_renderStart);
            m_renderActive = true;
        }
    } else if (m_renderActive) {
        ctx->End(m_renderEnd);
        ctx->End(m_renderDisjoint);
        m_renderActive = false;
        m_renderPending = true;
    }
}

void Renderer::RenderInternal(const Camera& camera, const RenderScene& scene, float time, bool wireframe,
                              ID3D11RenderTargetView* outputRtv, u32 outputWidth, u32 outputHeight) {
    if (outputWidth == 0 || outputHeight == 0) return;
    ID3D11DeviceContext* ctx = m_device->GetContext();
    m_currentScene = &scene; // OpaquePass 里的蒙皮绘制需要

    m_stats.drawCalls = 0;

    // ------------------------------------------------ 每帧常量
    const Vec3 sunDir = Normalize(scene.sunDirection);
    const Vec3 snappedFocus = SnapShadowFocus(scene.shadowFocus, sunDir, scene.shadowRadius);

    PerFrameCB frame{};
    frame.viewProj = camera.ViewProjection();
    frame.invViewProj = frame.viewProj.Inverse();
    frame.lightViewProj = ComputeLightMatrix(sunDir, snappedFocus, scene.shadowRadius);
    frame.cameraPosTime = Vec4(camera.position, time);
    frame.sunDirIntensity = Vec4(sunDir, scene.sunIntensity);
    frame.sunColorTexel = Vec4(scene.sunColor, 1.0f / static_cast<float>(m_shadowSize));
    frame.ambientUpCount =
        Vec4(scene.ambientUp, static_cast<float>(std::min<size_t>(scene.pointLights.size(), 8)));
    frame.ambientDownExposure = Vec4(scene.ambientDown, m_exposure);
    frame.shadowParams = Vec4(0.0025f, 0.035f, 1.0f, m_shadowsEnabled ? 1.0f : 0.0f);
    frame.misc = Vec4(static_cast<float>(m_debugView), scene.ambientIntensity, 0.0f, 0.0f);
    m_perFrame.Update(ctx, frame);

    PointLightsCB lights{};
    const size_t lightCount = std::min<size_t>(scene.pointLights.size(), 8);
    for (size_t i = 0; i < lightCount; ++i) {
        lights.posRange[i] = Vec4(scene.pointLights[i].position, scene.pointLights[i].range);
        lights.colorIntensity[i] = Vec4(scene.pointLights[i].color, scene.pointLights[i].intensity);
    }
    m_pointLights.Update(ctx, lights);

    m_perFrame.BindVSPS(ctx, 0);
    m_pointLights.BindPS(ctx, 1);

    UpdateGpuTiming(ctx, true);
    BuildBatches(camera, scene, true);
    UploadInstances(ctx);
    UpdateRenderTiming(ctx, true); // 渲染成本从这里开始计（Shadow 之前）

    // ------------------------------------------------ Shadow
    const double t0 = Timer::Now();
    ShadowPass(ctx);
    const double t1 = Timer::Now();

    // ------------------------------------------------ 主场景（HDR）
    if (!EnsureSceneTarget(outputWidth, outputHeight)) return;
    m_sceneTarget.Bind(ctx);
    m_sceneTarget.Clear(ctx, kClearColor);
    SetViewport(outputWidth, outputHeight);
    RenderStates::SetCullNone(ctx);

    SkyPass(ctx);
    OpaquePass(ctx, wireframe);
    DebugPass(ctx);
    // 3D UI：世界空间血条（不写深度，但被几何遮挡）

    const double t2 = Timer::Now();

    // ------------------------------------------------ Post → 输出
    PostPass(ctx, outputRtv, outputWidth, outputHeight);
    UpdateRenderTiming(ctx, false); // Post 之后结束：不含 Present/vsync 等待

    m_stats.cpuShadowMs = static_cast<float>((t1 - t0) * 1000.0);
    m_stats.cpuOpaqueMs = static_cast<float>((t2 - t1) * 1000.0);
    UpdateGpuTiming(ctx, false);
}

void Renderer::ShadowPass(ID3D11DeviceContext* ctx) {
    if (!m_shadowsEnabled || m_batches.empty()) return;

    SetViewport(m_shadowSize, m_shadowSize);
    m_shadowMap.BindAndClear(ctx);
    RenderStates::SetOpaque(ctx);
    ctx->RSSetState(m_shadowRaster);

    m_shadowShader.Bind(ctx);
    DrawBatches(ctx);

    ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void Renderer::SkyPass(ID3D11DeviceContext* ctx) {
    RenderStates::SetDepthNone(ctx);
    ID3D11ShaderResourceView* nullSrv[2] = {nullptr, nullptr};
    ctx->PSSetShaderResources(0, 2, nullSrv);
    m_skyShader.Bind(ctx);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->Draw(3, 0);
    ++m_stats.drawCalls;
}

void Renderer::SkinnedPass(ID3D11DeviceContext* ctx) {
    if (!m_currentScene || m_currentScene->skinned.empty()) return;

    m_skinnedShader.Bind(ctx);
    m_shadowMap.BindAsTexture(ctx, 0);
    m_perFrame.BindVSPS(ctx, 0);
    m_pointLights.BindPS(ctx, 1);
    Texture::BindSampler(ctx, 0, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    Texture::BindSampler(ctx, 1, D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP);

    for (const SkinnedDraw& draw : m_currentScene->skinned) {
        if (!draw.mesh || !draw.mesh->IsValid()) continue;

        BonesCB bones{};
        const size_t count = (std::min)(draw.bones.size(), static_cast<size_t>(64));
        for (size_t i = 0; i < count; ++i) bones.bones[i] = draw.bones[i];
        m_bones.Update(ctx, bones);

        SkinnedObjectCB object{};
        object.world = draw.world;
        object.albedo = draw.albedo;
        object.params = Vec4(draw.roughness, draw.metallic, 0.0f, 0.0f);
        m_skinnedObject.Update(ctx, object);

        m_bones.BindVS(ctx, 2);
        m_skinnedObject.BindVSPS(ctx, 3);
        draw.mesh->Bind(ctx);
        draw.mesh->Draw(ctx);

        ++m_stats.drawCalls;
        ++m_stats.instances;
        m_stats.triangles += draw.mesh->IndexCount() / 3;
    }
}

void Renderer::OpaquePass(ID3D11DeviceContext* ctx, bool wireframe) {
    const bool hasSkinned = m_currentScene && !m_currentScene->skinned.empty();
    if (m_batches.empty() && !hasSkinned) return;
    RenderStates::SetOpaque(ctx);
    RenderStates::SetCullBack(ctx, true);
    if (wireframe) RenderStates::SetWireframe(ctx, true);

    m_pbrShader.Bind(ctx);
    m_shadowMap.BindAsTexture(ctx, 0);
    m_perFrame.BindVSPS(ctx, 0);
    m_pointLights.BindPS(ctx, 1);
    Texture::BindSampler(ctx, 0, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    Texture::BindSampler(ctx, 1, D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP);

    DrawBatches(ctx);
    SkinnedPass(ctx);

    if (wireframe) RenderStates::SetWireframe(ctx, false);

    ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
    ctx->PSSetShaderResources(0, 1, nullSrv);
}

void Renderer::DebugPass(ID3D11DeviceContext* ctx) {
    if (!m_debug.LineCount()) return;

    m_debug.Flush(ctx);
    m_debugShader.Bind(ctx);
    m_perFrame.BindVSPS(ctx, 0);

    RenderStates::SetOpaque(ctx);
    RenderStates::SetCullNone(ctx);
    m_debug.Draw(ctx, true);

    RenderStates::SetDepthNone(ctx);
    m_debug.Draw(ctx, false);
    ++m_stats.drawCalls;
}

void Renderer::PostPass(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* target, u32 width, u32 height) {
    const double t0 = Timer::Now();

    if (!target) target = m_device->GetBackbufferRTV();
    ctx->OMSetRenderTargets(1, &target, nullptr);
    SetViewport(width, height);

    PostCB params{};
    params.params = Vec4(m_exposure, 0.0f, 1.2f, m_debugView == 5 ? 2.0f : 0.0f);
    params.screen = Vec4(static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width),
                         1.0f / static_cast<float>(height));
    m_postParams.Update(ctx, params);

    // 后处理是"全屏、无深度、无混合、无剔除"，状态语义独立（P2-6）
    RenderStates::SetDepthNone(ctx);
    RenderStates::SetBlendOpaque(ctx);
    RenderStates::SetCullNone(ctx);

    m_postShader.Bind(ctx);
    m_postParams.BindPS(ctx, 0);
    ID3D11ShaderResourceView* srv = m_sceneTarget.ColorSRV();
    ctx->PSSetShaderResources(0, 1, &srv);
    Texture::BindSampler(ctx, 0, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->Draw(3, 0);
    ++m_stats.drawCalls;

    ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
    ctx->PSSetShaderResources(0, 1, nullSrv);
    m_stats.cpuPostMs = static_cast<float>((Timer::Now() - t0) * 1000.0);
}

} // namespace erm
