// ErMao Engine - Web 2D 精灵渲染器（OpenGL ES 2.0 / WebGL 1.0）
#include "WebRenderer.h"

#include "core/Log.h"
#include "rhi/Texture.h"

#include <GLES2/gl2.h>

#include <cmath>
#include <cstddef>

namespace erm {

namespace {

const char* kVertexShader = R"(#version 100
attribute vec2 aPos;
attribute vec2 aUV;
attribute vec4 aColor;
uniform mat4 uVP;
varying vec2 vUV;
varying vec4 vColor;
void main() {
    gl_Position = uVP * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

const char* kFragmentShader = R"(#version 100
precision mediump float;
uniform sampler2D uTex;
varying vec2 vUV;
varying vec4 vColor;
void main() {
    gl_FragColor = texture2D(uTex, vUV) * vColor;
}
)";

GLuint Compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LogError("[Web] 着色器编译失败: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

bool WebRenderer::Init() {
    if (!CompileProgram()) return false;
    glGenBuffers(1, &m_vbo);
    return m_vbo != 0;
}

void WebRenderer::Shutdown() {
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

bool WebRenderer::CompileProgram() {
    const GLuint vs = Compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fs = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glBindAttribLocation(m_program, 0, "aPos");
    glBindAttribLocation(m_program, 1, "aUV");
    glBindAttribLocation(m_program, 2, "aColor");
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024]{};
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        LogError("[Web] 着色器链接失败: %s", log);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_vpLocation = glGetUniformLocation(m_program, "uVP");
    m_texLocation = glGetUniformLocation(m_program, "uTex");
    LogInfo("[Web] 精灵着色器就绪（program=%u）", m_program);
    return true;
}

void WebRenderer::Render(const RenderScene& scene, u32 width, u32 height) {
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    // ImGui 在 WebGL 下不再恢复 GL scissor 状态（iOS 不支持状态查询），这里显式清掉。
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.055f, 0.06f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (scene.sprites.empty() || m_program == 0 || width == 0 || height == 0) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Mat4 viewProj = scene.camera2D.ViewProjection(aspect);

    m_vertices.clear();
    m_vertices.reserve(scene.sprites.size() * 6);
    std::vector<Run> runs;
    runs.reserve(16);

    for (const SpriteDraw& sprite : scene.sprites) {
        if (!sprite.texture || !sprite.texture->IsValid()) continue;
        if (runs.empty() || runs.back().texture != sprite.texture) {
            runs.push_back(Run{sprite.texture, static_cast<u32>(m_vertices.size()), 0});
        }

        const float hw = sprite.dest.z * 0.5f;
        const float hh = sprite.dest.w * 0.5f;
        const float cx = sprite.dest.x;
        const float cy = sprite.dest.y;
        const float x0 = cx - hw;
        const float x1 = cx + hw;
        const float y0 = cy - hh;
        const float y1 = cy + hh;

        float u0 = sprite.uv.x;
        float v0 = sprite.uv.y;
        float u1 = sprite.uv.z;
        float v1 = sprite.uv.w;
        if (sprite.flipX) {
            const float tmp = u0;
            u0 = u1;
            u1 = tmp;
        }

        const float c = std::cos(sprite.rotation);
        const float s = std::sin(sprite.rotation);
        auto rotate = [&](float x, float y) {
            const float dx = x - cx;
            const float dy = y - cy;
            return Vec2(cx + dx * c - dy * s, cy + dx * s + dy * c);
        };
        const Vec2 p00 = rotate(x0, y0);
        const Vec2 p10 = rotate(x1, y0);
        const Vec2 p11 = rotate(x1, y1);
        const Vec2 p01 = rotate(x0, y1);

        const Vec4 color = sprite.color;
        auto push = [&](const Vec2& p, float u, float v) {
            m_vertices.push_back(SpriteVertex{p.x, p.y, u, v, color.x, color.y, color.z, color.w});
        };
        push(p00, u0, v1);
        push(p10, u1, v1);
        push(p11, u1, v0);
        push(p00, u0, v1);
        push(p11, u1, v0);
        push(p01, u0, v0);
        runs.back().count += 6;
    }

    if (m_vertices.empty()) return;

    glUseProgram(m_program);
    glUniformMatrix4fv(m_vpLocation, 1, GL_FALSE, &viewProj.m[0][0]);
    glUniform1i(m_texLocation, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(SpriteVertex)), m_vertices.data(),
                 GL_DYNAMIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(sizeof(SpriteVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, r)));

    for (const Run& run : runs) {
        if (!run.texture || run.count == 0) continue;
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(run.texture->GlHandle()));
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first), static_cast<GLsizei>(run.count));
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

} // namespace erm
