// ErMao Engine - AI 优先的无头脚本接口
//
// 设计目标：AI（或 CI）不需要打开编辑器，就能完成「建场景 / 绑资产 / 存场景 / 出图 / 打包」。
// 输入是一段 JSON 命令列表，输出是逐条结果；全部走反射，不新增 Editor 依赖。
//
// 示例：
//   ErMao.exe --script agent/examples/build_scene.json --json-out out.json
#include "runtime/Engine.h"

#include "asset/AssetManager.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "renderer/Renderer.h"
#include "game/Levels.h"
#include "game/ParticleEmitter.h"
#include "game/SkinningDemo.h"
#include "runtime/Packager.h"
#include <fstream>
#include "scene/Components.h"
#include "scene/World.h"

namespace erm {

namespace {

JsonValue Vec3Json(const Vec3& v) {
    JsonValue a = JsonValue::MakeArray();
    a.Push(v.x);
    a.Push(v.y);
    a.Push(v.z);
    return a;
}

JsonValue QuatJson(const Quat& q) {
    JsonValue a = JsonValue::MakeArray();
    a.Push(q.x);
    a.Push(q.y);
    a.Push(q.z);
    a.Push(q.w);
    return a;
}

bool ReadVec3(const JsonValue& value, Vec3& out) {
    if (!value.IsArray() || value.Size() < 3) return false;
    out = Vec3(value[0].AsFloat(), value[1].AsFloat(), value[2].AsFloat());
    return true;
}

bool ReadQuat(const JsonValue& value, Quat& out) {
    if (!value.IsArray() || value.Size() < 4) return false;
    out = Quat(value[0].AsFloat(), value[1].AsFloat(), value[2].AsFloat(), value[3].AsFloat());
    return true;
}

// 反射字段 → JSON
JsonValue FieldToJson(const FieldDesc& field, void* object) {
    void* ptr = field.access(object);
    switch (field.type) {
        case FieldType::Bool: return JsonValue(*static_cast<bool*>(ptr));
        case FieldType::Int: return JsonValue(*static_cast<int*>(ptr));
        case FieldType::Float: return JsonValue(*static_cast<float*>(ptr));
        case FieldType::String:
        case FieldType::MeshAsset:
        case FieldType::TextureAsset: return JsonValue(*static_cast<std::string*>(ptr));
        case FieldType::Vec2: {
            const Vec2& v = *static_cast<Vec2*>(ptr);
            JsonValue a = JsonValue::MakeArray();
            a.Push(v.x);
            a.Push(v.y);
            return a;
        }
        case FieldType::Vec3: return Vec3Json(*static_cast<Vec3*>(ptr));
        case FieldType::Vec4:
        case FieldType::Color: {
            const Vec4& v = *static_cast<Vec4*>(ptr);
            JsonValue a = JsonValue::MakeArray();
            a.Push(v.x);
            a.Push(v.y);
            a.Push(v.z);
            a.Push(v.w);
            return a;
        }
    }
    return JsonValue();
}

// JSON → 反射字段
bool JsonToField(const FieldDesc& field, void* object, const JsonValue& value) {
    void* ptr = field.access(object);
    switch (field.type) {
        case FieldType::Bool: *static_cast<bool*>(ptr) = value.AsBool(); return true;
        case FieldType::Int: *static_cast<int*>(ptr) = value.AsInt(); return true;
        case FieldType::Float: *static_cast<float*>(ptr) = value.AsFloat(); return true;
        case FieldType::String:
        case FieldType::MeshAsset:
        case FieldType::TextureAsset: *static_cast<std::string*>(ptr) = value.AsString(); return true;
        case FieldType::Vec2: {
            if (!value.IsArray() || value.Size() < 2) return false;
            *static_cast<Vec2*>(ptr) = Vec2(value[0].AsFloat(), value[1].AsFloat());
            return true;
        }
        case FieldType::Vec3: {
            Vec3 v;
            if (!ReadVec3(value, v)) return false;
            *static_cast<Vec3*>(ptr) = v;
            return true;
        }
        case FieldType::Vec4:
        case FieldType::Color: {
            if (!value.IsArray() || value.Size() < 4) return false;
            *static_cast<Vec4*>(ptr) =
                Vec4(value[0].AsFloat(), value[1].AsFloat(), value[2].AsFloat(), value[3].AsFloat());
            return true;
        }
    }
    return false;
}

Component* FindComponent(GameObject* object, const std::string& name) {
    for (const auto& component : object->Components()) {
        if (component->GetTypeInfo().name == name) return component.get();
    }
    return nullptr;
}

JsonValue Error(const std::string& message) {
    JsonValue result = JsonValue::MakeObject();
    result.Set("ok", JsonValue(false));
    result.Set("error", JsonValue(message));
    return result;
}

JsonValue Ok() {
    JsonValue result = JsonValue::MakeObject();
    result.Set("ok", JsonValue(true));
    return result;
}

} // namespace

JsonValue Engine::ExecuteAgentCommand(const JsonValue& command) {
    const std::string cmd = command["cmd"].AsString();

    // ---------------------------------------------------------------- 场景
    if (cmd == "scene.new") {
        m_world.Clear();
        // scene.new 是"清空世界"，也必须把引擎侧的关卡模式状态复位；否则从 2D 关卡
        // 建空场景后 m_is2D/m_survivorsLevel 仍为 true，截图与运行会沿用上一关。
        m_survivorsLevel = false;
        m_survivors3dLevel = false;
        m_waveLevel = false;
        m_is2D = false;
        m_playSessionStarted = false;
        m_waveGame.ReturnToMenu();
        m_survivors.ReturnToMenu();
        m_survivors3d.ReturnToMenu();
        return Ok();
    }
    if (cmd == "scene.save") {
        const std::string path = command["path"].AsString();
        if (path.empty()) return Error("缺少 path");
        if (!m_world.SaveToFile(path)) return Error("保存失败: " + path);
        JsonValue result = Ok();
        result.Set("path", JsonValue(path));
        return result;
    }
    if (cmd == "scene.load") {
        const std::string path = command["path"].AsString();
        if (path.empty() || !m_world.LoadFromFile(path)) return Error("加载失败: " + path);
        ApplyLevelMode(m_world.LevelKind());
        JsonValue result = Ok();
        result.Set("objects", JsonValue(static_cast<unsigned>(m_world.ObjectCount())));
        return result;
    }
    if (cmd == "scene.query") {
        JsonValue result = Ok();
        JsonValue objects = JsonValue::MakeArray();
        for (GameObject* root : m_world.RootObjects()) {
            root->ForEach([&](GameObject* object) {
                JsonValue entry = JsonValue::MakeObject();
                entry.Set("name", JsonValue(object->Name()));
                entry.Set("position", Vec3Json(object->Position()));
                entry.Set("rotation", QuatJson(object->Rotation()));
                entry.Set("scale", Vec3Json(object->Scale()));
                JsonValue components = JsonValue::MakeArray();
                for (const auto& component : object->Components()) {
                    components.Push(JsonValue(component->GetTypeInfo().name));
                }
                entry.Set("components", components);
                objects.Push(entry);
            });
        }
        result.Set("objects", objects);
        return result;
    }

    // ---------------------------------------------------------------- 对象
    if (cmd == "object.create") {
        const std::string name = command["name"].AsString();
        if (name.empty()) return Error("缺少 name");
        GameObject* object = m_world.CreateGameObject(name);
        if (command.Has("position")) {
            Vec3 position;
            if (ReadVec3(command["position"], position)) object->SetPosition(position);
        }
        if (command.Has("rotation")) {
            Quat rotation;
            if (ReadQuat(command["rotation"], rotation)) object->SetRotation(rotation);
        }
        if (command.Has("scale")) {
            Vec3 scale;
            if (ReadVec3(command["scale"], scale)) object->SetScale(scale);
        }
        JsonValue result = Ok();
        result.Set("name", JsonValue(name));
        return result;
    }
    if (cmd == "object.destroy") {
        GameObject* object = m_world.FindByName(command["name"].AsString());
        if (!object) return Error("找不到对象");
        m_world.DestroyObject(object);
        m_world.ProcessPendingDestruction();
        return Ok();
    }
    if (cmd == "object.set") {
        GameObject* object = m_world.FindByName(command["name"].AsString());
        if (!object) return Error("找不到对象");
        if (command.Has("position")) object->SetPosition(Vec3(command["position"][0].AsFloat(),
                                                              command["position"][1].AsFloat(),
                                                              command["position"][2].AsFloat()));
        if (command.Has("scale")) object->SetScale(Vec3(command["scale"][0].AsFloat(),
                                                        command["scale"][1].AsFloat(),
                                                        command["scale"][2].AsFloat()));
        if (command.Has("rotation")) {
            Quat rotation;
            if (ReadQuat(command["rotation"], rotation)) object->SetRotation(rotation);
        }
        if (command.Has("active")) object->SetActive(command["active"].AsBool());
        return Ok();
    }

    // ---------------------------------------------------------------- 组件
    if (cmd == "component.add") {
        GameObject* object = m_world.FindByName(command["object"].AsString());
        if (!object) return Error("找不到对象");
        TypeInfo* type = Reflection::Find(command["component"].AsString());
        if (!type || !type->factory) return Error("未知组件: " + command["component"].AsString());
        if (!type->factory(object)) return Error("组件创建失败");
        JsonValue result = Ok();
        result.Set("component", JsonValue(type->name));
        return result;
    }
    if (cmd == "component.set" || cmd == "component.get") {
        GameObject* object = m_world.FindByName(command["object"].AsString());
        if (!object) return Error("找不到对象");
        Component* component = FindComponent(object, command["component"].AsString());
        if (!component) return Error("对象没有该组件");
        FieldDesc* field = const_cast<TypeInfo&>(component->GetTypeInfo()).FindField(command["field"].AsString());
        if (!field) return Error("字段不存在: " + command["field"].AsString());
        if (cmd == "component.get") {
            JsonValue result = Ok();
            result.Set("value", FieldToJson(*field, component));
            return result;
        }
        if (!JsonToField(*field, component, command["value"])) return Error("值类型不匹配");
        return Ok();
    }

    if (cmd == "camera.set") {
        if (command.Has("position")) {
            Vec3 position;
            if (ReadVec3(command["position"], position)) m_editorCamera.position = position;
        }
        if (command.Has("yaw")) m_editorCamera.yaw = command["yaw"].AsFloat();
        if (command.Has("pitch")) m_editorCamera.pitch = command["pitch"].AsFloat();
        if (command.Has("fov")) m_editorCamera.fovY = command["fov"].AsFloat();
        return Ok();
    }

    // ---------------------------------------------------------------- 资产
    if (cmd == "assets.import") {
        const std::string kind = command["kind"].AsString();
        const std::string path = command["path"].AsString();
        std::string error;
        JsonValue result = Ok();
        if (kind == "mesh") {
            Mesh* mesh = m_assets.ImportMesh(path, &error);
            if (!mesh) return Error(error);
            result.Set("name", JsonValue(path));
            result.Set("triangles", JsonValue(static_cast<unsigned>(mesh->IndexCount() / 3)));
        } else if (kind == "texture") {
            Texture* texture = m_assets.ImportTexture(path, &error);
            if (!texture) return Error(error);
            result.Set("name", JsonValue(path));
        } else {
            return Error("kind 必须是 mesh 或 texture");
        }
        return result;
    }
    if (cmd == "assets.list") {
        JsonValue result = Ok();
        JsonValue list = JsonValue::MakeArray();
        for (const auto& asset : m_assets.Assets()) {
            JsonValue entry = JsonValue::MakeObject();
            entry.Set("name", JsonValue(asset.name));
            entry.Set("type", JsonValue(asset.type == AssetManager::AssetType::Mesh     ? "mesh"
                                        : asset.type == AssetManager::AssetType::Texture ? "texture"
                                                                                         : "scene"));
            entry.Set("builtin", JsonValue(asset.builtin));
            list.Push(entry);
        }
        result.Set("assets", list);
        return result;
    }

    // ---------------------------------------------------------------- 关卡
    if (cmd == "level.build") {
        const std::string kind = command["kind"].AsString();
        // 关卡切换后同步 2D 标记，避免无头截图沿用上一关的 2D/3D 状态。
        m_is2D = (kind == "survivors");
        m_world.Clear();
        if (kind == "survivors3d") {
            BuildSurvivors3DLevel(m_world, m_assets);
            m_survivors3dLevel = true;
        } else if (kind == "survivors") {
            BuildSurvivorsLevel(m_world, m_assets);
            m_survivorsLevel = true;
        } else if (kind == "skinning") {
            game::BuildSkinningLevel(m_world, m_renderer);
        } else if (kind == "thirdperson") {
            game::BuildThirdPersonLevel(m_world);
        } else {
            return Error("未知关卡: " + kind);
        }
        m_world.SetLevelKind(kind);
        JsonValue result = Ok();
        result.Set("objects", JsonValue(static_cast<unsigned>(m_world.ObjectCount())));
        result.Set("levelKind", JsonValue(kind));
        return result;
    }

    // ---------------------------------------------------------------- 截图 / 校验 / 打包
    if (cmd == "screenshot") {
        const std::string path = command["path"].AsString();
        if (!RenderAgentScreenshot(path)) return Error("截图失败");
        JsonValue result = Ok();
        result.Set("path", JsonValue(path));
        return result;
    }
    if (cmd == "validate") {
        return ValidateWorld();
    }
    if (cmd == "package.game") {
        const std::string game = command["name"].AsString();
        const std::string level = command["level"].AsString();
        const std::string target = command["target"].AsString();
        if (game.empty() || level.empty()) return Error("缺少 name/level");
        const PackageResult result = (target == "linux") ? Packager::PackageGameLinuxSource(game, level)
                                                         : Packager::PackageGameWindows(game, level);
        if (!result.success) return Error(result.message);
        JsonValue out = Ok();
        out.Set("output", JsonValue(result.outputDir));
        out.Set("message", JsonValue(result.message));
        return out;
    }

    return Error("未知命令: " + cmd);
}

bool Engine::RenderAgentScreenshot(const std::string& path) {
    m_renderer.Debug().Begin();
    m_world.BuildRenderScene(m_scene, m_editorCamera);
    // 无头截图复用视口渲染的场景准备：2D 关卡必须用与运行时一致的 Camera2D。
    m_scene.is2D = m_is2D;
    if (m_is2D) {
        Camera2D camera2D;
        camera2D.halfHeight = 6.4f;
        if (GameObject* player = m_world.FindByName("玩家")) {
            const Vec3 position = player->Position();
            camera2D.position = Vec2(Clamp(position.x, -20.0f, 20.0f), Clamp(position.y, -8.0f, 8.0f));
        }
        m_scene.camera2D = camera2D;
    }
    // 与 RenderViewportScene / MainLoop 的视口路径保持一致：蒙皮与粒子在 BuildRenderScene 之后收集。
    game::CollectSkinnedDraws(m_world, m_scene);
    CollectParticles(m_world, m_scene);
    m_renderer.Render(m_editorCamera, m_scene, m_time, false);
    CaptureScreenshot(path);
    return FileSystem::Exists(path);
}

JsonValue Engine::ValidateWorld() {
    JsonValue result = JsonValue::MakeObject();
    JsonValue errors = JsonValue::MakeArray();
    u32 objectCount = 0;
    u32 componentCount = 0;

    for (GameObject* root : m_world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            ++objectCount;
            for (const auto& component : object->Components()) {
                ++componentCount;
                auto* renderer = dynamic_cast<MeshRenderer*>(component.get());
                if (!renderer) continue;
                if (!renderer->mesh.empty() && !m_assets.GetMesh(renderer->mesh)) {
                    // 允许路径式网格
                    if (renderer->mesh.find('/') == std::string::npos) {
                        errors.Push(JsonValue("对象 " + object->Name() + " 的网格不存在: " + renderer->mesh));
                    }
                }
                if (!renderer->texture.empty() && renderer->texture.find('/') == std::string::npos &&
                    !m_assets.GetTexture(renderer->texture)) {
                    errors.Push(JsonValue("对象 " + object->Name() + " 的贴图不存在: " + renderer->texture));
                }
            }
        });
    }

    result.Set("ok", JsonValue(errors.Size() == 0));
    result.Set("objects", JsonValue(static_cast<unsigned>(objectCount)));
    result.Set("components", JsonValue(static_cast<unsigned>(componentCount)));
    result.Set("errors", errors);
    return result;
}

