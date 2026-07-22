#include "Ui.hpp"
#include "Language.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include <imgui.h>

namespace palbreed
{
    namespace
    {
        const ImVec4 kMale{0.42f, 0.66f, 1.00f, 1.0f};
        const ImVec4 kFemale{1.00f, 0.52f, 0.72f, 1.0f};
        const ImVec4 kChild{0.50f, 0.94f, 0.58f, 1.0f};
        const ImVec4 kEgg{1.00f, 0.83f, 0.42f, 1.0f};
        const ImVec4 kDim{0.60f, 0.61f, 0.66f, 1.0f};
        const ImVec4 kFaint{0.42f, 0.43f, 0.48f, 1.0f};

        constexpr float kParentIcon = 92.0f;    // retrato do pai escolhido
        constexpr float kBigIcon = 104.0f;      // ovo e filhote no resultado
        constexpr float kListIcon = 32.0f;
        constexpr float kPoolIcon = 64.0f;

        struct Frame
        {
            ImVec2 min, max;
        };

        // O texto do mod e sempre ingles; os nomes seguem o idioma do jogo.
        auto localized(const char* portuguese, const char* english) -> const char*
        {
            return game_language() == Language::Portuguese ? portuguese : english;
        }

        auto display_name(const PalInfo& pal) -> const char*
        {
            return localized(pal.name, pal.name_en);
        }

        auto display_name(const EggInfo& egg) -> const char*
        {
            return localized(egg.name, egg.name_en);
        }

        // Cores por elemento — dao leitura imediata do tipo do Pal/ovo.
        auto element_color(const char* element) -> ImVec4
        {
            if (element == nullptr) return kDim;
            const std::string_view e{element};
            if (e == "Fire") return {1.00f, 0.47f, 0.30f, 1.0f};
            if (e == "Water") return {0.40f, 0.72f, 1.00f, 1.0f};
            if (e == "Leaf") return {0.52f, 0.87f, 0.42f, 1.0f};
            if (e == "Electricity") return {1.00f, 0.87f, 0.33f, 1.0f};
            if (e == "Ice") return {0.62f, 0.90f, 0.98f, 1.0f};
            if (e == "Earth") return {0.83f, 0.68f, 0.44f, 1.0f};
            if (e == "Dark") return {0.72f, 0.55f, 0.95f, 1.0f};
            if (e == "Dragon") return {0.95f, 0.60f, 0.95f, 1.0f};
            return {0.78f, 0.79f, 0.82f, 1.0f};
        }

