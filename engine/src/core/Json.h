// ErMao Engine - 手写 JSON（GAMES104 第 3/13 讲：场景序列化的基础）
//
// 没有第三方依赖，支持 对象/数组/数字/字符串/布尔/null 与 // 注释；
// 解析失败会返回错误位置，Dump() 输出带缩进的可读文本（适合 diff 与手改）。
#pragma once

#include "core/Types.h"

namespace erm {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;
    JsonValue(bool v) : m_type(Type::Bool), m_bool(v) {}
    JsonValue(int v) : m_type(Type::Number), m_number(v) {}
    JsonValue(unsigned v) : m_type(Type::Number), m_number(v) {}
    JsonValue(float v) : m_type(Type::Number), m_number(v) {}
    JsonValue(double v) : m_type(Type::Number), m_number(v) {}
    JsonValue(const char* v) : m_type(Type::String), m_string(v) {}
    JsonValue(std::string v) : m_type(Type::String), m_string(std::move(v)) {}

    static JsonValue MakeArray() {
        JsonValue v;
        v.m_type = Type::Array;
        return v;
    }
    static JsonValue MakeObject() {
        JsonValue v;
        v.m_type = Type::Object;
        return v;
    }

    Type GetType() const { return m_type; }
    bool IsNull() const { return m_type == Type::Null; }
    bool IsBool() const { return m_type == Type::Bool; }
    bool IsNumber() const { return m_type == Type::Number; }
    bool IsString() const { return m_type == Type::String; }
    bool IsArray() const { return m_type == Type::Array; }
    bool IsObject() const { return m_type == Type::Object; }

    // 对象访问（不存在时返回静态 null 值，避免崩溃）
    JsonValue&       operator[](const std::string& key);
    const JsonValue& operator[](const std::string& key) const;
    bool             Has(const std::string& key) const;
    void             Set(const std::string& key, JsonValue value);
    const std::vector<std::pair<std::string, JsonValue>>& Members() const { return m_object; }

    // 数组访问
    void                          Push(JsonValue value);
    size_t                        Size() const;
    JsonValue&                    operator[](size_t index);
    const JsonValue&              operator[](size_t index) const;
    const std::vector<JsonValue>& Elements() const { return m_array; }

    // 取值（带默认值，类型不符时返回默认值）
    bool        AsBool(bool def = false) const;
    int         AsInt(int def = 0) const;
    float       AsFloat(float def = 0.0f) const;
    double      AsNumber(double def = 0.0) const;
    std::string AsString(const std::string& def = "") const;

    std::string      Dump(int indent = 2) const;
    static JsonValue Parse(const std::string& text, std::string* error = nullptr);

private:
    void DumpTo(std::string& out, int indent, int depth) const;

    Type        m_type = Type::Null;
    bool        m_bool = false;
    double      m_number = 0.0;
    std::string m_string;
    std::vector<JsonValue>                        m_array;
    std::vector<std::pair<std::string, JsonValue>> m_object;
};

} // namespace erm
