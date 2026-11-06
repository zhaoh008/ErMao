// ErMao Engine - 通用世界快照复制（引擎级，不绑定 2D / 3D 玩法）
//
// 服务端把 World 里“标记为可同步”的 GameObject 通过反射字段做增量快照；
// 客户端用同一套反射重建/更新对象，并对 Transform 做插值。
//
// 复用的关键：
//   * 任何带反射 Describe 的组件都能同步，无需为 2D/3D 各写一份网络代码；
//   * 动态实体由游戏提供 isReplicated 谓词（例如“带 Survivors Enemy 组件的对象”），
//     不需要在每个生成点手写 MarkForReplication；
//   * Transform 单独走快照头（位置/四元数/缩放），字段走反射增量。
#pragma once

#include "core/Types.h"
#include "net/NetCommon.h"
#include "scene/Component.h"

#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace erm {

class World;
class GameObject;
struct TypeInfo;
struct FieldDesc;

namespace net {

// 客户端/服务端稳定的网络实体 id。挂在 GameObject 上；服务端扫描时自动分配。
class NetworkIdentityComponent : public Component {
public:
    ER_COMPONENT(NetworkIdentityComponent, "Network Identity")

    NetId  netId = 0;
    PeerId ownerPeer = kInvalidPeer; // 该实体由哪个玩家控制（仅服务端有效）
    bool   locallyPredicted = false; // 客户端：本地玩家，跳过插值、走预测
};

// 线上组件类型表：按名字排序，保证同一份构建的客户端/服务端索引一致。
// 握手时服务端把名字表发一次，客户端按名字解析成本地 TypeInfo，允许两端字段顺序一致。
class NetTypeTable {
public:
    void BuildServer();
    bool BuildFromNames(const std::vector<std::string>& names);
    const std::vector<std::string>& Names() const { return m_names; }
    const TypeInfo* Type(size_t index) const;
    int             IndexOf(const TypeInfo& info) const;
    size_t          Count() const { return m_names.size(); }
    bool            Empty() const { return m_names.empty(); }

    // 过滤规则：有 factory（客户端能重建）、不是 Network Identity、字段数 <= 64。
    static bool IsReplicatedType(const TypeInfo& info);

private:
    std::vector<std::string>             m_names;
    std::vector<const TypeInfo*>         m_types;
    std::unordered_map<std::string, int> m_indexByName;
};

// 服务端增量快照生成器。
class ReplicationServer {
public:
    void Reset();
    // 扫描世界、分配 netId、计算与上一帧的差异。每帧（或每个快照 tick）调用一次。
    void Refresh(World& world, const std::function<bool(GameObject&)>& isReplicated);
    // 为一个 peer 写一份快照。full=true 时把所有实体按 spawn 发一遍。
    void WriteSnapshot(BitWriter& writer, bool full, u32 tick, u32 ack, u64 serverTimeMs);
    // 有 peer 首次连接/重连时调用，下一次写快照强制全量。
    void RequestFullSnapshot() { m_forceFull = true; }
    bool ConsumeFullSnapshotRequest() {
        const bool value = m_forceFull;
        m_forceFull = false;
        return value;
    }
    bool FindOwnedEntity(World& world, PeerId peer, GameObject*& object, NetId& netId) const;
    size_t EntityCount() const { return m_states.size(); }
    void EnsureTypes() { if (!m_typesBuilt) { m_types.BuildServer(); m_typesBuilt = true; } }
    NetTypeTable& Types() { return m_types; }

private:
    struct ComponentState {
        int                              typeIndex = -1;
        std::vector<std::vector<u8>>     fieldBytes;
    };
    struct EntityState {
        NetId                 netId = 0;
        u64                   objectId = 0;
        u32                   parentNetId = 0;
        std::string           name;
        bool                  active = true;
        NetTransform          transform;
        std::vector<ComponentState> components;
        bool                  alive = true;
    };
    struct ComponentDelta {
        int                          typeIndex = -1;
        u64                          fieldMask = 0;
        std::vector<std::vector<u8>> fieldBytes; // 与 mask 对应的字段值
    };
    struct EntityDelta {
        NetId                       netId = 0;
        bool                        spawn = false;
        bool                        transformDirty = false;
        bool                        activeDirty = false;
        bool                        componentsDirty = false;
        std::vector<ComponentDelta> components;
    };

