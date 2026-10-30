// ErMao Engine - 资源管理（GAMES104 第 3/14 讲）
#pragma once

#include "core/Types.h"
#include "renderer/Mesh.h"
#include "rhi/Texture.h"

#include <unordered_map>

namespace erm {

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

    bool Init(ID3D11Device* device);
    void Shutdown();

    Mesh*    GetMesh(const std::string& name);
    Texture* GetTexture(const std::string& name);
    Mesh*    CreateMesh(const std::string& key, const MeshData& data);

    // 导入外部资源：复制到 exe 旁的 assets/{models,textures}，再加载并注册
    Mesh*    ImportMesh(const std::string& sourcePath, std::string* error);
    Texture* ImportTexture(const std::string& sourcePath, std::string* error);
    void     RegisterSceneAsset(const std::string& path);

    const std::vector<AssetInfo>& Assets() const { return m_registry; }
    void Refresh();

    size_t MeshCount() const { return m_meshes.size(); }
    size_t TextureCount() const { return m_textures.size(); }

private:
    Mesh*    CreatePrimitive(const std::string& name);
    Texture* CreateProceduralTexture(const std::string& name);
    AssetInfo& Register(const std::string& name, AssetType type, const std::string& sourcePath, bool builtin);
    AssetInfo* FindAsset(const std::string& name);

    ID3D11Device*                                   m_device = nullptr;
    std::unordered_map<std::string, Scope<Mesh>>    m_meshes;
    std::unordered_map<std::string, Scope<Texture>> m_textures;
    std::vector<AssetInfo>                          m_registry;
};

} // namespace erm
