# Monta mod/workshop/PalBreedCalc — a pasta que o uploader de mods do Palworld
# publica na Steam Workshop.
#
#   powershell -ExecutionPolicy Bypass -File tools\package_workshop.ps1
#
# O layout espelha o do PalMiniMap (que ja e publicado): Info.json + thumbnail
# na raiz e uma pasta por alvo da InstallRule. O tipo "Lua" instala cada alvo
# em Mods\NativeMods\UE4SS\Mods\<PackageName>\, que e exatamente onde o mod
# precisa ficar (dlls, icons e Scripts juntos).
#
# enabled.txt faz o UE4SS carregar o mod sem precisar editar o mods.txt — e o
# que torna o pacote autossuficiente para quem instala pela Workshop.

param(
    [string]$Version = "v1.0.0"
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

# limpa so o conteudo instalavel; a thumbnail e gerada por tools/make_thumbnail.py
foreach ($folder in @("dlls", "icons", "Scripts")) {
    $target = Join-Path $out $folder
    if (Test-Path $target) { Remove-Item $target -Recurse -Force }
}
New-Item -ItemType Directory -Force -Path $out | Out-Null

Copy-Item (Join-Path $source "dlls") $out -Recurse -Force
Copy-Item (Join-Path $source "icons") $out -Recurse -Force
Copy-Item (Join-Path $source "Scripts") $out -Recurse -Force
Set-Content -Path (Join-Path $out "enabled.txt") -Value "" -Encoding ASCII

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
            Targets = @("./dlls", "./icons", "./Scripts", "./enabled.txt")
        }
    )
}
$info | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out "Info.json") -Encoding UTF8

if (-not (Test-Path (Join-Path $out "thumbnail.png"))) {
    Write-Warning "thumbnail.png ausente -- rode: python tools\make_thumbnail.py"
}

$files = (Get-ChildItem $out -Recurse -File)
$size = ($files | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("`npronto: {0}" -f $out) -ForegroundColor Green
Write-Host ("{0} arquivos, {1:N1} MB" -f $files.Count, $size)
Write-Host "Publique pelo gerenciador de mods do proprio Palworld apontando para essa pasta."
