// ErMao Engine - 网络同步公共类型（与平台/传输库无关）
//
// 这一层只做两件事：
//   1) 定义线上协议的最小类型：输入帧、实体变换、时间；
//   2) 提供一个带边界检查的字节读写器，快照/输入都通过它编解码。
//
// 设计约束：
//   * 不 include windows.h / ENet 头文件，方便单独编译与测试；
//   * 浮点按原始 4 字节发送（先保证可读、可验证；量化是后续优化项）；
//   * 所有字段按固定小端字节序写，避免主机字节序差异。
#pragma once

#include "core/Math.h"
#include "core/Types.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace erm::net {

// 协议版本：客户端与服务端不一致时直接拒绝连接，并记录日志。
constexpr u32 kProtocolVersion = 1;
constexpr u16 kDefaultPort = 7777;
constexpr u32 kInvalidPeer = 0;
constexpr u32 kMaxPeers = 8;
constexpr u32 kSnapshotsPerSecond = 20;
constexpr u32 kInputsPerSecond = 60;
constexpr float kInterpolationDelayMs = 100.0f;

using PeerId = u32;
using NetId = u32;

// 本地输入帧：与具体玩法无关的模拟量。2D 用 moveX/moveY（XY 平面），
// 3D 用 moveX/moveY 映射到 XZ 平面；buttons 预留给跳/射击等离散动作。
struct NetInput {
    u32   sequence = 0;
    float moveX = 0.0f;
    float moveY = 0.0f;
    float yaw = 0.0f;
    u32   buttons = 0;
};

struct NetTransform {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    bool operator==(const NetTransform& other) const {
        return std::memcmp(&position, &other.position, sizeof(Vec3)) == 0 &&
               std::memcmp(&rotation, &other.rotation, sizeof(Quat)) == 0 &&
               std::memcmp(&scale, &other.scale, sizeof(Vec3)) == 0;
    }
    bool operator!=(const NetTransform& other) const { return !(*this == other); }
};

