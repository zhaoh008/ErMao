// ErMao Engine - 打包（Windows 可运行包 / Linux 源码包）
//
// Windows：exe + shaders + assets，双击 run.bat 即可运行。
// Linux：引擎当前只有 D3D11 后端，无法在 Linux 上直接运行；因此导出的是
//        "源码 + 资源 + 构建脚本" 的包，并附 README 说明需要补什么后端。
//        这一点在 UI 与文档里都明确标注，避免给出一个跑不起来的二进制。
#pragma once

#include "core/Types.h"

namespace erm {

struct PackageResult {
    bool        success = false;
    std::string outputDir;
    std::string message;
};

class Packager {
public:
    // outputRoot 为空时使用 <工程目录>/dist
    static PackageResult PackageWindows(const std::string& outputRoot = {});
    static PackageResult PackageLinuxSource(const std::string& outputRoot = {});
};

} // namespace erm
