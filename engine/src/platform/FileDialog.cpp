// ErMao Engine - 系统文件对话框实现
#include "platform/FileDialog.h"
#include "core/Log.h"

#include <windows.h>
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

namespace erm {

namespace {
std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(size > 0 ? size - 1 : 0, L'\0');
    if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
    return out;
}

std::string WideToUtf8(const wchar_t* text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size - 1 : 0, '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

// filter 是 UTF-8 的 "名称\0模式\0...\0\0"，需要整体转成 UTF-16
std::wstring ConvertFilter(const char* filter) {
    std::wstring result;
    const char* cursor = filter;
    while (*cursor) {
        result += Utf8ToWide(cursor);
        result.push_back(L'\0');
        cursor += std::strlen(cursor) + 1;
    }
    result.push_back(L'\0');
    return result;
}
} // namespace

bool OpenFileDialog(const char* title, const char* filter, std::string& outPath) {
    wchar_t buffer[MAX_PATH * 2] = {0};
    const std::wstring wfilter = ConvertFilter(filter);
    const std::wstring wtitle = Utf8ToWide(title ? title : "打开文件");

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return false;
    outPath = WideToUtf8(buffer);
    return !outPath.empty();
}

bool SaveFileDialog(const char* title, const char* filter, const char* defaultExt, std::string& outPath) {
    wchar_t buffer[MAX_PATH * 2] = {0};
    const std::wstring wfilter = ConvertFilter(filter);
    const std::wstring wtitle = Utf8ToWide(title ? title : "保存文件");
    const std::wstring wext = Utf8ToWide(defaultExt ? defaultExt : "json");

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrDefExt = wext.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) return false;
    outPath = WideToUtf8(buffer);
    return !outPath.empty();
}

} // namespace erm
