// ErMao Engine - 导航网格与 A*（GAMES104 第 16 讲）
//
// 教学实现：XZ 平面栅格 + 8 邻域 A*（二叉堆开表）+ 视线拉直。
// 与工业引擎的 NavMesh（多边形、Recast 生成）相比，栅格版更容易读懂，
// 代价是内存与路径精度；文档里给出了 NavMesh 的演进路径。
#pragma once

#include "core/Math.h"
#include "core/Types.h"

#include <queue>

namespace erm {

class World;

class NavGrid {
public:
    void Build(const World& world, float minX, float maxX, float minZ, float maxZ, float cellSize, float agentRadius);
    void Clear();

    bool  IsValid() const { return m_width > 0 && m_height > 0; }
    int   Width() const { return m_width; }
    int   Height() const { return m_height; }
    float CellSize() const { return m_cellSize; }
    bool  Walkable(int x, int z) const;
    Vec3  CellCenter(int x, int z) const;
    bool  WorldToCell(const Vec3& world, int& x, int& z) const;
    bool  FindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& outPath) const;

    // 两点之间是否有可通行视线（AI 感知 / 路径拉直共用）
    bool  HasLineOfSight(const Vec3& a, const Vec3& b) const;

    // 调试：被阻挡的格子
    const std::vector<u8>& Blocked() const { return m_blocked; }

private:
    struct Node {
        float f = 0.0f;
        int   index = 0;
        bool operator>(const Node& other) const { return f > other.f; }
    };

    void RasterizeBoxes(const World& world, float agentRadius);
    void SmoothPath(std::vector<Vec3>& path) const;
    bool LineOfSight(const Vec3& a, const Vec3& b) const;

    int   m_width = 0;
    int   m_height = 0;
    float m_cellSize = 0.5f;
    float m_originX = 0.0f;
    float m_originZ = 0.0f;
    std::vector<u8> m_blocked;
};

} // namespace erm
