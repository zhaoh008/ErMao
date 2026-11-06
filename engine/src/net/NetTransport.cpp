// ErMao Engine - ENet 传输层实现
#include "net/NetTransport.h"

#include "core/Log.h"

#include <enet/enet.h>

#include <unordered_map>

namespace erm::net {

namespace {

struct EnetRuntime {
    EnetRuntime() {
        if (enet_initialize() != 0) {
            LogError("[网络] ENet 初始化失败");
        }
    }
    ~EnetRuntime() { enet_deinitialize(); }
};

EnetRuntime& Runtime() {
    static EnetRuntime runtime;
    return runtime;
}

} // namespace

struct Transport::Impl {
    ENetHost*                             host = nullptr;
    bool                                  server = false;
    PeerId                                nextPeer = 1;
    std::unordered_map<PeerId, ENetPeer*> peers;
    std::unordered_map<ENetPeer*, PeerId> reverse;
};

Transport::Transport() : m_impl(MakeScope<Impl>()) { Runtime(); }

Transport::~Transport() { Stop(); }

bool Transport::StartServer(u16 port, u32 maxPeers) {
    Stop();
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    m_impl->host = enet_host_create(&address, maxPeers == 0 ? kMaxPeers : maxPeers, 2, 0, 0);
    if (!m_impl->host) {
        m_lastError = "创建 ENet 服务端失败（端口可能被占用）";
        LogError("[网络] %s（端口 %u）", m_lastError.c_str(), port);
        return false;
    }
    m_impl->server = true;
    m_impl->nextPeer = 1;
    ENetAddress bound{};
    if (enet_socket_get_address(m_impl->host->socket, &bound) == 0) m_impl->host->address = bound;
    LogInfo("[网络] 服务端监听 :%u（最多 %u 个客户端）", m_impl->host->address.port, maxPeers);
    return true;
}

bool Transport::StartClient(const std::string& hostName, u16 port) {
    Stop();
    ENetAddress address{};
    address.port = port;
    if (enet_address_set_host(&address, hostName.c_str()) != 0) {
        m_lastError = "无法解析主机名: " + hostName;
        LogError("[网络] %s", m_lastError.c_str());
        return false;
    }
    m_impl->host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!m_impl->host) {
        m_lastError = "创建 ENet 客户端失败";
        LogError("[网络] %s", m_lastError.c_str());
        return false;
    }
    m_impl->server = false;
    ENetPeer* peer = enet_host_connect(m_impl->host, &address, 2, 0);
    if (!peer) {
        m_lastError = "发起连接失败（没有可用 peer）";
        enet_host_destroy(m_impl->host);
        m_impl->host = nullptr;
        LogError("[网络] %s", m_lastError.c_str());
        return false;
    }
    LogInfo("[网络] 正在连接 %s:%u ...", hostName.c_str(), port);
    return true;
}

void Transport::Stop() {
    if (!m_impl || !m_impl->host) return;
    // 先礼貌断开（发 FIN），再销毁 host；服务端等待中的 peer 也一并清理。
    for (auto& [id, peer] : m_impl->peers) {
        if (peer) enet_peer_disconnect_now(peer, 0);
    }
    enet_host_flush(m_impl->host);
    enet_host_destroy(m_impl->host);
    m_impl->host = nullptr;
    m_impl->peers.clear();
    m_impl->reverse.clear();
    m_impl->server = false;
}

bool Transport::Running() const { return m_impl && m_impl->host != nullptr; }
bool Transport::IsServer() const { return m_impl && m_impl->server; }
bool Transport::IsClient() const { return m_impl && m_impl->host && !m_impl->server; }

void Transport::Poll(std::vector<Event>& out) {
    if (!Running()) return;
    ENetEvent event;
    while (enet_host_service(m_impl->host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                PeerId id = m_impl->server ? m_impl->nextPeer++ : 1;
                if (m_impl->server && id >= m_impl->nextPeer) m_impl->nextPeer = id + 1;
                m_impl->peers[id] = event.peer;
                m_impl->reverse[event.peer] = id;
                // 断线检测：3 s 无响应开始重传探测，30 s 判定掉线。
                enet_peer_timeout(event.peer, 32, 5000, 30000);
                Event connected;
                connected.type = EventType::Connected;
                connected.peer = id;
                out.push_back(std::move(connected));
                LogInfo("[网络] peer %u 已连接", id);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                auto it = m_impl->reverse.find(event.peer);
                if (it != m_impl->reverse.end()) {
                    const PeerId id = it->second;
                    m_impl->reverse.erase(it);
                    m_impl->peers.erase(id);
                    Event disconnected;
                    disconnected.type = EventType::Disconnected;
                    disconnected.peer = id;
                    out.push_back(std::move(disconnected));
                    LogInfo("[网络] peer %u 已断开", id);
                }
                event.peer = nullptr;
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                auto it = m_impl->reverse.find(event.peer);
                if (it != m_impl->reverse.end() && event.packet && event.packet->dataLength > 0) {
                    Event packet;
                    packet.type = EventType::Packet;
                    packet.peer = it->second;
                    packet.data.assign(event.packet->data, event.packet->data + event.packet->dataLength);
                    out.push_back(std::move(packet));
                }
                if (event.packet) enet_packet_destroy(event.packet);
                break;
            }
            default: break;
        }
    }
}

void Transport::Flush() {
    if (m_impl && m_impl->host) enet_host_flush(m_impl->host);
}

bool Transport::Send(PeerId peer, const void* data, size_t size, bool reliable) {
    if (!Running() || !data || size == 0) return false;
    auto it = m_impl->peers.find(peer);
    if (it == m_impl->peers.end() || !it->second) return false;
    ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet) return false;
    if (enet_peer_send(it->second, reliable ? 0 : 1, packet) != 0) {
        enet_packet_destroy(packet);
        return false;
    }
    return true;
}

u32 Transport::RttMs(PeerId peer) const {
    if (!Running()) return 0;
    auto it = m_impl->peers.find(peer);
    if (it == m_impl->peers.end() || !it->second) return 0;
    return static_cast<u32>(it->second->roundTripTime);
}

u32 Transport::ConnectedPeerCount() const {
    return m_impl ? static_cast<u32>(m_impl->peers.size()) : 0;
}

u16 Transport::BoundPort() const {
    if (!Running() || !m_impl->host) return 0;
    ENetAddress address{};
    if (enet_socket_get_address(m_impl->host->socket, &address) == 0) return address.port;
    return m_impl->host->address.port;
}

} // namespace erm::net