        // "Etiqueta" arredondada com o nome do elemento/tamanho.
        auto chip(const char* text, const ImVec4& color) -> void
        {
            const ImVec2 padding{8.0f, 2.0f};
            const ImVec2 size = ImGui::CalcTextSize(text);
            const ImVec2 top_left = ImGui::GetCursorScreenPos();
            const ImVec2 bottom_right{top_left.x + size.x + padding.x * 2.0f,
                                      top_left.y + size.y + padding.y * 2.0f};
            ImGui::GetWindowDrawList()->AddRectFilled(
                top_left, bottom_right, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.18f)),
                (size.y + padding.y * 2.0f) * 0.5f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(top_left.x + padding.x, top_left.y + padding.y),
                                                ImGui::GetColorU32(color), text);
            ImGui::Dummy(ImVec2(bottom_right.x - top_left.x, bottom_right.y - top_left.y));
        }

        // Fundo arredondado atras de um icone: mantem o bloco visivel mesmo
        // quando o Pal nao tem textura.
        auto icon_frame(float size, const ImVec4& tint) -> Frame
        {
            const ImVec2 top_left = ImGui::GetCursorScreenPos();
            const ImVec2 bottom_right{top_left.x + size, top_left.y + size};
            ImGui::GetWindowDrawList()->AddRectFilled(
                top_left, bottom_right,
                ImGui::GetColorU32(ImVec4(tint.x * 0.16f, tint.y * 0.16f, tint.z * 0.16f, 0.60f)), 8.0f);
            return Frame{top_left, bottom_right};
        }

        auto section_label(const char* text, const ImVec4& color) -> void
        {
            ImGui::TextColored(color, "%s", text);
            ImGui::Spacing();
        }

        // Texto centralizado numa largura fixa, cortado com reticencias quando
        // nao cabe (nomes como "Jormuntide Ignis" embaixo de um icone de 64px).
        auto centered_caption(const char* text, float width, const ImVec4& color) -> void
        {
            std::string label = text;
            if (ImGui::CalcTextSize(label.c_str()).x > width)
            {
                while (!label.empty() && ImGui::CalcTextSize((label + "...").c_str()).x > width)
                {
                    // respeita os bytes de continuacao do UTF-8
                    do
                    {
                        label.pop_back();
                    } while (!label.empty() && (static_cast<unsigned char>(label.back()) & 0xC0) == 0x80);
                }
                label += "...";
            }
            const float offset = (width - ImGui::CalcTextSize(label.c_str()).x) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, offset));
            ImGui::TextColored(color, "%s", label.c_str());
        }

        auto draw_arrow(float size, const ImVec4& color) -> void
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float middle = origin.y + size * 0.5f;
            auto* draw = ImGui::GetWindowDrawList();
            const ImU32 packed = ImGui::GetColorU32(color);
            draw->AddLine(ImVec2(origin.x + 4.0f, middle), ImVec2(origin.x + size - 10.0f, middle),
                          packed, 2.0f);
            draw->AddTriangleFilled(ImVec2(origin.x + size - 14.0f, middle - 7.0f),
                                    ImVec2(origin.x + size - 14.0f, middle + 7.0f),
                                    ImVec2(origin.x + size, middle), packed);
            ImGui::Dummy(ImVec2(size, size));
        }
    } // namespace

    Ui::Ui(const Engine& engine) : m_engine(engine)
    {
    }

    auto Ui::shutdown() -> void
    {
        m_textures.shutdown();
        m_textures_ready = false;
    }

    auto Ui::select(const PalInfo* male, const PalInfo* female) -> void
    {
        m_male = male;
        m_female = female;
    }

    auto Ui::select_child(const PalInfo* child) -> void
    {
        m_child = child;
        m_pending_tab = 2;
    }

    auto Ui::apply_style() -> void
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 10.0f;
        style.ChildRounding = 10.0f;
        style.FrameRounding = 7.0f;
        style.PopupRounding = 8.0f;
        style.ScrollbarRounding = 8.0f;
        style.WindowPadding = ImVec2(14, 12);
        style.FramePadding = ImVec2(9, 5);
        style.ItemSpacing = ImVec2(9, 7);
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 1.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.075f, 0.095f, 0.97f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.105f, 0.13f, 0.85f);
        colors[ImGuiCol_Border] = ImVec4(0.22f, 0.23f, 0.29f, 0.70f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.21f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.23f, 0.29f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.27f, 0.34f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.24f, 0.26f, 0.34f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.31f, 0.40f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.33f, 0.35f, 0.45f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.21f, 0.27f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.29f, 0.37f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.34f, 0.44f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.23f, 0.29f, 0.60f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.60f);
    }

    auto Ui::pal_icon(const PalInfo* pal, float size, bool highlight) -> void
    {
        const Frame frame = icon_frame(size, highlight ? kChild : ImVec4(1, 1, 1, 1));
        const ImTextureID texture = pal ? m_textures.pal(pal->id, pal->tribe) : 0;
        if (texture)
        {
            ImGui::Image(texture, ImVec2(size, size));
        }
        else
        {
            ImGui::Dummy(ImVec2(size, size));
        }
        if (highlight)
        {
            ImGui::GetWindowDrawList()->AddRect(frame.min, frame.max, ImGui::GetColorU32(kChild),
                                                8.0f, 0, 2.0f);
        }
    }

    auto Ui::render_pal_summary(const PalInfo& pal, const ImVec4& color) -> void
    {
        ImGui::TextColored(color, "%s", display_name(pal));
        if (pal.zukan > 0)
        {
            ImGui::SameLine();
            ImGui::TextColored(kFaint, "#%03d", pal.zukan);
        }
        ImGui::Spacing();
        chip(pal.element ? pal.element : "?", element_color(pal.element));
        ImGui::SameLine();
        chip(pal.size ? pal.size : "?", kDim);
        ImGui::Spacing();
        ImGui::TextColored(kFaint, "rank %d  ·  %d%% male", pal.combi_rank, pal.male_probability);
    }

    auto Ui::render_picker_card(const char* title, const ImVec4& color, const PalInfo* pal,
                                char (&filter)[64], const PalInfo** selected) -> void
    {
        ImGui::PushID(title);
        section_label(title, color);

        // retrato a esquerda; dados e busca na coluna da direita, para o card
        // ocupar o minimo de altura e sobrar espaco para a lista
        ImGui::BeginGroup();
        pal_icon(pal, kParentIcon);
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (pal)
        {
            render_pal_summary(*pal, color);
        }
        else
        {
            ImGui::TextColored(kFaint, "pick one from the list below");
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "search...", filter, sizeof(filter));
        ImGui::EndGroup();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.085f, 0.11f, 1.0f));
        if (ImGui::BeginListBox("##list", ImVec2(-1, -1)))
        {
            for (const auto* candidate : m_engine.pool())
            {
                if (!Engine::matches(*candidate, filter))
                {
                    continue;
                }
                ImGui::PushID(candidate);
                const bool is_selected = (*selected == candidate);
                const ImVec2 row_start = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##item", is_selected, 0, ImVec2(0, kListIcon)))
                {
                    *selected = candidate;
                }
                // icone e nome desenhados por cima da area clicavel
                auto* draw = ImGui::GetWindowDrawList();
                if (const ImTextureID texture = m_textures.pal(candidate->id, candidate->tribe))
                {
                    draw->AddImage(texture, row_start,
                                   ImVec2(row_start.x + kListIcon, row_start.y + kListIcon));
                }
                const float text_y = row_start.y + (kListIcon - ImGui::GetTextLineHeight()) * 0.5f;
                draw->AddText(ImVec2(row_start.x + kListIcon + 10.0f, text_y),
                              ImGui::GetColorU32(is_selected ? color : ImVec4(0.90f, 0.91f, 0.94f, 1.0f)),
                              display_name(*candidate));
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    auto Ui::render_egg_pool(const Result& result) -> void
    {
        ImGui::TextColored(kDim, "Pals that can hatch from this same egg (%d)",
                           static_cast<int>(result.egg_pool.size()));

        if (!ImGui::BeginChild("##egg_pool", ImVec2(0, 0), ImGuiChildFlags_Borders))
        {
            ImGui::EndChild();
            return;
        }

        const float step = kPoolIcon + ImGui::GetStyle().ItemSpacing.x + 6.0f;
        const int columns = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / step));

        int column = 0;
        for (const auto* pal : result.egg_pool)
        {
            const bool is_child = (pal == result.child);
            ImGui::BeginGroup();
            pal_icon(pal, kPoolIcon, is_child);
            centered_caption(display_name(*pal), kPoolIcon, is_child ? kChild : kDim);
            ImGui::EndGroup();

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s%s", display_name(*pal),
                                  is_child ? "  (result of this pairing)" : "");
            }
            if (++column % columns != 0)
            {
                ImGui::SameLine();
            }
        }
        ImGui::EndChild();
    }

    auto Ui::render_result(float height, const Result* maybe_result) -> void
    {
        if (maybe_result == nullptr || maybe_result->child == nullptr)
        {
            ImGui::BeginChild("##result", ImVec2(0, height));
            ImGui::TextColored(kFaint, m_male && m_female
                                           ? "No result for this pair."
                                           : "Pick a male and a female to see the egg and "
                                             "which Pals can hatch from it.");
            ImGui::EndChild();
            return;
        }
        const Result& result = *maybe_result;

        ImGui::BeginChild("##result", ImVec2(0, height));

        // recapitulacao do par escolhido
        ImGui::TextColored(kMale, "%s", display_name(*m_male));
        ImGui::SameLine();
        ImGui::TextColored(kFaint, "×");
        ImGui::SameLine();
        ImGui::TextColored(kFemale, "%s", display_name(*m_female));
        ImGui::SameLine();
        ImGui::TextColored(kFaint, "   %s", result.from_unique
                                                ? "unique combination"
                                                : "closest rank to the average");
        ImGui::Spacing();

        // ovo -> filhote
        ImGui::BeginGroup();
        const Frame egg_frame = icon_frame(kBigIcon, kEgg);
        if (const ImTextureID texture = result.egg ? m_textures.egg(result.egg->icon) : 0)
        {
            ImGui::Image(texture, ImVec2(kBigIcon, kBigIcon));
        }
        else
        {
            ImGui::Dummy(ImVec2(kBigIcon, kBigIcon));
        }
        ImGui::GetWindowDrawList()->AddRect(egg_frame.min, egg_frame.max, ImGui::GetColorU32(kEgg),
                                            8.0f, 0, 1.5f);
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, kBigIcon * 0.5f - 16.0f));
        draw_arrow(46.0f, kFaint);
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        pal_icon(result.child, kBigIcon, true);
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        render_pal_summary(*result.child, kChild);
        ImGui::Spacing();
        ImGui::TextColored(kEgg, "%s", result.egg ? display_name(*result.egg) : "unknown egg");
        ImGui::EndGroup();

        ImGui::Spacing();
        render_egg_pool(result);
        ImGui::EndChild();
    }

    auto Ui::render_pair_list(float height) -> void
    {
        if (m_child == nullptr)
        {
            ImGui::TextColored(kFaint, "Pick a Pal on the left to see every pairing "
                                       "that produces it.");
            return;
        }
        if (m_pairs_for != m_child)
        {
            m_pairs = m_engine.pairs_for(*m_child);   // ~35 mil pares, so quando muda
            m_pairs_for = m_child;
        }

        ImGui::TextColored(kDim, "%d pairings produce", static_cast<int>(m_pairs.size()));
        ImGui::SameLine();
        ImGui::TextColored(kChild, "%s", display_name(*m_child));

        if (!ImGui::BeginChild("##pairs", ImVec2(0, height), ImGuiChildFlags_Borders))
        {
            ImGui::EndChild();
            return;
        }

        constexpr float icon = 40.0f;
        const float row_height = icon + 8.0f;

        // sao milhares de linhas; o clipper desenha so as visiveis
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(m_pairs.size()), row_height);
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const ParentPair& pair = m_pairs[static_cast<std::size_t>(index)];
                const ImVec2 row = ImGui::GetCursorScreenPos();
                auto* draw = ImGui::GetWindowDrawList();

                if (index % 2 == 1)
                {
                    draw->AddRectFilled(row, ImVec2(row.x + ImGui::GetContentRegionAvail().x,
                                                    row.y + row_height),
                                        ImGui::GetColorU32(ImVec4(1, 1, 1, 0.025f)), 6.0f);
                }

                const float text_y = row.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f;
                float x = row.x + 4.0f;

                const auto draw_parent = [&](const PalInfo* pal, const ImVec4& color) {
                    if (const ImTextureID texture = m_textures.pal(pal->id, pal->tribe))
                    {
                        draw->AddImage(texture, ImVec2(x, row.y + 4.0f),
                                       ImVec2(x + icon, row.y + 4.0f + icon));
                    }
                    x += icon + 8.0f;
                    draw->AddText(ImVec2(x, text_y), ImGui::GetColorU32(color), display_name(*pal));
                    x += 170.0f;
                };

                draw_parent(pair.male, kMale);
                draw->AddText(ImVec2(x - 24.0f, text_y), ImGui::GetColorU32(kFaint), "x");
                draw_parent(pair.female, kFemale);

                if (pair.from_unique)
                {
                    draw->AddText(ImVec2(x, text_y), ImGui::GetColorU32(kEgg), "unique");
                    x += 62.0f;
                }
                if (pair.gender_specific)
                {
                    draw->AddText(ImVec2(x, text_y), ImGui::GetColorU32(kFaint),
                                  "genders as shown");
                }

                // area clicavel: manda o par para a aba de ida
                ImGui::SetCursorScreenPos(row);
                ImGui::PushID(index);
                if (ImGui::InvisibleButton("##pair", ImVec2(ImGui::GetContentRegionAvail().x,
                                                            row_height)))
                {
                    m_male = pair.male;
                    m_female = pair.female;
                    m_pending_tab = 1;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Click to load this pair into the other tab");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    auto Ui::render_forward() -> void
    {
        if (ImGui::Button("Swap sides"))
        {
            const PalInfo* previous = m_male;
            m_male = m_female;
            m_female = previous;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            m_male = m_female = nullptr;
            m_male_filter[0] = m_female_filter[0] = '\0';
        }
        ImGui::SameLine();
        ImGui::TextColored(kFaint, "        F6 closes this window");
        ImGui::Separator();

        const bool has_pair = m_male != nullptr && m_female != nullptr;
        const Result result = has_pair ? m_engine.breed(*m_male, *m_female) : Result{};
        m_egg_pool_size = result.egg_pool.size();

        // O bloco de baixo (ovo + filhotes) tem altura conhecida; os pais ficam
        // com o resto, para a lista crescer junto com a janela em vez de o
        // rodape sumir quando a janela e pequena.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float spacing = style.ItemSpacing.x;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float column = (available.x - spacing) * 0.5f;

        // a grade de filhotes ocupa so as linhas que precisa (ate 3), o que
        // sobra vai para as listas de selecao
        const float line = ImGui::GetTextLineHeightWithSpacing();
        const float pool_step = kPoolIcon + style.ItemSpacing.x + 6.0f;
        const int pool_columns = (std::max)(1, static_cast<int>(
            (available.x - style.WindowPadding.x * 2.0f) / pool_step));
        const int pool_count = static_cast<int>(m_egg_pool_size);
        const float pool_rows = static_cast<float>(
            std::clamp((pool_count + pool_columns - 1) / pool_columns, 1, 3));
        const float pool_height = (kPoolIcon + line) * pool_rows + style.WindowPadding.y * 2.0f;
        // a coluna de texto do filhote (nome, etiquetas, rank, ovo) pode passar
        // da altura do icone
        const float info_height = (std::max)(kBigIcon, line * 4.0f + style.ItemSpacing.y);
        const float result_height = style.WindowPadding.y * 2.0f     // recuo do child
                                    + line                            // recapitulacao do par
                                    + info_height                     // ovo -> filhote
                                    + line                            // titulo da grade
                                    + pool_height
                                    + style.ItemSpacing.y * 4.0f;
        const float parents_height = (std::max)(240.0f, available.y - result_height - spacing);

        ImGui::BeginChild("##father", ImVec2(column, parents_height), ImGuiChildFlags_Borders);
        render_picker_card("Male", kMale, m_male, m_male_filter, &m_male);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##mother", ImVec2(column, parents_height), ImGuiChildFlags_Borders);
        render_picker_card("Female", kFemale, m_female, m_female_filter, &m_female);
        ImGui::EndChild();

        ImGui::Spacing();
        render_result(ImGui::GetContentRegionAvail().y, has_pair ? &result : nullptr);
    }

    auto Ui::render_reverse() -> void
    {
        if (ImGui::Button("Clear"))
        {
            m_child = nullptr;
            m_child_filter[0] = '\0';
        }
        ImGui::SameLine();
        ImGui::TextColored(kFaint, "        F6 closes this window");
        ImGui::Separator();

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float column = (available.x - style.ItemSpacing.x) * 0.42f;
        // desconta o recuo de baixo da janela: sem isso a lista passa alguns
        // pixels da borda e a janela ganha uma barra de rolagem inutil
        const float height = available.y - style.WindowPadding.y;

        ImGui::BeginChild("##child_pick", ImVec2(column, height), ImGuiChildFlags_Borders);
        render_picker_card("Child", kChild, m_child, m_child_filter, &m_child);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##pairs_side", ImVec2(0, height));
        render_pair_list(ImGui::GetContentRegionAvail().y);
        ImGui::EndChild();
    }

    auto Ui::render(bool* open, ITextureBackend* textures, const char* icons_dir) -> void
    {
        if (!m_textures_ready && textures != nullptr)
        {
            m_textures.init(textures, icons_dir);
            m_textures_ready = true;
        }
        if (!m_style_applied)
        {
            apply_style();
            m_style_applied = true;
        }

        ImGui::SetNextWindowSize(ImVec2(1080, 860), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(860, 560), ImVec2(FLT_MAX, FLT_MAX));
        if (!ImGui::Begin("Breeding Calculator###PalBreedCalc", open, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("##mode"))
        {
            const int pending = m_pending_tab;
            m_pending_tab = 0;

            if (ImGui::BeginTabItem("Parents -> Child", nullptr,
                                    pending == 1 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                render_forward();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Child -> Parents", nullptr,
                                    pending == 2 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                render_reverse();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }
} // namespace palbreed
