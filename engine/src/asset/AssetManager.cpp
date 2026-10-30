// ErMao Engine - 资源管理实现
#include "asset/AssetManager.h"
#include "asset/ObjLoader.h"
#include "core/FileSystem.h"
#include "core/Log.h"

#include <windows.h>

#include <algorithm>

namespace erm {

namespace {
// 细网格线纹理（比纯棋盘更适合做地面，不容易产生视觉噪点）
void GridTexture(std::vector<u8>& rgba, u32 size, u32 cell, const Vec3& base, const Vec3& line) {
    rgba.resize(static_cast<size_t>(size) * size * 4);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const bool onLine = (x % cell < 2) || (y % cell < 2);
            const Vec3 c = onLine ? line : base;
            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            rgba[i + 0] = static_cast<u8>(Saturate(c.x) * 255.0f);
            rgba[i + 1] = static_cast<u8>(Saturate(c.y) * 255.0f);
            rgba[i + 2] = static_cast<u8>(Saturate(c.z) * 255.0f);
            rgba[i + 3] = 255;
        }
    }
}
} // namespace

bool AssetManager::Init(ID3D11Device* device) {
    m_device = device;
    for (const char* name : {"cube", "sphere", "plane", "torus", "cylinder", "capsule"}) {
        GetMesh(name);
    }
    for (const char* name : {"white", "checker", "grid", "noise"}) {
        GetTexture(name);
    }
    LogInfo("资源管理器就绪：%zu 个网格 / %zu 张贴图", m_meshes.size(), m_textures.size());
    return true;
}

void AssetManager::Shutdown() {
    m_registry.clear();
    m_meshes.clear();
    m_textures.clear();
}

AssetManager::AssetInfo& AssetManager::Register(const std::string& name, AssetType type,
                                                const std::string& sourcePath, bool builtin) {
    for (auto& info : m_registry) {
        if (info.name == name && info.type == type) {
            info.sourcePath = sourcePath;
            info.builtin = builtin;
            return info;
        }
    }
    AssetInfo info;
    info.name = name;
    info.sourcePath = sourcePath;
    info.type = type;
    info.builtin = builtin;
    m_registry.push_back(info);
    return m_registry.back();
}

AssetManager::AssetInfo* AssetManager::FindAsset(const std::string& name) {
    for (auto& info : m_registry) {
        if (info.name == name) return &info;
    }
    return nullptr;
}

Mesh* AssetManager::GetMesh(const std::string& name) {
    auto it = m_meshes.find(name);
    if (it != m_meshes.end()) return it->second.get();

    Mesh* mesh = nullptr;
    if (name == "cube" || name == "sphere" || name == "plane" || name == "torus" || name == "cylinder" ||
        name == "capsule") {
        mesh = CreatePrimitive(name);
    } else {
        const std::string path = FileSystem::AssetPath(name);
        if (!path.empty()) {
            MeshData data;
            std::string error;
            if (LoadObjMesh(path, data, &error)) {
                mesh = CreateMesh(name, data);
                if (mesh) {
                    AssetInfo& info = Register(name, AssetType::Mesh, path, false);
                    info.vertexCount = mesh->CPU().VertexCount();
                    info.indexCount = mesh->IndexCount();
                }
            } else {
                LogError("OBJ 加载失败: %s", error.c_str());
            }
        } else {
            LogWarn("找不到网格资源: %s（回退到 cube）", name.c_str());
            return GetMesh("cube");
        }
    }
    return mesh;
}

Mesh* AssetManager::CreateMesh(const std::string& key, const MeshData& data) {
    auto mesh = MakeScope<Mesh>();
    if (!mesh->Create(m_device, data)) return nullptr;
    Mesh* raw = mesh.get();
    m_meshes[key] = std::move(mesh);
    return raw;
}

Mesh* AssetManager::CreatePrimitive(const std::string& name) {
    MeshData data;
    if (name == "cube") data = primitives::Cube(1.0f);
    else if (name == "sphere") data = primitives::Sphere(0.5f, 40, 24);
    else if (name == "plane") data = primitives::Plane(1.0f, 1.0f);
    else if (name == "torus") data = primitives::Torus(0.7f, 0.24f, 40, 20);
    else if (name == "cylinder") data = primitives::Cylinder(0.5f, 1.0f, 32);
    else if (name == "capsule") data = primitives::Capsule(0.4f, 1.2f, 20, 12);
    else return nullptr;

    Mesh* mesh = CreateMesh(name, data);
    if (mesh) {
        AssetInfo& info = Register(name, AssetType::Mesh, "", true);
        info.vertexCount = mesh->CPU().VertexCount();
        info.indexCount = mesh->IndexCount();
    }
    return mesh;
}

