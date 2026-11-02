// ErMao Engine - 导航网格与 A* 实现
#include "ai/NavGrid.h"
#include "core/Log.h"
#include "scene/Components.h"
#include "scene/World.h"

#include <algorithm>

namespace erm {

void NavGrid::Clear() {
    m_width = m_height = 0;
    m_blocked.clear();
}

void NavGrid::Build(const World& world, float minX, float maxX, float minZ, float maxZ, float cellSize,
                    float agentRadius) {
    Clear();
    m_cellSize = cellSize;
    m_originX = minX;
    m_originZ = minZ;
    m_width = static_cast<int>((maxX - minX) / cellSize);
    m_height = static_cast<int>((maxZ - minZ) / cellSize);
    if (m_width <= 0 || m_height <= 0) return;
    m_blocked.assign(static_cast<size_t>(m_width) * m_height, 0);

    RasterizeBoxes(world, agentRadius);
    LogInfo("导航网格生成：%d x %d 格（%.2f m/格）", m_width, m_height, cellSize);
}

void NavGrid::RasterizeBoxes(const World& world, float agentRadius) {
    for (GameObject* root : world.RootObjects()) {
        root->ForEach([&](GameObject* object) {
            auto* body = object->GetComponent<RigidBodyComponent>();
            if (!body || !body->isStatic) return;
            const Vec3 center = object->WorldPosition();
            // 静态盒的 XZ footprint + 角色半径
            const Vec3 ext = body->halfExtents + Vec3(agentRadius, 0.0f, agentRadius);
            const AABB box = AABB::FromCenterExtents(center, ext);

            int x0, z0, x1, z1;
            WorldToCell(Vec3(box.min.x, 0, box.min.z), x0, z0);
            WorldToCell(Vec3(box.max.x, 0, box.max.z), x1, z1);
            x0 = (std::max)(0, x0);
            z0 = (std::max)(0, z0);
            x1 = (std::min)(m_width - 1, x1);
            z1 = (std::min)(m_height - 1, z1);
            for (int z = z0; z <= z1; ++z) {
                for (int x = x0; x <= x1; ++x) {
                    m_blocked[static_cast<size_t>(z) * m_width + x] = 1;
                }
            }
        });
    }

    // 世界边界一圈设为阻挡，避免路径走出地图
    for (int x = 0; x < m_width; ++x) {
        m_blocked[x] = 1;
        m_blocked[static_cast<size_t>(m_height - 1) * m_width + x] = 1;
    }
    for (int z = 0; z < m_height; ++z) {
        m_blocked[static_cast<size_t>(z) * m_width] = 1;
        m_blocked[static_cast<size_t>(z) * m_width + m_width - 1] = 1;
    }
}

bool NavGrid::Walkable(int x, int z) const {
    if (x < 0 || z < 0 || x >= m_width || z >= m_height) return false;
    return m_blocked[static_cast<size_t>(z) * m_width + x] == 0;
}

Vec3 NavGrid::CellCenter(int x, int z) const {
    return Vec3(m_originX + (x + 0.5f) * m_cellSize, 0.0f, m_originZ + (z + 0.5f) * m_cellSize);
}

bool NavGrid::WorldToCell(const Vec3& world, int& x, int& z) const {
    x = static_cast<int>((world.x - m_originX) / m_cellSize);
    z = static_cast<int>((world.z - m_originZ) / m_cellSize);
    return x >= 0 && z >= 0 && x < m_width && z < m_height;
}

bool NavGrid::FindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& outPath) const {
    outPath.clear();
    if (!IsValid()) return false;

    int sx, sz, gx, gz;
    if (!WorldToCell(start, sx, sz) || !WorldToCell(goal, gx, gz)) return false;
    if (!Walkable(gx, gz)) {
        // 目标在障碍里：就近找一个可走格
        bool found = false;
        for (int r = 1; r <= 4 && !found; ++r) {
            for (int dz = -r; dz <= r && !found; ++dz) {
                for (int dx = -r; dx <= r && !found; ++dx) {
                    if (Walkable(gx + dx, gz + dz)) {
                        gx += dx;
                        gz += dz;
                        found = true;
                    }
                }
            }
        }
        if (!found) return false;
    }
    if (!Walkable(sx, sz)) return false; // 起点卡在障碍里：交给上层重试，不给穿墙兜底

    const int  count = m_width * m_height;
    const int  startIdx = sz * m_width + sx;
    const int  goalIdx = gz * m_width + gx;
    std::vector<float> gScore(static_cast<size_t>(count), FLT_MAX);
    std::vector<int>   cameFrom(static_cast<size_t>(count), -1);
    std::vector<u8>    closed(static_cast<size_t>(count), 0);

    auto heuristic = [&](int idx) {
        const int x = idx % m_width;
        const int z = idx / m_width;
        const float dx = static_cast<float>(std::abs(x - gx));
        const float dz = static_cast<float>(std::abs(z - gz));
        // 八方向距离
        return (dx + dz) + (1.41421356f - 2.0f) * (std::min)(dx, dz);
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    gScore[startIdx] = 0.0f;
    open.push({heuristic(startIdx), startIdx});

    const int offsets[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    bool found = false;

    while (!open.empty()) {
        const Node current = open.top();
        open.pop();
        if (closed[current.index]) continue;
        closed[current.index] = 1;
        if (current.index == goalIdx) {
            found = true;
            break;
        }
        const int cx = current.index % m_width;
        const int cz = current.index / m_width;

        for (const auto& offset : offsets) {
            const int nx = cx + offset[0];
            const int nz = cz + offset[1];
            if (!Walkable(nx, nz)) continue;
            // 禁止贴角穿越
            if (offset[0] != 0 && offset[1] != 0) {
                if (!Walkable(cx + offset[0], cz) || !Walkable(cx, cz + offset[1])) continue;
            }
            const int  nIdx = nz * m_width + nx;
            if (closed[nIdx]) continue;
            const float step = (offset[0] != 0 && offset[1] != 0) ? 1.41421356f : 1.0f;
            const float tentative = gScore[current.index] + step;
            if (tentative < gScore[nIdx]) {
                gScore[nIdx] = tentative;
                cameFrom[nIdx] = current.index;
                open.push({tentative + heuristic(nIdx), nIdx});
            }
        }
    }

    if (!found) return false;

    std::vector<Vec3> raw;
    int index = goalIdx;
    while (index != -1 && index != startIdx) {
        raw.push_back(CellCenter(index % m_width, index / m_width));
        index = cameFrom[index];
    }
    std::reverse(raw.begin(), raw.end());
    raw.push_back(Vec3(goal.x, 0.0f, goal.z));

    SmoothPath(raw);
    outPath = raw;
    return !outPath.empty();
}

bool NavGrid::HasLineOfSight(const Vec3& a, const Vec3& b) const { return LineOfSight(a, b); }

bool NavGrid::LineOfSight(const Vec3& a, const Vec3& b) const {
    const Vec3 delta = b - a;
    const float length = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const int steps = (std::max)(1, static_cast<int>(length / (m_cellSize * 0.5f)));
    for (int i = 0; i <= steps; ++i) {
        const Vec3 p = a + delta * (static_cast<float>(i) / steps);
        int x, z;
        if (!WorldToCell(p, x, z) || !Walkable(x, z)) return false;
    }
    return true;
}

void NavGrid::SmoothPath(std::vector<Vec3>& path) const {
    if (path.size() < 3) return;
    std::vector<Vec3> result;
    result.push_back(path.front());
    size_t i = 0;
    while (i + 1 < path.size()) {
        size_t farthest = i + 1;
        for (size_t j = path.size() - 1; j > i + 1; --j) {
            if (LineOfSight(path[i], path[j])) {
                farthest = j;
                break;
            }
        }
        result.push_back(path[farthest]);
        i = farthest;
    }
    path = result;
}

} // namespace erm
