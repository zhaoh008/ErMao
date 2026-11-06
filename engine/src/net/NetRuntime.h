// ErMao Engine - 引擎级网络运行时
//
// 把“传输 + 输入 + 快照 + 插值 + 预测”串成引擎主循环可以直接调用的两段：
//   PreFrame()  ：收包、服务端应用输入、客户端预测/发输入
//   PostFrame() ：服务端按 20Hz 生成并广播快照；客户端 flush
//
// 引擎与玩法的边界（NetGameHooks）：
//   * isReplicated：哪些 GameObject 参与同步（2D 幸存者/3D 幸存者/其它玩法各自实现）；
//   * onPeerConnected/Disconnected：为新玩家生成/移除角色；
//   * applyServerInput：把客户端输入交给服务端权威模拟；
//   * predictLocal：客户端本地玩家立即响应输入；
//   * onSnapshotApplied：客户端快照落地后刷新玩法侧缓存（小地图、敌人列表等）。
#pragma once

#include "net/NetCommon.h"
#include "net/NetReplication.h"
#include "net/NetTransport.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace erm {

class World;
class GameObject;

namespace net {

constexpr u8 kMsgClientHello = 1;
constexpr u8 kMsgWelcome = 2;
constexpr u8 kMsgTypeTable = 3;
constexpr u8 kMsgInput = 4;
constexpr u8 kMsgSnapshot = 5;
constexpr u8 kMsgOwnEntity = 6;

struct NetGameHooks {
    std::function<bool(GameObject&)> isReplicated;
    std::function<void(PeerId)> onPeerConnected;
    std::function<void(PeerId)> onPeerDisconnected;
    // 客户端连接前清掉本地预生成的玩家/动态对象，避免与快照实体重复。
    std::function<void(World&)> prepareClientWorld;
    std::function<void(World&)> onSnapshotApplied;
    std::function<void(GameObject&, const NetInput&, float)> applyServerInput;
    std::function<void(GameObject&, const NetInput&, float)> predictLocal;
    std::function<void(GameObject*)> onLocalEntity; // nullptr = 本地实体消失/断开
};

class NetRuntime {
public:
    enum class Mode { Off, Server, Client };

    NetRuntime();
    ~NetRuntime();
    NetRuntime(const NetRuntime&) = delete;
    NetRuntime& operator=(const NetRuntime&) = delete;

    bool StartServer(World& world, u16 port, NetGameHooks hooks);
    bool StartClient(World& world, const std::string& address, NetGameHooks hooks);
    void Stop(World* world);

    void PreFrame(World& world, float dt, const NetInput& localInput);
    void PostFrame(World& world, float dt);

    Mode GetMode() const { return m_mode; }
    bool IsServer() const { return m_mode == Mode::Server; }
    bool IsClient() const { return m_mode == Mode::Client; }
    bool IsClientReplica() const { return m_mode == Mode::Client; }
    bool Connected() const;
    bool ServerReady() const { return m_serverStarted; }

    u32  RttMs() const;
    u32  PeerCount() const { return static_cast<u32>(m_peers.size()); }
    u32  EntityCount() const;
    NetId LocalEntity() const { return m_localNetId; }
    GameObject* FindLocalEntity(World& world);
    u16  Port() const;
    const std::string& Address() const { return m_address; }
    std::string StatusText() const;
    const char* LastError() const { return m_lastError.c_str(); }

private:
    struct PeerState {
        PeerId   peer = kInvalidPeer;
        bool     ready = false;
        NetId    ownNetId = 0;
        u32      ack = 0;
        NetInput input;
        u64      lastInputMs = 0;
    };

    void HandleEvents(World& world);
    void HandleClientPacket(World& world, PeerId peer, const std::vector<u8>& data);
    void HandleServerPacket(World& world, PeerId peer, const std::vector<u8>& data);
    void SendTypeTable(PeerId peer);
    void SendSnapshot(World& world, bool full);
    void ApplyServerInputs(World& world, float dt);
    void SendClientInput();
    void PredictClient(World& world, float dt, const NetInput& input);
    void NotifyLocalEntity(World& world);

    Mode                    m_mode = Mode::Off;
    Transport               m_transport;
    ReplicationServer       m_server;
    ReplicationClient       m_client;
    NetGameHooks            m_hooks;
    std::unordered_map<PeerId, PeerState> m_peers;
    std::string             m_address;
    std::string             m_lastError;

    bool  m_serverStarted = false;
    bool  m_clientStarted = false;
    bool  m_clientConnected = false;
    bool  m_clientReady = false;
    bool  m_forceFullSnapshot = false;
    bool  m_localNotified = false;
    NetId m_localNetId = 0;
    GameObject* m_lastLocalObject = nullptr;

    u32   m_serverTick = 0;
    u32   m_inputSequence = 0;
    NetInput m_pendingInput; // 客户端最近一帧的本地输入（按 60Hz 发送）
    float m_snapshotTimer = 0.0f;
    float m_inputTimer = 0.0f;
    u64   m_startTimeMs = 0;
};

} // namespace net
} // namespace erm
