#pragma once
//
// The mod's own text is always English. Pal, egg and item names follow the
// game's language: Portuguese names when Palworld runs in Portuguese, English
// otherwise (only those two localisations are extracted — see LANGS in
// tools/extract_game_data.py).
//
namespace palbreed
{
    enum class Language
    {
        English,
        Portuguese,
    };

    // Detected once, from Steam's per-app language, falling back to the
    // Windows UI language. See Language.cpp for why the game's own config
    // files are not used.
    auto game_language() -> Language;
} // namespace palbreed
