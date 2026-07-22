# Decisões de arquitetura

## Como o mod é montado

Mod C++ do UE4SS (`Mods/PalBreedCalc/dlls/main.dll`) que desenha a própria
janela ImGui dentro do jogo. Foi a opção **A** das três avaliadas — as outras
eram um pak de UMG e um app externo.

```
mod-cpp/src/
  main.cpp          ciclo de vida do mod (CppUserModBase) e o atalho F6
  Overlay.cpp       hooks (swapchain, fila de comandos, WndProc) + ImGui
  RendererD3D11.cpp  desenho e texturas em DX11
  RendererD3D12.cpp  desenho e texturas em DX12
  Ui.cpp            a janela (só apresentação)
  Breeding.cpp      a regra de reprodução (sem I/O, sem UI)
  Textures.cpp      leitura dos .dds + cache (comum às duas APIs)
  Language.cpp      idioma do jogo, para escolher os nomes
  PalData.gen.cpp   dados extraídos do pak (gerado)
```

## Compilar sem a SDK do UE4SS

O caminho oficial (`UE4SSCPPTemplate`) manda compilar o UE4SS inteiro junto,
mas isso não é possível: o submódulo `UEPseudo`, com os headers da Unreal,
virou repositório privado. Em vez disso:

- o repositório `RE-UE4SS` é clonado **só pelos headers** (`external/RE-UE4SS`),
  no mesmo commit `c838a8ac` que a `UE4SS.dll` instalada reporta no log;
- o link é feito contra uma **import lib gerada da própria DLL instalada**
  (`tools/make_import_lib.py` lê a tabela de exports e monta o `.lib`);
- `mod-cpp/shim/GUI/GUI.hpp` substitui o único header da cadeia que puxaria
  `Unreal/...` (`GUI.hpp` → `LiveView.hpp`). Nada do que usamos precisa dele.

Consequência: o mod C++ **não acessa objetos da Unreal**. Os dados do jogo vêm
compilados (extraídos do pak), e quem lê as DataTables ao vivo é a parte Lua.
Se um dia for preciso ler UObject do C++, o shim tem que sair e a SDK real
entrar.

## Por que um hook próprio de swapchain

A GUI do próprio UE4SS é uma **janela separada** (`RenderMode=ExternalThread`,
GLFW/OpenGL), não um overlay — `register_tab` colocaria a calculadora dentro
da janela de debug do UE4SS, não dentro do jogo. Para a janela aparecer sobre
o jogo, `Overlay.cpp`:

1. cria um swapchain descartável só para ler a vtable de `IDXGISwapChain`
   (a vtable é compartilhada por todos os swapchains do processo);
2. troca as entradas `Present` (8) e `ResizeBuffers` (13) na vtable;
3. inicializa um contexto ImGui **próprio** (independente do UE4SS) com os
   backends Win32 + DX11 no primeiro `Present`;
4. intercepta o `WndProc` para alimentar o ImGui e engolir o input enquanto a
   janela está aberta — inclusive `WM_INPUT`, senão a câmera continua girando
   por trás da janela.

## DX11 e DX12

O Palworld roda em DX11 por padrão (`DefaultGraphicsRHI_DX11` no
`DefaultEngine.ini`), mas o jogador pode escolher DX12 — e aí quase tudo muda.
O que o mod faz:

- na instalação, cria um swapchain descartável **de cada API** e engancha as
  duas vtables (se forem a mesma, engancha uma vez só);
- no primeiro `Present`, pergunta ao swapchain qual é o device: `ID3D11Device`
  → renderizador DX11, `ID3D12Device` → DX12;
- no DX12 ainda falta a **fila de comandos**, que o swapchain não expõe. Ela é
  capturada enganchando `ID3D12CommandQueue::ExecuteCommandLists` (índice 10 na
  vtable) e guardando a primeira fila do tipo DIRECT. Enquanto ela não aparece,
  a inicialização do overlay simplesmente é adiada para o próximo frame;
