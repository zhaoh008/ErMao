// ErMao Engine - 玩法组件注册实现
#include "game/RegisterComponents.h"

#include "animation/Animation.h"
#include "game/ParticleEmitter.h"
#include "game/SkinnedCharacter.h"
#include "game/ThirdPersonController.h"
#include "game/WaveGame.h"
#include "scene/Component.h"

namespace erm::game {

void RegisterGameComponents() {
    Reflection::Register<AnimatorComponent>("Animator");
    Reflection::Register<ThirdPersonController>("Third Person Controller");
    Reflection::Register<HealthComponent>("Health");
    Reflection::Register<CombatComponent>("Combat");
    Reflection::Register<ProjectileComponent>("Projectile");
    Reflection::Register<EnemyComponent>("Enemy");
    Reflection::Register<SkinnedCharacterComponent>("Skinned Character");
    Reflection::Register<ParticleEmitterComponent>("Particle Emitter");
}

} // namespace erm::game
