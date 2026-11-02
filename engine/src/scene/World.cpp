// ErMao Engine - 世界实现
#include "scene/World.h"
#include "ai/Agents.h"
#include "ai/NavGrid.h"
#include "animation/Animation.h"
#include "asset/AssetManager.h"
#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"
#include "physics/PhysicsWorld.h"
#include "renderer/Camera.h"
#include "renderer/RenderScene.h"
#include "scene/Components.h"

#include <algorithm>

namespace erm {

namespace {

JsonValue Vec3ToJson(const Vec3& v) {
    JsonValue a = JsonValue::MakeArray();
    a.Push(v.x);
    a.Push(v.y);
    a.Push(v.z);
    return a;
}

JsonValue Vec4ToJson(const Vec4& v) {
    JsonValue a = JsonValue::MakeArray();
    a.Push(v.x);
    a.Push(v.y);
    a.Push(v.z);
    a.Push(v.w);
    return a;
}

Vec3 JsonToVec3(const JsonValue& a, const Vec3& def) {
    if (!a.IsArray() || a.Size() < 3) return def;
    return Vec3(a[0].AsFloat(def.x), a[1].AsFloat(def.y), a[2].AsFloat(def.z));
}

Vec4 JsonToVec4(const JsonValue& a, const Vec4& def) {
    if (!a.IsArray() || a.Size() < 4) return def;
    return Vec4(a[0].AsFloat(def.x), a[1].AsFloat(def.y), a[2].AsFloat(def.z), a[3].AsFloat(def.w));
}

JsonValue SerializeField(const FieldDesc& field, void* object) {
    void* ptr = field.access(object);
    switch (field.type) {
        case FieldType::Bool: return JsonValue(*static_cast<bool*>(ptr));
        case FieldType::Int: return JsonValue(*static_cast<int*>(ptr));
        case FieldType::Float: return JsonValue(*static_cast<float*>(ptr));
        case FieldType::Vec2: {
            JsonValue a = JsonValue::MakeArray();
            const Vec2& v = *static_cast<Vec2*>(ptr);
            a.Push(v.x);
            a.Push(v.y);
            return a;
        }
        case FieldType::Vec3: return Vec3ToJson(*static_cast<Vec3*>(ptr));
        case FieldType::Vec4:
        case FieldType::Color: return Vec4ToJson(*static_cast<Vec4*>(ptr));
        case FieldType::String:
        case FieldType::MeshAsset:
        case FieldType::TextureAsset: return JsonValue(*static_cast<std::string*>(ptr));
    }
    return JsonValue();
}

void DeserializeField(const FieldDesc& field, void* object, const JsonValue& value) {
    void* ptr = field.access(object);
    switch (field.type) {
        case FieldType::Bool: *static_cast<bool*>(ptr) = value.AsBool(*static_cast<bool*>(ptr)); break;
        case FieldType::Int: *static_cast<int*>(ptr) = value.AsInt(*static_cast<int*>(ptr)); break;
        case FieldType::Float: *static_cast<float*>(ptr) = value.AsFloat(*static_cast<float*>(ptr)); break;
        case FieldType::Vec2: {
            Vec2& v = *static_cast<Vec2*>(ptr);
            if (value.IsArray() && value.Size() >= 2) {
                v.x = value[0].AsFloat(v.x);
                v.y = value[1].AsFloat(v.y);
            }
            break;
        }
        case FieldType::Vec3: *static_cast<Vec3*>(ptr) = JsonToVec3(value, *static_cast<Vec3*>(ptr)); break;
        case FieldType::Vec4:
        case FieldType::Color: *static_cast<Vec4*>(ptr) = JsonToVec4(value, *static_cast<Vec4*>(ptr)); break;
        case FieldType::String:
        case FieldType::MeshAsset:
        case FieldType::TextureAsset: *static_cast<std::string*>(ptr) = value.AsString(*static_cast<std::string*>(ptr)); break;
    }
}

} // namespace

World::World() : m_navGrid(MakeScope<NavGrid>()) {}
World::~World() { Clear(); }

void World::Init(AssetManager* assets) {
    m_assets = assets;
    m_physics = MakeScope<PhysicsWorld>();
    // 基础组件由 scene/ai 注册；game 组件由 runtime 的 Engine::Init 调 game::RegisterGameComponents()。
    RegisterAllComponents();
    RegisterAgentComponents();
}

void World::Shutdown() {
    Clear();
    m_physics.reset();
}

void World::Clear() {
    m_levelKind.clear();
    m_pendingDestroy.clear();
    m_objects.clear(); // unique_ptr 释放，真正销毁对象
    m_roots.clear();
    m_idToObject.clear();
    m_nextId = 1;
    m_started = false;
}

GameObject* World::CreateGameObject(const std::string& name, GameObject* parent) {
    auto object = MakeScope<GameObject>(this, m_nextId++, name);
    GameObject* raw = object.get();

    // 关键：World 拥有所有对象。父节点只持有裸指针，避免"子对象随局部 unique_ptr 销毁"。
    m_objects.push_back(std::move(object));
    m_idToObject[raw->Id()] = raw;

    if (parent) {
        raw->m_parent = parent;
        parent->m_children.push_back(raw);
    } else {
        m_roots.push_back(raw);
    }
    if (m_started) raw->Start();
    return raw;
}

void World::DestroyObject(GameObject* object) {
    if (!object) return;
    object->MarkForDestroy();
    m_pendingDestroy.push_back(object);
}

void World::ProcessPendingDestruction() {
    if (m_pendingDestroy.empty()) return;

    std::vector<GameObject*> toKill;
    for (auto* object : m_pendingDestroy) {
        if (!object) continue;
        object->ForEach([&](GameObject* o) { toKill.push_back(o); });
    }

    // 先把所有待删对象从层级里摘掉（此时它们都还活着）
    for (auto* object : toKill) {
        if (object->m_parent) {
            auto& siblings = object->m_parent->m_children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), object), siblings.end());
        } else {
            m_roots.erase(std::remove(m_roots.begin(), m_roots.end(), object), m_roots.end());
        }
        m_idToObject.erase(object->Id());
    }

    // 再统一释放（unique_ptr 析构）
    m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                   [&toKill](const Scope<GameObject>& ptr) {
                                       return std::find(toKill.begin(), toKill.end(), ptr.get()) != toKill.end();
                                   }),
                    m_objects.end());
    m_pendingDestroy.clear();
}