Texture* AssetManager::GetTexture(const std::string& name) {
    if (name.empty()) return GetTexture("white");
    auto it = m_textures.find(name);
    if (it != m_textures.end()) return it->second.get();

    if (name == "white" || name == "checker" || name == "grid" || name == "noise") {
        return CreateProceduralTexture(name);
    }

    const std::string path = FileSystem::AssetPath(name);
    if (path.empty()) {
        LogWarn("找不到贴图资源: %s（回退到 white）", name.c_str());
        return GetTexture("white");
    }
    auto tex = MakeScope<Texture>();
    if (!tex->CreateFromFile(m_device, path, true)) return GetTexture("white");
    Texture* raw = tex.get();
    m_textures[name] = std::move(tex);
    Register(name, AssetType::Texture, path, false);
    return raw;
}

Texture* AssetManager::CreateProceduralTexture(const std::string& name) {
    std::vector<u8> pixels;
    u32 size = 256;
    if (name == "white") {
        size = 4;
        pixels.assign(static_cast<size_t>(size) * size * 4, 255);
    } else if (name == "checker") {
        procedural::Checkerboard(pixels, 512, 64, Vec3(0.40f, 0.41f, 0.43f), Vec3(0.30f, 0.31f, 0.33f));
        size = 512;
    } else if (name == "grid") {
        GridTexture(pixels, 512, 64, Vec3(0.44f, 0.45f, 0.47f), Vec3(0.58f, 0.60f, 0.64f));
        size = 512;
    } else if (name == "noise") {
        procedural::Noise(pixels, 512, 11, Vec3(0.55f, 0.5f, 0.45f), 0.35f);
        size = 512;
    }

    auto tex = MakeScope<Texture>();
    if (!tex->CreateFromRGBA(m_device, size, size, pixels.data(), true)) return nullptr;
    Texture* raw = tex.get();
    m_textures[name] = std::move(tex);
    Register(name, AssetType::Texture, "", true);
    return raw;
}

Mesh* AssetManager::ImportMesh(const std::string& sourcePath, std::string* error) {
    const std::string fileName = FileSystem::FileNameOf(sourcePath);
    if (FileSystem::ExtensionOf(fileName) != "obj") {
        if (error) *error = "只支持 .obj 模型";
        return nullptr;
    }
    const std::string destination = FileSystem::Join(FileSystem::Join(FileSystem::WritableAssetDir(), "models"), fileName);
    if (!FileSystem::CopyFileTo(sourcePath, destination)) {
        if (error) *error = "复制模型到 assets/models 失败";
        return nullptr;
    }

    const std::string assetName = "models/" + fileName;
    // 若同名资源已缓存（重复导入），先丢弃缓存以便重新加载
    m_meshes.erase(assetName);

    Mesh* mesh = GetMesh(assetName);
    if (!mesh) {
        if (error) *error = "解析 OBJ 失败";
        return nullptr;
    }
    AssetInfo& info = Register(assetName, AssetType::Mesh, destination, false);
    info.vertexCount = mesh->CPU().VertexCount();
    info.indexCount = mesh->IndexCount();
    LogInfo("导入模型成功: %s（%u 顶点 / %u 三角形）", assetName.c_str(), info.vertexCount,
            info.indexCount / 3);
    return mesh;
}

Texture* AssetManager::ImportTexture(const std::string& sourcePath, std::string* error) {
    const std::string fileName = FileSystem::FileNameOf(sourcePath);
    const std::string ext = FileSystem::ExtensionOf(fileName);
    if (ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "bmp" && ext != "tga") {
        if (error) *error = "不支持的贴图格式（支持 png/jpg/bmp）";
        return nullptr;
    }
    const std::string destination =
        FileSystem::Join(FileSystem::Join(FileSystem::WritableAssetDir(), "textures"), fileName);
    if (!FileSystem::CopyFileTo(sourcePath, destination)) {
        if (error) *error = "复制贴图到 assets/textures 失败";
        return nullptr;
    }

    const std::string assetName = "textures/" + fileName;
    m_textures.erase(assetName);
    Texture* texture = GetTexture(assetName);
    if (!texture) {
        if (error) *error = "解码贴图失败";
        return nullptr;
    }
    Register(assetName, AssetType::Texture, destination, false);
    LogInfo("导入贴图成功: %s (%ux%u)", assetName.c_str(), texture->Width(), texture->Height());
    return texture;
}

void AssetManager::RegisterSceneAsset(const std::string& path) {
    const std::string name = FileSystem::FileNameOf(path);
    for (auto& info : m_registry) {
        if (info.type == AssetType::Scene && info.sourcePath == path) return;
    }
    Register(name, AssetType::Scene, path, false);
}

void AssetManager::Refresh() {
    // 扫描 exe 旁 assets/scenes，把场景文件注册进资源列表
    const std::string scenesDir = FileSystem::Join(FileSystem::WritableAssetDir(), "scenes");
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = FileSystem::Utf8ToWide(FileSystem::Join(scenesDir, "*.json"));
    HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) return;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        RegisterSceneAsset(FileSystem::Join(scenesDir, FileSystem::WideToUtf8(data.cFileName)));
    } while (FindNextFileW(handle, &data));
    FindClose(handle);
}

} // namespace erm