- o desenho em DX12 usa um par de alocador/lista de comandos por back buffer,
  uma fence própria para não reciclar um alocador ainda em uso, e um heap de
  descritores compartilhado com o ImGui (que a partir da 1.92 aloca um
  descritor por textura, daí os callbacks `SrvDescriptorAllocFn/FreeFn`).

Os ícones são os mesmos `.dds` nos dois modos: em DX11 viram
`ID3D11ShaderResourceView`; em DX12, um recurso em heap default preenchido por
um upload heap (com o alinhamento de 256 bytes por linha que o D3D12 exige).

## Busca inversa (filhote → pares de pais)

Testar os 263×263 pares na força bruta a cada consulta seria caro dentro de um
frame, então a regra 2 vira tabela: como o filhote só depende do rank alvo,
`Engine` monta na inicialização um vetor `rank → filhote` (uns 3 mil itens) e
cada par passa a custar uma consulta O(1). A regra 1 continua sendo a varredura
das 258 linhas únicas.

`pairs_for` percorre os pares não ordenados e avalia **as duas ordens de
gênero**, porque algumas linhas de `DT_PalCombiUnique` exigem macho ou fêmea
específico — quando só uma ordem funciona, o par é marcado como
`gender_specific` e a janela mostra "genders as shown". O resultado fica em
cache até o filhote escolhido mudar, e a lista usa `ImGuiListClipper` porque um
Pal comum pode ter centenas de pares.

A conferência dessa parte não repete a lógica: `validate_cpp.py` agrupa por
filhote o mesmo despejo de 69.169 cruzamentos que já é comparado com o Python e
confere contagem total, quantos vêm de combinação única e quantos dependem do
gênero.

## Três implementações da mesma regra

`tools/breeding.py` (referência), `mod/PalBreedCalc/Scripts/breeding.lua` e
`mod-cpp/src/Breeding.cpp`. Parece redundante, mas é o que garante a
conferência automática: `validate_cpp.py` roda o motor C++ em todos os 69.169
pares possíveis e compara com o Python; `validate_lua.py` faz o mesmo com o
Lua. Qualquer divergência aparece antes de virar bug em jogo.

## Idioma

O texto do mod é sempre inglês. Os nomes de Pals, ovos e itens seguem o idioma
do jogo, e a base gerada carrega os dois (`name` em pt-BR, `name_en`).

Descobrir o idioma pela Unreal exigiria os headers da SDK, que este mod não
linka. O Palworld também não grava a escolha em `Saved/Config` — o menu de
idioma segue o idioma do app na Steam. Então `Language.cpp` lê
`steamapps/appmanifest_1623730.acf` (`"UserConfig" { "language" "brazilian" }`),
localizado a partir do caminho do executável, e cai no idioma da interface do
Windows quando o manifesto não existe (Game Pass, atalho fora da Steam).

## Empacotamento para a Workshop

`mod/PalBreedCalc` é a cópia para instalar na mão (habilitada via `mods.txt`).
`tools/package_workshop.ps1` monta `mod/workshop/PalBreedCalc` no formato do
gerenciador de mods do jogo: `Info.json` com `InstallRule`, `thumbnail.png` e
uma pasta por alvo. Os tipos aceitos (extraídos do executável) são
`LogicMods`, `Lua`, `Paks`, `PalSchema` e `UE4SS`; usamos `Lua`, que instala
cada alvo em `Mods\NativeMods\UE4SS\Mods\<PackageName>\` — foi assim que o
PalMiniMap instalou seu `Scripts/`. O `enabled.txt` faz o UE4SS carregar o mod
sem editar o `mods.txt`, o que deixa o pacote autossuficiente.

## Iterar no visual sem abrir o jogo

`mod-cpp/tests/preview.cpp` sobe um D3D11 próprio, desenha a mesma `Ui` e
salva um BMP. Abrir o Palworld a cada ajuste de layout levaria minutos; o
preview leva segundos e ainda serve de regressão visual.
