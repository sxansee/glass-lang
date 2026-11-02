#pragma once
// Русское склонение по числу: 1 ошибка, 2 ошибки, 5 ошибок, 11 ошибок, 21 ошибка.
// Header-only, без зависимостей (как и Utf8.h).

#include <cstddef>
#include <string>

namespace glass {

// Какая из трёх форм подходит числу n: one — «1, 21, 101», few — «2–4, 22–24», many — остальное, в том числе «11–14».
inline const char* pluralForm(size_t n, const char* one, const char* few, const char* many) {
    const size_t last2 = n % 100;
    const size_t last = n % 10;
    if (last2 >= 11 && last2 <= 14) return many;
    if (last == 1) return one;
    if (last >= 2 && last <= 4) return few;
    return many;
}

// Число вместе со словом в нужной форме: countWithWord(3, "аргумент", "аргумента", "аргументов") == "3 аргумента".
inline std::string countWithWord(size_t n, const char* one, const char* few, const char* many) {
    return std::to_string(n) + " " + pluralForm(n, one, few, many);
}

}  // namespace glass
