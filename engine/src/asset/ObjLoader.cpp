// ErMao Engine - OBJ 加载实现
#include "asset/ObjLoader.h"
#include "core/FileSystem.h"
#include "core/Log.h"

#include <sstream>

namespace erm {

namespace {
struct ObjIndex {
    int v = 0, vt = 0, vn = 0;
};

ObjIndex ParseIndexToken(const std::string& token) {
    ObjIndex idx;
    int values[3] = {0, 0, 0};
    int slot = 0;
    std::string current;
    for (size_t i = 0; i <= token.size(); ++i) {
        const char c = (i < token.size()) ? token[i] : '/';
        if (c == '/') {
            if (!current.empty() && slot < 3) values[slot] = std::atoi(current.c_str());
            ++slot;
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    idx.v = values[0];
    idx.vt = values[1];
    idx.vn = values[2];
    return idx;
}

int ResolveIndex(int objIndex, size_t count) {
    if (objIndex > 0) return objIndex - 1;
    if (objIndex < 0) return static_cast<int>(count) + objIndex;
    return -1;
}
} // namespace

bool LoadObjMesh(const std::string& path, MeshData& out, std::string* error) {
    std::string text;
    if (!FileSystem::ReadTextFile(path, text)) {
        if (error) *error = "无法打开文件: " + path;
        return false;
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<ObjIndex> cornerIndices;
    std::vector<std::pair<u32, u32>> polygonRanges; // (起点, 顶点数)

    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.size() > 2 && line[0] == 'v' && line[1] == ' ') {
            Vec3 p;
            std::sscanf(line.c_str(), "v %f %f %f", &p.x, &p.y, &p.z);
            positions.push_back(p);
        } else if (line.size() > 3 && line[0] == 'v' && line[1] == 'n') {
            Vec3 n;
            std::sscanf(line.c_str(), "vn %f %f %f", &n.x, &n.y, &n.z);
            normals.push_back(n);
        } else if (line.size() > 3 && line[0] == 'v' && line[1] == 't') {
            Vec2 t;
            std::sscanf(line.c_str(), "vt %f %f", &t.x, &t.y);
            uvs.push_back(t);
        } else if (line.size() > 2 && line[0] == 'f' && line[1] == ' ') {
            std::istringstream lineStream(line.substr(2));
            std::string token;
            const u32 start = static_cast<u32>(cornerIndices.size());
            std::vector<ObjIndex> face;
            while (lineStream >> token) face.push_back(ParseIndexToken(token));
            if (face.size() < 3) continue;
            for (auto& f : face) cornerIndices.push_back(f);
            polygonRanges.emplace_back(start, static_cast<u32>(face.size()));
        }
    }

    if (positions.empty() || cornerIndices.empty()) {
        if (error) *error = "OBJ 中没有有效几何: " + path;
        return false;
    }

    const bool hasNormals = !normals.empty();
    const bool hasUvs = !uvs.empty();

    // 按 (v, vt, vn) 三元组去重顶点
    struct Key {
        int v, vt, vn;
        bool operator==(const Key& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
    };
    std::vector<Key> keys;
    std::unordered_map<u64, u32> lookup;
    auto keyHash = [](const Key& k) -> u64 {
        return (static_cast<u64>(k.v) * 73856093ull) ^ (static_cast<u64>(k.vt) * 19349663ull) ^
               (static_cast<u64>(k.vn) * 83492791ull);
    };

    for (const auto& corner : cornerIndices) {
        const int vi = ResolveIndex(corner.v, positions.size());
        const int ti = hasUvs ? ResolveIndex(corner.vt, uvs.size()) : -1;
        const int ni = hasNormals ? ResolveIndex(corner.vn, normals.size()) : -1;
        if (vi < 0 || vi >= static_cast<int>(positions.size())) continue;

        Key key{vi, ti, ni};
        const u64 hash = keyHash(key);
        auto it = lookup.find(hash);
        bool found = false;
        if (it != lookup.end() && keys[it->second] == key) found = true;

        u32 index = 0;
        if (found) {
            index = it->second;
        } else {
            index = static_cast<u32>(out.positions.size());
            out.positions.push_back(positions[vi]);
            out.normals.push_back(ni >= 0 ? normals[ni] : Vec3(0, 1, 0));
            out.uvs.push_back(ti >= 0 ? uvs[ti] : Vec2(0, 0));
            keys.push_back(key);
            lookup[hash] = index;
        }
        out.indices.push_back(index);
    }

    if (!hasNormals) out.ComputeNormals();
    out.ComputeBounds();
    return true;
}

} // namespace erm
