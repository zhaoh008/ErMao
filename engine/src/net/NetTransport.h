// ErMao Engine - ENet 传输层封装
//
// 只负责“把字节从一个 peer 送到另一个 peer”和连接生命周期事件；
// 快照、插值、预测等同步逻辑在 NetRuntime / NetReplication 里，不依赖 ENet。
//
// 为什么选 ENet（MIT，2002 年至今仍在维护）：
//   * 单文件级 C 实现，vendored 进 third_party/enet，不需要包管理器；
//   * 自带 UDP 上的可靠/有序/无序通道、ACK、重传、分片、连接管理；
//   * 跨 Windows / Linux / macOS，Windows 上只依赖 ws2_32 + winmm；
//   * 比 GameNetworkingSockets（需要 protobuf/OpenSSL/abseil）更适合教学引擎的
//     “一条命令构建”约束；比 yojimbo/netcode.io 更通用、更成熟。
#pragma once

#include "core/Types.h"
#include "net/NetCommon.h"

#include <string>
#include <vector>

namespace erm::net {

class Transport {
public:
    enum class EventType { Connected, Disconnected, Packet };

    struct Event {
        EventType             type = EventType::Packet;
        PeerId                peer = kInvalidPeer;
        std::vector<u8>       data;
    };

    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // 服务端：监听 port；0 表示让系统分配（自检用）。
    bool StartServer(u16 port, u32 maxPeers = kMaxPeers);
    // 客户端：连接 host:port；连接是异步的，完成后收到 Connected 事件。
    bool StartClient(const std::string& host, u16 port);
    void Stop();

    bool Running() const;
    bool IsServer() const;
    bool IsClient() const;

    // 非阻塞收包。所有事件（连接/断开/数据）按发生顺序追加到 out。
    void Poll(std::vector<Event>& out);
    // 把 ENet 发送队列刷到 socket。一次 PreFrame 内批量 Send 后调用一次即可。
    void Flush();

    bool Send(PeerId peer, const void* data, size_t size, bool reliable = true);

    u32 RttMs(PeerId peer) const;
    u32 ConnectedPeerCount() const;
    // 服务端实际监听端口（StartServer(0) 后用于自检）。
    u16 BoundPort() const;

    const char* LastError() const { return m_lastError.c_str(); }

private:
    struct Impl;
    Scope<Impl> m_impl;
    std::string m_lastError;
};

} // namespace erm::net
