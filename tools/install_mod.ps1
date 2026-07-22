# Instala o mod na pasta de Mods do UE4SS e o habilita no mods.txt.
#
#   powershell -ExecutionPolicy Bypass -File tools\install_mod.ps1
#
# Use -Uninstall para remover.

param(
    [string]$UE4SS = "G:\SteamLibrary\steamapps\common\Palworld\Mods\NativeMods\UE4SS",
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$modName = "PalBreedCalc"
$source = Join-Path $root "mod\$modName"
$modsDir = Join-Path $UE4SS "Mods"
$target = Join-Path $modsDir $modName
$modsTxt = Join-Path $modsDir "mods.txt"

if (-not (Test-Path $modsDir)) { throw "pasta de Mods do UE4SS nao encontrada: $modsDir" }

if ($Uninstall) {
    if (Test-Path $target) { Remove-Item $target -Recurse -Force }
    (Get-Content $modsTxt) | Where-Object { $_ -notmatch "^\s*$modName\s*:" } |
        Set-Content $modsTxt -Encoding UTF8
    Write-Host "removido." -ForegroundColor Green
    exit 0
}

if (-not (Test-Path (Join-Path $source "dlls\main.dll"))) {
    throw "main.dll nao existe -- rode antes: powershell -File mod-cpp\build.ps1"
}

# O jogo trava o main.dll enquanto roda
$running = Get-Process -Name "Palworld-Win64-Shipping" -ErrorAction SilentlyContinue
if ($running) { throw "feche o Palworld antes de instalar (o main.dll fica em uso)" }

New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item (Join-Path $source "*") $target -Recurse -Force
Write-Host "copiado para $target"

# mods.txt: a entrada precisa vir antes do bloco de keybinds embutido
$lines = @(Get-Content $modsTxt)
if ($lines -notmatch "^\s*$modName\s*:") {
    $marker = ($lines | Select-String -Pattern "^; Built-in keybinds" | Select-Object -First 1)
    if ($marker) {
        $index = $marker.LineNumber - 1
        $lines = $lines[0..($index - 1)] + "$modName : 1" + $lines[$index..($lines.Count - 1)]
    } else {
        $lines += "$modName : 1"
    }
    $lines | Set-Content $modsTxt -Encoding UTF8
    Write-Host "habilitado no mods.txt"
} else {
    Write-Host "ja estava no mods.txt"
}

Write-Host "`npronto. Abra o jogo e aperte F6." -ForegroundColor Green
