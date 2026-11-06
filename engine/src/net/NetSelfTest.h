// ErMao Engine - 引擎级网络同步自检（--net-test）
#pragma once

namespace erm {

class AssetManager;

namespace net {

// 在同一进程里起一个 ENet 主机和一个 ENet 客户端（本地回环），
// 验证：握手、类型表、实体快照、客户端重建、输入上行、本地预测、快照下行。
// 不依赖窗口/渲染；CI 上以退出码判断结果。
bool RunSelfTest(AssetManager* assets);

} // namespace net
} // namespace erm
