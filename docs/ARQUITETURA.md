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
  Config.cpp        config.ini do usuário (atalho e tamanho da janela)
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

## Espécies selecionáveis × pool de resultados

`IgnoreCombi` (em `DT_PalMonsterParameter`) marca as lendárias/especiais
(Lyleen, Jetragon, Frostallion, ...): elas **nunca saem de um cruzamento de
rank**, mas continuam sendo pais válidos e se auto-cruzam. Por isso o motor tem
dois conjuntos, não um:

- `species` (290) — uma linha canônica por tribo, incluindo as `IgnoreCombi`
  capturáveis (zukan > 0, não-alfa). É a lista da UI e o conjunto de pais da
  busca reversa. **Todo Pal capturável é selecionável.**
- `pool` (263) — `species` menos as `IgnoreCombi`. É o alvo da busca por
  `CombiRank` (regra 3) e a base das listas de ovo.

A regra 2 (mesma espécie → a própria espécie) é o que faz as lendárias
nascerem: a maioria também tem uma linha self em `DT_PalCombiUnique` (pega pela
regra 1), mas as mais novas (Panthalus, Astralym) não têm, e a regra 2 as
cobre. Sem separar os dois conjuntos, Lyleen sumia da lista (o bug relatado):
o pool antigo servia de lista *e* de resultado ao mesmo tempo e excluía tudo
que era `IgnoreCombi`. O flag `selectable` é pré-calculado por
`gen_cpp_data.py` (o C++ não recomputa nada em runtime).

## O desempate do rank (a causa dos resultados errados)

Relataram cruzamentos que davam outro Pal no jogo. Eram duas regras erradas, e
as duas ficavam invisíveis para os testes existentes — porque os três motores
concordavam entre si, todos errados do mesmo jeito.

**1. O desempate estava invertido.** Quando dois Pals ficam à mesma distância do
rank alvo, vence o **maior** `CombiDuplicatePriority`, não o menor. Isso não é
um caso raro: quase todo `CombiRank` é múltiplo de 10, então sempre que os pais
têm paridade diferente o alvo cai exatamente no meio de dois ranks vizinhos —
**45,9% dos pares**. A prova está nos próprios dados: `CombiDuplicatePriority`
é `CombiRank × 100` para 254 dos 263 Pals do pool, e as **nove exceções** têm
valores minúsculos (571 a 581):

```
BlueDragon_Ice        Azurobe Cryst      rank 1220   priority   578
CaptainPenguin_Black  Penking Lux        rank 1850   priority   573
BluePlatypus_Fire     Fuack Ignis        rank 2300   priority   575
...
```

Todas são variantes que só saem de combinação única. Prioridade mínima é o jeito
de o jogo dizer "este perde qualquer empate" — o que só faz sentido se o maior
vence. Com a regra invertida, eram justamente essas nove que ganhavam todo
empate.

**2. Filho de combinação única só nasce daquela combinação.** O motor não
excluía esses Pals da busca por rank, então pares comuns "geravam" Jormuntide
Ignis, Penking Lux e afins. O pool de rank cai de 263 para 184 espécies.

A conferência agora tem uma âncora externa: `KNOWN_COMBOS` em
`tools/validate.py` traz 15 cruzamentos com resultado documentado pelo Game8.
Foram eles que decidiram a questão — as quatro combinações possíveis das duas
regras acertavam 6, 10, 12 e 15 de 15. Só a última fecha. Uma segunda conferência
contra o palbreeding.com bateu 20/20 nos pares de Anubis.

## Quem entra na lista de espécies

A revisão contra o palbreeding.com (299 Pals) mostrou que a lista tinha três
Pals a mais e um com o número errado. Duas correções, as duas vindas dos dados:

**A linha canônica da tribo é escolhida antes de olhar o `IgnoreCombi`.** O
código filtrava `IgnoreCombi` primeiro e só depois escolhia a linha
representante. Em três tribos — Mimog, Boltmane e Monkey_Ice — a linha normal
tem `IgnoreCombi=true` e a do **alfa** tem `false`, então o alfa sobrevivia ao
filtro e virava o representante da espécie. Consequências: Mimog aparecia na
lista sem o `#144` (a linha do alfa não tem `ZukanIndex`) e entrava como
resultado de cruzamento por rank, coisa que a linha normal proíbe.

