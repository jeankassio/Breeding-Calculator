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

## Configuração (`config.ini`)

Fica em `Mods\PalBreedCalc\config.ini`, ao lado da pasta `dlls`. Se o arquivo
não existir, o mod cria um com os padrões comentados na primeira execução.
Vale reiniciar o jogo depois de editar.

| chave | padrão | o que faz |
|---|---|---|
| `hotkey` | `F6` | tecla que abre/fecha. Aceita `F1`–`F24`, `A`–`Z`, `0`–`9`, `NUM_0`–`NUM_9`, `HOME`, `END`, `INSERT`, `DELETE`, `PAGE_UP`, `PAGE_DOWN`, `TAB`, `SPACE` |
| `ctrl` / `alt` / `shift` | `false` | exige o modificador junto (`ctrl = true` + `hotkey = H` → **Ctrl+H**) |
| `height_percent` | `60` | altura da janela em % da tela |
| `width_percent` | `62` | largura da janela em % da tela |
| `position` | `top-center` | onde ela abre: `top-center`, `center`, `top-left`, `top-right` |

A janela abre encostada no topo e centralizada, com 60% da altura da tela — mas
nunca menor que 1080×860, senão a aba *Parents → Child* não cabe inteira. Em
telas grandes (1440p, 4K) a porcentagem é que manda e a janela cresce junto.
Depois que o jogador mover ou redimensionar, a escolha dele vale até fechar o
jogo.

O `hotkey` do `config.ini` vale para a janela da DLL (a normal). A janela Lua de
fallback lê o dela em `Scripts/uiconfig.lua`.

## Estrutura

