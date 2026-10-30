// ErMao Engine - 系统文件对话框（UTF-8 路径）
#pragma once

#include "core/Types.h"

namespace erm {

// filter 形如 "OBJ 模型\0*.obj\0所有文件\0*.*\0\0"（注意双 \0 结尾）
bool OpenFileDialog(const char* title, const char* filter, std::string& outPath);
bool SaveFileDialog(const char* title, const char* filter, const char* defaultExt, std::string& outPath);

} // namespace erm
