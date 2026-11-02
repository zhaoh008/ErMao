// ErMao Engine - 蒙皮演示关卡
#pragma once
#include "core/Math.h"

namespace erm {
class World;
class Renderer;
struct RenderScene;

namespace game {
void BuildSkinningLevel(World& world, Renderer& renderer);
void CollectSkinnedDraws(World& world, RenderScene& scene);
} // namespace game
} // namespace erm