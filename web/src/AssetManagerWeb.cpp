// ErMao Engine - Web 资源管理（贴图缓存；网格接口为空实现）
#include "asset/AssetManager.h"
#include "core/FileSystem.h"
#include "core/Log.h"

#include <algorithm>

namespace erm {

namespace {

void GridTexture(std::vector<u8>& rgba, u32 size, u32 cell, const Vec3& base, const Vec3& line) {
    rgba.resize(static_cast<size_t>(size) * size * 4);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const bool onLine = (x % cell < 2) || (y % cell < 2);
            const Vec3 color = onLine ? line : base;
            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            rgba[i + 0] = (u8)(Saturate(color.x) * 255.0f);
            rgba[i + 1] = (u8)(Saturate(color.y) * 255.0f);
            rgba[i + 2] = (u8)(Saturate(color.z) * 255.0f);
            rgba[i + 3] = 255;
        }
    }
}

} // namespace

bool AssetManager::Init() {
    m_textures.clear();
    m_registry.clear();
    return true;
}

void AssetManager::Shutdown() {
    m_textures.clear();
    m_registry.clear();
}

Mesh* AssetManager::GetMesh(const std::string&) { return nullptr; }
Mesh* AssetManager::GetMeshAsset(const std::string&) { return nullptr; }

Texture* AssetManager::GetTexture(const std::string& name) {
    if (name.empty()) return GetTexture("white");
    auto it = m_textures.find(name);
    if (it != m_textures.end()) return it->second.get();

    if (name == "white" || name == "checker" || name == "grid" || name == "noise") {
        return CreateProceduralTexture(name);
    }

    // 允许 GetTexture 直接接收 assets/ 下的相对路径
    const std::string path = FileSystem::AssetPath(name);
    if (!path.empty()) {
        auto texture = MakeScope<Texture>();
        if (texture->CreateFromFile(path, true)) {
            Texture* raw = texture.get();
            m_textures[name] = std::move(texture);
            Register(name, AssetType::Texture, path, false);
            return raw;
        }
    }

    LogWarn("[Web] 找不到贴图资源: %s（回退到 white）", name.c_str());
    return GetTexture("white");
}

Texture* AssetManager::GetTextureAsset(const std::string& relativePath) {
    if (relativePath.empty()) return GetTexture("white");
    auto it = m_textures.find(relativePath);
    if (it != m_textures.end()) return it->second.get();

    const std::string path = FileSystem::AssetPath(relativePath);
    if (path.empty()) {
        LogWarn("[Web] 找不到贴图资源: %s（回退到 white）", relativePath.c_str());
        return GetTexture("white");
    }

    auto texture = MakeScope<Texture>();
    if (!texture->CreateFromFile(path, true)) {
        LogError("[Web] 解码贴图失败: %s（回退到 white）", path.c_str());
        return GetTexture("white");
    }
    Texture* raw = texture.get();
    m_textures[relativePath] = std::move(texture);
    Register(relativePath, AssetType::Texture, path, false);
    return raw;
}

Texture* AssetManager::CreateTextureRGBA(const std::string& name, u32 width, u32 height, const u8* rgba) {
    auto texture = MakeScope<Texture>();
    if (!texture->CreateFromRGBA(width, height, rgba, true)) {
        LogError("[Web] 创建程序化贴图失败: %s", name.c_str());
        return nullptr;
    }
    Texture* raw = texture.get();
    m_textures[name] = std::move(texture);
    Register(name, AssetType::Texture, "", true);
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

    auto texture = MakeScope<Texture>();
    if (!texture->CreateFromRGBA(size, size, pixels.data(), true)) return nullptr;
    Texture* raw = texture.get();
    m_textures[name] = std::move(texture);
    Register(name, AssetType::Texture, "", true);
    return raw;
}

Mesh* AssetManager::ImportMesh(const std::string&, std::string* error) {
    if (error) *error = "Web 版暂不支持导入模型";
    return nullptr;
}

Texture* AssetManager::ImportTexture(const std::string&, std::string* error) {
    if (error) *error = "Web 版暂不支持导入外部贴图";
    return nullptr;
}

void AssetManager::RegisterSceneAsset(const std::string&) {}
void AssetManager::Refresh() {}
void AssetManager::RegisterLevels() {}

AssetManager::AssetInfo& AssetManager::Register(const std::string& name, AssetType type,
                                                const std::string& sourcePath, bool builtin) {
    for (auto& info : m_registry) {
        if (info.name == name) {
            info.sourcePath = sourcePath;
            info.builtin = builtin;
            info.type = type;
            return info;
        }
    }
    m_registry.push_back(AssetInfo{name, sourcePath, type, builtin, 0, 0});
    return m_registry.back();
}

AssetManager::AssetInfo* AssetManager::FindAsset(const std::string& name) {
    for (auto& info : m_registry) {
        if (info.name == name) return &info;
    }
    return nullptr;
}

} // namespace erm
