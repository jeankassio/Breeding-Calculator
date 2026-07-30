# Monta mod/workshop/PalBreedCalc — a pasta que o uploader de mods do Palworld
# publica na Steam Workshop. Traz a DLL (overlay ImGui, a interface principal),
# os icones .dds e os scripts Lua.
#
#   powershell -ExecutionPolicy Bypass -File tools\package_workshop.ps1
#
# Layout espelha o do PalMiniMap: Info.json + thumbnail na raiz e uma pasta por
# alvo da InstallRule. O tipo "Lua" instala cada alvo em
# Mods\NativeMods\UE4SS\Mods\<PackageName>\; enabled.txt dispensa o mods.txt.

param(
    [string]$Version = "v1.1.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$source = Join-Path $root "mod\PalBreedCalc"
$out = Join-Path $root "mod\workshop\PalBreedCalc"

if (-not (Test-Path (Join-Path $source "dlls\main.dll"))) {
    throw "main.dll nao existe -- rode antes: powershell -File mod-cpp\build.ps1"
}
if (-not (Test-Path (Join-Path $source "icons"))) {
    throw "icones ausentes -- rode antes: python tools\extract_icons.py"
}

foreach ($folder in @("dlls", "icons", "Scripts")) {
    $target = Join-Path $out $folder
    if (Test-Path $target) { Remove-Item $target -Recurse -Force }
}
New-Item -ItemType Directory -Force -Path $out | Out-Null

Copy-Item (Join-Path $source "dlls") $out -Recurse -Force
Copy-Item (Join-Path $source "icons") $out -Recurse -Force
Copy-Item (Join-Path $source "Scripts") $out -Recurse -Force
# atalho e tamanho da janela; a DLL recria este arquivo se ele faltar
Copy-Item (Join-Path $source "config.ini") $out -Force
Set-Content -Path (Join-Path $out "enabled.txt") -Value "" -Encoding ASCII

# "auto": usa a DLL (esta presente); cai na janela Lua so se ela faltar
@"
-- Breeding Calculator configuration (see the repo for details).
-- hotkey here is only for the pure-Lua window; the normal (DLL) window
-- reads its hotkey from ..\config.ini
return { lua_ui = "auto", language = "auto", hotkey = "F6" }
"@ | Set-Content (Join-Path $out "Scripts\uiconfig.lua") -Encoding UTF8

$info = [ordered]@{
    ModName      = "Breeding Calculator"
    PackageName  = "PalBreedCalc"
    Thumbnail    = "thumbnail.png"
    Version      = $Version
    MinRevision  = 82182
    Author       = "Jean Kassio"
    Dependencies = @("UE4SSExperimentalPW")
    Tags         = @("UE4SS", "User Interface")
    InstallRule  = @(
        [ordered]@{
            Type    = "Lua"
            Targets = @("./dlls", "./icons", "./Scripts", "./config.ini", "./enabled.txt")
        }
    )
}
$info | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out "Info.json") -Encoding UTF8

if (-not (Test-Path (Join-Path $out "thumbnail.png"))) {
    Write-Warning "thumbnail.png ausente -- rode: python tools\make_thumbnail.py"
}

$files = Get-ChildItem $out -Recurse -File
$size = ($files | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("`npronto: {0}" -f $out) -ForegroundColor Green
Write-Host ("{0} arquivos, {1:N1} MB (DLL + icones + scripts)" -f $files.Count, $size)
Write-Host "Publique pelo gerenciador de mods do proprio Palworld apontando para essa pasta."
