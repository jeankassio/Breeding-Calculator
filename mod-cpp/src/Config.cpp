#include "Config.hpp"
#include "Log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <unordered_map>

#include <Windows.h>

namespace palbreed
{
    namespace
    {
        // Endereco qualquer dentro deste modulo, para descobrir o caminho dele.
        auto module_anchor() -> void
        {
        }

        // Mods/PalBreedCalc/dlls/main.dll -> Mods/PalBreedCalc/
        auto mod_dir() -> std::string
        {
            HMODULE self{};
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                   | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&module_anchor), &self);
            wchar_t path[MAX_PATH]{};
            if (GetModuleFileNameW(self, path, MAX_PATH) == 0)
            {
                return {};
            }

            std::wstring dll_path(path);
            const auto dlls_dir = dll_path.find_last_of(L"\\/");
            if (dlls_dir == std::wstring::npos || dlls_dir == 0)
            {
                return {};
            }
            const auto parent = dll_path.find_last_of(L"\\/", dlls_dir - 1);
            if (parent == std::wstring::npos)
            {
                return {};
            }
            const std::wstring dir = dll_path.substr(0, parent + 1);

            const int needed =
                WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 1)
            {
                return {};
            }
            std::string out(static_cast<std::size_t>(needed - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, out.data(), needed, nullptr, nullptr);
            return out;
        }

        auto trim(std::string_view text) -> std::string
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
            {
                return {};
            }
            const auto last = text.find_last_not_of(" \t\r\n");
            return std::string(text.substr(first, last - first + 1));
        }

        auto lowered(std::string text) -> std::string
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        // Nomes aceitos no config.ini. O valor e o codigo virtual do Windows,
        // que e o mesmo numero do enum RC::Input::Key do UE4SS.
        auto key_table() -> const std::unordered_map<std::string, uint8_t>&
        {
            static const std::unordered_map<std::string, uint8_t> table = [] {
                std::unordered_map<std::string, uint8_t> keys{
                    {"tab", VK_TAB},           {"backspace", VK_BACK},
                    {"enter", VK_RETURN},      {"return", VK_RETURN},
                    {"space", VK_SPACE},       {"pause", VK_PAUSE},
                    {"caps_lock", VK_CAPITAL}, {"page_up", VK_PRIOR},
                    {"page_down", VK_NEXT},    {"end", VK_END},
                    {"home", VK_HOME},         {"insert", VK_INSERT},
                    {"ins", VK_INSERT},        {"delete", VK_DELETE},
                    {"del", VK_DELETE},        {"left", VK_LEFT},
                    {"up", VK_UP},             {"right", VK_RIGHT},
                    {"down", VK_DOWN},         {"num_lock", VK_NUMLOCK},
                    {"scroll_lock", VK_SCROLL}, {"print_screen", VK_SNAPSHOT},
                    {"multiply", VK_MULTIPLY}, {"add", VK_ADD},
                    {"subtract", VK_SUBTRACT}, {"decimal", VK_DECIMAL},
                    {"divide", VK_DIVIDE},
                };
                for (int i = 1; i <= 24; ++i)   // F1..F24
                {
                    keys["f" + std::to_string(i)] = static_cast<uint8_t>(VK_F1 + i - 1);
                }
                for (char c = 'a'; c <= 'z'; ++c)
                {
                    keys[std::string(1, c)] = static_cast<uint8_t>('A' + (c - 'a'));
                }
                for (char c = '0'; c <= '9'; ++c)
                {
                    keys[std::string(1, c)] = static_cast<uint8_t>(c);
                }
                for (int i = 0; i <= 9; ++i)    // teclado numerico
                {
                    keys["num_" + std::to_string(i)] = static_cast<uint8_t>(VK_NUMPAD0 + i);
                    keys["numpad_" + std::to_string(i)] = static_cast<uint8_t>(VK_NUMPAD0 + i);
                }
                return keys;
            }();
            return table;
        }

        // Rotulo curto para a janela ("F6", "Ctrl+H").
        auto display_name(const Config& cfg) -> std::string
        {
            std::string label;
            if (cfg.ctrl) label += "Ctrl+";
            if (cfg.alt) label += "Alt+";
            if (cfg.shift) label += "Shift+";
            return label + cfg.hotkey_name;
        }

        constexpr const char* kDefaultIni =
            "# ---------------------------------------------------------------\n"
            "# PalBreedCalc -- configuracao / configuration\n"
            "# Edite e reinicie o jogo. / Edit and restart the game.\n"
            "# ---------------------------------------------------------------\n"
            "\n"
            "# Tecla que abre e fecha a janela. / Key that opens and closes it.\n"
            "# Aceita: F1..F24, A..Z, 0..9, NUM_0..NUM_9, HOME, END, INSERT,\n"
            "#         DELETE, PAGE_UP, PAGE_DOWN, TAB, SPACE, ...\n"
            "hotkey = F6\n"
            "\n"
            "# Combinacao opcional (true/false). / Optional modifiers.\n"
            "ctrl = false\n"
            "alt = false\n"
            "shift = false\n"
            "\n"
            "# Tamanho da janela, em %% da tela. / Window size, in %% of screen.\n"
            "height_percent = 60\n"
            "width_percent = 62\n"
            "\n"
            "# Posicao inicial: top-center, center, top-left, top-right\n"
            "position = top-center\n";

        auto write_default_ini(const std::string& path) -> void
        {
            std::ifstream existing(path);
            if (existing)
            {
                return;                     // nunca sobrescreve o do usuario
            }
            std::ofstream out(path, std::ios::binary);
            if (out)
            {
                out << kDefaultIni;
            }
        }

        auto parse_bool(const std::string& value) -> bool
        {
            const std::string v = lowered(value);
            return v == "true" || v == "1" || v == "yes" || v == "on";
        }

        // Aceita "60" e "0.6" -- os dois querem dizer a mesma coisa.
        auto parse_percent(const std::string& value, float fallback) -> float
        {
            try
            {
                const float parsed = std::stof(value);
                const float fraction = parsed > 1.0f ? parsed / 100.0f : parsed;
                return std::clamp(fraction, 0.20f, 1.0f);
            }
            catch (...)
            {
                return fallback;
            }
        }

        auto load() -> Config
        {
            Config cfg{};
            const std::string dir = mod_dir();
            if (dir.empty())
            {
                return cfg;
            }
            const std::string path = dir + "config.ini";

#ifndef PALBREED_STANDALONE
            write_default_ini(path);
#endif

            std::ifstream file(path);
            if (!file)
            {
                return cfg;
            }

            std::string line;
            bool first = true;
            while (std::getline(file, line))
            {
                std::string clean = trim(line);
                // O Bloco de Notas salva UTF-8 com BOM. Sem tirar esses tres
                // bytes, a chave da PRIMEIRA linha nunca casa -- e como
                // "hotkey" costuma ser a primeira, o atalho do usuario era
                // silenciosamente ignorado.
                if (first && clean.size() >= 3 && static_cast<unsigned char>(clean[0]) == 0xEF
                    && static_cast<unsigned char>(clean[1]) == 0xBB
                    && static_cast<unsigned char>(clean[2]) == 0xBF)
                {
                    clean = trim(std::string_view(clean).substr(3));
                }
                first = false;
                if (clean.empty() || clean[0] == '#' || clean[0] == ';')
                {
                    continue;
                }
                const auto equals = clean.find('=');
                if (equals == std::string::npos)
                {
                    continue;
                }
                const std::string key = lowered(trim(std::string_view(clean).substr(0, equals)));
                const std::string value = trim(std::string_view(clean).substr(equals + 1));

                if (key == "hotkey")
                {
                    if (const uint8_t code = key_code_from_name(value))
                    {
                        cfg.hotkey_vk = code;
                        cfg.hotkey_name = value;
                    }
                    else
                    {
                        log_error("config.ini: unknown key \"" + value + "\" -- keeping F6");
                    }
                }
                else if (key == "ctrl") cfg.ctrl = parse_bool(value);
                else if (key == "alt") cfg.alt = parse_bool(value);
                else if (key == "shift") cfg.shift = parse_bool(value);
                else if (key == "height_percent")
                    cfg.height_percent = parse_percent(value, cfg.height_percent);
                else if (key == "width_percent")
                    cfg.width_percent = parse_percent(value, cfg.width_percent);
                else if (key == "position") cfg.position = lowered(value);
            }

            cfg.hotkey_name = display_name(cfg);
            return cfg;
        }

        std::string g_path{};
    } // namespace

    auto key_code_from_name(std::string name) -> uint8_t
    {
        name = lowered(trim(name));
        // "num0" e "numpad0" tambem, sem o sublinhado
        std::string compact;
        compact.reserve(name.size());
        for (const char c : name)
        {
            if (c != '_' && c != ' ' && c != '-')
            {
                compact += c;
            }
        }

        const auto& table = key_table();
        if (const auto found = table.find(name); found != table.end())
        {
            return found->second;
        }
        for (const auto& [candidate, code] : table)
        {
            std::string candidate_compact;
            for (const char c : candidate)
            {
                if (c != '_')
                {
                    candidate_compact += c;
                }
            }
            if (candidate_compact == compact)
            {
                return code;
            }
        }
        return 0;
    }

    auto config() -> const Config&
    {
        static const Config cfg = [] {
            const std::string dir = mod_dir();
            g_path = dir.empty() ? std::string{} : dir + "config.ini";
            return load();
        }();
        return cfg;
    }

    auto config_path() -> const std::string&
    {
        config();                           // garante que g_path foi preenchido
        return g_path;
    }
} // namespace palbreed
