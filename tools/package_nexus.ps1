# Monta o pacote para o Nexus Mods: DLL (overlay ImGui) + icones + scripts,
# no formato que o usuario extrai na pasta Mods do UE4SS.
#
#   powershell -ExecutionPolicy Bypass -File tools\package_nexus.ps1 [-Version v1.1.0]
#
# Instalacao (usuario): extrair a pasta PalBreedCalc em
#   <jogo>\Pal\Binaries\Win64\ue4ss\Mods\
# O enabled.txt dispensa editar o mods.txt.

param(
    [string]$Version = "v1.1.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$source = Join-Path $root "mod\PalBreedCalc"
$stage = Join-Path $root "mod\nexus\PalBreedCalc"
$zip = Join-Path $root ("mod\nexus\PalBreedCalc-{0}.zip" -f $Version)

if (-not (Test-Path (Join-Path $source "dlls\main.dll"))) {
    throw "main.dll nao existe -- rode antes: powershell -File mod-cpp\build.ps1"
}
if (-not (Test-Path (Join-Path $source "icons"))) {
    throw "icones ausentes -- rode antes: python tools\extract_icons.py"
}

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item (Join-Path $source "dlls") $stage -Recurse -Force
Copy-Item (Join-Path $source "icons") $stage -Recurse -Force
Copy-Item (Join-Path $source "Scripts") $stage -Recurse -Force
# atalho e tamanho da janela; a DLL recria este arquivo se ele faltar
Copy-Item (Join-Path $source "config.ini") $stage -Force
Set-Content -Path (Join-Path $stage "enabled.txt") -Value "" -Encoding ASCII

@"
-- Breeding Calculator configuration.
-- language: "auto" (follows your Steam game language), "pt-BR" or "en".
-- hotkey here is only for the pure-Lua window; the normal (DLL) window
-- reads its hotkey from ..\config.ini
return { lua_ui = "auto", language = "auto", hotkey = "F6" }
"@ | Set-Content (Join-Path $stage "Scripts\uiconfig.lua") -Encoding UTF8

@"
Breeding Calculator $Version
============================

Requirements: UE4SS (Experimental build for Palworld).

Install: extract the PalBreedCalc folder into your UE4SS "Mods" folder,
usually  <game>\Pal\Binaries\Win64\ue4ss\Mods\
The included enabled.txt activates the mod automatically.

Press F6 in game (inside a world) to open the calculator.

Two modes:
  Parents -> Child : pick a male and a female, see the egg and offspring.
  Child -> Parents : pick the child, see every pair that produces it.

Console helpers (UE4SS console):
  PalBreedCalc("Lamball", "Cattiva")  -- quick query
  PalBreedCalcCheck()                 -- verify data against a game patch
"@ | Set-Content (Join-Path $stage "README.txt") -Encoding UTF8

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip

$files = Get-ChildItem $stage -Recurse -File
Write-Host ("`npronto: {0}" -f $zip) -ForegroundColor Green
Write-Host ("{0} arquivos, {1:N1} MB (DLL + icones + scripts)" -f `
    $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB))
