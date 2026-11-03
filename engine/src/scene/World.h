// ErMao Engine - 世界（GAMES104 第 2/3 讲：场景管理与系统调度）
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "scene/GameObject.h"

#include <unordered_map>

namespace erm {

class AssetManager;
class Camera;
class JsonValue;
class NavGrid;
class PhysicsWorld;
class Renderer;
struct RenderScene;

// World 持有场景中所有 GameObject，并负责：
//   * 创建/销毁（延迟到帧尾，避免迭代器失效）
//   * 固定步长物理 + 可变步长逻辑
//   * 把场景翻译成 RenderScene（Renderer 不认识 GameObject）
//   * JSON 存档 / 读档（编辑器保存场景、Play-In-Editor 回滚都复用它）
class World {
public:
    World();
    ~World();

    void Init(AssetManager* assets);
    void Shutdown();
    void Clear();

    // ------------------------------------------------ 对象管理
    GameObject* CreateGameObject(const std::string& name, GameObject* parent = nullptr);
    void        DestroyObject(GameObject* object);
    void        ProcessPendingDestruction();

    const std::vector<GameObject*>& RootObjects() const { return m_roots; }
    GameObject*                            FindByName(const std::string& name) const;
    GameObject*                            FindById(u64 id) const;
    size_t                                 ObjectCount() const { return m_idToObject.size(); }

    // ------------------------------------------------ 每帧
    void Update(float dt);      // 逻辑：脚本、动画、AI、相机同步
    void FixedUpdate(float dt); // 物理：固定步长

    // ------------------------------------------------ 渲染输入
    void BuildRenderScene(RenderScene& scene, const Camera& viewCamera) const;
    Camera* GetActiveCamera();
    bool    HasActiveCamera() const;

    // ------------------------------------------------ 查询（拾取 / 角色碰撞 / AI 视线）
    struct RaycastHit {
        GameObject* object = nullptr;
        float       distance = 0.0f;
        Vec3        point;
        Vec3        normal;
    };
    // ignore：跳过指定对象（射线起点位于角色自身 AABB 内时，避免 t=0 自命中抢占最近命中）。
    bool Raycast(const Ray& ray, float maxDistance, RaycastHit& out, const GameObject* ignore = nullptr) const;
    // 收集所有带 MeshRenderer 的物体的世界 AABB（角色碰撞、相机弹簧臂用）
    void CollectMeshAABBs(std::vector<std::pair<GameObject*, AABB>>& out) const;
    bool GetObjectBounds(GameObject* object, AABB& out) const;

    // ------------------------------------------------ 序列化
    JsonValue ToJson() const;
    bool      FromJson(const JsonValue& root);
    // 场景来自哪个内置关卡（"survivors" / "survivors3d" / ...），用于读档后恢复 2D/3D 模式
    void               SetLevelKind(const std::string& kind) { m_levelKind = kind; }
    const std::string& LevelKind() const { return m_levelKind; }

    bool      SaveToFile(const std::string& path) const;
    bool      LoadFromFile(const std::string& path);

    // ------------------------------------------------ 演示场景
    void CreateDemoScene();

    AssetManager*  Assets() const { return m_assets; }
    PhysicsWorld*  Physics() const { return m_physics.get(); }
    u64            NextId() const { return m_nextId; }

    // 导航网格（AI 用）：场景变化后调用 RebuildNavigation
    NavGrid&       Nav() { return *m_navGrid; }
    const NavGrid& Nav() const { return *m_navGrid; }
    void           RebuildNavigation();

    // 全局光照参数（编辑器可直接调）
    Vec3  sunDirection{0.45f, 0.75f, 0.35f};
    Vec3  sunColor{1.0f, 0.95f, 0.88f};
    float sunIntensity = 3.4f;
    Vec3  ambientUp{0.30f, 0.38f, 0.52f};
    Vec3  ambientDown{0.10f, 0.09f, 0.08f};
    float ambientIntensity = 0.9f;

    int   drawCalls = 0; // 由 Renderer 回填，用于 Stats 面板

private:
    friend class GameObject;
    void DestroyRecursive(GameObject* object);
    void RegisterRecursive(GameObject* object);

    AssetManager*                            m_assets = nullptr;
    std::string                              m_levelKind;
    Scope<PhysicsWorld>                      m_physics;
    Scope<NavGrid>                           m_navGrid;
    std::vector<Scope<GameObject>>           m_objects; // 所有权统一在 World
    std::vector<GameObject*>                 m_roots;
    std::unordered_map<u64, GameObject*>     m_idToObject;
    std::vector<GameObject*>                 m_pendingDestroy;
    u64                                      m_nextId = 1;
    bool                                     m_started = false;
};

} // namespace erm
