# Compila o mod C++ (gera mod/PalBreedCalc/dlls/main.dll).
#
#   powershell -ExecutionPolicy Bypass -File mod-cpp\build.ps1
#
# Precisa das Build Tools do VS 2022 (cl.exe), CMake e Ninja.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root "build"

# vcvars64: sem ele o CMake nao acha cl.exe/link.exe nem os headers do SDK
$vcvars = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $vcvars) { throw "vcvars64.bat nao encontrado (instale as Build Tools do VS 2022)" }

# Importa o ambiente do MSVC para esta sessao
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

cmake -S $root -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "cmake configure falhou" }

cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "build falhou" }

Write-Host "`nok: mod/PalBreedCalc/dlls/main.dll" -ForegroundColor Green