GameObject* World::FindByName(const std::string& name) const {
    for (GameObject* root : m_roots) {
        GameObject* found = nullptr;
        root->ForEach([&](GameObject* o) {
            if (!found && o->Name() == name) found = o;
        });
        if (found) return found;
    }
    return nullptr;
}

GameObject* World::FindById(u64 id) const {
    auto it = m_idToObject.find(id);
    return it != m_idToObject.end() ? it->second : nullptr;
}

void World::Update(float dt) {
    if (dt <= 0.0f) return;
    m_started = true;
    // 快照根节点：组件在 OnUpdate 里会生成箭/宝石/特效（例如幸存者击杀掉落），
    // 直接遍历 m_roots 会在 vector 扩容时失效，造成偶发 0xC0000005。
    // 新生成的对象从下一帧开始 Start/Update，行为更可预测。
    const std::vector<GameObject*> roots = m_roots;
    for (GameObject* root : roots) {
        root->ForEach([dt](GameObject* object) {
            object->Start();
            object->Update(dt);
        });
    }

    // 相机组件跟随 Transform
    for (GameObject* root : roots) {
        root->ForEach([](GameObject* object) {
            if (auto* cam = object->GetComponent<CameraComponent>()) cam->SyncFromTransform();
        });
    }
    ProcessPendingDestruction();
}

void World::FixedUpdate(float dt) {
    if (dt <= 0.0f) return;
    if (m_physics) m_physics->Step(*this, dt);
    const std::vector<GameObject*> roots = m_roots; // 同上：FixedUpdate 里也可能生成对象
    for (GameObject* root : roots) {
        root->ForEach([dt](GameObject* object) { object->FixedUpdate(dt); });
    }
}

Camera* World::GetActiveCamera() {
    Camera* result = nullptr;
    for (GameObject* root : m_roots) {
        root->ForEach([&result](GameObject* object) {
            auto* cam = object->GetComponent<CameraComponent>();
            if (cam && cam->primary && !result) result = &cam->camera;
        });
    }
    return result;
}

