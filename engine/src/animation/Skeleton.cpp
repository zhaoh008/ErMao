// ErMao Engine - 骨骼/动画采样实现（GAMES104 第 8/9 讲）
#include "animation/Skeleton.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace erm {

namespace {

// 四元数 -> 列主序旋转矩阵（与 Math.h 的 m[col][row] 约定一致）
Mat4 QuatToMatrix(const Quat& q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 m;
    m.m[0][0] = 1.0f - 2.0f * (y * y + z * z);
    m.m[0][1] = 2.0f * (x * y + z * w);
    m.m[0][2] = 2.0f * (x * z - y * w);
    m.m[1][0] = 2.0f * (x * y - z * w);
    m.m[1][1] = 1.0f - 2.0f * (x * x + z * z);
    m.m[1][2] = 2.0f * (y * z + x * w);
    m.m[2][0] = 2.0f * (x * z + y * w);
    m.m[2][1] = 2.0f * (y * z - x * w);
    m.m[2][2] = 1.0f - 2.0f * (x * x + y * y);
    return m;
}

Quat MatrixToQuat(const Mat4& m) {
    const float m00 = m.m[0][0], m01 = m.m[0][1], m02 = m.m[0][2];
    const float m10 = m.m[1][0], m11 = m.m[1][1], m12 = m.m[1][2];
    const float m20 = m.m[2][0], m21 = m.m[2][1], m22 = m.m[2][2];
    const float trace = m00 + m11 + m22;
    Quat        q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m12 - m21) / s;
        q.y = (m20 - m02) / s;
        q.z = (m01 - m10) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m12 - m21) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m20 - m02) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m01 - m10) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return q.Normalized();
}

} // namespace

Mat4 BoneTransform::ToMatrix() const {
    // 局部变换 = T * R * S（列向量约定：先缩放、再旋转、最后平移）
    return Mat4::Translation(translation) * QuatToMatrix(rotation) * Mat4::Scale(scale);
}

BoneTransform BoneTransform::FromMatrix(const Mat4& m) {
    BoneTransform r;
    r.translation = Vec3(m.m[3][0], m.m[3][1], m.m[3][2]);

    const Vec3 col0(m.m[0][0], m.m[0][1], m.m[0][2]);
    const Vec3 col1(m.m[1][0], m.m[1][1], m.m[1][2]);
    const Vec3 col2(m.m[2][0], m.m[2][1], m.m[2][2]);
    r.scale = Vec3(Length(col0), Length(col1), Length(col2));

    Mat4 rot = m;
    if (r.scale.x > 1e-6f) {
        rot.m[0][0] /= r.scale.x;
        rot.m[0][1] /= r.scale.x;
        rot.m[0][2] /= r.scale.x;
    }
    if (r.scale.y > 1e-6f) {
        rot.m[1][0] /= r.scale.y;
        rot.m[1][1] /= r.scale.y;
        rot.m[1][2] /= r.scale.y;
    }
    if (r.scale.z > 1e-6f) {
        rot.m[2][0] /= r.scale.z;
        rot.m[2][1] /= r.scale.z;
        rot.m[2][2] /= r.scale.z;
    }
    r.rotation = MatrixToQuat(rot);
    return r;
}

int Skeleton::AddBone(const std::string& name, int parent, const BoneTransform& localBind) {
    Bone bone;
    bone.name = name;
    bone.parent = parent;
    bone.bindLocal = localBind;
    bones.push_back(bone);
    return static_cast<int>(bones.size()) - 1;
}

void Skeleton::FinalizeBindPose() {
    std::vector<Mat4> world(bones.size(), Mat4::Identity());
    for (size_t i = 0; i < bones.size(); ++i) {
        const int parent = bones[i].parent;
        const Mat4 local = bones[i].bindLocal.ToMatrix();
        world[i] = (parent >= 0) ? world[static_cast<size_t>(parent)] * local : local;
    }
    for (size_t i = 0; i < bones.size(); ++i) {
        bones[i].inverseBind = world[i].Inverse();
    }
}

void Skeleton::ComputeWorld(const std::vector<BoneTransform>& local, std::vector<Mat4>& world) const {
    world.resize(bones.size(), Mat4::Identity());
    for (size_t i = 0; i < bones.size(); ++i) {
        const Mat4 localMatrix = (i < local.size()) ? local[i].ToMatrix() : bones[i].bindLocal.ToMatrix();
        const int  parent = bones[i].parent;
        world[i] = (parent >= 0 && static_cast<size_t>(parent) < world.size())
                       ? world[static_cast<size_t>(parent)] * localMatrix
                       : localMatrix;
    }
}

void Skeleton::ComputeSkinning(const std::vector<Mat4>& world, std::vector<Mat4>& outSkinning) const {
    outSkinning.resize(bones.size(), Mat4::Identity());
    for (size_t i = 0; i < bones.size(); ++i) {
        // S_i = P_i * B_i^{-1}
        outSkinning[i] = (i < world.size() ? world[i] : Mat4::Identity()) * bones[i].inverseBind;
    }
}

int Skeleton::FindBone(const std::string& name) const {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

BoneTransform BoneTrack::Sample(float time) const {
    if (keys.empty()) return BoneTransform{};
    if (keys.size() == 1) return keys[0].value;

    // 找到 time 落在哪两个关键帧之间（片段外由调用方做循环/夹取）
    size_t next = 1;
    while (next < keys.size() && keys[next].time < time) ++next;
    if (next >= keys.size()) return keys.back().value;

    const BoneKey& a = keys[next - 1];
    const BoneKey& b = keys[next];
    const float    span = b.time - a.time;
    const float    alpha = span > 1e-6f ? (time - a.time) / span : 0.0f;

    BoneTransform out;
    out.translation = Lerp(a.value.translation, b.value.translation, alpha);
    out.scale = Lerp(a.value.scale, b.value.scale, alpha);
    out.rotation = Quat::Slerp(a.value.rotation, b.value.rotation, alpha);
    return out;
}

void SkeletonClip::Sample(float time, const Skeleton& skeleton, std::vector<BoneTransform>& outLocal) const {
    outLocal.resize(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) outLocal[i] = skeleton.bones[i].bindLocal;

    float t = time;
    if (duration > 1e-6f) {
        if (loop) {
            t = std::fmod(t, duration);
            if (t < 0.0f) t += duration;
        } else {
            t = Clamp(t, 0.0f, duration);
        }
    }

    for (const BoneTrack& track : tracks) {
        if (track.bone < 0 || static_cast<size_t>(track.bone) >= outLocal.size()) continue;
        outLocal[static_cast<size_t>(track.bone)] = track.Sample(t);
    }
}

void BlendPoses(const std::vector<BoneTransform>& a, const std::vector<BoneTransform>& b, float alpha,
                std::vector<BoneTransform>& out) {
    const size_t count = (std::max)(a.size(), b.size());
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const BoneTransform& pa = i < a.size() ? a[i] : BoneTransform{};
        const BoneTransform& pb = i < b.size() ? b[i] : BoneTransform{};
        BoneTransform        r;
        r.translation = Lerp(pa.translation, pb.translation, alpha);
        r.scale = Lerp(pa.scale, pb.scale, alpha);
        r.rotation = Quat::Slerp(pa.rotation, pb.rotation, alpha);
        out[i] = r;
    }
}

} // namespace erm
