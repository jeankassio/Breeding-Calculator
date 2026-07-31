#pragma once
//
// Motor de reproducao. Mesma regra do jogo (UPalCombiMonsterParameter::
// FindChildCharacterID) implementada em tools/breeding.py e breeding.lua:
//
//   1. combinacao unica (DT_PalCombiUnique), em qualquer ordem de pais e
//      respeitando o genero quando a linha exige um;
//   2. senao, rank alvo = (rankA + rankB + 1) / 2 e vence o CombiRank mais
//      proximo, desempatando pelo MAIOR CombiDuplicatePriority.
//
// O desempate decide metade dos cruzamentos: os CombiRank sao multiplos de 10,
// entao o alvo cai exatamente no meio de dois ranks vizinhos sempre que os pais
// tem paridade diferente. Que vence o MAIOR esta nos proprios dados -- as nove
// variantes que so saem de combinacao unica tem prioridade 571-581 em vez do
// padrao rank*100, ou seja, o jogo as fez perder todo empate.
//
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "PalData.hpp"

namespace palbreed
{
    // So o filhote, sem montar a lista do ovo — e o que a busca reversa usa,
    // milhares de vezes por consulta.
    struct Outcome
    {
        const PalInfo* child{};
        bool from_unique{};
        int target_rank{};
    };

    struct Result
    {
        const PalInfo* child{};
        const EggInfo* egg{};
        std::vector<const PalInfo*> egg_pool{};   // todos que saem desse ovo
        bool from_unique{};                       // regra 1 ou regra 2
        int target_rank{};
    };

    // Um par de pais que gera um filhote pedido.
    struct ParentPair
    {
        const PalInfo* male{};
        const PalInfo* female{};
        // true quando so funciona nesta ordem de generos (linhas de
        // DT_PalCombiUnique que exigem macho ou femea especifico)
        bool gender_specific{};
        bool from_unique{};
    };

    class Engine
    {
      public:
        Engine();

        // Pais como ids de linha ("SheepBall"). Genero so importa para as
        // combinacoes unicas que exigem um.
        auto breed(const PalInfo& male, const PalInfo& female) const -> Result;
        auto outcome(const PalInfo& male, const PalInfo& female) const -> Outcome;

        // Caminho inverso: todos os pares que geram este filhote.
        auto pairs_for(const PalInfo& child) const -> std::vector<ParentPair>;

        // Quem compartilha um item de ovo (exclui IgnoreCombi). E a lista
        // "sai deste mesmo ovo" da janela.
        auto pool() const -> const std::vector<const PalInfo*>&
        {
            return m_pool;
        }

        // Especies selecionaveis como pai/mae: o pool + as IgnoreCombi
        // (lendarias), que so nascem de auto-cruzamento. E a lista da UI.
        auto species() const -> const std::vector<const PalInfo*>&
        {
            return m_species;
        }

        auto find(std::string_view id) const -> const PalInfo*;
        auto egg(std::string_view id) const -> const EggInfo*;

        // true se o texto casa com o nome (pt-BR ou ingles) ou com o id.
        static auto matches(const PalInfo& pal, std::string_view needle) -> bool;

      private:
        auto unique_child(const PalInfo& a, const PalInfo& b) const -> const PalInfo*;
        auto nearest(int target_rank) const -> const PalInfo*;

        std::vector<const PalInfo*> m_pool{};
        // Candidatos de um cruzamento por rank: o pool menos os `unique_only`.
        std::vector<const PalInfo*> m_rank_pool{};
        std::vector<const PalInfo*> m_species{};
        // tribo -> especie canonica (auto-cruzamento gera ela mesma)
        std::unordered_map<std::string_view, const PalInfo*> m_species_by_tribe{};
        // rank alvo -> filhote. A regra 2 so depende do rank, entao a resposta
        // cabe numa tabela e a busca reversa fica O(1) por par.
        std::vector<const PalInfo*> m_by_rank{};
    };
} // namespace palbreed
