// ErMao Engine - JSON 实现
#include "core/Json.h"

#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>

namespace erm {

namespace {
const JsonValue& NullValue() {
    static const JsonValue null;
    return null;
}
} // namespace

JsonValue& JsonValue::operator[](const std::string& key) {
    if (m_type != Type::Object) {
        m_type = Type::Object;
        m_object.clear();
    }
    for (auto& kv : m_object)
        if (kv.first == key) return kv.second;
    m_object.emplace_back(key, JsonValue());
    return m_object.back().second;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (m_type == Type::Object) {
        for (const auto& kv : m_object)
            if (kv.first == key) return kv.second;
    }
    return NullValue();
}

bool JsonValue::Has(const std::string& key) const {
    if (m_type != Type::Object) return false;
    for (const auto& kv : m_object)
        if (kv.first == key) return true;
    return false;
}

void JsonValue::Set(const std::string& key, JsonValue value) {
    if (m_type != Type::Object) {
        m_type = Type::Object;
        m_object.clear();
    }
    for (auto& kv : m_object) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    }
    m_object.emplace_back(key, std::move(value));
}

void JsonValue::Push(JsonValue value) {
    if (m_type != Type::Array) {
        m_type = Type::Array;
        m_array.clear();
    }
    m_array.push_back(std::move(value));
}

size_t JsonValue::Size() const {
    if (m_type == Type::Array) return m_array.size();
    if (m_type == Type::Object) return m_object.size();
    return 0;
}

JsonValue& JsonValue::operator[](size_t index) {
    if (m_type != Type::Array) {
        m_type = Type::Array;
        m_array.clear();
    }
    if (index >= m_array.size()) m_array.resize(index + 1);
    return m_array[index];
}

const JsonValue& JsonValue::operator[](size_t index) const {
    if (m_type == Type::Array && index < m_array.size()) return m_array[index];
    return NullValue();
}

bool JsonValue::AsBool(bool def) const {
    if (m_type == Type::Bool) return m_bool;
    if (m_type == Type::Number) return m_number != 0.0;
    return def;
}

int JsonValue::AsInt(int def) const {
    if (m_type == Type::Number) return static_cast<int>(m_number);
    if (m_type == Type::Bool) return m_bool ? 1 : 0;
    return def;
}

float JsonValue::AsFloat(float def) const {
    if (m_type == Type::Number) return static_cast<float>(m_number);
    return def;
}

double JsonValue::AsNumber(double def) const {
    if (m_type == Type::Number) return m_number;
    if (m_type == Type::Bool) return m_bool ? 1.0 : 0.0;
    return def;
}

std::string JsonValue::AsString(const std::string& def) const {
    if (m_type == Type::String) return m_string;
    return def;
}

// ------------------------------------------------------------------ 输出
namespace {
void EscapeString(std::string& out, const std::string& s) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void NumberToString(std::string& out, double v) {
    char buf[64];
    if (v == static_cast<double>(static_cast<long long>(v)) && std::fabs(v) < 1e15) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        out += buf;
        return;
    }

    // 用最短可往返表示（C++17 to_chars）：
    // 场景里的数值都是 float，按 float 打印能保证 float -> 文本 -> float 完全一致，
    // 同时避免 "0.30000000000000004" 这类噪音。
#ifdef ERMAO_WEB
    // Emscripten 的 libc++ 版本对浮点 to_chars 支持因版本而异；Web 端用 snprintf
    // 保证可移植（场景数值只要求 9 位有效数字）。
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    out += buf;
#else
    const float f = static_cast<float>(v);
    std::to_chars_result res;
    if (static_cast<double>(f) == v) {
        res = std::to_chars(buf, buf + sizeof(buf) - 1, f);
    } else {
        res = std::to_chars(buf, buf + sizeof(buf) - 1, v);
    }
    if (res.ec == std::errc()) {
        *res.ptr = '\0';
        out += buf;
        return;
    }
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    out += buf;
#endif
}
} // namespace

void JsonValue::DumpTo(std::string& out, int indent, int depth) const {
    const std::string pad(static_cast<size_t>(indent) * static_cast<size_t>(depth + 1), ' ');
    const std::string padEnd(static_cast<size_t>(indent) * static_cast<size_t>(depth), ' ');
    switch (m_type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += m_bool ? "true" : "false"; break;
        case Type::Number: NumberToString(out, m_number); break;
        case Type::String: EscapeString(out, m_string); break;
        case Type::Array: {
            if (m_array.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < m_array.size(); ++i) {
                out += pad;
                m_array[i].DumpTo(out, indent, depth + 1);
                if (i + 1 < m_array.size()) out += ',';
                out += '\n';
            }
            out += padEnd + "]";
            break;
        }
        case Type::Object: {
            if (m_object.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < m_object.size(); ++i) {
                out += pad;
                EscapeString(out, m_object[i].first);
                out += ": ";
                m_object[i].second.DumpTo(out, indent, depth + 1);
                if (i + 1 < m_object.size()) out += ',';
                out += '\n';
            }
            out += padEnd + "}";
            break;
        }
    }
}

