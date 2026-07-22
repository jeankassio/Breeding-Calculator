#include "Log.hpp"

#ifdef PALBREED_STANDALONE
// O preview roda fora do jogo, sem a UE4SS.dll carregada.
#include <cstdio>

namespace palbreed
{
    auto log_info(std::string_view message) -> void
    {
        std::printf("[PalBreedCalc] %.*s\n", static_cast<int>(message.size()), message.data());
    }

    auto log_error(std::string_view message) -> void
    {
        std::printf("[PalBreedCalc] ERRO: %.*s\n", static_cast<int>(message.size()), message.data());
    }
} // namespace palbreed
#else
#include <string>

#include <DynamicOutput/DynamicOutput.hpp>

namespace palbreed
{
    namespace
    {
        // O UE4SS loga em wchar_t; as mensagens aqui sao ASCII, entao a
        // conversao e direta.
        auto widen(std::string_view text) -> std::wstring
        {
            return std::wstring(text.begin(), text.end());
        }
    } // namespace

    auto log_info(std::string_view message) -> void
    {
        RC::Output::send<RC::LogLevel::Verbose>(STR("[PalBreedCalc] {}\n"), widen(message));
    }

    auto log_error(std::string_view message) -> void
    {
        RC::Output::send<RC::LogLevel::Error>(STR("[PalBreedCalc] {}\n"), widen(message));
    }
} // namespace palbreed
#endif
