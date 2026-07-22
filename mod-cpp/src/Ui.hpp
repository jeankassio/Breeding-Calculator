#pragma once
//
// A janela da calculadora. Recebe o motor pronto e so desenha.
//
// Dois modos:
//   Parents -> Child   escolhe pai e mae, mostra o ovo e o filhote
//   Child -> Parents   escolhe o filhote, mostra os pares que o geram
//
#include <vector>

#include "Breeding.hpp"
#include "Textures.hpp"

namespace palbreed
{
    class Ui
    {
      public:
        explicit Ui(const Engine& engine);

        // Chamado dentro do frame do ImGui (ver Overlay). O backend cria as
        // texturas dos icones na primeira vez (DX11 ou DX12).
        auto render(bool* open, ITextureBackend* textures, const char* icons_dir = nullptr) -> void;
        auto shutdown() -> void;

        // Pre-selecao usada pelo preview fora do jogo.
        auto select(const PalInfo* male, const PalInfo* female) -> void;
        auto select_child(const PalInfo* child) -> void;

      private:
        auto apply_style() -> void;
        auto render_forward() -> void;
        auto render_reverse() -> void;
        auto render_picker_card(const char* title, const ImVec4& color, const PalInfo* pal,
                                char (&filter)[64], const PalInfo** selected) -> void;
        auto render_pal_summary(const PalInfo& pal, const ImVec4& color) -> void;
        auto render_result(float height, const Result* result) -> void;
        auto render_egg_pool(const Result& result) -> void;
        auto render_pair_list(float height) -> void;
        auto pal_icon(const PalInfo* pal, float size, bool highlight = false) -> void;

        const Engine& m_engine;
        TextureCache m_textures{};
        bool m_textures_ready{};
        bool m_style_applied{};
        std::size_t m_egg_pool_size{};   // quantos filhotes a grade precisa mostrar

        const PalInfo* m_male{};
        const PalInfo* m_female{};
        char m_male_filter[64]{};
        char m_female_filter[64]{};

        const PalInfo* m_child{};
        char m_child_filter[64]{};
        // 0 = nenhuma, 1 = "Parents -> Child", 2 = "Child -> Parents": clicar
        // num par salta para a outra aba
        int m_pending_tab{};
        // pares que geram m_child, recalculados so quando ele muda
        std::vector<ParentPair> m_pairs{};
        const PalInfo* m_pairs_for{};
    };
} // namespace palbreed
