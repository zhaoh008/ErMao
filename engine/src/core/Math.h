// ErMao Engine - 数学库（GAMES104 第 1/4 讲：引擎的数学基础）
//
// 约定：
//   * Mat4 为「列主序」存储：m[col][row]，与 HLSL 的 float4x4 内存布局一致，
//     可以直接 memcpy 进常量缓冲，不需要转置。
//   * 向量是列向量，变换写作 v' = M * v；矩阵乘法 C = A * B 表示「先 B 后 A」。
//   * 坐标系：右手系，相机朝向 -Z，投影后深度范围 [0,1]（D3D 约定）。
//   * 旋转统一用四元数（第 8 讲：避免万向锁、方便 Slerp 插值）。
#pragma once

#include "core/Types.h"

namespace erm {

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kTwoPi   = 6.28318530717958647692f;
constexpr float kHalfPi  = 1.57079632679489661923f;
constexpr float kEpsilon = 1e-6f;

inline float ToRadians(float deg) { return deg * (kPi / 180.0f); }
inline float ToDegrees(float rad) { return rad * (180.0f / kPi); }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Saturate(float v) { return Clamp01(v); }
inline float Sign(float v) { return v < 0.0f ? -1.0f : 1.0f; }
inline bool  NearlyEqual(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
inline float SmoothStep(float e0, float e1, float x) {
    float t = Saturate((x - e0) / (e1 - e0 + kEpsilon));
    return t * t * (3.0f - 2.0f * t);
}

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    explicit Vec2(float v) : x(v), y(v) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

inline float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float Length(const Vec2& v) { return std::sqrt(Dot(v, v)); }
inline Vec2  Normalize(const Vec2& v) { float l = Length(v); return l > kEpsilon ? v / l : Vec2(0, 0); }

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3(float v) : x(v), y(v), z(v) {}
    explicit Vec3(const Vec2& v, float z_ = 0.0f) : x(v.x), y(v.y), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }
    float  operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
};

