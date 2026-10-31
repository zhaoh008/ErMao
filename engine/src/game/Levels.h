// ErMao Engine - 关卡（代码优先）
//
// 这里刻意不用"编辑器拖出来的场景"：关卡就是一个普通 C++ 函数，
// 用一组小工具函数把物体、碰撞、灯光、AI 摆进去。
// 这样做的好处：可 diff、可参数化、可被单元测试/自动化关卡生成复用。
#pragma once

namespace erm {
class World;
class Renderer;
}

namespace erm::game {

// 第三人称演示关卡：平台、台阶、跳台、障碍、AI、终点光环
void BuildThirdPersonLevel(World& world);
// 刷怪 Demo 的竞技场：围墙、掩体、玩家（带生命/射击），敌人由 WaveGame 动态生成
void BuildWaveArena(World& world);
// 蒙皮演示关卡：需要 Renderer 来创建 GPU 蒙皮网格
void BuildSkinningLevel(World& world, Renderer& renderer);
// GPU 剔除压测：2000 个同批次立方体
void BuildCullTestLevel(World& world);

// 早期的基础演示场景（保留，方便对比）
void BuildDemoScene(World& world);

} // namespace erm::game
