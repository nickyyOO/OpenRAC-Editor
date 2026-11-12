// SPDX-License-Identifier: MIT
// A small JSON reader for .cutscene files: enough for what the editor writes, no dependencies.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cutscene::json {

// One JSON value. A missing key reads as null, so lookups chain without checks.
class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind() const { return m_kind; }
    bool is_null() const { return m_kind == Kind::Null; }
    bool is_number() const { return m_kind == Kind::Number; }
    bool is_string() const { return m_kind == Kind::String; }
    bool is_array() const { return m_kind == Kind::Array; }
    bool is_object() const { return m_kind == Kind::Object; }
    bool is_bool() const { return m_kind == Kind::Bool; }

    double number(double fallback = 0.0) const { return m_kind == Kind::Number ? m_number : fallback; }
    int integer(int fallback = 0) const {
        return m_kind == Kind::Number ? static_cast<int>(m_number) : fallback;
    }
    bool boolean(bool fallback = false) const { return m_kind == Kind::Bool ? m_bool : fallback; }
    const std::string& string() const { return m_string; }
    std::string string_or(const std::string& fallback) const {
        return m_kind == Kind::String ? m_string : fallback;
    }

    const std::vector<Value>& items() const { return m_items; }
    std::size_t size() const { return m_items.size(); }
    const Value& operator[](std::size_t i) const { return i < m_items.size() ? m_items[i] : null(); }
    const Value& operator[](int i) const { return i >= 0 ? (*this)[static_cast<std::size_t>(i)] : null(); }

    const std::map<std::string, Value>& members() const { return m_members; }
    bool has(const std::string& key) const { return m_members.count(key) != 0; }
    const Value& operator[](const std::string& key) const {
        auto it = m_members.find(key);
        return it == m_members.end() ? null() : it->second;
    }
    const Value& operator[](const char* key) const { return (*this)[std::string(key)]; }

    static const Value& null() {
        static const Value v;
        return v;
    }

    // Reads `text`; on a mistake returns null and sets `error`.
    static Value parse(std::string_view text, std::string& error) {
        Reader r{text, 0, error};
        Value v = r.value(0);
        r.space();
        if (error.empty() && r.pos != text.size()) {
            r.fail("text after the value");
        }
        return error.empty() ? v : Value();
    }

private:
    struct Reader {
        std::string_view text;
        std::size_t pos;
        std::string& error;

        void fail(const char* what) {
            if (error.empty()) {
                error = std::string(what) + " at byte " + std::to_string(pos);
            }
        }
        void space() {
            while (pos < text.size()) {
                const char c = text[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    ++pos;
                } else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/') {
                    // Comments are not JSON, but the format's examples use them: skip them.
                    while (pos < text.size() && text[pos] != '\n') ++pos;
                } else {
                    break;
                }
            }
        }
        bool take(char c) {
            space();
            if (pos < text.size() && text[pos] == c) {
                ++pos;
                return true;
            }
            return false;
        }
        static void utf8(std::string& out, std::uint32_t cp) {
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
        std::uint32_t hex4() {
            if (pos + 4 > text.size()) {
                fail("short \\u escape");
                return 0;
            }
            const std::string digits(text.substr(pos, 4));
            pos += 4;
            return static_cast<std::uint32_t>(std::strtoul(digits.c_str(), nullptr, 16));
        }
        std::string str() {
            std::string out;
            ++pos;  // the opening quote
            while (pos < text.size() && text[pos] != '"') {
                char c = text[pos++];
                if (c != '\\') {
                    out += c;
                    continue;
                }
                if (pos >= text.size()) break;
                c = text[pos++];
                switch (c) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        std::uint32_t cp = hex4();
                        if (cp >= 0xD800 && cp < 0xDC00 && pos + 1 < text.size() && text[pos] == '\\'
                            && text[pos + 1] == 'u') {
                            pos += 2;
                            const std::uint32_t low = hex4();
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        utf8(out, cp);
                        break;
                    }
                    default: out += c; break;
                }
            }
            if (pos >= text.size()) {
                fail("a string without its end");
            } else {
                ++pos;
            }
            return out;
        }
        Value value(int depth) {
            Value v;
            if (depth > 64) {
                fail("too deep");
                return v;
            }
            space();
            if (pos >= text.size()) {
                fail("no value");
                return v;
            }
            const char c = text[pos];
            if (c == '{') {
                ++pos;
                v.m_kind = Kind::Object;
                if (take('}')) return v;
                do {
                    space();
                    if (pos >= text.size() || text[pos] != '"') {
                        fail("a key");
                        return v;
                    }
                    std::string key = str();
                    if (!take(':')) {
                        fail("':'");
                        return v;
                    }
                    v.m_members[key] = value(depth + 1);
                    if (!error.empty()) return v;
                } while (take(','));
                if (!take('}')) fail("'}'");
            } else if (c == '[') {
                ++pos;
                v.m_kind = Kind::Array;
                if (take(']')) return v;
                do {
                    v.m_items.push_back(value(depth + 1));
                    if (!error.empty()) return v;
                } while (take(','));
                if (!take(']')) fail("']'");
            } else if (c == '"') {
                v.m_kind = Kind::String;
                v.m_string = str();
            } else if (text.substr(pos, 4) == "true") {
                pos += 4;
                v.m_kind = Kind::Bool;
                v.m_bool = true;
            } else if (text.substr(pos, 5) == "false") {
                pos += 5;
                v.m_kind = Kind::Bool;
            } else if (text.substr(pos, 4) == "null") {
                pos += 4;
            } else {
                const std::string rest(text.substr(pos, 40));
                char* end = nullptr;
                const double d = std::strtod(rest.c_str(), &end);
                if (end == rest.c_str()) {
                    fail("a value");
                    return v;
                }
                pos += static_cast<std::size_t>(end - rest.c_str());
                v.m_kind = Kind::Number;
                v.m_number = d;
            }
            return v;
        }
    };

    Kind m_kind = Kind::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<Value> m_items;
    std::map<std::string, Value> m_members;
};

}  // namespace cutscene::json
