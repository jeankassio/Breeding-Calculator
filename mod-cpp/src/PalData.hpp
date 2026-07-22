#pragma once
//
// Dados extraidos do jogo (DataTables do Pal-Windows.pak), compilados junto
// com o mod. As tabelas em si vivem em PalData.gen.cpp, gerado por
// tools/gen_cpp_data.py -- regere depois de cada patch do Palworld.
//
// Todas as strings sao UTF-8 (ImGui espera UTF-8).
//
#include <cstddef>

namespace palbreed
{
    struct PalInfo
    {
        const char* id;             // linha da DT_PalMonsterParameter (ex.: "BOSS_Anubis")
        const char* name;           // nome traduzido (pt-BR)
        const char* name_en;
        const char* tribe;          // EPalTribeID -- e o que a reproducao usa
        int zukan;                  // numero no Paldex (-1 = nao listado)
        int combi_rank;
        int combi_priority;         // CombiDuplicatePriority: desempate
        const char* element;        // elemento primario
        const char* size;           // XS..XL
        const char* egg;            // item de ovo gerado por esta especie
        int male_probability;
        bool in_pool;               // pode ser resultado de um cruzamento
    };

    struct UniqueCombo
    {
        const char* parent_a;
        const char* gender_a;       // "None" = qualquer genero
        const char* parent_b;
        const char* gender_b;
        const char* child;          // id da linha do filhote
    };

    struct EggInfo
    {
        const char* id;
        const char* name;           // pt-BR
        const char* name_en;
        const char* icon;
    };

    extern const PalInfo kPals[];
    extern const std::size_t kPalCount;
    extern const UniqueCombo kUnique[];
    extern const std::size_t kUniqueCount;
    extern const EggInfo kEggs[];
    extern const std::size_t kEggCount;
} // namespace palbreed
