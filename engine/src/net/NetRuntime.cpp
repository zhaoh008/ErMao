// ErMao Engine - 引擎级网络运行时实现
#include "net/NetRuntime.h"

#include "core/Log.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <cmath>
#include <cstdio>

namespace erm::net {

NetRuntime::NetRuntime() = default;
NetRuntime::~NetRuntime() = default;

bool NetRuntime::StartServer(World& world, u16 port, NetGameHooks hooks) {
    Stop(&world);
    m_hooks = std::move(hooks);
    if (!m_hooks.isReplicated) {
        m_lastError = "启动主机缺少 isReplicated 回调";
        LogError("[网络] %s", m_lastError.c_str());
        return false;
    }
    if (!m_transport.StartServer(port)) {
        m_lastError = m_transport.LastError();
        return false;
    }
    m_server.Reset();
    m_server.EnsureTypes();
    m_mode = Mode::Server;
    m_serverStarted = true;
    m_startTimeMs = NowMs();
    m_snapshotTimer = 0.0f;
    m_address.clear();
    LogInfo("[网络] 主机已启动（端口 %u）", m_transport.BoundPort());
    return true;
}

bool NetRuntime::StartClient(World& world, const std::string& address, NetGameHooks hooks) {
    Stop(&world);
    m_hooks = std::move(hooks);
    m_address = address;
    if (m_hooks.prepareClientWorld) m_hooks.prepareClientWorld(world);
    m_client.Reset(nullptr);
    std::string host;
    u16         port = kDefaultPort;
    if (!SplitHostPort(address, host, port)) {
        m_lastError = "连接地址格式不正确（示例 127.0.0.1:7777）";
        LogError("[网络] %s", m_lastError.c_str());
        return false;
    }
    if (!m_transport.StartClient(host, port)) {
        m_lastError = m_transport.LastError();
        return false;
    }
    m_mode = Mode::Client;
    m_clientStarted = true;
    m_clientConnected = false;
    m_clientReady = false;
    m_localNetId = 0;
    m_localNotified = false;
    m_lastLocalObject = nullptr;
    m_startTimeMs = NowMs();
    return true;
}

void NetRuntime::Stop(World* world) {
    if (m_mode == Mode::Client) {
        if (m_lastLocalObject && m_hooks.onLocalEntity) m_hooks.onLocalEntity(nullptr);
        m_client.Reset(world);
    }
    m_transport.Stop();
    m_peers.clear();
    m_mode = Mode::Off;
    m_serverStarted = false;
    m_clientStarted = false;
    m_clientConnected = false;
    m_clientReady = false;
    m_forceFullSnapshot = false;
    m_localNotified = false;
    m_localNetId = 0;
    m_lastLocalObject = nullptr;
    m_serverTick = 0;
    m_inputSequence = 0;
    m_snapshotTimer = 0.0f;
    m_inputTimer = 0.0f;
}

void NetRuntime::HandleEvents(World& world) {
    std::vector<Transport::Event> events;
    m_transport.Poll(events);
    for (Transport::Event& event : events) {
        if (m_mode == Mode::Server) {
            if (event.type == Transport::EventType::Connected) {
                PeerState state;
                state.peer = event.peer;
                m_peers[event.peer] = state;
                m_forceFullSnapshot = true;
                if (m_hooks.onPeerConnected) m_hooks.onPeerConnected(event.peer);
            } else if (event.type == Transport::EventType::Disconnected) {
                if (m_hooks.onPeerDisconnected) m_hooks.onPeerDisconnected(event.peer);
                m_peers.erase(event.peer);
            } else if (event.type == Transport::EventType::Packet) {
                HandleServerPacket(world, event.peer, event.data);
            }
        } else if (m_mode == Mode::Client) {
            if (event.type == Transport::EventType::Connected) {
                m_clientConnected = true;
                BitWriter hello;
                hello.U8(kMsgClientHello);
                hello.U32(kProtocolVersion);
                hello.String("ErMao");
                m_transport.Send(event.peer, hello.Data().data(), hello.Size(), true);
                LogInfo("[网络] 已连接到服务端，等待 Welcome 与实体快照");
            } else if (event.type == Transport::EventType::Disconnected) {
                m_clientConnected = false;
                m_clientReady = false;
                if (m_lastLocalObject && m_hooks.onLocalEntity) m_hooks.onLocalEntity(nullptr);
                m_localNotified = false;
                m_lastLocalObject = nullptr;
                LogWarn("[网络] 与服务端断开");
            } else if (event.type == Transport::EventType::Packet) {
                HandleClientPacket(world, event.peer, event.data);
            }
        }
    }
}

void NetRuntime::HandleServerPacket(World& /*world*/, PeerId peer, const std::vector<u8>& data) {
    if (data.empty()) return;
    BitReader reader(data.data() + 1, data.size() - 1);
    auto      it = m_peers.find(peer);
    if (it == m_peers.end()) return;
    switch (data[0]) {
        case kMsgClientHello: {
            const u32 version = reader.U32();
            const std::string name = reader.String();
            if (!reader.Ok()) return;
            if (version != kProtocolVersion) {
                m_lastError = "协议版本不一致";
                LogError("[网络] peer %u 协议版本 %u，服务端 %u，拒绝同步", peer, version, kProtocolVersion);
                return;
            }
            BitWriter welcome;
            welcome.U8(kMsgWelcome);
            welcome.U32(kProtocolVersion);
            welcome.U32(kSnapshotsPerSecond);
            welcome.U32(kInputsPerSecond);
            m_transport.Send(peer, welcome.Data().data(), welcome.Size(), true);
            SendTypeTable(peer);
            it->second.ready = true;
            m_forceFullSnapshot = true;
            LogInfo("[网络] 客户端 '%s'（peer %u）握手完成", name.c_str(), peer);
            break;
        }
        case kMsgInput: {
            NetInput input;
            input.sequence = reader.U32();
            input.moveX = reader.F32();
            input.moveY = reader.F32();
            input.yaw = reader.F32();
            input.buttons = reader.U32();
            if (!reader.Ok()) return;
            if (!std::isfinite(input.moveX) || !std::isfinite(input.moveY) || !std::isfinite(input.yaw)) return;
            const float length = std::sqrt(input.moveX * input.moveX + input.moveY * input.moveY);
            if (length > 1.0f) {
                input.moveX /= length;
                input.moveY /= length;
            }
            it->second.input = input;
            it->second.ack = input.sequence;
            it->second.lastInputMs = NowMs();
            break;
        }
        default: break;
    }
}

void NetRuntime::HandleClientPacket(World& world, PeerId /*peer*/, const std::vector<u8>& data) {
    if (data.empty()) return;
    BitReader reader(data.data() + 1, data.size() - 1);
    switch (data[0]) {
        case kMsgWelcome: {
            const u32 version = reader.U32();
            (void)reader.U32(); // snapshot rate
            (void)reader.U32(); // input rate
            if (!reader.Ok()) return;
            if (version != kProtocolVersion) {
                m_lastError = "协议版本不一致";
                LogError("[网络] 服务端协议版本 %u，客户端 %u", version, kProtocolVersion);
                return;
            }
            m_clientReady = true;
            LogInfo("[网络] 握手完成，等待快照");
            break;
        }
        case kMsgTypeTable: {
            if (!m_client.ReadTypeTable(reader)) {
                LogWarn("[网络] 类型表存在本端不认识的组件（对应实体可能缺组件）");
            }
            break;
        }
        case kMsgOwnEntity: {
            m_localNetId = reader.U32();
            if (!reader.Ok()) return;
            m_client.SetLocalEntity(m_localNetId);
            m_localNotified = false;
            m_lastLocalObject = nullptr;
            LogInfo("[网络] 本地玩家实体 netId=%u", m_localNetId);
            break;
        }
        case kMsgSnapshot: {
            if (m_client.Types().Empty()) {
                LogWarn("[网络] 在类型表之前收到快照，已忽略");
                return;
            }
            if (!m_client.ApplySnapshot(world, reader, NowMs())) {
                LogWarn("[网络] 快照解析失败（已丢弃该帧）");
                return;
            }
            if (m_hooks.onSnapshotApplied) m_hooks.onSnapshotApplied(world);
            NotifyLocalEntity(world);
            break;
        }
        default: break;
    }
}

void NetRuntime::SendTypeTable(PeerId peer) {
    const auto& names = m_server.Types().Names();
    BitWriter writer;
    writer.U8(kMsgTypeTable);
    writer.U16(static_cast<u16>(names.size()));
    for (const std::string& name : names) writer.String(name);
    m_transport.Send(peer, writer.Data().data(), writer.Size(), true);
}

void NetRuntime::ApplyServerInputs(World& world, float dt) {
    if (!m_hooks.applyServerInput) return;
    const u64 now = NowMs();
    for (auto& [peerId, state] : m_peers) {
        if (!state.ready) continue;
        GameObject* object = nullptr;
        NetId       netId = 0;
        if (!m_server.FindOwnedEntity(world, peerId, object, netId) || !object) continue;
        NetInput input = state.input;
        // 500ms 没有新输入就当作松开按键，避免客户端卡住/掉线后角色一直向前跑。
        if (state.lastInputMs == 0 || now - state.lastInputMs > 500) {
            input.moveX = 0.0f;
            input.moveY = 0.0f;
            input.buttons = 0;
        }
        m_hooks.applyServerInput(*object, input, dt);
    }
}

void NetRuntime::SendSnapshot(World& world, bool full) {
    m_server.Refresh(world, m_hooks.isReplicated);
    const u64 serverTimeMs = NowMs() - m_startTimeMs;
    for (auto& [peerId, state] : m_peers) {
        if (!state.ready) continue;
        BitWriter writer;
        writer.U8(kMsgSnapshot);
        m_server.WriteSnapshot(writer, full, m_serverTick, state.ack, serverTimeMs);
        if (!m_transport.Send(peerId, writer.Data().data(), writer.Size(), true)) {
            LogWarn("[网络] 向 peer %u 发送快照失败（%zu 字节）", peerId, writer.Size());
        }
    }
    m_transport.Flush();
    ++m_serverTick;
}

void NetRuntime::SendClientInput() {
    if (!m_clientConnected) return;
    m_inputSequence = m_inputSequence + 1;
    // 输入由 PreFrame 的 localInput 参数携带，这里在客户端侧保存一份供下一帧使用。
    BitWriter writer;
    writer.U8(kMsgInput);
    writer.U32(m_inputSequence);
    writer.F32(m_pendingInput.moveX);
    writer.F32(m_pendingInput.moveY);
    writer.F32(m_pendingInput.yaw);
    writer.U32(m_pendingInput.buttons);
    m_transport.Send(1, writer.Data().data(), writer.Size(), true);
}

void NetRuntime::PredictClient(World& world, float dt, const NetInput& input) {
    if (m_localNetId == 0 || !m_hooks.predictLocal) return;
    GameObject* object = m_client.FindObject(world, m_localNetId);
    if (!object) return;

    // 轻量校正：不重放未确认输入，只在服务器权威位置与本地预测相差过大时拉回。
    NetTransform authoritative;
    if (m_client.GetLatestTransform(m_localNetId, authoritative)) {
        const float error = Length(authoritative.position - object->Position());
        if (error > 1.5f) {
            object->SetPosition(authoritative.position);
            object->SetRotation(authoritative.rotation);
        }
    }
    m_hooks.predictLocal(*object, input, dt);
}

void NetRuntime::NotifyLocalEntity(World& world) {
    if (m_localNetId == 0) return;
    GameObject* object = m_client.FindObject(world, m_localNetId);
    if (!object) {
        m_localNotified = false;
        m_lastLocalObject = nullptr;
        return;
    }
    // 全量快照会销毁并重建实体；指针变了就重新通知玩法层，否则 HUD/相机会指向旧对象。
    if (object == m_lastLocalObject && m_localNotified) return;
    m_lastLocalObject = object;
    m_localNotified = true;
    if (m_hooks.onLocalEntity) m_hooks.onLocalEntity(object);
    LogInfo("[网络] 本地玩家实体已落地：%s", object->Name().c_str());
}

void NetRuntime::PreFrame(World& world, float dt, const NetInput& localInput) {
    HandleEvents(world);
    if (m_mode == Mode::Server) {
        // 先给每个客户端发“你控制哪个实体”，再应用输入。
        for (auto& [peerId, state] : m_peers) {
            if (!state.ready) continue;
            GameObject* object = nullptr;
            NetId       netId = 0;
            if (m_server.FindOwnedEntity(world, peerId, object, netId) && object && netId != state.ownNetId) {
                state.ownNetId = netId;
                BitWriter writer;
                writer.U8(kMsgOwnEntity);
                writer.U32(netId);
                m_transport.Send(peerId, writer.Data().data(), writer.Size(), true);
            }
        }
        ApplyServerInputs(world, dt);
        m_transport.Flush();
    } else if (m_mode == Mode::Client) {
        m_pendingInput = localInput;
        if (m_clientReady) {
            PredictClient(world, dt, localInput);
            m_inputTimer += dt;
            const float interval = 1.0f / static_cast<float>(kInputsPerSecond);
            if (m_inputTimer >= interval) {
                m_inputTimer -= interval;
                if (m_inputTimer > 1.0f) m_inputTimer = 0.0f;
                SendClientInput();
            }
        }
        m_client.Interpolate(world, NowMs());
        NotifyLocalEntity(world);
        m_transport.Flush();
    }
}

void NetRuntime::PostFrame(World& world, float dt) {
    if (m_mode == Mode::Server) {
        m_snapshotTimer += dt;
        const float interval = 1.0f / static_cast<float>(kSnapshotsPerSecond);
        if (m_snapshotTimer >= interval) {
            m_snapshotTimer -= interval;
            if (m_snapshotTimer > 0.5f) m_snapshotTimer = 0.0f;
            const bool full = m_forceFullSnapshot;
            m_forceFullSnapshot = false;
            SendSnapshot(world, full);
        }
    }
}

bool NetRuntime::Connected() const {
    if (m_mode == Mode::Server) return m_serverStarted;
    if (m_mode == Mode::Client) return m_clientConnected && m_clientReady;
    return false;
}

u32 NetRuntime::RttMs() const {
    if (m_mode == Mode::Client && m_clientConnected) return m_transport.RttMs(1);
    return 0;
}

u32 NetRuntime::EntityCount() const {
    if (m_mode == Mode::Server) return static_cast<u32>(m_server.EntityCount());
    if (m_mode == Mode::Client) return static_cast<u32>(m_client.EntityCount());
    return 0;
}

GameObject* NetRuntime::FindLocalEntity(World& world) {
    if (m_mode != Mode::Client || m_localNetId == 0) return nullptr;
    return m_client.FindObject(world, m_localNetId);
}

u16 NetRuntime::Port() const { return m_transport.BoundPort(); }

std::string NetRuntime::StatusText() const {
    char buffer[256];
    if (m_mode == Mode::Server) {
        std::snprintf(buffer, sizeof(buffer), "主机 :%u · 客户端 %u · 同步实体 %u · 20Hz",
                      static_cast<unsigned>(m_transport.BoundPort()), PeerCount(), EntityCount());
    } else if (m_mode == Mode::Client) {
        std::snprintf(buffer, sizeof(buffer), "客户端 %s · RTT %ums · 插值 %.0fms · %s",
                      m_address.c_str(), RttMs(), static_cast<double>(kInterpolationDelayMs),
                      Connected() ? "已同步" : (m_clientConnected ? "握手..." : "连接中..."));
    } else {
        std::snprintf(buffer, sizeof(buffer), "未启用网络");
    }
    return buffer;
}

} // namespace erm::net
