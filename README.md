# Breeding Calculator — mod de Palworld

Calculadora de reprodução, aberta com **F6** dentro do jogo (DirectX 11 ou 12),
com dois modos:

- **Parents → Child**: escolha o macho e a fêmea e veja o ovo gerado, o filhote
  e todos os Pals que podem sair de um ovo igual àquele.
- **Child → Parents**: escolha o filhote e veja todos os pares que o geram
  (combinações únicas primeiro). Clicar num par o carrega no outro modo.

A interface do mod é toda em inglês; os nomes de Pals, ovos e itens seguem o
idioma do jogo — português quando o Palworld está em português (detectado pelo
idioma do app na Steam, com o idioma do Windows como reserva), inglês nos
demais casos.

Base: UE4SS 3.0.1 (já instalado em
`G:\SteamLibrary\steamapps\common\Palworld\Mods\NativeMods\UE4SS`).

## Estrutura

```
tools/     pipeline de extração + motor de referência em Python
data/      dados gerados a partir do jogo (pals.json, eggs.json, combi_unique.json)
mod-cpp/   código do mod (C++, ImGui) — gera mod/PalBreedCalc/dlls/main.dll
mod/PalBreedCalc/   o mod pronto para instalar na mão (dll + ícones + Lua)
mod/workshop/       o mesmo mod empacotado para a Steam Workshop
external/  RE-UE4SS (só os headers), ImGui, fmt e a import lib gerada
build/     cache da extração (.uasset e .json crus) — descartável
docs/      decisões de arquitetura
```

## Dados

Tudo vem do próprio jogo, do `Pal-Windows.pak`:

| DataTable | o que fornece |
|---|---|
| `DT_PalMonsterParameter` | espécie, `CombiRank`, `CombiDuplicatePriority`, `IgnoreCombi`, elemento, tamanho, chance de macho |
| `DT_PalCombiUnique` | 258 combinações especiais (par de pais → filhote fixo) |
| `DT_ItemDataTable` | itens de ovo `PalEgg_<elemento>_<tamanho>` |
| `DT_PalNameText*`, `DT_ItemNameText*` | nomes traduzidos (pt-BR, com fallback para inglês) |
| `DT_PalCharacterIconDataTable`, `DT_ItemIconDataTable` | ícones dos Pals e dos ovos |

Regenerar depois de um patch do jogo:

```powershell
python tools/extract_game_data.py   # dados  -> data/*.json, data.lua, PalData.gen.cpp
python tools/extract_icons.py       # ícones -> mod/PalBreedCalc/icons/*.dds
powershell -ExecutionPolicy Bypass -File mod-cpp\build.ps1
powershell -ExecutionPolicy Bypass -File tools\install_mod.ps1
```

Requer `repak`, `UAssetGUI` e `Palworld.usmap` (já em `D:\mods_palworld\_tools`;
dá para trocar os caminhos por variáveis de ambiente — ver topo do script).

Os ícones saem como `.dds` porque as texturas do jogo já são BC3/BC7 — o mesmo
formato que o D3D11 consome: basta reempacotar o mip 0 com um cabeçalho DDS e
não é preciso decodificador nenhum em tempo de execução.

## Regra de reprodução

Igual à do jogo (`UPalCombiMonsterParameter::FindChildCharacterID`):

1. Se o par de tribos existe em `DT_PalCombiUnique` (em qualquer ordem, e
   respeitando o gênero quando a linha exige), o filhote é o dessa linha.
2. Caso contrário, `rank alvo = floor((rankA + rankB + 1) / 2)` e vence o Pal
   com o `CombiRank` mais próximo, desempatando pelo menor
   `CombiDuplicatePriority`.

O ovo é `PalEgg_<elemento primário do filhote>_<tamanho do filhote>`, e os
"Pals que podem sair desse ovo" são todos os que compartilham esse mesmo item.

## Conferência

```powershell
python tools/validate.py       # invariantes: auto-cruzamento, combinações únicas, ovos
python tools/validate_lua.py   # roda o Lua do mod e compara com o Python
python tools/validate_cpp.py   # roda o motor C++ em TODOS os pares e compara
python tools/breeding.py Lamball Cattiva
```

Estado atual: 263 espécies no pool, 258/258 combinações únicas reproduzidas,
263/263 auto-cruzamentos corretos, 69.169/69.169 cruzamentos idênticos entre o
C++ do mod e o Python de referência, e 263/263 buscas inversas conferindo com
os mesmos cruzamentos agrupados por filhote.

Para ajustar o visual sem abrir o jogo, o `preview.exe` sobe um D3D11 próprio,
desenha exatamente a mesma janela e salva um BMP:

```powershell
mod-cpp\build\preview.exe mod\PalBreedCalc\icons saida.bmp Lamball Cattiva
mod-cpp\build\preview.exe mod\PalBreedCalc\icons saida.bmp reverse Anubis
```

## Instalação do mod

```powershell
powershell -ExecutionPolicy Bypass -File tools\install_mod.ps1
```

Copia `mod/PalBreedCalc` para `Mods\` do UE4SS e habilita no `mods.txt`
(feche o jogo antes — o `main.dll` fica em uso). Depois é só apertar **F6**.

## Publicar na Steam Workshop

```powershell
python tools/make_thumbnail.py                                   # 525x525
powershell -ExecutionPolicy Bypass -File tools\package_workshop.ps1
```

Monta `mod/workshop/PalBreedCalc` no formato que o gerenciador de mods do
próprio Palworld publica: `Info.json` + `thumbnail.png` na raiz e uma pasta por
alvo da `InstallRule`. O pacote traz um `enabled.txt`, então quem instalar pela
Workshop não precisa mexer no `mods.txt`. A dependência `UE4SSExperimentalPW`
está declarada no `Info.json`.

O mod não precisa do jogo rodando para calcular: os dados vão compilados na
DLL. A parte Lua é opcional e serve para conferir se um patch mexeu nos
números — pelo console do UE4SS:

```lua
PalBreedCalc("Lamball", "Cattiva")   -- consulta rápida
PalBreedCalcCheck()                  -- compara os dados embutidos com o jogo
```