std::string JsonValue::Dump(int indent) const {
    std::string out;
    DumpTo(out, indent, 0);
    out += '\n';
    return out;
}

// ------------------------------------------------------------------ 解析
namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : m_text(text) {}

    bool Parse(JsonValue& out, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(out)) {
            error = m_error.empty() ? "unexpected end of input" : m_error;
            return false;
        }
        SkipWhitespace();
        if (m_pos < m_text.size()) {
            error = ErrorAt("trailing characters");
            return false;
        }
        return true;
    }

private:
    std::string ErrorAt(const char* what) const {
        return std::string(what) + " at offset " + std::to_string(m_pos);
    }

    void SkipWhitespace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
            } else {
                break;
            }
        }
    }

    bool Match(const char* literal) {
        const size_t len = std::strlen(literal);
        if (m_text.compare(m_pos, len, literal) != 0) return false;
        m_pos += len;
        return true;
    }

    bool ParseValue(JsonValue& out) {
        if (m_depth > 64) {
            m_error = "nesting too deep (max 64)";
            return false;
        }
        if (m_pos >= m_text.size()) {
            m_error = "unexpected end";
            return false;
        }
        ++m_depth;
        struct DepthGuard {
            int& depth;
            ~DepthGuard() { --depth; }
        } guard{m_depth};
        switch (m_text[m_pos]) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': {
                std::string s;
                if (!ParseString(s)) return false;
                out = JsonValue(std::move(s));
                return true;
            }
            case 't':
                if (Match("true")) { out = JsonValue(true); return true; }
                m_error = ErrorAt("invalid literal");
                return false;
            case 'f':
                if (Match("false")) { out = JsonValue(false); return true; }
                m_error = ErrorAt("invalid literal");
                return false;
            case 'n':
                if (Match("null")) { out = JsonValue(); return true; }
                m_error = ErrorAt("invalid literal");
                return false;
            default: return ParseNumber(out);
        }
    }

    bool ParseNumber(JsonValue& out) {
        const size_t start = m_pos;
        if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) ++m_pos;
        bool digits = false;
        while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
            ++m_pos;
            digits = true;
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) ++m_pos;
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
        }
        if (!digits) {
            m_error = ErrorAt("invalid number");
            return false;
        }
                // 场景数值都是 float：用 strtof 保证正确舍入到 float（strtod + 窄化可能差 1 ULP）
        const std::string text = m_text.substr(start, m_pos - start);
        out = JsonValue(static_cast<double>(std::strtof(text.c_str(), nullptr)));
        return true;
    }

    bool ParseString(std::string& out) {
        if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
            m_error = ErrorAt("expected string");
            return false;
        }
        ++m_pos;
        out.clear();
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_pos >= m_text.size()) break;
            const char esc = m_text[m_pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (m_pos + 4 > m_text.size()) {
                        m_error = ErrorAt("bad unicode escape");
                        return false;
                    }
                    const unsigned code = static_cast<unsigned>(
                        std::strtoul(m_text.substr(m_pos, 4).c_str(), nullptr, 16));
                    m_pos += 4;
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default:
                    m_error = ErrorAt("bad escape");
                    return false;
            }
        }
        m_error = ErrorAt("unterminated string");
        return false;
    }

    bool ParseArray(JsonValue& out) {
        ++m_pos; // '['
        out = JsonValue::MakeArray();
        SkipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }
        while (true) {
            JsonValue element;
            SkipWhitespace();
            if (!ParseValue(element)) return false;
            out.Push(std::move(element));
            SkipWhitespace();
            if (m_pos >= m_text.size()) {
                m_error = ErrorAt("unterminated array");
                return false;
            }
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }
            m_error = ErrorAt("expected ',' or ']'");
            return false;
        }
    }

    bool ParseObject(JsonValue& out) {
        ++m_pos; // '{'
        out = JsonValue::MakeObject();
        SkipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }
        while (true) {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') {
                m_error = ErrorAt("expected ':'");
                return false;
            }
            ++m_pos;
            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.Set(key, std::move(value));
            SkipWhitespace();
            if (m_pos >= m_text.size()) {
                m_error = ErrorAt("unterminated object");
                return false;
            }
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            m_error = ErrorAt("expected ',' or '}'");
            return false;
        }
    }

    const std::string& m_text;
    size_t             m_pos = 0;
    int                m_depth = 0;
    std::string        m_error;
};

} // namespace

JsonValue JsonValue::Parse(const std::string& text, std::string* error) {
    JsonValue    value;
    JsonParser   parser(text);
    std::string  err;
    if (!parser.Parse(value, err)) {
        if (error) *error = err;
        return JsonValue();
    }
    return value;
}

} // namespace erm
