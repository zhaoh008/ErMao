#pragma once

#include "core/Types.h"
#include "renderer/RenderScene.h"

#include <vector>

namespace erm {

// Web 端 2D 精灵渲染器：OpenGL ES 2.0 / WebGL 1.0。
// 与桌面端 Renderer::SpritePass 使用同一份 RenderScene::sprites 数据与相机矩阵。
class WebRenderer {
public:
    bool Init();
    void Shutdown();
    void Render(const RenderScene& scene, u32 width, u32 height);

private:
    struct SpriteVertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    struct Run {
        const Texture* texture = nullptr;
        u32            first = 0;
        u32            count = 0;
    };

    bool CompileProgram();

    u32 m_program = 0;
    u32 m_vbo = 0;
    int m_vpLocation = -1;
    int m_texLocation = -1;
    std::vector<SpriteVertex> m_vertices;
};

} // namespace erm
