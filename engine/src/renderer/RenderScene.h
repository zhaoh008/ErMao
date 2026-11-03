// ErMao Engine - 渲染输入（Scene 层把世界翻译成这份扁平数据，Renderer 不认识 GameObject）
#pragma once

#include "core/Math.h"
#include "core/Types.h"
#include "renderer/Camera.h"

namespace erm {

// 渲染输入只持有资源指针，不拥有 GPU 资源，也不需要它们的完整定义。
// 这样 scene/game 层 include RenderScene.h 时不会顺带把 d3d11.h 拉进来。
class Mesh;
class Texture;
class SkinnedMesh;

// 一个可绘制对象：网格 + 世界矩阵 + 材质参数
struct RenderItem {
    const Mesh*    mesh = nullptr;
    Mat4           world;
    Vec4           albedo{1.0f, 1.0f, 1.0f, 1.0f};
    float          roughness = 0.5f;
    float          metallic = 0.0f;
    float          emissive = 0.0f;
    float          uvScale = 0.0f; // 0 表示不采样贴图
    const Texture* baseColor = nullptr;
    bool           castShadow = true;
};

struct PointLightRender {
    Vec3  position{0, 0, 0};
    float range = 10.0f;
    Vec3  color{1, 1, 1};
    float intensity = 1.0f;
};

// 世界空间血条（3D UI）：由玩法层收集，Renderer 用世界空间四边形绘制
struct HealthBarRender {
    Vec3  position{0, 0, 0};
    float width = 1.2f;
    float height = 0.14f;
    float ratio = 1.0f;
    float flash = 0.0f;
    Vec4  color{1, 1, 1, 1};
};

// 蒙皮网格的绘制项（GAMES104 第 8 讲）：骨骼矩阵在 C++ 侧算好，直接喂给 shader
struct SkinnedDraw {
    const class SkinnedMesh* mesh = nullptr;
    Mat4                     world = Mat4::Identity();
    std::vector<Mat4>        bones;      // 每人一份 S_i = P_i * B_i^{-1}
    Vec4                     albedo{0.8f, 0.8f, 0.8f, 1.0f};
    float                    roughness = 0.5f;
    float                    metallic = 0.0f;
};

// 粒子（第 12 讲）：位置 + 大小 + 颜色，渲染器负责画成面向相机的 billboard
struct ParticleRender {
    Vec3                 position{0, 0, 0};
    float                size = 0.1f;
    Vec4                 color{1, 1, 1, 1};
    const class Texture* texture = nullptr; // 非空则采样贴图（火焰/火花），否则画程序化圆点
};

// 2D 精灵（第 23 讲）：纹理 + 目标矩形 + uv + 颜色
struct SpriteDraw {
    const class Texture* texture = nullptr;
    Vec4                 dest{0, 0, 1, 1}; // x,y,w,h（世界单位，x/y 是中心）
    Vec4                 uv{0, 0, 1, 1};   // u0,v0,u1,v1
    Vec4                 color{1, 1, 1, 1};
    bool                 flipX = false;
    float                rotation = 0.0f;  // 绕中心旋转（弧度）
};

// 一帧的渲染场景
struct RenderScene {
    bool                          is2D = false;
    Camera2D                      camera2D;
    std::vector<SpriteDraw>       sprites;
    std::vector<RenderItem>       items;
    std::vector<ParticleRender>   particles;
    std::vector<SkinnedDraw>      skinned;
    std::vector<PointLightRender> pointLights;
    std::vector<HealthBarRender>  healthBars;

    Vec3  sunDirection{0.45f, 0.75f, 0.35f}; // 指向太阳
    Vec3  sunColor{1.0f, 0.95f, 0.88f};
    float sunIntensity = 3.2f;

    Vec3  ambientUp{0.32f, 0.40f, 0.55f};
    Vec3  ambientDown{0.10f, 0.09f, 0.07f};
    float ambientIntensity = 0.85f;

    Vec3  shadowFocus{0, 0, 0};
    float shadowRadius = 28.0f;
};

} // namespace erm