    void BuildDelta(World& world, GameObject& object, EntityState& state, EntityDelta& delta);
    void AddSpawnComponents(GameObject& object, EntityState& state, EntityDelta& delta);

    NetTypeTable                        m_types;
    std::unordered_map<NetId, EntityState> m_states;
    std::unordered_map<u64, NetId>      m_objectToNet;
    std::vector<EntityDelta>            m_deltas;
    std::vector<NetId>                  m_removed;
    NetId                               m_nextNetId = 1;
    bool                                m_forceFull = true;
    bool                                m_typesBuilt = false;
};

// 客户端快照应用器 + 插值缓冲。
class ReplicationClient {
public:
    // world 为空时只清内部映射（World 已经先被清掉/析构的场景）。
    void Reset(World* world);
    // 读取并应用快照；localNowMs 用于估算服务端时间。返回是否成功。
    bool ApplySnapshot(World& world, BitReader& reader, u64 localNowMs);
    // 每帧把非本地预测实体的 Transform 插值到“服务端时间 - 插值延迟”。
    void Interpolate(World& world, u64 localNowMs);
    bool ReadTypeTable(BitReader& reader);

    void SetLocalEntity(NetId netId) {
        m_localEntity = netId;
        auto it = m_entities.find(netId);
        if (it != m_entities.end()) it->second.locallyPredicted = true;
    }
    NetId LocalEntity() const { return m_localEntity; }
    bool HasSnapshot() const { return m_hasSnapshot; }
    size_t EntityCount() const { return m_entities.size(); }
    GameObject* FindObject(World& world, NetId netId) const;
    bool GetLatestTransform(NetId netId, NetTransform& out) const;
    const NetTypeTable& Types() const { return m_types; }
    u64 ServerTimeMs() const { return m_serverTimeMs; }
    float InterpolationDelayMs() const { return kInterpolationDelayMs; }

private:
    struct TransformSample {
        u64          serverTimeMs = 0;
        NetTransform transform;
    };
    struct ClientEntity {
        NetId                     netId = 0;
        GameObject*               object = nullptr;
        std::deque<TransformSample> samples;
        bool                      locallyPredicted = false;
    };

    void ApplySpawn(World& world, NetId netId, GameObject*& object, const std::string& name, u32 parentNetId,
                    const NetTransform& transform, bool active, u64 serverTimeMs);
    void ApplyComponents(World& world, GameObject& object, BitReader& reader);
    void UpdateSample(ClientEntity& entity, u64 serverTimeMs, const NetTransform& transform);
    void DestroyAll(World& world);

    NetTypeTable                       m_types;
    std::vector<std::string>           m_pendingTypeNames;
    std::unordered_map<NetId, ClientEntity> m_entities;
    std::unordered_map<u32, NetId>     m_pendingParents; // 子对象 id → 父 netId
    NetId                              m_localEntity = 0;
    u64                                m_serverTimeMs = 0;
    u64                                m_lastSnapshotLocalMs = 0;
    float                              m_clockOffsetMs = 0.0f;
    bool                               m_hasClockOffset = false;
    bool                               m_hasSnapshot = false;
};

// 组件反射字段的编解码（也供 NetRuntime 的握手/测试使用）
void WriteFieldValue(BitWriter& writer, const FieldDesc& field, void* component);
void ReadFieldValue(BitReader& reader, const FieldDesc& field, void* component);
void EncodeFieldValue(std::vector<u8>& out, const FieldDesc& field, void* component);
bool FieldMaskBit(size_t index, u64 mask);

// 注册网络组件（Network Identity），由 Engine 初始化调用。
void RegisterNetComponents();

} // namespace net
} // namespace erm