bool Engine::RunAgentScript(const std::string& path, const std::string& outputPath) {
    std::string text;
    if (!FileSystem::ReadTextFile(path, text)) {
        LogError("[AI] 读不到脚本: %s", path.c_str());
        return false;
    }
    std::string parseError;
    const JsonValue root = JsonValue::Parse(text, &parseError);
    if (!root.IsObject() || !root.Has("commands")) {
        LogError("[AI] 脚本格式错误: %s", parseError.c_str());
        return false;
    }

    JsonValue report = JsonValue::MakeObject();
    report.Set("script", JsonValue(path));
    JsonValue results = JsonValue::MakeArray();
    int okCount = 0;
    for (const JsonValue& command : root["commands"].Elements()) {
        JsonValue result = ExecuteAgentCommand(command);
        JsonValue entry = JsonValue::MakeObject();
        entry.Set("cmd", JsonValue(command["cmd"].AsString()));
        entry.Set("result", result);
        results.Push(entry);
        if (result["ok"].AsBool()) ++okCount;
        LogInfo("[AI] %s -> ok=%d", command["cmd"].AsString().c_str(), result["ok"].AsBool() ? 1 : 0);
    }
    report.Set("ok", JsonValue(okCount == static_cast<int>(root["commands"].Size())));
    report.Set("total", JsonValue(static_cast<int>(root["commands"].Size())));
    report.Set("succeeded", JsonValue(okCount));
    report.Set("results", results);

    const std::string json = report.Dump(2);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath, std::ios::binary);
        out << json;
        LogInfo("[AI] 结果写入 %s", outputPath.c_str());
    }
    LogInfo("[AI] 脚本完成：%d/%d 条命令成功", okCount, static_cast<int>(root["commands"].Size()));
    return okCount == static_cast<int>(root["commands"].Size());
}

} // namespace erm