```
tools/     pipeline de extração + motor de referência em Python
data/      dados gerados a partir do jogo (pals.json, eggs.json, combi_unique.json)
mod-cpp/   código do mod (C++, ImGui) — gera mod/PalBreedCalc/dlls/main.dll
mod/PalBreedCalc/   o mod pronto para instalar na mão (dll + ícones + Lua + config.ini)
mod/workshop/       o mesmo mod empacotado para a Steam Workshop
mod/nexus/          distribuição SEM DLL para o Nexus (janela UMG em Lua puro)
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
2. Mesma espécie × mesma espécie → a própria espécie (auto-cruzamento).
3. Caso contrário, `rank alvo = floor((rankA + rankB + 1) / 2)` e vence o Pal
   com o `CombiRank` mais próximo, desempatando pelo **maior**
   `CombiDuplicatePriority`. Quem é filho de uma combinação única fica **fora**
   dessa busca: só nasce da combinação dele.

O desempate não é detalhe: quase todo `CombiRank` é múltiplo de 10, então
sempre que os pais têm "paridade" diferente o alvo cai exatamente no meio de
dois ranks vizinhos — **cerca de metade dos pares**. Que vence o maior está nos
próprios dados: as nove variantes que só saem de combinação única (Penking Lux,
Fuack Ignis, Azurobe Cryst, …) têm prioridade 571–581 em vez do padrão
`rank × 100`, ou seja, o jogo as fez perder todo empate.

Três conjuntos de Pals:

- **espécies selecionáveis** (287): todo Pal capturável pode ser escolhido como
  pai — inclui as lendárias/especiais (`IgnoreCombi`, como Lyleen, Jetragon,
  Frostallion), que **só nascem de auto-cruzamento** e por isso nunca saem de
  um cruzamento de rank;
- **pool do ovo** (260): quem compartilha um item de ovo — é a lista "sai deste
  mesmo ovo" (as `IgnoreCombi` ficam de fora);
- **pool de rank** (183): o pool do ovo menos os filhos de combinação única. É
  o único conjunto que a regra 3 pode devolver.

Os 287 são os 204 números do Paldex + 84 variantes − Astralym (#204). Três
Pals ficam de fora por motivos que vêm dos dados, não de exceções no código:

| fora da lista | por quê |
|---|---|
| Astralym (#204) | único Pal sem `ElementType1` e **sem item de ovo** — é o chefe final, não tem spawn e não entra na fazenda de reprodução |
| Boltmane, Monkey_Ice | sem número de Paldex: são Pals **não lançados** que continuam na DataTable |

A linha que representa cada tribo é escolhida **antes** de olhar o `IgnoreCombi`.
Três tribos (Mimog, Boltmane, Monkey_Ice) têm a linha normal com
`IgnoreCombi=true` e a do alfa com `false`; filtrando antes, o alfa virava o
representante da espécie — era assim que Mimog aparecia na lista sem o **#144**
e ainda entrava como resultado de cruzamento por rank.

A regra 2 é o único caminho para as lendárias e garante que qualquer Pal × ele
mesmo gera ele mesmo. O ovo é
`PalEgg_<elemento primário do filhote>_<tamanho do filhote>`, e os "Pals que
podem sair desse ovo" são os do pool que compartilham esse mesmo item.

## Conferência

```powershell
python tools/validate.py       # invariantes + combos e contagens conferidos com o jogo
python tools/validate_lua.py   # roda o Lua do mod e compara com o Python
python tools/validate_cpp.py   # roda o motor C++ em TODOS os pares e compara
python tools/breeding.py Lamball Cattiva
```

Estado atual: 260/260 auto-cruzamentos corretos, 258/258 combinações únicas
reproduzidas, 82.369/82.369 cruzamentos idênticos entre o C++ do mod e o Python
de referência, 287/287 buscas inversas conferindo com os mesmos cruzamentos
agrupados por filhote, **15/15 combos conferidos contra o jogo** e **234 pares
para Anubis**, o mesmo número do palbreeding.com.

Essas três âncoras externas (`KNOWN_COMBOS`, `EXPECTED_SPECIES` e
`ANUBIS_PAIRS`, em `tools/validate.py`) existem porque comparar os três motores
entre si prova que eles **concordam**, não que estejam **certos** — foi assim
que uma regra de desempate errada sobreviveu a 84 mil comparações. Depois de um
patch do jogo esses números mudam: reconfira nas fontes antes de atualizá-los.

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
(feche o jogo antes — o `main.dll` fica em uso). Depois é só apertar **F6** (ou
a tecla escolhida em `config.ini`).

## Distribuições — todas com a DLL

Toda distribuição publicada traz a **DLL** (overlay ImGui em C++), que é a
interface principal: rápida, leve e com as correções de estabilidade da seção
[Interface](#interface). O pacote também inclui a janela Lua (`ui_umg.lua`)
como **fallback automático** — se a DLL não carregar em alguma máquina, o F6
abre a janela Lua no lugar (`uiconfig.lua` com `lua_ui = "auto"`).

```powershell
python tools/make_thumbnail.py                                        # 525x525
powershell -ExecutionPolicy Bypass -File tools\package_workshop.ps1   # Steam Workshop
powershell -ExecutionPolicy Bypass -File tools\package_nexus.ps1      # Nexus (zip)
powershell -ExecutionPolicy Bypass -File tools\package_gamepass.ps1   # Game Pass (zip)
```

Todos partem de `mod/PalBreedCalc` (DLL + `icons/*.dds` + `Scripts/*.lua`):

- **Workshop**: `mod/workshop/PalBreedCalc` no formato do gerenciador de mods
  do jogo (`Info.json` + `thumbnail.png` + `InstallRule` tipo `Lua` com alvos
  `dlls`/`icons`/`Scripts`), com `enabled.txt` para não mexer no `mods.txt`.
  Dependência `UE4SSExperimentalPW` declarada.
- **Nexus**: `mod/nexus/PalBreedCalc-<versão>.zip`, extraído em `ue4ss\Mods\`.
- **Game Pass (WinGDK)**: `mod/gamepass/PalBreedCalc-<versão>-GamePass.zip`
  com a árvore `Pal\Binaries\WinGDK\ue4ss\Mods\PalBreedCalc` para mesclar na
  pasta do jogo (mesmo esquema do PalMiniMap-GamePass). A mesma `main.dll` roda
  no WinGDK (linka contra a `UE4SS.dll` por nome — símbolos iguais entre os
  builds Win64 e WinGDK). Sem Steam não há manifesto de idioma: quem joga em
  português troca `language = "pt-BR"` no `Scripts/uiconfig.lua`.

`uiconfig.lua` decide o F6: `"auto"` (padrão — DLL quando `dlls/main.dll`
existe, Lua caso contrário), `false` (força DLL), `true` (força Lua).
`tools/test_ui_umg.py` cobre a janela Lua fora do jogo (18 checks) para o
fallback continuar íntegro.

O mod não precisa do jogo rodando para calcular: os dados vão compilados na
DLL. A parte Lua é opcional e serve para conferir se um patch mexeu nos
números — pelo console do UE4SS:

```lua
PalBreedCalc("Lamball", "Cattiva")   -- consulta rápida
PalBreedCalcCheck()                  -- compara os dados embutidos com o jogo
```
