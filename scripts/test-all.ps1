# Собирает C++-часть и прогоняет ВСЕ тесты: одиннадцать C++-наборов и .NET-тесты ядра IDE
# (последние — против живой glass_capi.dll). Запуск из любого места: .\scripts\test-all.ps1
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')

cmake -S . -B build
if ($LASTEXITCODE -ne 0) { exit 1 }
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { exit 1 }

$failed = $false
foreach ($t in 'lexer','parser','sema','body_checker','vm','object','exception','introspection','debug','capi','language') {
    $exe = Join-Path 'build\tests\Release' "${t}_tests.exe"
    if (-not (Test-Path $exe)) { $exe = Join-Path 'build\tests' "${t}_tests.exe" }
    if (-not (Test-Path $exe)) {
        Write-Host ("{0,-14} СБОЙ: не найден {1}" -f $t, $exe)
        $failed = $true
        continue
    }

    # Вывод собираем целиком, а не одну последнюю строку: если набор не запустился или упал, он нужен для диагноза.
    # stderr склеиваем со stdout; на время вызова ослабляем 'Stop', иначе Windows PowerShell 5.1 примет
    # строку из stderr за терминирующую ошибку.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = @(& $exe 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous

    if ($code -eq 0 -and $out.Count -gt 0) {
        # Итоговая строка — «N проверок, M провалено.»; ищем её по форме (цифры, текст, цифры, текст), а не по русским
        # словам: так не зависим от кодировки консоли. Последняя строка вывода не годится: часть вывода (печать программ на
        # Glass из glass_capi.dll) сбрасывается другим буфером и может оказаться уже после итога.
        $summary = $out | Where-Object { $_ -match '^\d+\D+\d+\D+$' } | Select-Object -Last 1
        if (-not $summary) { $summary = $out | Select-Object -Last 1 }
        Write-Host ("{0,-14} {1}" -f $t, $summary)
    } else {
        # 0xC0000135 — не найдена DLL, 0xC0000005 — обращение к недоступной памяти (крах).
        $hex = '{0:X8}' -f $code
        Write-Host ("{0,-14} СБОЙ: код выхода {1} (0x{2}), строк вывода: {3}" -f $t, $code, $hex, $out.Count)
        foreach ($line in $out) { Write-Host "    $line" }
        $failed = $true
    }
}

Push-Location 'dotnet\Glass.Studio.Tests'
dotnet run -c Release
if ($LASTEXITCODE -ne 0) { $failed = $true }
Pop-Location

if ($failed) { exit 1 }