inline u64 NowMs() {
    // steady_clock 只用于测 RTT/插值，不参与游戏逻辑，不受系统时间调整影响。
    using namespace std::chrono;
    return static_cast<u64>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------- 字节写入
class BitWriter {
public:
    void Clear() { m_data.clear(); }
    const std::vector<u8>& Data() const { return m_data; }
    std::vector<u8>&       MutableData() { return m_data; }
    size_t                 Size() const { return m_data.size(); }

    void U8(u8 value) { m_data.push_back(value); }
    void U16(u16 value) {
        m_data.push_back(static_cast<u8>(value & 0xff));
        m_data.push_back(static_cast<u8>((value >> 8) & 0xff));
    }
    void U32(u32 value) {
        for (int i = 0; i < 4; ++i) m_data.push_back(static_cast<u8>((value >> (8 * i)) & 0xff));
    }
    void U64(u64 value) {
        for (int i = 0; i < 8; ++i) m_data.push_back(static_cast<u8>((value >> (8 * i)) & 0xff));
    }
    void I32(i32 value) { U32(static_cast<u32>(value)); }
    void F32(float value) {
        u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        U32(bits);
    }
    void Bool(bool value) { U8(value ? 1 : 0); }
    void String(const std::string& value) {
        const size_t length = (std::min)(value.size(), static_cast<size_t>(0xffff));
        U16(static_cast<u16>(length));
        m_data.insert(m_data.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(length));
    }
    void Bytes(const void* data, size_t size) {
        const u8* bytes = static_cast<const u8*>(data);
        m_data.insert(m_data.end(), bytes, bytes + size);
    }
    void Vec2(const Vec2& value) {
        F32(value.x);
        F32(value.y);
    }
    void Vec3(const Vec3& value) {
        F32(value.x);
        F32(value.y);
        F32(value.z);
    }
    void QuatValue(const Quat& value) {
        F32(value.x);
        F32(value.y);
        F32(value.z);
        F32(value.w);
    }
    void Transform(const NetTransform& transform) {
        Vec3(transform.position);
        QuatValue(transform.rotation);
        Vec3(transform.scale);
    }

private:
    std::vector<u8> m_data;
};

// ---------------------------------------------------------------- 字节读取
class BitReader {
public:
    BitReader(const u8* data, size_t size) : m_data(data), m_size(size) {}
    explicit BitReader(const std::vector<u8>& data) : BitReader(data.data(), data.size()) {}

    bool Ok() const { return m_ok; }
    bool Eof() const { return m_offset >= m_size; }
    size_t Offset() const { return m_offset; }
    size_t Remaining() const { return m_ok && m_offset <= m_size ? m_size - m_offset : 0; }
    const u8* Data() const { return m_data; }
    size_t    Size() const { return m_size; }

    u8 U8() {
        if (!Require(1)) return 0;
        return m_data[m_offset++];
    }
    u16 U16() {
        if (!Require(2)) return 0;
        u16 value = static_cast<u16>(m_data[m_offset]) | (static_cast<u16>(m_data[m_offset + 1]) << 8);
        m_offset += 2;
        return value;
    }
    u32 U32() {
        if (!Require(4)) return 0;
        u32 value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<u32>(m_data[m_offset + i]) << (8 * i);
        m_offset += 4;
        return value;
    }
    u64 U64() {
        if (!Require(8)) return 0;
        u64 value = 0;
        for (int i = 0; i < 8; ++i) value |= static_cast<u64>(m_data[m_offset + i]) << (8 * i);
        m_offset += 8;
        return value;
    }
    i32 I32() { return static_cast<i32>(U32()); }
    float F32() {
        const u32 bits = U32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    bool Bool() { return U8() != 0; }
    std::string String() {
        const u16 length = U16();
        if (!Require(length)) return std::string();
        std::string value(reinterpret_cast<const char*>(m_data + m_offset), length);
        m_offset += length;
        return value;
    }
    bool Bytes(void* out, size_t size) {
        if (!Require(size)) return false;
        std::memcpy(out, m_data + m_offset, size);
        m_offset += size;
        return true;
    }
    Vec2 Vec2Value() {
        Vec2 value;
        value.x = F32();
        value.y = F32();
        return value;
    }
    Vec3 Vec3Value() {
        Vec3 value;
        value.x = F32();
        value.y = F32();
        value.z = F32();
        return value;
    }
    Quat QuatValue() {
        Quat value;
        value.x = F32();
        value.y = F32();
        value.z = F32();
        value.w = F32();
        return value;
    }
    NetTransform Transform() {
        NetTransform value;
        value.position = Vec3Value();
        value.rotation = QuatValue();
        value.scale = Vec3Value();
        return value;
    }

private:
    bool Require(size_t count) {
        if (!m_ok || m_offset + count > m_size) {
            m_ok = false;
            return false;
        }
        return true;
    }

    const u8* m_data = nullptr;
    size_t    m_size = 0;
    size_t    m_offset = 0;
    bool      m_ok = true;
};

// 字符串/地址拼接的小工具（跨平台，不依赖 WinSock inet_pton）
inline bool SplitHostPort(const std::string& address, std::string& host, u16& port) {
    if (address.empty()) return false;
    host = address;
    port = kDefaultPort;
    const size_t colon = address.rfind(':');
    if (colon != std::string::npos) {
        // IPv6 字面量（[::1]:7777）暂不支持；引擎演示使用 IPv4/主机名。
        const std::string portText = address.substr(colon + 1);
        if (!portText.empty()) {
            char*       end = nullptr;
            const long  parsed = std::strtol(portText.c_str(), &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > 65535) return false;
            port = static_cast<u16>(parsed);
            host = address.substr(0, colon);
        }
    }
    return !host.empty();
}

} // namespace erm::net