**Sem item de ovo, não entra na fazenda.** Astralym (#204) é o único Pal do jogo
com `ElementType1 = None` e sem `PalEgg_*` — é o chefe final, não tem spawn nem
alfa. Na `DT_PalMonsterParameter` **não existe** campo que o separe de
Panthalus (`IgnoreCombi`, `IsBoss`, `IsRaidBoss`, `IsTowerBoss` e
`CaptureRateCorrect` são idênticos nos dois), então a ausência do ovo é o único
sinal disponível — e é ele que o motor usa, em vez de um nome fixo no código.

O efeito é verificável: a lista sai de 290 para 287 (204 do Paldex + 84
variantes − Astralym) e a busca reversa de Anubis sai de 236 para **234**, o
número exato do palbreeding.com. Boltmane, aliás, nunca esteve no jogo — é um
Pal anunciado que não entrou nem no acesso antecipado nem na 1.0.

O `pool` continua servindo à lista "sai deste mesmo ovo" (um Pal de combinação
única *nasce* de um ovo daquele tipo); quem encolheu foi só o conjunto que a
regra 3 pode devolver, o `rank_pool`.

## Três implementações da mesma regra

`tools/breeding.py` (referência), `mod/PalBreedCalc/Scripts/breeding.lua` e
`mod-cpp/src/Breeding.cpp`. Parece redundante, mas é o que garante a
conferência automática: `validate_cpp.py` roda o motor C++ em todos os 84.100
pares possíveis e compara com o Python; `validate_lua.py` faz o mesmo com o
Lua. Qualquer divergência aparece antes de virar bug em jogo.

Com uma ressalva que o bug do desempate deixou clara: essa conferência prova que
os três **concordam**, não que estejam **certos**. Foi por isso que uma regra
errada sobreviveu a 84.100 comparações. O que fecha essa brecha é a âncora
externa (`KNOWN_COMBOS`), e é ela que precisa crescer quando surgir um relato de
resultado divergente — reproduza o caso, confirme no jogo, e ele vira teste.

## Idioma

O texto do mod é sempre inglês. Os nomes de Pals, ovos e itens seguem o idioma
do jogo, e a base gerada carrega os dois (`name` em pt-BR, `name_en`).

Descobrir o idioma pela Unreal exigiria os headers da SDK, que este mod não
linka. O Palworld também não grava a escolha em `Saved/Config` — o menu de
idioma segue o idioma do app na Steam. Então `Language.cpp` lê
`steamapps/appmanifest_1623730.acf` (`"UserConfig" { "language" "brazilian" }`),
localizado a partir do caminho do executável, e cai no idioma da interface do
Windows quando o manifesto não existe (Game Pass, atalho fora da Steam).

## Estabilidade do overlay (relatos de crash e de "o F6 parou")

Depois dos relatos de crash ao abrir e de a tecla parar de responder, a revisão
achou seis defeitos que explicam os dois sintomas. Todos estão corrigidos:

1. **Corrida na instalação do hook** (crash no início). `install()` trocava a
   entrada `Present` na vtable e **só depois** guardava o ponteiro original. O
   jogo apresenta 60–144 quadros por segundo: um `Present` caindo nessa janela
   de microssegundos lia `original_present == nullptr` e pulava para o
   endereço zero. Pior: se `VirtualProtect` falhasse, a vtable já estava
   trocada e o original nunca era guardado — crash garantido no quadro
   seguinte. Agora os originais são lidos e publicados **antes** da troca, e
   uma falha desfaz o que já tinha sido feito.
2. **`catch (...)` não pegava nada** (crash virava fechamento do jogo). O
   projeto compila com `/EHsc`, em que violação de acesso é exceção do Windows
   (SEH) e **não** é capturada por `catch (...)`. O `try` em volta do frame
   dava uma falsa sensação de rede de proteção. O guarda de verdade agora é um
   `__try/__except` (`draw_frame_guarded`).
3. **`give_up` era definitivo e silencioso** (o "F6 não fecha mais"). Qualquer
   falha, mesmo passageira, marcava o overlay como morto para sempre — sem
   log. A tecla continuava alternando um booleano que ninguém mais lia, então
   parecia que a tecla tinha morrido. Agora são precisas três falhas seguidas
   para desligar, e o atalho diz no log por que a janela não apareceu
   (`Overlay::status_text`).
4. **Input engolido por um overlay morto** (jogo "travado"). O `WndProc`
   devolvia 0 para teclado e mouse sempre que `visible` estava ligado — mesmo
   com o overlay já desligado. O jogador ficava sem controle nenhum. A captura
   agora exige um renderizador vivo. E `WM_INPUT` passa pelo `DefWindowProc`
   mesmo quando engolido: é ele quem libera o buffer de raw input, e devolver
   0 direto vazava a cada movimento do mouse.
5. **DX12: alocador reciclado com a GPU ainda usando** (device removido). A
   espera pela fence tinha timeout (200 ms no frame, 2 s no envio de textura) e
   o código seguia em frente **mesmo quando o timeout estourava**, resetando um
   `ID3D12CommandAllocator` com comandos em voo. Agora a fence é conferida
   depois da espera: o frame é pulado, e o envio de texturas se desliga.
6. **DX12: referências de back buffer vazando.** `create_render_targets` era
   chamada de novo quando só *um* dos buffers faltava, e sobrescrevia os
   outros sem soltar a referência anterior. Back buffers presos fazem o
   `ResizeBuffers` do jogo falhar — tela preta ou crash no alt-tab e na troca
   de resolução. Agora solta antes de sobrescrever.

Fora isso, dois cintos extras: `uninstall()` espera as threads saírem dos hooks
antes de devolver as vtables (senão um `Present` em voo volta para dentro de
uma DLL já descarregada), e um `.dds` truncado é recusado em vez de virar
leitura fora dos limites dentro do `Present` — download incompleto dos ícones
era crash certo.

E três correções anteriores, de peso ao abrir:

1. **Peso ao abrir**: as listas mostram 263 Pals e cada ícone era lido do disco
   e enviado à GPU no mesmo `Present` (em DX12, cada um esperando uma fence).
   Agora `TextureCache` tem orçamento de 8 carregamentos por frame — a janela
   abre instantânea e os ícones aparecem ao longo de ~1 s.
2. **Corrida de threads**: o `WndProc` (thread da janela) alimentava o ImGui
   enquanto a thread de render usava o mesmo contexto. Agora o `WndProc` só
   enfileira mensagens; elas são reaplicadas dentro do `Present`, na thread
   certa.
3. **Cintos de segurança**: exceção durante o frame desliga o overlay em vez de
   derrubar o jogo; `ResizeBuffers` no DX12 espera a GPU esvaziar antes de
   soltar os back buffers; `WndProc` original nulo cai em `DefWindowProc`;
   Esc fecha a janela.

## O atalho, e por que ele podia se anular

O UE4SS **não lê o teclado pelo `WndProc`**: `Win32AsyncInputSource` faz polling
com `GetAsyncKeyState` a cada 5 ms, numa thread própria. Isso é o que garante
que engolir `WM_KEYDOWN` no hook do overlay não cega o atalho — a suspeita
óbvia, e que se mostrou falsa.

O risco real é outro: `Handler::register_keydown_event` **acrescenta** o
callback a uma lista, e `unregister_keydown_events_for_lua_mod` só desfaz isso
para mods Lua — os eventos de mod C++ nunca são desregistrados. Se o mod for
construído duas vezes (hot reload do UE4SS), o F6 chama `toggle()` duas vezes no
mesmo toque e a janela abre e fecha no mesmo quadro: tecla aparentemente morta.
Por isso `Overlay::toggle()` ignora chamadas a menos de 200 ms uma da outra.

A tecla vem de `config.ini` (`Config.cpp`), guardada como código virtual do
Windows — que é exatamente o valor do enum `RC::Input::Key`, então a conversão
é um cast. Modificadores precisam ir na *inscrição*, não numa conferência
dentro do callback: o `Handler` só dispara quando
`key_data.required_modifier_keys == event.modifier_keys`, ou seja, um atalho
registrado sem modificador **não dispara** com Ctrl pressionado.

Isso obrigou a definir `HAS_INPUT` no CMake do mod: sem ele `Input::Handler` não
existe nos headers e a sobrecarga com modificadores nem é declarada. A
`UE4SS.dll` instalada foi compilada com esse define (`xmake.lua`), então o mod
passa a enxergar as mesmas declarações que a DLL exporta.

Uma segunda causa de lentidão ao abrir (relatada depois): as duas listas de
~290 espécies desenhavam **todas** as linhas por frame e cada uma pedia seu
ícone, enfileirando ~580 carregamentos que o orçamento de 8-16/frame espalhava
por dezenas de frames — a janela levava ~2 s "até ficar pronta". A correção é
um `ImGuiListClipper`: só as ~10 linhas visíveis de cada lista são desenhadas e
só elas pedem ícone, então a tela visível enche em 1-2 frames e o custo por
frame deixa de crescer com o tamanho da lista.

## Duas interfaces, um motor

A interface principal — e a de todas as distribuições — é o overlay ImGui da
DLL. Existe uma **segunda interface** para o mesmo motor Lua, `ui_umg.lua`, que
constrói a janela com widgets UMG do próprio jogo via reflexão
(`StaticConstructObject`, `WidgetBlueprintLibrary.Create`, `AddChild`...), sem
hook nenhum. Ela nasceu quando o Nexus recusou upload com DLL; o Nexus depois
voltou atrás (após ver o GitHub), então a DLL voltou a todas as distribuições e
a janela Lua ficou como **fallback automático**: `uiconfig.lua` com
`lua_ui = "auto"` usa a DLL quando `dlls/main.dll` está presente (sempre, nos
pacotes) e cai na Lua só se a DLL não carregar. `main.lua` só registra o F6 da
janela Lua quando a DLL está ausente, então nunca há F6 duplicado.

Como o Lua do UE4SS não liga delegates de Blueprint, a interação da janela Lua
usa widgets que o Slate opera sozinho, mais um truque que dá linhas clicáveis
de verdade: `CheckBox` é um `ContentWidget`, então **cada linha de lista é um
CheckBox contendo ícone+nome** — clicar em qualquer ponto da linha alterna um
estado que fica gravado no widget, e um laço de poll (`LoopAsync` →
`ExecuteInGameThread`, 150 ms) lê sem perder cliques, desmarcando as demais
para virar seleção única. `EditableTextBox` filtra (o poll só alterna a
visibilidade das 263 linhas fixas) e `ScrollBox` rola nativamente. Ícones vêm
de `LoadAsset` nos assets do jogo (caminhos embutidos no `data.lua`), com
retentativa — `LoadAsset` pode falhar no menu inicial e passar a funcionar
dentro do mundo, então a falha nunca é definitiva. `tools/test_ui_umg.py` roda
a janela inteira fora do jogo contra um mock da API do UE4SS (18 checks,
incluindo clique, seleção exclusiva, filtro e o clique-no-par do modo reverso).

## Empacotamento para a Workshop

`mod/PalBreedCalc` é a cópia para instalar na mão (habilitada via `mods.txt`).
`tools/package_workshop.ps1` monta `mod/workshop/PalBreedCalc` no formato do
gerenciador de mods do jogo: `Info.json` com `InstallRule`, `thumbnail.png` e
uma pasta por alvo. Os tipos aceitos (extraídos do executável) são
`LogicMods`, `Lua`, `Paks`, `PalSchema` e `UE4SS`; usamos `Lua`, que instala
cada alvo em `Mods\NativeMods\UE4SS\Mods\<PackageName>\` — foi assim que o
PalMiniMap instalou seu `Scripts/`. O `enabled.txt` faz o UE4SS carregar o mod
sem editar o `mods.txt`, o que deixa o pacote autossuficiente.

## Onde a janela abre, e por que ela tem um piso

A janela abre encostada no topo e centralizada, com a altura pedida em
`config.ini` (60% da tela). Só que porcentagem pura regride em tela pequena: em
1920×1080, 60% dão 648 px — **menos** que os 860 px fixos de antes, e a aba
*Parents → Child* perde a grade de filhotes. Então a porcentagem tem um piso de
1080×860 (limitado a 92% da tela, para caber em 720p). Em 1440p e 4K a
porcentagem é que manda e a janela cresce junto.

`ImGuiCond_FirstUseEver` faz posição e tamanho valerem só até o jogador mover ou
redimensionar; a partir daí a escolha dele vale pelo resto da sessão (não há
`imgui.ini`, então "sessão" é até fechar o jogo).

O layout da aba de ida também deixou de ser fixo: a grade de filhotes perde
linhas (3 → 1) antes de qualquer coisa ser cortada pela borda, e abaixo de
`ícone + linha de texto` ela desiste das legendas e mostra só os ícones — o
tooltip continua dizendo quem é cada um.

## Iterar no visual sem abrir o jogo

`mod-cpp/tests/preview.cpp` sobe um D3D11 próprio, desenha a mesma `Ui` e
salva um BMP. Abrir o Palworld a cada ajuste de layout levaria minutos; o
preview leva segundos e ainda serve de regressão visual.
