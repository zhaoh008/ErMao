// ErMao Engine - 文件系统与资源定位（UTF-8 路径安全）
#pragma once

#include "core/Types.h"

namespace erm {

class FileSystem {
public:
    // exe 所在目录（带尾部反斜杠）
    static const std::string& ExecutableDir();
    // 在若干候选根目录中查找 assets/<相对路径>，返回绝对路径；找不到返回空串
    static std::string AssetPath(const std::string& relative);
    static std::string ShaderPath(const std::string& relative);
    // 可写的 assets 目录（exe 旁边），不存在则创建
    static std::string WritableAssetDir();

    static bool        ReadTextFile(const std::string& path, std::string& out);
    static bool        ReadBinaryFile(const std::string& path, std::vector<u8>& out);
    static bool        WriteTextFile(const std::string& path, const std::string& content);
    static bool        WriteBinaryFile(const std::string& path, const void* data, size_t size);
    static bool        CopyFileTo(const std::string& source, const std::string& destination);
    static bool        Exists(const std::string& path);
    static std::string DirectoryOf(const std::string& path);
    static std::string FileNameOf(const std::string& path);
    static std::string ExtensionOf(const std::string& path);
    static std::string Join(const std::string& a, const std::string& b);

    static void EnsureDirectory(const std::string& path);

    // 路径编码转换（全程 UTF-8 对外，Win32 调用前转宽字符）
    static std::wstring Utf8ToWide(const std::string& text);
    static std::string  WideToUtf8(const std::wstring& text);
};

} // namespace erm
