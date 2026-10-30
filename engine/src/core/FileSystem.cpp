// ErMao Engine - 文件系统实现
//
// 审核报告 P2-8：所有路径统一用 UTF-8 字符串对外，进入 Win32 / CRT 前转成宽字符，
// 这样放在 "D:\我的项目\" 这类中文目录下也不会失效。
#include "core/FileSystem.h"
#include "core/Log.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace erm {

namespace {
std::string g_exeDir;
}

std::wstring FileSystem::Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring out(static_cast<size_t>(size) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
    return out;
}

std::string FileSystem::WideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<size_t>(size) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

const std::string& FileSystem::ExecutableDir() {
    if (!g_exeDir.empty()) return g_exeDir;
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path = path.substr(0, slash + 1);
    g_exeDir = WideToUtf8(path);
    return g_exeDir;
}

std::string FileSystem::Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '\\' || a.back() == '/') return a + b;
    return a + "\\" + b;
}

static std::vector<std::string> RootCandidates() {
    std::vector<std::string> roots;
    roots.push_back(FileSystem::ExecutableDir());
    roots.push_back(FileSystem::Join(FileSystem::ExecutableDir(), ".."));
    roots.push_back(FileSystem::Join(FileSystem::ExecutableDir(), "..\\.."));
    roots.push_back(FileSystem::Join(FileSystem::ExecutableDir(), "..\\..\\.."));
    return roots;
}

std::string FileSystem::AssetPath(const std::string& relative) {
    for (const auto& root : RootCandidates()) {
        const std::string p = Join(Join(root, "assets"), relative);
        if (Exists(p)) return p;
    }
    return {};
}

std::string FileSystem::ShaderPath(const std::string& relative) {
    for (const auto& root : RootCandidates()) {
        const std::string p = Join(Join(root, "shaders"), relative);
        if (Exists(p)) return p;
    }
    for (const auto& root : RootCandidates()) {
        const std::string p = Join(Join(root, "engine\\shaders"), relative);
        if (Exists(p)) return p;
    }
    return {};
}

std::string FileSystem::WritableAssetDir() {
    const std::string dir = Join(ExecutableDir(), "assets");
    EnsureDirectory(dir);
    return dir;
}

bool FileSystem::Exists(const std::string& path) {
    const DWORD attr = GetFileAttributesW(Utf8ToWide(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool FileSystem::ReadTextFile(const std::string& path, std::string& out) {
    std::ifstream f(Utf8ToWide(path).c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool FileSystem::ReadBinaryFile(const std::string& path, std::vector<u8>& out) {
    std::ifstream f(Utf8ToWide(path).c_str(), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), size));
}

bool FileSystem::WriteTextFile(const std::string& path, const std::string& content) {
    EnsureDirectory(DirectoryOf(path));
    std::ofstream f(Utf8ToWide(path).c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return true;
}

bool FileSystem::WriteBinaryFile(const std::string& path, const void* data, size_t size) {
    EnsureDirectory(DirectoryOf(path));
    std::ofstream f(Utf8ToWide(path).c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return true;
}

bool FileSystem::CopyFileTo(const std::string& source, const std::string& destination) {
    EnsureDirectory(DirectoryOf(destination));
    const BOOL ok = CopyFileW(Utf8ToWide(source).c_str(), Utf8ToWide(destination).c_str(), FALSE);
    if (!ok) LogError("复制文件失败: %s -> %s", source.c_str(), destination.c_str());
    return ok != FALSE;
}

std::string FileSystem::DirectoryOf(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string FileSystem::FileNameOf(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string FileSystem::ExtensionOf(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

void FileSystem::EnsureDirectory(const std::string& path) {
    if (path.empty()) return;
    std::wstring wide = Utf8ToWide(path);
    std::wstring current;
    for (wchar_t c : wide) {
        current.push_back(c);
        if (c == L'\\' || c == L'/') {
            if (current.size() > 3) CreateDirectoryW(current.c_str(), nullptr);
        }
    }
    CreateDirectoryW(wide.c_str(), nullptr);
}

} // namespace erm
