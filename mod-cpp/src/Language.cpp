#include "Language.hpp"
#include "Log.hpp"

#include <algorithm>
#include <fstream>
#include <string>

#include <Windows.h>

namespace palbreed
{
    namespace
    {
        // Palworld does not persist the chosen language in Saved/Config — the
        // language menu follows Steam's per-app setting, which lives in
        // steamapps/appmanifest_1623730.acf:
        //     "UserConfig" { "language" "brazilian" }
        // Reading UE's own internationalisation state would need the Unreal
        // headers, which this mod deliberately does not link against (see
        // docs/ARQUITETURA.md).
        constexpr const wchar_t* kAppManifest = L"appmanifest_1623730.acf";

        auto steam_language() -> std::string
        {
            // ...\steamapps\common\Palworld\Pal\Binaries\Win64\Palworld-Win64-Shipping.exe
            wchar_t exe[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0)
            {
                return {};
            }
            std::wstring path(exe);
            for (int up = 0; up < 5; ++up)      // Win64, Binaries, Pal, Palworld, common
            {
                const auto slash = path.find_last_of(L"\\/");
                if (slash == std::wstring::npos)
                {
                    return {};
                }
                path.resize(slash);
            }
            path += L"\\";
            path += kAppManifest;

            std::ifstream manifest(path);
            if (!manifest)
            {
                return {};
            }
            std::string line;
            while (std::getline(manifest, line))
            {
                const auto key = line.find("\"language\"");
                if (key == std::string::npos)
                {
                    continue;
                }
                const auto open = line.find('"', key + 10);
                const auto close = line.find('"', open + 1);
                if (open != std::string::npos && close != std::string::npos)
                {
                    std::string value = line.substr(open + 1, close - open - 1);
                    std::transform(value.begin(), value.end(), value.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    return value;
                }
            }
            return {};
        }

        auto detect() -> Language
        {
            const std::string steam = steam_language();
            if (!steam.empty())
            {
                const bool portuguese = steam.find("brazilian") != std::string::npos
                                        || steam.find("portuguese") != std::string::npos;
                log_info(std::string("game language: ") + steam);
                return portuguese ? Language::Portuguese : Language::English;
            }

            // sem o manifesto (Game Pass, atalho fora do Steam): cai no idioma
            // da interface do Windows
            const LANGID ui = GetUserDefaultUILanguage();
            const bool portuguese = PRIMARYLANGID(ui) == LANG_PORTUGUESE;
            log_info(portuguese ? "game language: windows UI (portuguese)"
                                : "game language: windows UI (english)");
            return portuguese ? Language::Portuguese : Language::English;
        }
    } // namespace

    auto game_language() -> Language
    {
        static const Language language = detect();
        return language;
    }
} // namespace palbreed