inline Vec3  operator*(float s, const Vec3& v) { return v * s; }
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3  Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
inline float LengthSq(const Vec3& v) { return Dot(v, v); }
inline float Distance(const Vec3& a, const Vec3& b) { return Length(a - b); }
inline Vec3  Normalize(const Vec3& v) { float l = Length(v); return l > kEpsilon ? v / l : Vec3(0, 0, 0); }
inline Vec3  Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3  Min(const Vec3& a, const Vec3& b) { return {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)}; }
inline Vec3  Max(const Vec3& a, const Vec3& b) { return {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)}; }
inline Vec3  Abs(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }
inline Vec3  Reflect(const Vec3& v, const Vec3& n) { return v - n * (2.0f * Dot(v, n)); }

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    explicit Vec4(float v) : x(v), y(v), z(v), w(v) {}

    Vec3 xyz() const { return {x, y, z}; }
    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Vec4 operator/(float s) const { return {x / s, y / s, z / s, w / s}; }
    Vec4& operator+=(const Vec4& o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
    bool operator==(const Vec4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
    bool operator!=(const Vec4& o) const { return !(*this == o); }
    float  operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
};

inline float Dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Vec4  Lerp(const Vec4& a, const Vec4& b, float t) { return a + (b - a) * t; }

// ------------------------------------------------------------------ Mat4
struct Mat4 {
    // m[col][row]
    float m[4][4];

    Mat4() { MakeIdentity(); }

    static Mat4 Identity() { return Mat4(); }

    void MakeIdentity() {
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) m[c][r] = (c == r) ? 1.0f : 0.0f;
    }

    float& At(int col, int row) { return m[col][row]; }
    float  At(int col, int row) const { return m[col][row]; }

    static Mat4 Translation(const Vec3& t) {
        Mat4 r;
        r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
        return r;
    }
    static Mat4 Scale(const Vec3& s) {
        Mat4 r;
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
        return r;
    }
    static Mat4 RotationX(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[1][1] = c; r.m[2][1] = -s; r.m[1][2] = s; r.m[2][2] = c;
        return r;
    }
    static Mat4 RotationY(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c; r.m[2][0] = s; r.m[0][2] = -s; r.m[2][2] = c;
        return r;
    }
    static Mat4 RotationZ(float rad) {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c; r.m[1][0] = -s; r.m[0][1] = s; r.m[1][1] = c;
        return r;
    }

    // 透视投影（右手系，-Z 朝前，深度映射到 [0,1]）
    static Mat4 Perspective(float fovYRad, float aspect, float nearZ, float farZ) {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rw = 0; rw < 4; ++rw) r.m[c][rw] = 0.0f;
        const float y = 1.0f / std::tan(fovYRad * 0.5f);
        const float x = y / aspect;
        r.m[0][0] = x;
        r.m[1][1] = y;
        r.m[2][2] = farZ / (nearZ - farZ);
        r.m[2][3] = -1.0f;
        r.m[3][2] = nearZ * farZ / (nearZ - farZ);
        return r;
    }

    static Mat4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ) {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rw = 0; rw < 4; ++rw) r.m[c][rw] = 0.0f;
        r.m[0][0] = 2.0f / (right - left);
        r.m[1][1] = 2.0f / (top - bottom);
        r.m[2][2] = 1.0f / (nearZ - farZ);
        r.m[3][0] = -(right + left) / (right - left);
        r.m[3][1] = -(top + bottom) / (top - bottom);
        r.m[3][2] = nearZ / (nearZ - farZ);
        r.m[3][3] = 1.0f;
        return r;
    }

    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        const Vec3 zaxis = Normalize(eye - target); // 相机 +Z 指向后方
        const Vec3 xaxis = Normalize(Cross(up, zaxis));
        const Vec3 yaxis = Cross(zaxis, xaxis);

        Mat4 r;
        r.m[0][0] = xaxis.x; r.m[1][0] = xaxis.y; r.m[2][0] = xaxis.z;
        r.m[0][1] = yaxis.x; r.m[1][1] = yaxis.y; r.m[2][1] = yaxis.z;
        r.m[0][2] = zaxis.x; r.m[1][2] = zaxis.y; r.m[2][2] = zaxis.z;
        r.m[3][0] = -Dot(xaxis, eye);
        r.m[3][1] = -Dot(yaxis, eye);
        r.m[3][2] = -Dot(zaxis, eye);
        r.m[3][3] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += m[k][row] * b.m[c][k];
                r.m[c][row] = s;
            }
        }
        return r;
    }

    Vec4 operator*(const Vec4& v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
                m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w};
    }

    Vec3 TransformPoint(const Vec3& p) const {
        return {m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0],
                m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1],
                m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2]};
    }
    Vec3 TransformVector(const Vec3& v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z};
    }

    Mat4 Transposed() const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rw = 0; rw < 4; ++rw) r.m[c][rw] = m[rw][c];
        return r;
    }

    // 通用求逆（高斯-约当 + 部分主元），教学优先、数值稳定；奇异时返回单位阵并告警
    Mat4 Inverse() const {
        double a[4][8];
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) a[r][c] = m[c][r];
            for (int c = 0; c < 4; ++c) a[r][4 + c] = (r == c) ? 1.0 : 0.0;
        }
        for (int col = 0; col < 4; ++col) {
            int pivot = col;
            double best = std::fabs(a[col][col]);
            for (int r = col + 1; r < 4; ++r) {
                double v = std::fabs(a[r][col]);
                if (v > best) { best = v; pivot = r; }
            }
            if (best < 1e-12) {
                // 奇异矩阵：返回单位阵（调用方应尽量避免，例如零缩放）
                return Mat4::Identity();
            }
            if (pivot != col) {
                for (int c = 0; c < 8; ++c) std::swap(a[col][c], a[pivot][c]);
            }
            const double inv = 1.0 / a[col][col];
            for (int c = 0; c < 8; ++c) a[col][c] *= inv;
            for (int r = 0; r < 4; ++r) {
                if (r == col) continue;
                const double f = a[r][col];
                if (std::fabs(f) < 1e-18) continue;
                for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
            }
        }
        Mat4 result;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) result.m[c][r] = static_cast<float>(a[r][4 + c]);
        return result;
    }

    // 法线矩阵：inverse(transpose(M)) 的 3x3 部分
    static Mat4 NormalMatrix(const Mat4& world) {
        Mat4 inv = world.Inverse();
        return inv.Transposed();
    }
};

