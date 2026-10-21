#!/usr/bin/env bash
# Собирает C++-часть и прогоняет ВСЕ тесты: одиннадцать C++-наборов и .NET-тесты ядра IDE
# (последние — против живой libglass_capi.so). Код возврата 0 — всё зелёное.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc 2>/dev/null || echo 2)"

failed=0
for t in lexer parser sema body_checker vm object exception introspection debug capi language; do
    printf '%-14s ' "$t"
    # Вывод собираем целиком, а не одну последнюю строку: если набор упал или не запустился, он нужен для диагноза.
    code=0
    out=$("./build/tests/${t}_tests" 2>&1) || code=$?
    if [ "$code" -eq 0 ] && [ -n "$out" ]; then
        printf '%s\n' "$out" | tail -n 1
    else
        echo "СБОЙ: код выхода $code, строк вывода: $(printf '%s\n' "$out" | grep -c '')"
        printf '%s\n' "$out" | sed 's/^/    /'
        failed=1
    fi
done

if command -v dotnet >/dev/null 2>&1; then
    (cd dotnet/Glass.Studio.Tests && dotnet run -c Release) || failed=1
else
    echo "dotnet не найден — .NET-тесты пропущены"
fi
exit $failed
