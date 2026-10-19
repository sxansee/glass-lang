# Как участвовать в проекте

*In short (English):* run `bash scripts/test-all.sh` (Linux/macOS) or `.\scripts\test-all.ps1` (Windows) before every pull
request — all 11 C++ suites and the .NET tests must pass and the C++ build must stay warning-free (`-Wall -Wextra`).
A language change needs tests at every layer it touches and an update of `glass-grammar.md`.

Glass — учебный язык, поэтому для проекта особенно важны ясность и проверяемость: каждое изменение должно быть понятно
студенту и подтверждено тестом.

## Сборка и проверка

| Платформа | Команда |
|---|---|
| Linux / macOS / WSL | `bash scripts/test-all.sh` |
| Windows (PowerShell) | `.\scripts\test-all.ps1` |

Скрипт собирает C++-часть (CMake), прогоняет все **одиннадцать** C++-наборов (`tests/`) и .NET-тесты ядра IDE против
живой `glass_capi`. Нужны: CMake ≥ 3.16, компилятор C++17 (gcc, clang или MSVC) и .NET SDK 8. WPF-оболочка
(`dotnet/Glass.Studio`) собирается только на Windows: `dotnet build dotnet/Glass.sln -c Release`.

Перед pull request:

- все тесты зелёные;
- C++-сборка без предупреждений (в CMake для gcc/clang включены `-Wall -Wextra`);
- новое поведение покрыто тестами (см. ниже).

## Как устроен проект

Исходник проходит слои `Lexer` → `Parser` → `Sema` (декларации: наследование, контракты facet, override) →
`BodyChecker` (типы тел функций) → `Compiler` (байткод) → `VM` (со сборщиком мусора). Поверх — C API (`src/capi`),
на нём — `Glass.Interop` и `Glass.Studio.Core` (вся логика IDE без WPF) и тонкая оболочка `Glass.Studio`.
Подробнее — раздел «Структура» в `README.md`.

## Как добавить возможность языка

1. Опишите синтаксис и семантику в `glass-grammar.md`.
2. Лексер, парсер, AST — тесты в `tests/lexer_tests.cpp`, `tests/parser_tests.cpp`.
3. Правила типов — `Sema`/`BodyChecker` и `tests/sema_tests.cpp`, `tests/body_checker_tests.cpp`. Обязательны
   **отрицательные** тесты: недопустимая программа должна отвергаться с понятным сообщением по-русски.
4. Байткод и VM — опкод в `src/bytecode/Chunk.h`, кодогенерация в `Compiler.cpp`, исполнение в `VM.cpp`,
   тесты в `tests/vm_tests.cpp`.
5. Сквозные программы — `tests/language_tests.cpp` (помощники `expectOutput`, `expectError`).
6. Если изменение видно в отладчике, куче или C API — `tests/debug_tests.cpp`, `tests/capi_tests.cpp`.
7. Обновите справочник IDE (`LanguageReference`), `README.md` и `CHANGELOG.md`.

## Стиль

- C++17, UTF-8; сообщения об ошибках и комментарии — по-русски, как в остальном коде.
- Не оставляйте «молчаливых» ветвей: недопустимая конструкция должна давать сообщение об ошибке, а не тихо игнорироваться.
- .NET: включён `Nullable`; `Glass.Studio.Core` не должен зависеть от WPF — иначе его нельзя проверять тестами на Linux.

## Коммиты и pull request

- Одно дело — один PR; сообщение коммита объясняет, что изменено и зачем («Добавить …», «Исправить …»).
- Заполните шаблон PR. Если меняется поведение языка — обновите грамматику и `CHANGELOG.md`.

## Сообщения об ошибках и идеи

Используйте шаблоны issue. Для ошибки в языке приложите минимальную программу на Glass и то, что она вывела.

## Лицензия

Отправляя изменения, вы соглашаетесь, что они распространяются на условиях MIT (см. `LICENSE`).
