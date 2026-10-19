# Как сделать релиз

Релиз собирает и публикует GitHub Actions (`.github/workflows/release.yml`) по тегу `vX.Y.Z`.
Руками остаётся подготовить версию и поставить тег.

## Что получится

Один архив `GlassStudio-X.Y.Z-win-x64.zip` в GitHub Release:

```
GlassStudio-X.Y.Z-win-x64/
├── GlassStudio/          — Glass Studio, самодостаточная сборка (.NET не нужен) + glass_capi.dll
├── glassc.exe            — консольный компилятор/REPL
├── examples/             — примеры на Glass
├── docs/                 — документация (если папка есть)
└── LICENSE, README.md, CHANGELOG.md, THIRD-PARTY-NOTICES.md, glass-grammar.md
```

C++-часть собирается со статической средой выполнения, поэтому «Visual C++ Redistributable» пользователю не нужен.

## Шаги

1. **Убедитесь, что `main` зелёный** (вкладка Actions) и `scripts/test-all.sh` / `scripts\test-all.ps1` проходит локально.
2. **Поднимите версию** в двух местах (тест «репозиторий: версия едина» проверит, что они совпадают):
   - `dotnet/Directory.Build.props` — `<Version>X.Y.Z</Version>`;
   - `CMakeLists.txt` — `project(Glass VERSION X.Y.Z ...)` (без суффикса: для `1.2.0-rc.1` здесь `1.2.0`).
3. **`CHANGELOG.md`**: заголовок раздела — `## [X.Y.Z] — ГГГГ-ММ-ДД`. Его текст станет описанием релиза.
4. **Закоммитьте и запушьте** в `main` (`Релиз X.Y.Z`), дождитесь зелёного CI.
5. **Поставьте тег и отправьте его:**
   ```
   git tag vX.Y.Z
   git push origin vX.Y.Z
   ```
6. **Дождитесь workflow «Release»** (Actions). Он сам: проверит, что тег совпадает с версией, соберёт C++ (MSVC) и прогонит все тесты,
   опубликует Glass Studio (win-x64, self-contained), соберёт zip, выложит его как артефакт и создаст GitHub Release.
   Версии с дефисом (`1.0.0-rc.1`) помечаются как pre-release.

## Проверка артефакта

Скачайте zip из Release, распакуйте на чистой машине (или в Windows Sandbox) и проверьте:

- `GlassStudio\GlassStudio.exe` запускается; открывается пример, программа выполняется, работает отладчик и схема кучи;
- `glassc.exe examples\shapes.glass` печатает результат (кириллица читается);
- в архиве есть `LICENSE` и `THIRD-PARTY-NOTICES.md`.

## Пробная сборка без релиза

Actions → **Release** → *Run workflow*: соберёт тот же zip и приложит его как артефакт запуска, но Release не создаст.

## Если что-то пошло не так

Удалите релиз на GitHub и тег, исправьте, повторите с шага 4:

```
git push --delete origin vX.Y.Z
git tag -d vX.Y.Z
```
