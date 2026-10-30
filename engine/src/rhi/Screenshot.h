// ErMao Engine - 后备缓冲截图（WIC 编码 PNG）
#pragma once

#include "core/Types.h"
#include <d3d11.h>
#include <dxgi1_2.h>

namespace erm {

// 把 swapchain 当前后备缓冲保存为 PNG；返回是否成功。
// 引擎自带截图能力，便于无人工干预地录制/验证画面。
bool SaveBackbufferToPng(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain1* swapChain,
                         const std::string& path);

} // namespace erm
