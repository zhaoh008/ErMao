// ErMao Engine - Web 文件系统（Emscripten MEMFS / 微信小游戏 CustomWritablePath）
#include "core/FileSystem.h"
#include "core/Log.h"

#include <sys/stat.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace erm {

namespace {

std::string g_exeDir = "/";
std::string g_writable = "/CustomWritablePath";

bool IsDirectory(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool IsFile(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

std::string NormalizeSeparators(std::string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return path;
}

} // namespace

const std::string& FileSystem::ExecutableDir() { return g_exeDir; }

std::string FileSystem::AssetPath(const std::string& relative) {
    const std::string rel = NormalizeSeparators(relative);
    const std::string candidates[] = {
        Join("/assets", rel),
        Join("/engine/assets", rel),
        rel,
    };
    for (const auto& candidate : candidates) {
        if (IsFile(candidate)) return candidate;
    }
    return {};
}

std::string FileSystem::ShaderPath(const std::string& relative) {
    const std::string path = Join("/shaders", NormalizeSeparators(relative));
    return IsFile(path) ? path : std::string();
}

std::string FileSystem::WritableAssetDir() {
    EnsureDirectory(g_writable);
    return g_writable;
}

bool FileSystem::ReadTextFile(const std::string& path, std::string& out) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) return false;
    std::ostringstream stream;
    stream << file.rdbuf();
    out = stream.str();
    return true;
}

bool FileSystem::ReadBinaryFile(const std::string& path, std::vector<u8>& out) {
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamsize size = file.tellg();
    if (size < 0) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size == 0) return true;
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

bool FileSystem::WriteTextFile(const std::string& path, const std::string& content) {
    EnsureDirectory(DirectoryOf(path));
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.flush();
    if (!file.good()) {
        LogError("写入文件失败: %s", path.c_str());
        return false;
    }
    return true;
}

bool FileSystem::WriteBinaryFile(const std::string& path, const void* data, size_t size) {
    EnsureDirectory(DirectoryOf(path));
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    if (size > 0) file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    file.flush();
    if (!file.good()) {
        LogError("写入文件失败: %s", path.c_str());
        return false;
    }
    return true;
}

bool FileSystem::CopyFileTo(const std::string& source, const std::string& destination) {
    std::vector<u8> bytes;
    if (!ReadBinaryFile(source, bytes)) return false;
    return WriteBinaryFile(destination, bytes.data(), bytes.size());
}

bool FileSystem::Exists(const std::string& path) { return IsFile(path); }

std::string FileSystem::DirectoryOf(const std::string& path) {
    const std::string normalized = NormalizeSeparators(path);
    const size_t slash = normalized.find_last_of('/');
    return slash == std::string::npos ? std::string() : normalized.substr(0, slash);
}

std::string FileSystem::FileNameOf(const std::string& path) {
    const std::string normalized = NormalizeSeparators(path);
    const size_t slash = normalized.find_last_of('/');
    return slash == std::string::npos ? normalized : normalized.substr(slash + 1);
}

std::string FileSystem::ExtensionOf(const std::string& path) {
    const std::string normalized = NormalizeSeparators(path);
    const size_t dot = normalized.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = normalized.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

std::string FileSystem::Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

void FileSystem::EnsureDirectory(const std::string& path) {
    if (path.empty()) return;
    std::string normalized = NormalizeSeparators(path);
    if (normalized == "/") return;
    if (normalized.find("://") != std::string::npos) return;

    std::string current;
    if (normalized[0] == '/') current = "/";
    for (size_t i = 0; i < normalized.size(); ++i) {
        const char c = normalized[i];
        if (c != '/') {
            current.push_back(c);
            continue;
        }
        if (current.size() > 1) {
            ::mkdir(current.c_str(), 0777);
        }
        if (current.empty() || current.back() != '/') current.push_back('/');
    }
    if (!current.empty() && current != "/") ::mkdir(current.c_str(), 0777);
}

std::wstring FileSystem::Utf8ToWide(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

std::string FileSystem::WideToUtf8(const std::wstring& text) {
    return std::string(text.begin(), text.end());
}

} // namespace erm
