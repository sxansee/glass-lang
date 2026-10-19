# Собирает C++-часть и прогоняет ВСЕ тесты: одиннадцать C++-наборов и .NET-тесты ядра IDE
# (последние — против живой glass_capi.dll). Запуск из любого места: .\scripts\test-all.ps1
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')

cmake -S . -B build
cmake --build build --config Release

$failed = $false
foreach ($t in 'lexer','parser','sema','body_checker','vm','object','exception','introspection','debug','capi','language') {
    $exe = Join-Path 'build\tests\Release' "${t}_tests.exe"
    if (-not (Test-Path $exe)) { $exe = Join-Path 'build\tests' "${t}_tests.exe" }
    $out = & $exe
    Write-Host ("{0,-14} {1}" -f $t, ($out | Select-Object -Last 1))
    if ($LASTEXITCODE -ne 0) { $failed = $true }
}

Push-Location 'dotnet\Glass.Studio.Tests'
dotnet run -c Release
if ($LASTEXITCODE -ne 0) { $failed = $true }
Pop-Location

if ($failed) { exit 1 }