// ------------------------------------------------------------------ Quat
struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat Identity() { return Quat(0, 0, 0, 1); }

    static Quat FromAxisAngle(const Vec3& axis, float rad) {
        const Vec3  n = Normalize(axis);
        const float h = rad * 0.5f;
        const float s = std::sin(h);
        return Quat(n.x * s, n.y * s, n.z * s, std::cos(h));
    }

    // UE 风格：yaw 绕 Y，pitch 绕 X，roll 绕 Z，复合顺序 yaw * pitch * roll
    static Quat FromEuler(float yawRad, float pitchRad, float rollRad) {
        const Quat qy = FromAxisAngle({0, 1, 0}, yawRad);
        const Quat qp = FromAxisAngle({1, 0, 0}, pitchRad);
        const Quat qr = FromAxisAngle({0, 0, 1}, rollRad);
        return qy * qp * qr;
    }

    Quat operator*(const Quat& b) const {
        return {w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
    }

    Quat Conjugated() const { return {-x, -y, -z, w}; }
    float Length() const { return std::sqrt(x * x + y * y + z * z + w * w); }

    Quat Normalized() const {
        const float l = Length();
        if (l < kEpsilon) return Identity();
        const float inv = 1.0f / l;
        return {x * inv, y * inv, z * inv, w * inv};
    }

    Vec3 RotateVector(const Vec3& v) const {
        const Vec3 u(x, y, z);
        const Vec3 t = Cross(u, v) * 2.0f;
        return v + t * w + Cross(u, t);
    }

    Mat4 ToMat4() const {
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;
        Mat4 r;
        r.m[0][0] = 1.0f - 2.0f * (yy + zz); r.m[1][0] = 2.0f * (xy - wz);       r.m[2][0] = 2.0f * (xz + wy);
        r.m[0][1] = 2.0f * (xy + wz);       r.m[1][1] = 1.0f - 2.0f * (xx + zz); r.m[2][1] = 2.0f * (yz - wx);
        r.m[0][2] = 2.0f * (xz - wy);       r.m[1][2] = 2.0f * (yz + wx);       r.m[2][2] = 1.0f - 2.0f * (xx + yy);
        return r;
    }

    static Quat Slerp(const Quat& a, const Quat& b, float t) {
        float cosTheta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        Quat  bb = b;
        if (cosTheta < 0.0f) { // 走最短路径
            bb = {-b.x, -b.y, -b.z, -b.w};
            cosTheta = -cosTheta;
        }
        if (cosTheta > 0.9995f) { // 近似线性插值，避免除以 0
            Quat r{erm::Lerp(a.x, bb.x, t), erm::Lerp(a.y, bb.y, t), erm::Lerp(a.z, bb.z, t),
                   erm::Lerp(a.w, bb.w, t)};
            return r.Normalized();
        }
        const float theta = std::acos(Clamp(cosTheta, -1.0f, 1.0f));
        const float sinTheta = std::sin(theta);
        const float wa = std::sin((1.0f - t) * theta) / sinTheta;
        const float wb = std::sin(t * theta) / sinTheta;
        return {a.x * wa + bb.x * wb, a.y * wa + bb.y * wb, a.z * wa + bb.z * wb, a.w * wa + bb.w * wb};
    }

    static Quat Lerp(const Quat& a, const Quat& b, float t) {
        return Slerp(a, b, t);
    }

    // 分解为 UE 风格的 YXZ 欧拉角（弧度），与 FromEuler 互逆
    void ToEuler(float& yawRad, float& pitchRad, float& rollRad) const {
        const Mat4 r = ToMat4();
        // 行主序 R[r][c] 在列主序存储里是 m[c][r]
        const float sp = Clamp(-r.m[2][1], -1.0f, 1.0f);
        pitchRad = std::asin(sp);
        if (std::fabs(sp) < 0.9999f) {
            yawRad = std::atan2(r.m[2][0], r.m[2][2]);
            rollRad = std::atan2(r.m[0][1], r.m[1][1]);
        } else {
            // 万向锁：yaw 与 roll 只能确定一个
            yawRad = std::atan2(-r.m[0][2], r.m[0][0]);
            rollRad = 0.0f;
        }
    }
};

// 变换（TRS）
struct Transform {
    Vec3 position{0, 0, 0};
    Quat rotation{};
    Vec3 scale{1, 1, 1};

    Mat4 ToMatrix() const {
        const Mat4 t = Mat4::Translation(position);
        const Mat4 r = rotation.ToMat4();
        const Mat4 s = Mat4::Scale(scale);
        return t * r * s; // 先缩放、再旋转、最后平移
    }

    Vec3 Forward() const { return rotation.RotateVector({0, 0, -1}); }
    Vec3 Right() const { return rotation.RotateVector({1, 0, 0}); }
    Vec3 Up() const { return rotation.RotateVector({0, 1, 0}); }
};

// ------------------------------------------------------------------ 几何
struct Ray {
    Vec3 origin{};
    Vec3 direction{0, 0, -1};
    Vec3 At(float t) const { return origin + direction * t; }
};

