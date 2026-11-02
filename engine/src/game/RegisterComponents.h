// ErMao Engine - 玩法组件注册（GAMES104 第 2 讲：分层与依赖方向）
//
// 组件工厂的注册属于“把各层组装起来”的职责，由 runtime 调 game 层完成。
// 这样 scene/World.cpp 不必反向 include game/*，World 只负责场景数据。
#pragma once

namespace erm::game {

void RegisterGameComponents();

} // namespace erm::game