bool World::HasActiveCamera() const {
    for (GameObject* root : m_roots) {
        bool found = false;
        root->ForEach([&found](GameObject* object) {
            auto* cam = object->GetComponent<CameraComponent>();
            if (cam && cam->primary) found = true;
        });
        if (found) return true;
    }
    return false;
}

void World::BuildRenderScene(RenderScene& scene, const Camera& viewCamera) const {
    scene.items.clear();
    scene.pointLights.clear();
    scene.sprites.clear();

    // 2D 精灵：按 layer 排序（2D 的"深度"就是绘制顺序）
    struct SpriteEntry {
        SpriteDraw draw;
        int        layer = 0;
    };
    std::vector<SpriteEntry> spriteEntries;
    for (GameObject* root : m_roots) {
        root->ForEach([&](GameObject* object) {
            if (!object->Active()) return;
            auto* sprite = object->GetComponent<SpriteRendererComponent>();
            if (!sprite) return;
            Texture* texture = m_assets ? m_assets->GetTextureAsset(sprite->texture) : nullptr;
            if (!texture) return;

            const Vec3 position = object->WorldPosition();
            SpriteDraw draw;
            draw.texture = texture;
            draw.dest = Vec4(position.x + sprite->offset.x, position.y + sprite->offset.y, sprite->size.x,
                             sprite->size.y);
            draw.uv = sprite->uv;
            draw.color = sprite->color;
            draw.flipX = sprite->flipX;
            // 2D 精灵同时响应 SpriteRenderer 自己的 rotation 和 Transform 的 Z 旋转，
            // 这样编辑器里用旋转 gizmo 转精灵能直接看到结果。
            float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
            object->Rotation().ToEuler(yaw, pitch, roll);
            draw.rotation = sprite->rotation + roll;
            spriteEntries.push_back({draw, sprite->layer});
        });
    }
    std::stable_sort(spriteEntries.begin(), spriteEntries.end(),
                     [](const SpriteEntry& a, const SpriteEntry& b) { return a.layer < b.layer; });
    for (auto& entry : spriteEntries) scene.sprites.push_back(entry.draw);

    scene.sunDirection = -viewCamera.Forward(); // 兜底
    scene.sunColor = sunColor;
    scene.sunIntensity = sunIntensity;
    scene.ambientUp = ambientUp;
    scene.ambientDown = ambientDown;
    scene.ambientIntensity = ambientIntensity;
    scene.shadowFocus = viewCamera.position + viewCamera.Forward() * 12.0f;
    scene.shadowRadius = 22.0f;

    bool sunFound = false;
    for (GameObject* root : m_roots) {
        root->ForEach([&](GameObject* object) {
            if (!object->Active()) return;

            if (auto* light = object->GetComponent<DirectionalLightComponent>()) {
                if (!sunFound) {
                    sunFound = true;
                    scene.sunDirection = -Normalize(object->Forward());
                    scene.sunColor = light->color.xyz();
                    scene.sunIntensity = light->intensity;
                }
            }
            if (auto* point = object->GetComponent<PointLightComponent>()) {
                if (scene.pointLights.size() < 8) {
                    PointLightRender pl;
                    pl.position = object->WorldPosition();
                    pl.color = point->color.xyz();
                    pl.intensity = point->intensity;
                    pl.range = point->range;
                    scene.pointLights.push_back(pl);
                }
            }
            if (auto* renderer = object->GetComponent<MeshRenderer>()) {
                if (!m_assets) return;
                RenderItem item;
                item.mesh = renderer->mesh.find('/') != std::string::npos ? m_assets->GetMeshAsset(renderer->mesh)
                                                                          : m_assets->GetMesh(renderer->mesh);
                item.world = object->WorldMatrix();
                item.albedo = renderer->albedo;
                item.roughness = renderer->roughness;
                item.metallic = renderer->metallic;
                item.emissive = renderer->emissive;
                item.uvScale = renderer->uvScale;
                if (!renderer->texture.empty()) {
                    item.baseColor = renderer->texture.find('/') != std::string::npos
                                         ? m_assets->GetTextureAsset(renderer->texture)
                                         : m_assets->GetTexture(renderer->texture);
                }
                item.castShadow = renderer->castShadow;
                if (item.mesh) scene.items.push_back(item);
            }
        });
    }
}