struct AABB {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};

    static AABB FromCenterExtents(const Vec3& c, const Vec3& e) { return {c - e, c + e}; }
    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Extents() const { return (max - min) * 0.5f; }
    Vec3 Size() const { return max - min; }
    void Expand(const Vec3& p) { min = Min(min, p); max = Max(max, p); }
    void Expand(const AABB& b) { min = Min(min, b.min); max = Max(max, b.max); }
    void ExpandBy(float r) { min -= Vec3(r); max += Vec3(r); }
    bool Contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    float SurfaceArea() const {
        const Vec3 d = max - min;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }
};

// 射线-AABB（slab 法）；返回是否相交，tmin 为进入距离
inline bool IntersectRayAABB(const Ray& ray, const AABB& box, float& tmin, Vec3* normal = nullptr) {
    tmin = 0.0f;
    float tmax = FLT_MAX;
    int   hitAxis = -1;
    float hitSign = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = ray.direction[i];
        const float o = ray.origin[i];
        if (std::fabs(d) < kEpsilon) {
            if (o < box.min[i] || o > box.max[i]) return false;
        } else {
            const float inv = 1.0f / d;
            float t1 = (box.min[i] - o) * inv;
            float t2 = (box.max[i] - o) * inv;
            float sign = -1.0f;
            if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
            if (t1 > tmin) { tmin = t1; hitAxis = i; hitSign = sign; }
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
    }
    if (normal && hitAxis >= 0) {
        *normal = Vec3(0, 0, 0);
        (*normal)[hitAxis] = hitSign;
    }
    return true;
}

struct Sphere {
    Vec3  center{};
    float radius = 1.0f;
};

inline bool IntersectRaySphere(const Ray& ray, const Sphere& s, float& t) {
    const Vec3 oc = ray.origin - s.center;
    const float a = Dot(ray.direction, ray.direction);
    const float b = 2.0f * Dot(oc, ray.direction);
    const float c = Dot(oc, oc) - s.radius * s.radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    const float sq = std::sqrt(disc);
    float t0 = (-b - sq) / (2.0f * a);
    float t1 = (-b + sq) / (2.0f * a);
    if (t0 > t1) std::swap(t0, t1);
    if (t0 < 0.0f) t0 = t1;
    if (t0 < 0.0f) return false;
    t = t0;
    return true;
}

struct Plane {
    Vec3  normal{0, 1, 0};
    float d = 0.0f; // 平面方程 dot(n, p) + d = 0

    float DistanceTo(const Vec3& p) const { return Dot(normal, p) + d; }
    void  Normalize() {
        const float l = Length(normal);
        if (l > kEpsilon) { normal = normal / l; d /= l; }
    }
};

// 视锥体（GAMES104 第 7 讲：视锥剔除是渲染管线的第一道优化）
struct Frustum {
    Plane planes[6]; // left, right, bottom, top, near, far

    static Frustum FromViewProj(const Mat4& vp) {
        // 取矩阵的 4 个"行"（列主序下 row = (m[0][r], m[1][r], m[2][r], m[3][r])）
        auto row = [&](int r) { return Vec4(vp.m[0][r], vp.m[1][r], vp.m[2][r], vp.m[3][r]); };
        const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto   makePlane = [](const Vec4& v) {
            Plane p;
            p.normal = {v.x, v.y, v.z};
            p.d = v.w;
            p.Normalize();
            return p;
        };
        Frustum f;
        f.planes[0] = makePlane(r3 + r0); // left
        f.planes[1] = makePlane(r3 - r0); // right
        f.planes[2] = makePlane(r3 + r1); // bottom
        f.planes[3] = makePlane(r3 - r1); // top
        f.planes[4] = makePlane(r2);      // near (z >= 0)
        f.planes[5] = makePlane(r3 - r2); // far
        return f;
    }

    bool ContainsPoint(const Vec3& p) const {
        for (const auto& pl : planes)
            if (pl.DistanceTo(p) < 0.0f) return false;
        return true;
    }

    bool IntersectsAABB(const AABB& box) const {
        for (const auto& pl : planes) {
            // 选择沿法线方向最远的顶点（p-vertex），只要它在负半空间，整个盒子就在外面
            const Vec3 p{pl.normal.x >= 0.0f ? box.max.x : box.min.x,
                         pl.normal.y >= 0.0f ? box.max.y : box.min.y,
                         pl.normal.z >= 0.0f ? box.max.z : box.min.z};
            if (pl.DistanceTo(p) < 0.0f) return false;
        }
        return true;
    }
};

} // namespace erm
