#pragma once
//
// Configuracao do usuario: Mods/PalBreedCalc/config.ini (ao lado da pasta
// dlls/). O arquivo e opcional -- se faltar, o mod escreve um com os padroes
// comentados na primeira execucao, para o usuario descobrir que ele existe.
//
// Nada aqui depende do UE4SS: a tecla e guardada como codigo virtual do
// Windows (que e exatamente o valor de RC::Input::Key), entao o preview fora
// do jogo compila com este mesmo arquivo.
//
#include <cstdint>
#include <string>

namespace palbreed
{
    struct Config
    {
        // ---- atalho -----------------------------------------------------
        uint8_t hotkey_vk{0x75};              // VK_F6
        std::string hotkey_name{"F6"};        // rotulo mostrado na janela
        bool ctrl{};
        bool alt{};
        bool shift{};

        // ---- janela -----------------------------------------------------
        // Fracoes da area util da tela. A altura e o que o usuario mais mexe;
        // a largura acompanha para a janela nao ficar desproporcional.
        float height_percent{0.60f};
        float width_percent{0.62f};
        // "top-center" (padrao), "center", "top-left", "top-right"
        std::string position{"top-center"};
        // respiro entre o topo da tela e a janela, em fracao da altura
        float top_margin{0.025f};
    };

    // Lida uma unica vez, no primeiro uso.
    auto config() -> const Config&;

    // Caminho do .ini realmente usado (vazio quando nao foi possivel achar).
    auto config_path() -> const std::string&;

    // Nome de tecla -> codigo virtual. Devolve 0 quando o nome nao existe.
    auto key_code_from_name(std::string name) -> uint8_t;
} // namespace palbreed
