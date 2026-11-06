// ErMao Engine - 通用世界快照复制实现
#include "net/NetReplication.h"

#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace erm::net {

namespace {

Component* FindComponentByName(GameObject& object, const std::string& name) {
    for (const auto& component : object.Components()) {
        if (component && component->GetTypeInfo().name == name) return component.get();
    }
    return nullptr;
}

u64 AllFieldMask(const TypeInfo& info) {
    u64 mask = 0;
    for (size_t i = 0; i < info.fields.size(); ++i) {
        if (info.fields[i].hidden) continue;
        if (i >= 64) break;
        mask |= (u64(1) << i);
    }
    return mask;
}

int FieldCount(const TypeInfo& info) {
    int count = 0;
    for (size_t i = 0; i < info.fields.size() && i < 64; ++i) {
        if (!info.fields[i].hidden) ++count;
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------- 网络实体标记
void NetworkIdentityComponent::Describe(TypeInfo& /*type*/) {
    // 不提供 factory：这个组件只由网络层创建，不参与场景 JSON / 反射复制。
}

void RegisterNetComponents() { Reflection::Register<NetworkIdentityComponent>("Network Identity"); }

// ---------------------------------------------------------------- 类型表
bool NetTypeTable::IsReplicatedType(const TypeInfo& info) {
    if (!info.factory) return false;
    if (info.name == "Network Identity") return false;
    // 字段用 u64 掩码编码；超过 64 个公开字段的组件暂不支持（当前引擎没有）。
    if (FieldCount(info) > 64) return false;
    return true;
}

void NetTypeTable::BuildServer() {
    m_names.clear();
    m_types.clear();
    m_indexByName.clear();
    for (const TypeInfo* info : Reflection::All()) {
        if (info && IsReplicatedType(*info)) m_names.push_back(info->name);
    }
    std::sort(m_names.begin(), m_names.end());
    m_names.erase(std::unique(m_names.begin(), m_names.end()), m_names.end());
    for (size_t i = 0; i < m_names.size(); ++i) {
        m_types.push_back(Reflection::Find(m_names[i]));
        m_indexByName[m_names[i]] = static_cast<int>(i);
    }
    LogInfo("[网络] 可复制组件类型 %zu 种", m_names.size());
}

bool NetTypeTable::BuildFromNames(const std::vector<std::string>& names) {
    m_names = names;
    m_types.clear();
    m_indexByName.clear();
    bool ok = true;
    for (size_t i = 0; i < names.size(); ++i) {
        const TypeInfo* info = Reflection::Find(names[i]);
        if (!info) {
            LogWarn("[网络] 客户端缺少组件类型: %s（该组件将被跳过）", names[i].c_str());
            ok = false;
        }
        m_types.push_back(info);
        m_indexByName[names[i]] = static_cast<int>(i);
    }
    return ok;
}

const TypeInfo* NetTypeTable::Type(size_t index) const {
    return index < m_types.size() ? m_types[index] : nullptr;
}

int NetTypeTable::IndexOf(const TypeInfo& info) const {
    auto it = m_indexByName.find(info.name);
    return it == m_indexByName.end() ? -1 : it->second;
}

// ---------------------------------------------------------------- 字段编解码
void WriteFieldValue(BitWriter& writer, const FieldDesc& field, void* component) {
    void* ptr = field.access(component);
    switch (field.type) {
        case FieldType::Bool: writer.Bool(*static_cast<bool*>(ptr)); break;
        case FieldType::Int: writer.I32(*static_cast<int*>(ptr)); break;
        case FieldType::Float: writer.F32(*static_cast<float*>(ptr)); break;
        case FieldType::Vec2: writer.Vec2(*static_cast<Vec2*>(ptr)); break;
        case FieldType::Vec3: writer.Vec3(*static_cast<Vec3*>(ptr)); break;
        case FieldType::Vec4:
        case FieldType::Color: {
            const Vec4& value = *static_cast<Vec4*>(ptr);
            writer.F32(value.x);
            writer.F32(value.y);
            writer.F32(value.z);
            writer.F32(value.w);
            break;
        }
        case FieldType::String:
        case FieldType::MeshAsset:
        case FieldType::TextureAsset: writer.String(*static_cast<std::string*>(ptr)); break;
    }
}

void ReadFieldValue(BitReader& reader, const FieldDesc& field, void* component) {
    void* ptr = field.access(component);
    switch (field.type) {
        case FieldType::Bool: *static_cast<bool*>(ptr) = reader.Bool(); break;
        case FieldType::Int: *static_cast<int*>(ptr) = reader.I32(); break;
        case FieldType::Float: *static_cast<float*>(ptr) = reader.F32(); break;
        case FieldType::Vec2: *static_cast<Vec2*>(ptr) = reader.Vec2Value(); break;
        case FieldType::Vec3: *static_cast<Vec3*>(ptr) = reader.Vec3Value(); break;
        case FieldType::Vec4:
        case FieldType::Color: {
            Vec4& value = *static_cast<Vec4*>(ptr);
            value.x = reader.F32();
            value.y = reader.F32();
            value.z = reader.F32();
            value.w = reader.F32();
            break;
        }
        case FieldType::String:
        case FieldType::MeshAsset:
        case FieldType::TextureAsset: *static_cast<std::string*>(ptr) = reader.String(); break;
    }
}

void EncodeFieldValue(std::vector<u8>& out, const FieldDesc& field, void* component) {
    BitWriter writer;
    WriteFieldValue(writer, field, component);
    out = writer.Data();
}

bool FieldMaskBit(size_t index, u64 mask) { return index < 64 && ((mask >> index) & 1) != 0; }

// ---------------------------------------------------------------- 服务端快照
void ReplicationServer::Reset() {
    m_states.clear();
    m_objectToNet.clear();
    m_deltas.clear();
    m_removed.clear();
    m_nextNetId = 1;
    m_forceFull = true;
    m_typesBuilt = false;
}

void ReplicationServer::Refresh(World& world, const std::function<bool(GameObject&)>& isReplicated) {
    EnsureTypes();
    m_deltas.clear();
    m_removed.clear();
    if (!isReplicated) return;

    std::vector<GameObject*> objects;
    for (GameObject* root : world.RootObjects()) {
        if (!root) continue;
        root->ForEach([&](GameObject* object) {
            if (object && isReplicated(*object)) objects.push_back(object);
        });
    }

    // 第一遍：为所有动态实体分配 netId。父节点可能排在子节点后面，先分配再做快照。
    for (GameObject* object : objects) {
        auto* identity = object->GetComponent<NetworkIdentityComponent>();
        if (!identity) identity = object->AddComponent<NetworkIdentityComponent>();
        if (identity && identity->netId == 0) identity->netId = m_nextNetId++;
    }

    std::unordered_set<NetId> seen;
    for (GameObject* object : objects) {
        auto* identity = object->GetComponent<NetworkIdentityComponent>();
        if (!identity || identity->netId == 0) continue;
        const NetId netId = identity->netId;
        seen.insert(netId);

        auto it = m_states.find(netId);
        if (it == m_states.end()) {
            EntityState state;
            state.netId = netId;
            state.objectId = object->Id();
            state.name = object->Name();
            state.active = object->Active();
            state.transform.position = object->Position();
            state.transform.rotation = object->Rotation();
            state.transform.scale = object->Scale();
            if (GameObject* parent = object->Parent()) {
                if (auto* parentIdentity = parent->GetComponent<NetworkIdentityComponent>()) {
                    state.parentNetId = parentIdentity->netId;
                }
            }

            EntityDelta delta;
            delta.netId = netId;
            delta.spawn = true;
            delta.transformDirty = true;
            delta.activeDirty = true;
            AddSpawnComponents(*object, state, delta);
            m_states.emplace(netId, std::move(state));
            m_objectToNet[object->Id()] = netId;
            m_deltas.push_back(std::move(delta));
        } else {
            EntityDelta delta;
            delta.netId = netId;
            BuildDelta(world, *object, it->second, delta);
            if (delta.transformDirty || delta.activeDirty || delta.componentsDirty) {
                m_deltas.push_back(std::move(delta));
            }
        }
    }

    for (auto it = m_states.begin(); it != m_states.end();) {
        if (seen.find(it->first) == seen.end()) {
            m_removed.push_back(it->first);
            m_objectToNet.erase(it->second.objectId);
            it = m_states.erase(it);
        } else {
            ++it;
        }
    }
}

void ReplicationServer::AddSpawnComponents(GameObject& object, EntityState& state, EntityDelta& delta) {
    for (const auto& component : object.Components()) {
        if (!component) continue;
        const TypeInfo& info = component->GetTypeInfo();
        if (!NetTypeTable::IsReplicatedType(info)) continue;
        const int typeIndex = m_types.IndexOf(info);
        if (typeIndex < 0) continue;

        ComponentState stored;
        stored.typeIndex = typeIndex;
        ComponentDelta changed;
        changed.typeIndex = typeIndex;
        for (size_t i = 0; i < info.fields.size() && i < 64; ++i) {
            const FieldDesc& field = info.fields[i];
            if (field.hidden) continue;
            std::vector<u8> bytes;
            EncodeFieldValue(bytes, field, component.get());
            stored.fieldBytes.push_back(bytes);
            changed.fieldBytes.push_back(std::move(bytes));
            changed.fieldMask |= (u64(1) << i);
        }
        state.components.push_back(std::move(stored));
        delta.components.push_back(std::move(changed));
    }
    delta.componentsDirty = true;
}

void ReplicationServer::BuildDelta(World& /*world*/, GameObject& object, EntityState& state, EntityDelta& delta) {
    NetTransform transform;
    transform.position = object.Position();
    transform.rotation = object.Rotation();
    transform.scale = object.Scale();
    if (transform != state.transform) {
        state.transform = transform;
        delta.transformDirty = true;
    }
    if (object.Active() != state.active) {
        state.active = object.Active();
        delta.activeDirty = true;
    }

    for (const auto& component : object.Components()) {
        if (!component) continue;
        const TypeInfo& info = component->GetTypeInfo();
        if (!NetTypeTable::IsReplicatedType(info)) continue;
        const int typeIndex = m_types.IndexOf(info);
        if (typeIndex < 0) continue;

        ComponentState* stored = nullptr;
        for (ComponentState& candidate : state.components) {
            if (candidate.typeIndex == typeIndex) {
                stored = &candidate;
                break;
            }
        }

        if (!stored) {
            ComponentState fresh;
            fresh.typeIndex = typeIndex;
            ComponentDelta added;
            added.typeIndex = typeIndex;
            for (size_t i = 0; i < info.fields.size() && i < 64; ++i) {
                const FieldDesc& field = info.fields[i];
                if (field.hidden) continue;
                std::vector<u8> bytes;
                EncodeFieldValue(bytes, field, component.get());
                fresh.fieldBytes.push_back(bytes);
                added.fieldBytes.push_back(std::move(bytes));
                added.fieldMask |= (u64(1) << i);
            }
            state.components.push_back(std::move(fresh));
            delta.components.push_back(std::move(added));
            continue;
        }

        const size_t fieldCount = static_cast<size_t>(FieldCount(info));
        if (stored->fieldBytes.size() != fieldCount) stored->fieldBytes.resize(fieldCount);

        ComponentDelta changed;
        changed.typeIndex = typeIndex;
        size_t slot = 0;
        for (size_t i = 0; i < info.fields.size() && i < 64; ++i) {
            const FieldDesc& field = info.fields[i];
            if (field.hidden) continue;
            std::vector<u8> bytes;
            EncodeFieldValue(bytes, field, component.get());
            if (slot >= stored->fieldBytes.size()) stored->fieldBytes.resize(slot + 1);
            if (bytes != stored->fieldBytes[slot]) {
                stored->fieldBytes[slot] = bytes;
                changed.fieldBytes.push_back(std::move(bytes));
                changed.fieldMask |= (u64(1) << i);
            }
            ++slot;
        }
        if (changed.fieldMask != 0) delta.components.push_back(std::move(changed));
    }
    delta.componentsDirty = !delta.components.empty();
}

void ReplicationServer::WriteSnapshot(BitWriter& writer, bool full, u32 tick, u32 ack, u64 serverTimeMs) {
    writer.U32(tick);
    writer.U32(ack);
    writer.U64(serverTimeMs);
    writer.U8(full ? 1u : 0u);
    writer.U16(static_cast<u16>((std::min)(m_removed.size(), static_cast<size_t>(0xffff))));
    for (NetId netId : m_removed) writer.U32(netId);

    auto writeFields = [&writer](int typeIndex, u64 mask, const std::vector<std::vector<u8>>& values) {
        writer.U16(static_cast<u16>(typeIndex));
        writer.U64(mask);
        for (const auto& bytes : values) writer.Bytes(bytes.data(), bytes.size());
    };

    if (full) {
        writer.U16(static_cast<u16>((std::min)(m_states.size(), static_cast<size_t>(0xffff))));
        for (const auto& [netId, state] : m_states) {
            (void)netId;
            writer.U32(state.netId);
            writer.U8(1); // spawn
            writer.String(state.name);
            writer.U32(state.parentNetId);
            writer.Transform(state.transform);
            writer.Bool(state.active);
            writer.U16(static_cast<u16>(state.components.size()));
            for (const ComponentState& component : state.components) {
                const TypeInfo* info = m_types.Type(static_cast<size_t>(component.typeIndex));
                const u64 mask = info ? AllFieldMask(*info) : 0;
                writeFields(component.typeIndex, mask, component.fieldBytes);
            }
        }
        return;
    }

    size_t count = 0;
    for (const EntityDelta& delta : m_deltas) {
        if (delta.spawn || delta.transformDirty || delta.activeDirty || delta.componentsDirty) ++count;
    }
    writer.U16(static_cast<u16>((std::min)(count, static_cast<size_t>(0xffff))));
    for (const EntityDelta& delta : m_deltas) {
        if (!(delta.spawn || delta.transformDirty || delta.activeDirty || delta.componentsDirty)) continue;
        writer.U32(delta.netId);
        u8 flags = 0;
        if (delta.spawn) flags |= 1;
        if (delta.transformDirty) flags |= 2;
        if (delta.activeDirty) flags |= 4;
        if (delta.componentsDirty) flags |= 8;
        writer.U8(flags);

        if (delta.spawn) {
            // spawn 需要名字/父节点；从状态表取（delta 不带字符串，保证热路径轻量）。
            auto it = m_states.find(delta.netId);
            if (it == m_states.end()) continue;
            writer.String(it->second.name);
            writer.U32(it->second.parentNetId);
            writer.Transform(it->second.transform);
            writer.Bool(it->second.active);
        } else {
            if (delta.transformDirty) {
                auto it = m_states.find(delta.netId);
                if (it != m_states.end()) writer.Transform(it->second.transform);
            }
            if (delta.activeDirty) {
                auto it = m_states.find(delta.netId);
                if (it != m_states.end()) writer.Bool(it->second.active);
            }
        }

        if (flags & 8) {
            writer.U16(static_cast<u16>(delta.components.size()));
            for (const ComponentDelta& component : delta.components) {
                writeFields(component.typeIndex, component.fieldMask, component.fieldBytes);
            }
        }
    }
}

bool ReplicationServer::FindOwnedEntity(World& world, PeerId peer, GameObject*& object, NetId& netId) const {
    object = nullptr;
    netId = 0;
    if (peer == kInvalidPeer) return false;
    for (GameObject* root : world.RootObjects()) {
        if (!root) continue;
        bool found = false;
        root->ForEach([&](GameObject* candidate) {
            if (found || !candidate) return;
            auto* identity = candidate->GetComponent<NetworkIdentityComponent>();
            if (identity && identity->ownerPeer == peer && identity->netId != 0) {
                object = candidate;
                netId = identity->netId;
                found = true;
            }
        });
        if (found) return true;
    }
    return false;
}

// ---------------------------------------------------------------- 客户端快照
void ReplicationClient::Reset(World* world) {
    if (world) DestroyAll(*world);
    m_entities.clear();
    m_pendingParents.clear();
    m_types = NetTypeTable();
    m_serverTimeMs = 0;
    m_lastSnapshotLocalMs = 0;
    m_clockOffsetMs = 0.0f;
    m_hasClockOffset = false;
    m_hasSnapshot = false;
    m_localEntity = 0;
}

void ReplicationClient::DestroyAll(World& world) {
    for (auto& [netId, entity] : m_entities) {
        (void)netId;
        if (entity.object) world.DestroyObject(entity.object);
    }
    world.ProcessPendingDestruction();
    m_entities.clear();
}

bool ReplicationClient::ReadTypeTable(BitReader& reader) {
    const u16 count = reader.U16();
    if (!reader.Ok()) return false;
    std::vector<std::string> names;
    names.reserve(count);
    for (u16 i = 0; i < count; ++i) names.push_back(reader.String());
    if (!reader.Ok()) return false;
    return m_types.BuildFromNames(names);
}

GameObject* ReplicationClient::FindObject(World& /*world*/, NetId netId) const {
    auto it = m_entities.find(netId);
    return it == m_entities.end() ? nullptr : it->second.object;
}

bool ReplicationClient::GetLatestTransform(NetId netId, NetTransform& out) const {
    auto it = m_entities.find(netId);
    if (it == m_entities.end() || it->second.samples.empty()) return false;
    out = it->second.samples.back().transform;
    return true;
}

void ReplicationClient::ApplySpawn(World& world, NetId netId, GameObject*& object, const std::string& name,
                                   u32 parentNetId, const NetTransform& transform, bool active, u64 serverTimeMs) {
    object = world.CreateGameObject(name);
    if (!object) return;
    object->SetPosition(transform.position);
    object->SetRotation(transform.rotation);
    object->SetScale(transform.scale);
    object->SetActive(active);
    auto* identity = object->AddComponent<NetworkIdentityComponent>();
    if (identity) identity->netId = netId;
    ClientEntity entity;
    entity.netId = netId;
    entity.object = object;
    UpdateSample(entity, serverTimeMs, transform);
    m_entities[netId] = std::move(entity);
    if (parentNetId != 0) m_pendingParents[netId] = parentNetId;
}

void ReplicationClient::UpdateSample(ClientEntity& entity, u64 serverTimeMs, const NetTransform& transform) {
    entity.samples.push_back({serverTimeMs, transform});
    // 只保留最近 2 秒、最多 64 个样本；插值窗口通常只需要 2 个。
    while (entity.samples.size() > 64) entity.samples.pop_front();
    while (entity.samples.size() > 2 && entity.samples.front().serverTimeMs + 2000 < serverTimeMs) {
        entity.samples.pop_front();
    }
}

void ReplicationClient::ApplyComponents(World& /*world*/, GameObject& object, BitReader& reader) {
    const u16 count = reader.U16();
    if (!reader.Ok()) return;
    for (u16 c = 0; c < count; ++c) {
        const u16 typeIndex = reader.U16();
        const u64 mask = reader.U64();
        if (!reader.Ok()) return;
        const TypeInfo* info = m_types.Type(typeIndex);
        if (!info) {
            LogWarn("[网络] 快照包含未知组件类型 index=%u，后续数据无法跳过", typeIndex);
            reader = BitReader(nullptr, 0);
            return;
        }
        Component* component = object.AddComponentByTypeName(info->name);
        if (!component) {
            // 类型表里应有 factory；没有 factory 说明两端版本不一致，无法继续解析。
            reader = BitReader(nullptr, 0);
            return;
        }
        for (size_t i = 0; i < info->fields.size() && i < 64; ++i) {
            if (!FieldMaskBit(i, mask)) continue;
            ReadFieldValue(reader, info->fields[i], component);
        }
    }
}

bool ReplicationClient::ApplySnapshot(World& world, BitReader& reader, u64 localNowMs) {
    (void)reader.U32(); // tick
    (void)reader.U32(); // ack（客户端当前没有回滚重放，仅保留协议字段）
    const u64  serverTimeMs = reader.U64();
    const bool full = reader.U8() != 0;
    const u16  removedCount = reader.U16();
    if (!reader.Ok()) return false;

    std::vector<NetId> removed;
    removed.reserve(removedCount);
    for (u16 i = 0; i < removedCount; ++i) removed.push_back(reader.U32());
    if (!reader.Ok()) return false;

    if (full) DestroyAll(world);

    for (NetId netId : removed) {
        auto it = m_entities.find(netId);
        if (it == m_entities.end()) continue;
        if (it->second.object) world.DestroyObject(it->second.object);
        m_entities.erase(it);
    }
    world.ProcessPendingDestruction();

    const u16 entityCount = reader.U16();
    if (!reader.Ok()) return false;
    m_pendingParents.clear();
    for (u16 e = 0; e < entityCount; ++e) {
        const NetId netId = reader.U32();
        const u8    flags = reader.U8();
        if (!reader.Ok()) return false;

        if (flags & 1) {
            const std::string name = reader.String();
            const u32         parentNetId = reader.U32();
            const NetTransform transform = reader.Transform();
            const bool        active = reader.Bool();
            if (!reader.Ok()) return false;

            auto existing = m_entities.find(netId);
            if (existing != m_entities.end()) {
                if (existing->second.object) world.DestroyObject(existing->second.object);
                m_entities.erase(existing);
                world.ProcessPendingDestruction();
            }
            GameObject* object = nullptr;
            ApplySpawn(world, netId, object, name, parentNetId, transform, active, serverTimeMs);
            if (!object) {
                LogError("[网络] 无法创建快照实体 netId=%u", netId);
                return false;
            }
            ApplyComponents(world, *object, reader);
            object->Start();
            m_entities[netId].locallyPredicted = (netId == m_localEntity);
        } else {
            auto it = m_entities.find(netId);
            if (it == m_entities.end()) {
                LogWarn("[网络] 收到未知实体的增量更新 netId=%u（丢弃整帧快照）", netId);
                return false;
            }
            if (flags & 2) {
                const NetTransform transform = reader.Transform();
                UpdateSample(it->second, serverTimeMs, transform);
            }
            if (flags & 4) it->second.object->SetActive(reader.Bool());
            if (flags & 8) ApplyComponents(world, *it->second.object, reader);
            it->second.locallyPredicted = (netId == m_localEntity);
        }
        if (!reader.Ok()) return false;
    }

    for (const auto& [childNetId, parentNetId] : m_pendingParents) {
        auto child = m_entities.find(childNetId);
        auto parent = m_entities.find(parentNetId);
        if (child != m_entities.end() && parent != m_entities.end() && child->second.object && parent->second.object) {
            child->second.object->SetParent(parent->second.object);
        }
    }
    m_pendingParents.clear();

    m_serverTimeMs = serverTimeMs;
    m_lastSnapshotLocalMs = localNowMs;
    const float targetOffset = static_cast<float>(static_cast<double>(serverTimeMs) - static_cast<double>(localNowMs));
    if (!m_hasClockOffset) {
        m_clockOffsetMs = targetOffset;
        m_hasClockOffset = true;
    } else {
        m_clockOffsetMs += (targetOffset - m_clockOffsetMs) * 0.12f;
    }
    m_hasSnapshot = true;
    return true;
}

void ReplicationClient::Interpolate(World& /*world*/, u64 localNowMs) {
    if (!m_hasSnapshot) return;
    const double renderTime = static_cast<double>(localNowMs) + static_cast<double>(m_clockOffsetMs) -
                              static_cast<double>(kInterpolationDelayMs);
    for (auto& [netId, entity] : m_entities) {
        (void)netId;
        if (!entity.object || entity.locallyPredicted || entity.samples.empty()) continue;
        const auto& samples = entity.samples;
        NetTransform out;
        if (renderTime <= static_cast<double>(samples.front().serverTimeMs) || samples.size() == 1) {
            out = samples.front().transform;
        } else if (renderTime >= static_cast<double>(samples.back().serverTimeMs)) {
            const TransformSample& last = samples.back();
            if (samples.size() >= 2) {
                const TransformSample& prev = samples[samples.size() - 2];
                const double span = static_cast<double>(last.serverTimeMs) - static_cast<double>(prev.serverTimeMs);
                const double ahead = (std::min)(renderTime - static_cast<double>(last.serverTimeMs), 250.0);
                if (span > 1.0 && ahead > 0.0) {
                    const float scale = static_cast<float>(ahead / span);
                    out.position = last.transform.position + (last.transform.position - prev.transform.position) * scale;
                } else {
                    out.position = last.transform.position;
                }
            } else {
                out.position = last.transform.position;
            }
            out.rotation = last.transform.rotation;
            out.scale = last.transform.scale;
        } else {
            size_t index = 0;
            while (index + 1 < samples.size() &&
                   static_cast<double>(samples[index + 1].serverTimeMs) < renderTime) {
                ++index;
            }
            const TransformSample& a = samples[index];
            const TransformSample& b = samples[index + 1];
            const double span = static_cast<double>(b.serverTimeMs) - static_cast<double>(a.serverTimeMs);
            const float  t = span > 1.0
                                 ? static_cast<float>((renderTime - static_cast<double>(a.serverTimeMs)) / span)
                                 : 0.0f;
            out.position = Lerp(a.transform.position, b.transform.position, Clamp01(t));
            out.rotation = Quat::Slerp(a.transform.rotation, b.transform.rotation, Clamp01(t));
            out.scale = Lerp(a.transform.scale, b.transform.scale, Clamp01(t));
        }
        entity.object->SetPosition(out.position);
        entity.object->SetRotation(out.rotation);
        entity.object->SetScale(out.scale);
    }
}

} // namespace erm::net
