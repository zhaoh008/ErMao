// ErMao Engine - Web 纹理（stb_image + OpenGL ES 2.0）
#include "rhi/Texture.h"
#include "core/FileSystem.h"
#include "core/Log.h"

#include <GLES2/gl2.h>

#include <cmath>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace erm {

namespace {
u32 g_nextTextureId = 0;

// WebGL1 的 EXT_sRGB 在部分实现（含 SwiftShader/部分微信机型）不可靠。
// 这里在 CPU 侧把 sRGB 颜色贴图转成线性值，效果等价于桌面 D3D11 的 sRGB SRV，
// 且只使用最基础的 RGBA 上传路径，兼容性最好。
const u8* LinearizeForSrgb(const u8* rgba, u32 width, u32 height, std::vector<u8>& storage) {
    static u8 lut[256];
    static bool lutReady = false;
    if (!lutReady) {
        for (int i = 0; i < 256; ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            const float linear = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            lut[i] = static_cast<u8>(linear * 255.0f + 0.5f);
        }
        lutReady = true;
    }
    const size_t pixels = static_cast<size_t>(width) * height;
    storage.resize(pixels * 4);
    for (size_t i = 0; i < pixels; ++i) {
        storage[i * 4 + 0] = lut[rgba[i * 4 + 0]];
        storage[i * 4 + 1] = lut[rgba[i * 4 + 1]];
        storage[i * 4 + 2] = lut[rgba[i * 4 + 2]];
        storage[i * 4 + 3] = rgba[i * 4 + 3];
    }
    return storage.data();
}
} // namespace

Texture::~Texture() {
    if (m_glTexture != 0) {
        glDeleteTextures(1, &m_glTexture);
        m_glTexture = 0;
    }
}

bool Texture::CreateFromRGBA(u32 width, u32 height, const u8* rgba, bool srgb, bool) {
    if (width == 0 || height == 0 || !rgba) return false;

    std::vector<u8> linearStorage;
    const u8* uploadPixels = srgb ? LinearizeForSrgb(rgba, width, height, linearStorage) : rgba;

    GLuint handle = 0;
    glGenTextures(1, &handle);
    if (handle == 0) return false;
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 微信 iOS 的 GL 包装层对 ImGui 的部分 glGetIntegerv（如 GL_TEXTURE_BINDING_2D）
    // 会返回 GL_INVALID_ENUM；先清空遗留错误，避免把上一个查询的错误误判成贴图上传失败。
    while (glGetError() != GL_NO_ERROR) {
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, uploadPixels);
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        glDeleteTextures(1, &handle);
        LogError("[Web] glTexImage2D 失败: %ux%u (0x%04X)", width, height, static_cast<unsigned>(error));
        return false;
    }

    if (m_glTexture != 0) glDeleteTextures(1, &m_glTexture);
    m_glTexture = handle;
    m_width = width;
    m_height = height;
    m_id = ++g_nextTextureId;
    return true;
}

bool Texture::CreateFromFile(const std::string& path, bool) {
    std::vector<u8> bytes;
    if (!FileSystem::ReadBinaryFile(path, bytes) || bytes.empty()) {
        LogError("[Web] 读取贴图失败: %s", path.c_str());
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) {
        LogError("[Web] stb_image 解码失败: %s (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }

    const bool ok = CreateFromRGBA(static_cast<u32>(width), static_cast<u32>(height), pixels);
    stbi_image_free(pixels);
    return ok;
}

} // namespace erm
