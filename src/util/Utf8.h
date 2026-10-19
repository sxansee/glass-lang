#pragma once
// Строки Glass хранятся как UTF-8, а индексы, длина и срезы считаются в СИМВОЛАХ (кодовых
// точках), а не в байтах: "Привет"[0] — это "П", а не половина буквы, len("Привет") == 6.
// Всё здесь — header-only, без зависимостей.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace glass {
namespace utf8 {

inline bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }

inline bool isAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

// Число символов.
inline size_t length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if (!isContinuation(c)) n++;
    }
    return n;
}

// Байтовые смещения начала каждого символа + конец строки (size()): символ i занимает
// байты [b[i], b[i+1]).
inline std::vector<size_t> boundaries(const std::string& s) {
    std::vector<size_t> b;
    b.reserve(s.size() + 1);
    for (size_t i = 0; i < s.size(); i++) {
        if (!isContinuation(static_cast<unsigned char>(s[i]))) b.push_back(i);
    }
    b.push_back(s.size());
    return b;
}

// Номер символа, с которого начинается байт byteOffset (для indexOf).
inline size_t charIndexOfByte(const std::string& s, size_t byteOffset) {
    size_t n = 0;
    for (size_t i = 0; i < byteOffset && i < s.size(); i++) {
        if (!isContinuation(static_cast<unsigned char>(s[i]))) n++;
    }
    return n;
}

inline uint32_t decode(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp;
    int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { cp = c; extra = 0; }
    i++;
    for (int k = 0; k < extra && i < s.size() && isContinuation(static_cast<unsigned char>(s[i])); k++, i++) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    }
    return cp;
}

inline void encode(uint32_t cp, std::string& out) {
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

// Регистр для латиницы, Latin-1, греческого и кириллицы (русский, украинский, белорусский...).
inline uint32_t toUpper(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;     // Latin-1: à-þ
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;  // греческие строчные
    if (c == 0x3C2) return 0x3A3;                               // конечная сигма
    if (c >= 0x430 && c <= 0x44F) return c - 32;                // а-я
    if (c >= 0x450 && c <= 0x45F) return c - 80;                // ѐ-џ (в т.ч. ё)
    if (c >= 0x490 && c <= 0x4BF && (c & 1)) return c - 1;      // ґ и др. парные буквы
    return c;
}

inline uint32_t toLower(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    if (c >= 0x490 && c <= 0x4BF && !(c & 1)) return c + 1;
    return c;
}

inline std::string mapCase(const std::string& s, bool upper) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = decode(s, i);
        encode(upper ? toUpper(cp) : toLower(cp), out);
    }
    return out;
}

}  // namespace utf8
}  // namespace glass
