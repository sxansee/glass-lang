# Сборка и тесты

## Что нужно

- CMake 3.16 или новее и компилятор C++17: gcc или clang на Linux, MSVC (Visual Studio 2022 или новее) на Windows;
- .NET SDK 8 для тестов ядра IDE; для сборки решения `dotnet/Glass.slnx` из командной строки — SDK 9.0.200 или новее;
- Visual Studio 2022 17.14+ (или 2026) для WPF-оболочки: она собирается только на Windows.

## Всё одной командой

| Платформа | Команда |
|---|---|
| Linux, macOS, WSL | `bash scripts/test-all.sh` |
| Windows (PowerShell) | `.\scripts\test-all.ps1` |

Скрипт собирает C++-часть, прогоняет все C++-наборы тестов и .NET-тесты против настоящей `glass_capi`. При сбое он печатает код выхода и весь вывод набора, а не одну последнюю строку. Код возврата `0` — всё зелёное.

## По частям

```
cmake -S . -B build
cmake --build build --config Release
```

Получатся консольный компилятор `glassc` (запуск: `glassc программа.glass`; без аргументов — интерактивная консоль) и библиотека `glass_capi` с тестами в `build/tests`. Каждый набор тестов — отдельный исполняемый файл (`lexer_tests`, `parser_tests`, `sema_tests`, …, `language_tests`); они же запускаются через `ctest`.

.NET-тесты:

```
cd dotnet/Glass.Studio.Tests
dotnet run -c Release
```

Они ищут нативную библиотеку в `build` (и `build/Release`); другой путь задаёт переменная окружения `GLASS_CAPI_PATH`. С фильтром запускаются выбранные тесты: `dotnet run -c Release -- курс`.

## Windows и MSVC

- Исходники в UTF-8 (русские сообщения компилятора лежат в строковых литералах), поэтому для MSVC включён ключ `/utf-8`.
- C++-среда выполнения линкуется статически: `glass_capi.dll` и `glassc.exe` не требуют «Visual C++ Redistributable».
- У Windows нет RPATH, поэтому после сборки `capi_tests` CMake копирует `glass_capi.dll` рядом с тестом. Тот же приём делает проект Glass Studio: он копирует библиотеку в папку приложения.
- `glassc.exe` переключает консоль на UTF-8 на время работы, чтобы кириллица в выводе читалась.

## Glass Studio

```
dotnet build dotnet/Glass.slnx -c Release
```

Решение в формате `.slnx` объединяет четыре проекта: `Glass.Interop`, `Glass.Studio.Core`, `Glass.Studio.Tests` и `Glass.Studio` (WPF). Построить его можно и из Visual Studio (**Файл → Открыть → Проект или решение**).

## Тесты документации

Страницы в `docs` проверяются тестами в `Glass.Studio.Tests`: каждый пример ```` ```glass run ```` исполняется настоящей VM, и его вывод сравнивается с блоком ```` ```output ````; справочник `docs/reference/language-reference.md` должен совпадать со сгенерированным из `LanguageReference`; ссылки между страницами должны быть живыми. Если вы изменили язык или текст записи справочника и тест показывает расхождение, обновите файлы командой:

```
GLASS_UPDATE_DOCS=1 dotnet run -c Release -- документация
```

(в PowerShell: `$env:GLASS_UPDATE_DOCS = "1"` перед командой) и проверьте изменения глазами.

## Непрерывная проверка и релизы

На GitHub при каждом пуше работает CI (`.github/workflows/ci.yml`): Linux с gcc и Windows с MSVC, включая сборку WPF-оболочки. Как выпустить релиз — в [`RELEASING.md`](../RELEASING.md), правила для участников — в [`CONTRIBUTING.md`](../CONTRIBUTING.md).
