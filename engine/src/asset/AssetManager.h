// ErMao Engine - 资源管理（GAMES104 第 3/14 讲）
//
// 桌面端使用 D3D11 纹理/网格；Web 端（ERMAO_WEB）只保留贴图缓存，
// 网格接口作为空实现保留，保证共享的 scene/game 代码仍可编译。
#pragma once

#include "core/Types.h"
#include "rhi/Texture.h"

#ifndef ERMAO_WEB
#include "renderer/Mesh.h"
#endif

#include <unordered_map>

namespace erm {

class Mesh;

// 资源缓存 + 资产注册表（编辑器资源浏览器直接读它）
class AssetManager {
public:
    enum class AssetType { Mesh, Texture, Scene };

    struct AssetInfo {
        std::string name;       // 引擎内名字（内置名或 assets/ 下的相对路径）
        std::string sourcePath; // 磁盘绝对路径（内置资源为空）
        AssetType   type = AssetType::Mesh;
        bool        builtin = false;
        u32         vertexCount = 0;
        u32         indexCount = 0;
    };

#ifdef ERMAO_WEB
    bool Init();
#else
    bool Init(ID3D11Device* device);
#endif
    void Shutdown();

    Mesh*    GetMesh(const std::string& name);
    Texture* GetTexture(const std::string& name);
    // 2D：按 assets/ 下的相对路径懒加载贴图，例如 "sprites/soldier_walk.png"
    Texture* GetTextureAsset(const std::string& relativePath);
    // 2D：用 CPU 像素创建纹理（程序化图标）
    Texture* CreateTextureRGBA(const std::string& name, u32 width, u32 height, const u8* rgba);
    // 3D：按 assets/ 下的相对路径懒加载 OBJ，例如 "models/kenney/character-a.obj"
    Mesh* GetMeshAsset(const std::string& relativePath);
#ifndef ERMAO_WEB
    Mesh*    CreateMesh(const std::string& key, const MeshData& data);
#endif

    // 导入外部资源：复制到 exe 旁的 assets/{models,textures}，再加载并注册
    Mesh*    ImportMesh(const std::string& sourcePath, std::string* error);
    Texture* ImportTexture(const std::string& sourcePath, std::string* error);
    void     RegisterSceneAsset(const std::string& path);

    const std::vector<AssetInfo>& Assets() const { return m_registry; }
    void Refresh();
    // 内置关卡（"关卡 · 2D 幸存者" 等）会出现在资源浏览器的「场景」分类里
    void RegisterLevels();

#ifdef ERMAO_WEB
    size_t MeshCount() const { return 0; }
#else
    size_t MeshCount() const { return m_meshes.size(); }
#endif
    size_t TextureCount() const { return m_textures.size(); }

private:
#ifndef ERMAO_WEB
    Mesh*    CreatePrimitive(const std::string& name);
#endif
    Texture* CreateProceduralTexture(const std::string& name);
    AssetInfo& Register(const std::string& name, AssetType type, const std::string& sourcePath, bool builtin);
    AssetInfo* FindAsset(const std::string& name);

#ifndef ERMAO_WEB
    ID3D11Device*                                   m_device = nullptr;
    std::unordered_map<std::string, Scope<Mesh>>    m_meshes;
#endif
    std::unordered_map<std::string, Scope<Texture>> m_textures;
    std::vector<AssetInfo>                          m_registry;
};

} // namespace erm
