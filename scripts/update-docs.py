#!/usr/bin/env python3
"""Обновляет блоки ```output у примеров ```glass error в docs/**/*.md по настоящему выводу glassc.

Зачем. Тест «документация: примеры исполняются» проверяет примеры ```glass run и сам умеет переписывать их
вывод (GLASS_UPDATE_DOCS=1 dotnet run -c Release -- документация). Для примеров ```glass error так нельзя:
в блоке записан полный вывод glassc (заголовок со счётчиком и все сообщения), а тест требует лишь, чтобы
сообщение из C API в нём содержалось. Этот скрипт запускает glassc на каждом таком примере и записывает
его stderr в блок — не нужно копировать вывод вручную.

Запуск (после сборки, из корня репозитория):
    python3 scripts/update-docs.py            # переписать блоки, у которых вывод изменился
    python3 scripts/update-docs.py --check    # только проверить: код выхода 1, если какой-то блок устарел

glassc ищется в build/glassc, build/Release/glassc.exe, build/Debug/glassc.exe, out/build/...; нестандартное
место — аргумент --glassc или переменная окружения GLASSC.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")

# Тот же шаблон, что в DocsTests.cs: пример glass error и сразу за ним блок output.
EXAMPLE = re.compile(r"```glass error\n(?P<code>.*?)```\n\s*```output\n(?P<out>.*?)```", re.DOTALL)

CANDIDATES = [
    "build/glassc",
    "build/Release/glassc.exe",
    "build/Debug/glassc.exe",
    "out/build/Release/glassc.exe",
    "out/build/x64-Release/glassc.exe",
    "out/build/x64-Debug/glassc.exe",
]


def find_glassc(explicit):
    for path in [explicit, os.environ.get("GLASSC")] + [os.path.join(ROOT, c) for c in CANDIDATES]:
        if path and os.path.isfile(path):
            return path
    sys.exit("glassc не найден: соберите проект (cmake --build build) или укажите --glassc / переменную GLASSC")


def compile_output(glassc, code):
    """Вывод glassc на программе: для примера glass error это сообщения компилятора (stderr)."""
    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, "example.glass")
        with open(source, "w", encoding="utf-8", newline="\n") as f:
            f.write(code)
        run = subprocess.run([glassc, source], capture_output=True, text=True, encoding="utf-8", timeout=30)
    if run.returncode == 0:
        sys.exit("пример glass error скомпилировался без ошибок — он обязан не компилироваться:\n" + code)
    return run.stderr.replace("\r\n", "\n").rstrip("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="не писать файлы, только сообщить об устаревших блоках")
    parser.add_argument("--glassc", help="путь к glassc")
    args = parser.parse_args()
    glassc = find_glassc(args.glassc)

    stale = 0
    total = 0
    for folder, _, names in sorted(os.walk(DOCS)):
        for name in sorted(names):
            if not name.endswith(".md"):
                continue
            path = os.path.join(folder, name)
            with open(path, encoding="utf-8", newline="") as f:
                text = f.read().replace("\r\n", "\n")

            def replace(match):
                nonlocal stale, total
                total += 1
                actual = compile_output(glassc, match.group("code"))
                if match.group("out").rstrip("\n") == actual:
                    return match.group(0)
                stale += 1
                print("устарел блок в %s:\n  было:   %s\n  стало:  %s" % (
                    os.path.relpath(path, ROOT),
                    match.group("out").rstrip("\n").replace("\n", "\n          "),
                    actual.replace("\n", "\n          ")))
                return "```glass error\n" + match.group("code") + "```\n\n```output\n" + actual + "\n```"

            updated = EXAMPLE.sub(replace, text)
            if updated != text and not args.check:
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(updated)

    print("примеров glass error: %d, устаревших блоков: %d" % (total, stale))
    if args.check and stale:
        sys.exit(1)


if __name__ == "__main__":
    main()