// ---------------------------------------------------------------- 序列化
JsonValue World::ToJson() const {
    JsonValue root = JsonValue::MakeObject();
    root.Set("version", JsonValue(1));
    if (!m_levelKind.empty()) root.Set("levelKind", JsonValue(m_levelKind));

    JsonValue sun = JsonValue::MakeObject();
    sun.Set("direction", Vec3ToJson(sunDirection));
    sun.Set("color", Vec3ToJson(sunColor));
    sun.Set("intensity", JsonValue(sunIntensity));
    sun.Set("ambientUp", Vec3ToJson(ambientUp));
    sun.Set("ambientDown", Vec3ToJson(ambientDown));
    sun.Set("ambientIntensity", JsonValue(ambientIntensity));
    root.Set("sun", sun);

    JsonValue objects = JsonValue::MakeArray();
    for (GameObject* rootObject : m_roots) {
        rootObject->ForEach([&](GameObject* object) {
            JsonValue node = JsonValue::MakeObject();
            node.Set("id", JsonValue(static_cast<int>(object->Id())));
            node.Set("name", JsonValue(object->Name()));
            node.Set("active", JsonValue(object->Active()));
            node.Set("parent", JsonValue(static_cast<int>(object->Parent() ? object->Parent()->Id() : 0)));
            node.Set("position", Vec3ToJson(object->Position()));
            JsonValue rot = JsonValue::MakeArray();
            rot.Push(object->Rotation().x);
            rot.Push(object->Rotation().y);
            rot.Push(object->Rotation().z);
            rot.Push(object->Rotation().w);
            node.Set("rotation", rot);
            node.Set("scale", Vec3ToJson(object->Scale()));

            JsonValue components = JsonValue::MakeArray();
            for (const auto& comp : object->Components()) {
                const TypeInfo& info = comp->GetTypeInfo();
                JsonValue c = JsonValue::MakeObject();
                c.Set("type", JsonValue(info.name));
                for (const auto& field : info.fields) {
                    if (field.hidden) continue;
                    c.Set(field.name, SerializeField(field, comp.get()));
                }
                components.Push(c);
            }
            node.Set("components", components);
            objects.Push(node);
        });
    }
    root.Set("objects", objects);
    return root;
}

bool World::FromJson(const JsonValue& root) {
    if (!root.IsObject() || !root.Has("objects")) {
        LogError("场景 JSON 格式不正确（缺少 objects）");
        return false;
    }

    Clear();
    m_levelKind = root["levelKind"].AsString();

    if (root.Has("sun")) {
        const JsonValue& sun = root["sun"];
        sunDirection = JsonToVec3(sun["direction"], sunDirection);
        sunColor = JsonToVec3(sun["color"], sunColor);
        sunIntensity = sun["intensity"].AsFloat(sunIntensity);
        ambientUp = JsonToVec3(sun["ambientUp"], ambientUp);
        ambientDown = JsonToVec3(sun["ambientDown"], ambientDown);
        ambientIntensity = sun["ambientIntensity"].AsFloat(ambientIntensity);
    }

    const JsonValue& objects = root["objects"];
    std::unordered_map<u64, GameObject*> byId;
    std::vector<std::pair<GameObject*, u64>> parents;

    // 第一遍：建对象与变换
    for (size_t i = 0; i < objects.Size(); ++i) {
        const JsonValue& node = objects[i];
        const u64 id = static_cast<u64>(node["id"].AsInt(0));
        GameObject* object = CreateGameObject(node["name"].AsString("GameObject"));
        object->SetActive(node["active"].AsBool(true));
        object->SetPosition(JsonToVec3(node["position"], Vec3(0, 0, 0)));
        const JsonValue& rot = node["rotation"];
        if (rot.IsArray() && rot.Size() >= 4) {
            object->SetRotation(Quat(rot[0].AsFloat(0), rot[1].AsFloat(0), rot[2].AsFloat(0), rot[3].AsFloat(1)));
        }
        object->SetScale(JsonToVec3(node["scale"], Vec3(1, 1, 1)));

        // 保留存档里的 id，保证父子引用稳定
        if (id != 0) {
            m_idToObject.erase(object->Id());
            m_idToObject[id] = object;
            object->m_id = id;
            if (id >= m_nextId) m_nextId = id + 1;
        }
        byId[object->Id()] = object;

        const u64 parentId = static_cast<u64>(node["parent"].AsInt(0));
        if (parentId != 0) parents.emplace_back(object, parentId);

        // 组件 + 字段
        const JsonValue& components = node["components"];
        for (size_t c = 0; c < components.Size(); ++c) {
            const JsonValue& compJson = components[c];
            const std::string typeName = compJson["type"].AsString();
            TypeInfo* info = Reflection::Find(typeName);
            if (!info) {
                LogWarn("场景引用了未知组件类型: %s", typeName.c_str());
                continue;
            }
            Component* comp = object->AddComponentByTypeName(typeName);
            if (!comp) continue;
            for (const auto& field : info->fields) {
                if (compJson.Has(field.name)) DeserializeField(field, comp, compJson[field.name]);
            }
        }
    }

    // 第二遍：恢复父子关系
    for (const auto& [object, parentId] : parents) {
        auto it = byId.find(parentId);
        if (it != byId.end()) object->SetParent(it->second);
    }

    m_started = true;
    RebuildNavigation();
    LogInfo("场景加载完成：%zu 个对象", m_idToObject.size());
    return true;
}

