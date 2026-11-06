// ErMao Engine - 引擎级网络同步自检实现
#include "net/NetSelfTest.h"

#include "core/Log.h"
#include "game/Survivors.h"
#include "net/NetRuntime.h"
#include "scene/GameObject.h"
#include "scene/World.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace erm::net {

namespace {

void Pump(NetRuntime& server, NetRuntime& client, World& hostWorld, World& clientWorld, const NetInput& input) {
    const float dt = 1.0f / 60.0f;
    server.PreFrame(hostWorld, dt, NetInput{});
    client.PreFrame(clientWorld, dt, input);
    server.PostFrame(hostWorld, dt);
    client.PostFrame(clientWorld, dt);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void MoveByInput(GameObject& object, const NetInput& input, float dt) {
    auto* player = object.GetComponent<SurvivorsPlayerComponent>();
    if (!player) return;
    Vec3 position = object.Position();
    position.x += input.moveX * player->speed * dt;
    position.y += input.moveY * player->speed * dt;
    object.SetPosition(position);
}

} // namespace

bool RunSelfTest(AssetManager* assets) {
    LogInfo("[网络自检] 开始：本机回环主机 + 客户端、输入上行、快照下行、预测/重建");

    World hostWorld;
    World clientWorld;
    hostWorld.Init(assets);
    clientWorld.Init(assets);

    GameObject* hostPlayer = hostWorld.CreateGameObject("玩家");
    auto*       player = hostPlayer->AddComponent<SurvivorsPlayerComponent>();
    player->speed = 4.6f;
    auto* identity = hostPlayer->AddComponent<NetworkIdentityComponent>();
    identity->netId = 0;

    GameObject* localEntity = nullptr;
    bool        sawSnapshot = false;

    NetGameHooks serverHooks;
    serverHooks.isReplicated = [](GameObject& object) {
        return object.GetComponent<SurvivorsPlayerComponent>() != nullptr;
    };
    serverHooks.onPeerConnected = [hostPlayer](PeerId peer) {
        if (auto* id = hostPlayer->GetComponent<NetworkIdentityComponent>()) id->ownerPeer = peer;
    };
    serverHooks.applyServerInput = [](GameObject& object, const NetInput& input, float dt) {
        MoveByInput(object, input, dt);
    };

    NetGameHooks clientHooks;
    clientHooks.predictLocal = [](GameObject& object, const NetInput& input, float dt) {
        MoveByInput(object, input, dt);
    };
    clientHooks.onLocalEntity = [&localEntity](GameObject* object) { localEntity = object; };
    clientHooks.onSnapshotApplied = [&sawSnapshot](World&) { sawSnapshot = true; };

    NetRuntime server;
    NetRuntime client;
    bool       ok = true;
    auto       require = [&](bool condition, const char* what) {
        if (!condition) {
            LogError("[网络自检] 失败：%s", what);
            ok = false;
        }
    };

    if (!server.StartServer(hostWorld, 0, serverHooks)) {
        LogError("[网络自检] 无法启动本机主机");
        return false;
    }
    const u16 port = server.Port();
    require(port != 0, "主机拿到监听端口");
    char address[64];
    std::snprintf(address, sizeof(address), "127.0.0.1:%u", static_cast<unsigned>(port));
    if (!client.StartClient(clientWorld, address, clientHooks)) {
        LogError("[网络自检] 无法连接本机主机 %s", address);
        server.Stop(&hostWorld);
        return false;
    }

    // 1) 握手 + 首帧全量快照：最多等 8 秒。
    for (int frame = 0; frame < 8000 && !(server.Connected() && client.Connected() && localEntity); ++frame) {
        Pump(server, client, hostWorld, clientWorld, NetInput{});
    }
    require(server.Connected(), "主机进入可服务状态");
    require(client.Connected(), "客户端完成握手");
    require(localEntity != nullptr, "客户端重建本地玩家实体");
    require(client.EntityCount() == 1, "客户端实体数 = 1");
    require(sawSnapshot, "客户端收到快照");

    // 2) 输入上行 + 本地预测 + 快照回传：向右走 1.5 秒。
    const Vec3 start = hostPlayer->Position();
    NetInput   input;
    input.moveX = 1.0f;
    for (int frame = 0; frame < 90; ++frame) Pump(server, client, hostWorld, clientWorld, input);

    const float serverDistance = Length(hostPlayer->Position() - start);
    const float clientDistance = localEntity ? Length(localEntity->Position() - start) : 0.0f;
    require(serverDistance > 1.5f, "服务端应用客户端输入并移动");
    require(clientDistance > 1.5f, "客户端本地预测立即移动");
    require(localEntity && std::fabs(localEntity->Position().x - hostPlayer->Position().x) < 0.8f,
            "预测位置与权威位置收敛（误差 < 0.8m）");
    LogInfo("[网络自检] 服务端移动 %.2fm，客户端预测 %.2fm，RTT %ums，快照实体 %u", serverDistance,
            clientDistance, client.RttMs(), client.EntityCount());

    // 3) 清理：客户端断开后重建的实体应被销毁。
    client.Stop(&clientWorld);
    require(clientWorld.ObjectCount() == 0, "客户端停止后清空快照实体");
    server.Stop(&hostWorld);
    require(hostWorld.ObjectCount() == 1, "服务端保留权威世界对象");

    LogInfo("[网络自检] %s", ok ? "通过" : "失败");
    return ok;
}

} // namespace erm::net
