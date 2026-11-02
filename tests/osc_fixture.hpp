#pragma once
// 测试用最小 OSC 组包器：只负责产出合法字节流，验证交给被测代码。
// 与 fgb/kit/osc.hpp 的解析口径对齐（4 字节对齐、大端、地址/类型标记补零）。

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace oscfx {

inline void PadTo4(std::string& out) {
    while (out.size() % 4 != 0) {
        out.push_back('\0');
    }
}

inline void PutU32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 24) & 0xFFu));
    out.push_back(static_cast<char>((value >> 16) & 0xFFu));
    out.push_back(static_cast<char>((value >> 8) & 0xFFu));
    out.push_back(static_cast<char>(value & 0xFFu));
}

inline void PutFloat(std::string& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    PutU32(out, bits);
}

inline void PutInt(std::string& out, std::int32_t value) {
    PutU32(out, static_cast<std::uint32_t>(value));
}

inline float NaN() {
    return std::numeric_limits<float>::quiet_NaN();
}

// 一条 OSC 消息：地址、类型标记（不含逗号前缀的原始串，如 "fff"）、浮点参数。
inline std::string MessageFloats(std::string_view address, const std::vector<float>& values) {
    std::string out;
    out.append(address);
    out.push_back('\0');
    PadTo4(out);
    out.push_back(',');
    for (std::size_t i = 0; i < values.size(); ++i) {
        out.push_back('f');
    }
    out.push_back('\0');
    PadTo4(out);
    for (const float value : values) {
        PutFloat(out, value);
    }
    return out;
}

// 带自定义类型标记与原始参数区的消息，用来构造「参数缺失 / 类型不对」的坏包。
inline std::string MessageRaw(std::string_view address, std::string_view typetags,
                              const std::string& payload) {
    std::string out;
    out.append(address);
    out.push_back('\0');
    PadTo4(out);
    out.push_back(',');
    out.append(typetags);
    out.push_back('\0');
    PadTo4(out);
    out.append(payload);
    return out;
}

// #bundle：8 字节标记（含结尾 NUL）+ 8 字节时间戳 + 各元素（4 字节长度前缀）。
inline std::string Bundle(const std::vector<std::string>& elements) {
    std::string out("#bundle\0", 8);
    for (int i = 0; i < 8; ++i) {
        out.push_back('\0');  // 时间戳 0 = 立即
    }
    for (const auto& element : elements) {
        PutU32(out, static_cast<std::uint32_t>(element.size()));
        out.append(element);
    }
    return out;
}

} // namespace oscfx