bool World::SaveToFile(const std::string& path) const {
    const std::string text = ToJson().Dump(2);
    if (!FileSystem::WriteTextFile(path, text)) {
        LogError("场景保存失败: %s", path.c_str());
        return false;
    }
    LogInfo("场景已保存: %s (%zu 字节)", path.c_str(), text.size());
    return true;
}

bool World::LoadFromFile(const std::string& path) {
    std::string text;
    if (!FileSystem::ReadTextFile(path, text)) {
        LogError("场景文件读取失败: %s", path.c_str());
        return false;
    }
    std::string error;
    JsonValue root = JsonValue::Parse(text, &error);
    if (!error.empty()) {
        LogError("场景 JSON 解析失败: %s", error.c_str());
        return false;
    }
    return FromJson(root);
}

// ---------------------------------------------------------------- 演示场景
void World::CreateDemoScene() {
    Clear();

    // 太阳（方向光）
    GameObject* sun = CreateGameObject("太阳");
    sun->SetRotation(Quat::FromEuler(ToRadians(-35.0f), ToRadians(-40.0f), 0.0f));
    auto* sunLight = sun->AddComponent<DirectionalLightComponent>();
    sunLight->color = Vec4(1.0f, 0.95f, 0.86f, 1.0f);
    sunLight->intensity = 3.6f;

    // 地面
    GameObject* ground = CreateGameObject("地面");
    ground->SetScale(Vec3(60, 1, 60));
    auto* groundRenderer = ground->AddComponent<MeshRenderer>();
    groundRenderer->mesh = "plane";
    groundRenderer->texture = "grid";
    groundRenderer->uvScale = 30.0f;
    groundRenderer->roughness = 0.9f;
    groundRenderer->albedo = Vec4(0.75f, 0.76f, 0.78f, 1.0f);

    // 相机
    GameObject* cameraObject = CreateGameObject("主相机");
    cameraObject->SetPosition(Vec3(6.0f, 4.5f, 10.0f));
    cameraObject->SetRotationEulerDegrees(Vec3(-18.0f, 25.0f, 0.0f));
    auto* camera = cameraObject->AddComponent<CameraComponent>();
    camera->fovDegrees = 55.0f;

    // 静态箱子（物理 + 渲染）
    struct CrateDef {
        Vec3 position;
        Vec3 scale;
        Vec4 color;
    };
    const CrateDef crates[] = {
        {{-3.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.62f, 0.36f, 0.22f, 1.0f}},
        {{-3.0f, 1.5f, 0.0f}, {0.9f, 0.9f, 0.9f}, {0.70f, 0.45f, 0.28f, 1.0f}},
        {{-1.6f, 0.5f, -2.0f}, {1.2f, 1.0f, 1.2f}, {0.30f, 0.42f, 0.52f, 1.0f}},
        {{2.6f, 0.5f, -1.4f}, {1.0f, 1.0f, 1.0f}, {0.35f, 0.50f, 0.40f, 1.0f}},
        {{0.4f, 0.4f, -4.5f}, {0.8f, 0.8f, 0.8f}, {0.55f, 0.40f, 0.55f, 1.0f}},
    };
    int index = 0;
    for (const auto& def : crates) {
        GameObject* crate = CreateGameObject("木箱_" + std::to_string(index++));
        crate->SetPosition(def.position);
        crate->SetScale(def.scale);
        auto* renderer = crate->AddComponent<MeshRenderer>();
        renderer->mesh = "cube";
        renderer->albedo = def.color;
        renderer->roughness = 0.65f;
        renderer->texture = "noise";
        renderer->uvScale = 1.0f;
        auto* body = crate->AddComponent<RigidBodyComponent>();
        body->shape = static_cast<int>(RigidBodyComponent::Shape::Box);
        body->halfExtents = def.scale * 0.5f;
        body->isStatic = true;
    }

    // 动态小球
    const Vec4 ballColors[] = {{0.9f, 0.25f, 0.2f, 1.0f}, {0.2f, 0.7f, 0.9f, 1.0f}, {0.95f, 0.8f, 0.25f, 1.0f}};
    for (int i = 0; i < 6; ++i) {
        GameObject* ball = CreateGameObject("物理球_" + std::to_string(i));
        ball->SetPosition(Vec3(-2.0f + i * 0.9f, 0.55f + (i % 3) * 0.5f, 2.2f - (i % 3) * 0.9f));
        ball->SetScale(Vec3(0.5f, 0.5f, 0.5f));
        auto* renderer = ball->AddComponent<MeshRenderer>();
        renderer->mesh = "sphere";
        renderer->albedo = ballColors[i % 3];
        renderer->roughness = 0.25f;
        renderer->metallic = 0.1f;
        auto* body = ball->AddComponent<RigidBodyComponent>();
        body->shape = static_cast<int>(RigidBodyComponent::Shape::Sphere);
        body->radius = 0.5f;
        body->mass = 1.0f;
        body->restitution = 0.6f;
        body->velocity = Vec3((i % 2) ? 1.5f : -1.2f, 0.0f, 0.6f);
    }

    // 自转装饰
    GameObject* ornament = CreateGameObject("旋转雕塑");
    ornament->SetPosition(Vec3(3.6f, 1.4f, 2.4f));
    auto* ornamentRenderer = ornament->AddComponent<MeshRenderer>();
    ornamentRenderer->mesh = "torus";
    ornamentRenderer->albedo = Vec4(0.85f, 0.62f, 0.15f, 1.0f);
    ornamentRenderer->roughness = 0.2f;
    ornamentRenderer->metallic = 0.85f;
    auto* spinner = ornament->AddComponent<SpinnerComponent>();
    spinner->axis = Vec3(0.2f, 1.0f, 0.0f);
    spinner->degreesPerSecond = 55.0f;

    // 点光源（灯笼）
    GameObject* lamp = CreateGameObject("点光源");
    lamp->SetPosition(Vec3(-1.0f, 2.6f, 3.5f));
    auto* point = lamp->AddComponent<PointLightComponent>();
    point->color = Vec4(1.0f, 0.55f, 0.25f, 1.0f);
    point->intensity = 9.0f;
    point->range = 9.0f;

    GameObject* lampBulb = CreateGameObject("灯笼");
    lampBulb->SetPosition(Vec3(-1.0f, 2.6f, 3.5f));
    lampBulb->SetScale(Vec3(0.25f, 0.25f, 0.25f));
    auto* bulbRenderer = lampBulb->AddComponent<MeshRenderer>();
    bulbRenderer->mesh = "sphere";
    bulbRenderer->albedo = Vec4(1.0f, 0.72f, 0.35f, 1.0f);
    bulbRenderer->emissive = 2.5f;
    auto* bulbSpinner = lampBulb->AddComponent<SpinnerComponent>();
    bulbSpinner->degreesPerSecond = 20.0f;

    // ------------------------------------------------ 玩家（WASD 控制 + 第三人称相机）
    GameObject* player = CreateGameObject("玩家");
    player->SetPosition(Vec3(0.0f, 1.0f, 7.0f));
    auto* playerRenderer = player->AddComponent<MeshRenderer>();
    playerRenderer->mesh = "capsule";
    playerRenderer->albedo = Vec4(0.22f, 0.48f, 0.92f, 1.0f);
    playerRenderer->roughness = 0.4f;
    auto* playerController = player->AddComponent<PlayerController>();
    playerController->height = 1.0f;

    GameObject* playerCamera = CreateGameObject("玩家相机", player);
    playerCamera->SetPosition(Vec3(0.0f, 2.6f, 6.4f));
    playerCamera->SetRotation(Quat::FromEuler(0.0f, ToRadians(-11.0f), 0.0f));
    auto* playerCam = playerCamera->AddComponent<CameraComponent>();
    playerCam->fovDegrees = 55.0f;
    playerCam->primary = true;

    // 旧的静态相机不再是主相机
    cameraObject->GetComponent<CameraComponent>()->primary = false;

    // ------------------------------------------------ AI 小怪（A* 巡逻/追击）
    const Vec3 agentSpawns[] = {{-6.0f, 1.0f, -4.0f}, {6.0f, 1.0f, -6.0f}, {-8.0f, 1.0f, 5.0f}};
    int agentIndex = 0;
    for (const auto& spawn : agentSpawns) {
        GameObject* agent = CreateGameObject("小怪_" + std::to_string(agentIndex++));
        agent->SetPosition(spawn);
        auto* renderer = agent->AddComponent<MeshRenderer>();
        renderer->mesh = "capsule";
        renderer->albedo = Vec4(0.85f, 0.25f, 0.28f, 1.0f);
        renderer->roughness = 0.5f;
        auto* navAgent = agent->AddComponent<NavAgentComponent>();
        navAgent->patrolCenter = spawn;
        navAgent->patrolRadius = 4.5f;
        navAgent->moveSpeed = 2.4f;
        navAgent->sightRange = 12.0f;
    }

    // ------------------------------------------------ 关键帧动画演示（第 8 讲：节点动画）
    GameObject* rotor = CreateGameObject("风车机关");
    rotor->SetPosition(Vec3(7.0f, 2.6f, -3.0f));
    auto* rotorAnimator = rotor->AddComponent<AnimatorComponent>();
    rotorAnimator->clip = "windmill";
    rotorAnimator->target = "风车叶片";

    GameObject* blade = CreateGameObject("风车叶片", rotor);
    auto* bladeRenderer = blade->AddComponent<MeshRenderer>();
    bladeRenderer->mesh = "cube";
    bladeRenderer->albedo = Vec4(0.72f, 0.62f, 0.35f, 1.0f);
    bladeRenderer->roughness = 0.4f;
    blade->SetScale(Vec3(0.18f, 0.18f, 2.6f));

    GameObject* pod = CreateGameObject("风车吊舱", blade);
    pod->SetPosition(Vec3(0.0f, 0.0f, 1.5f));
    auto* podRenderer = pod->AddComponent<MeshRenderer>();
    podRenderer->mesh = "sphere";
    podRenderer->albedo = Vec4(0.85f, 0.75f, 0.30f, 1.0f);
    podRenderer->metallic = 0.6f;
    podRenderer->roughness = 0.2f;
    pod->SetScale(Vec3(0.55f, 0.55f, 0.55f));

    GameObject* bobRoot = CreateGameObject("浮动水晶");
    bobRoot->SetPosition(Vec3(-6.0f, 1.8f, 2.0f));
    auto* bobAnimator = bobRoot->AddComponent<AnimatorComponent>();
    bobAnimator->clip = "bob";
    bobAnimator->target = "水晶";

    GameObject* crystal = CreateGameObject("水晶", bobRoot);
    auto* crystalRenderer = crystal->AddComponent<MeshRenderer>();
    crystalRenderer->mesh = "torus";
    crystalRenderer->albedo = Vec4(0.35f, 0.85f, 0.95f, 1.0f);
    crystalRenderer->emissive = 0.6f;
    crystalRenderer->roughness = 0.15f;
    crystalRenderer->metallic = 0.4f;
    crystal->SetScale(Vec3(0.6f, 0.6f, 0.6f));

    m_started = true;
    RebuildNavigation();
    LogInfo("演示场景已创建：%zu 个对象（含玩家、%d 个 AI、2 个动画）", m_idToObject.size(), agentIndex);
}

void World::RebuildNavigation() {
    m_navGrid->Build(*this, -25.0f, 25.0f, -25.0f, 25.0f, 0.5f, 0.45f);
}

} // namespace erm
