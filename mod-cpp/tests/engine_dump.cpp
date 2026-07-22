// Executavel de conferencia, comparado com o Python por tools/validate_cpp.py.
//
//   engine_dump            todos os cruzamentos possiveis (pool x pool):
//                          macho;femea;filhote;regra;ovo;tamanho_da_lista_do_ovo
//   engine_dump reverse    a busca inversa, por filhote:
//                          filhote;pares;pares_unicos;pares_com_genero_fixo

#include <cstdio>
#include <cstring>

#include "Breeding.hpp"

namespace
{
    auto dump_forward(const palbreed::Engine& engine) -> void
    {
        std::printf("male;female;child;rule;egg;egg_pool\n");
        for (const auto* male : engine.pool())
        {
            for (const auto* female : engine.pool())
            {
                const auto result = engine.breed(*male, *female);
                std::printf("%s;%s;%s;%s;%s;%zu\n", male->id, female->id,
                            result.child ? result.child->id : "",
                            result.from_unique ? "unique" : "rank",
                            result.egg ? result.egg->id : "", result.egg_pool.size());
            }
        }
    }

    auto dump_reverse(const palbreed::Engine& engine) -> void
    {
        std::printf("child;pairs;unique_pairs;gender_specific\n");
        for (const auto* child : engine.pool())
        {
            const auto pairs = engine.pairs_for(*child);
            std::size_t unique_pairs = 0;
            std::size_t gender_specific = 0;
            for (const auto& pair : pairs)
            {
                unique_pairs += pair.from_unique ? 1 : 0;
                gender_specific += pair.gender_specific ? 1 : 0;
            }
            std::printf("%s;%zu;%zu;%zu\n", child->id, pairs.size(), unique_pairs, gender_specific);
        }
    }
} // namespace

int main(int argc, char** argv)
{
    const palbreed::Engine engine{};
    if (argc > 1 && std::strcmp(argv[1], "reverse") == 0)
    {
        dump_reverse(engine);
    }
    else
    {
        dump_forward(engine);
    }
    return 0;
}
