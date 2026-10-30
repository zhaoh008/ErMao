// ErMao Engine - OBJ 模型加载（GAMES104 第 3/14 讲：资源导入）
#pragma once

#include "core/Types.h"
#include "renderer/Mesh.h"

namespace erm {

// 支持 v / vn / vt / f（v、v/vt、v/vt/vn、v//vn、负索引），把多个 group 合并成一个子网格。
bool LoadObjMesh(const std::string& path, MeshData& out, std::string* error = nullptr);

} // namespace erm
