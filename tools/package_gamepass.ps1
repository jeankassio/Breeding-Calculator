# Monta o pacote para Game Pass / Microsoft Store (WinGDK): zip com a arvore
# Pal\Binaries\WinGDK\ue4ss\Mods\PalBreedCalc que o usuario mescla na pasta do
# jogo. Traz a DLL, os icones e os scripts (mesmo esquema do PalMiniMap-GamePass).
#
# A mesma main.dll roda no WinGDK: ela linka contra a UE4SS.dll por nome (os
# simbolos sao os mesmos entre os builds Win64 e WinGDK do mesmo UE4SS) e o
# hook de swapchain e generico. Se em alguma maquina a DLL nao carregar, o mod
# cai sozinho na janela Lua (uiconfig lua_ui = "auto").
#
#   powershell -ExecutionPolicy Bypass -File tools\package_gamepass.ps1 [-Version v1.1.0]

param(
    [string]$Version = "v1.1.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$source = Join-Path $root "mod\PalBreedCalc"
$stage = Join-Path $root "mod\gamepass\PalBreedCalc-GamePass"
$modDir = Join-Path $stage "Pal\Binaries\WinGDK\ue4ss\Mods\PalBreedCalc"
$zip = Join-Path $root ("mod\gamepass\PalBreedCalc-{0}-GamePass.zip" -f $Version)

if (-not (Test-Path (Join-Path $source "dlls\main.dll"))) {
    throw "main.dll nao existe -- rode antes: powershell -File mod-cpp\build.ps1"
}
if (-not (Test-Path (Join-Path $source "icons"))) {
    throw "icones ausentes -- rode antes: python tools\extract_icons.py"
}

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $modDir | Out-Null

Copy-Item (Join-Path $source "dlls") $modDir -Recurse -Force
Copy-Item (Join-Path $source "icons") $modDir -Recurse -Force
Copy-Item (Join-Path $source "Scripts") $modDir -Recurse -Force
# atalho e tamanho da janela; a DLL recria este arquivo se ele faltar
Copy-Item (Join-Path $source "config.ini") $modDir -Force
Set-Content -Path (Join-Path $modDir "enabled.txt") -Value "" -Encoding ASCII

# Game Pass nao tem o manifesto da Steam: idioma "auto" cai em ingles.
@"
-- Breeding Calculator configuration.
-- language: "auto" (English on Game Pass), "pt-BR" or "en".
-- hotkey here is only for the pure-Lua window; the normal (DLL) window
-- reads its hotkey from ..\config.ini
return { lua_ui = "auto", language = "auto", hotkey = "F6" }
"@ | Set-Content (Join-Path $modDir "Scripts\uiconfig.lua") -Encoding UTF8

@"
Breeding Calculator $Version  (GAME PASS / WinGDK build)
========================================================

*** This is the GAME PASS / Microsoft Store (WinGDK) build. ***
*** Steam users: use the Workshop or the normal download.    ***

REQUIREMENTS
- Palworld installed via Xbox app / Game Pass (the WinGDK build).
- UE4SS for Game Pass (WinGDK) already installed in:
    ...\Palworld\Content\Pal\Binaries\WinGDK
  (use the UE4SS release that supports Game Pass / WinGDK).

INSTALLATION
1. Open your Game Pass Palworld install folder, by default:
     C:\XboxGames\Palworld\Content\
   (the folder that CONTAINS the "Pal" folder)
2. Copy the "Pal" folder from this archive into it and let it MERGE.
   Nothing is overwritten - it only adds files.
3. Press F6 in game (inside a world) to open the calculator.

Playing in Portuguese? Game Pass has no Steam language manifest, so
edit  ...\Mods\PalBreedCalc\Scripts\uiconfig.lua  and set:
    language = "pt-BR"
"@ | Set-Content (Join-Path $stage "README.txt") -Encoding UTF8

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

$files = Get-ChildItem $stage -Recurse -File
Write-Host ("`npronto: {0}" -f $zip) -ForegroundColor Green
Write-Host ("{0} arquivos, {1:N1} MB (DLL + icones + scripts)" -f `
    $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB))
