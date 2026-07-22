#include "Breeding.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace palbreed
{
    namespace
    {
        auto equals(const char* a, const char* b) -> bool
        {
            return a && b && std::strcmp(a, b) == 0;
        }

        auto lower(std::string_view text) -> std::string
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        // Ordem do Paldex; quem nao tem numero (alfa/variante) vai para o fim.
        auto paldex_order(const PalInfo* p) -> int
        {
            return p->zukan > 0 ? p->zukan : 9999;
        }
    } // namespace

    Engine::Engine()
    {
        m_pool.reserve(300);
        for (std::size_t i = 0; i < kPalCount; ++i)
        {
            if (kPals[i].in_pool)
            {
                m_pool.push_back(&kPals[i]);
            }
        }
        std::sort(m_pool.begin(), m_pool.end(), [](const PalInfo* a, const PalInfo* b) {
            if (paldex_order(a) != paldex_order(b))
            {
                return paldex_order(a) < paldex_order(b);
            }
            return std::strcmp(a->id, b->id) < 0;
        });

        // Tabela rank -> filhote: o rank alvo nunca passa do maior CombiRank
        // do pool, entao ela cobre qualquer par.
        int max_rank = 0;
        for (const auto* pal : m_pool)
        {
            max_rank = (std::max)(max_rank, pal->combi_rank);
        }
        m_by_rank.resize(static_cast<std::size_t>(max_rank) + 1);
        for (int rank = 0; rank <= max_rank; ++rank)
        {
            m_by_rank[static_cast<std::size_t>(rank)] = nearest(rank);
        }
    }

    auto Engine::find(std::string_view id) const -> const PalInfo*
    {
        for (std::size_t i = 0; i < kPalCount; ++i)
        {
            if (id == kPals[i].id)
            {
                return &kPals[i];
            }
        }
        return nullptr;
    }

    auto Engine::egg(std::string_view id) const -> const EggInfo*
    {
        for (std::size_t i = 0; i < kEggCount; ++i)
        {
            if (id == kEggs[i].id)
            {
                return &kEggs[i];
            }
        }
        return nullptr;
    }

    auto Engine::matches(const PalInfo& pal, std::string_view needle) -> bool
    {
        if (needle.empty())
        {
            return true;
        }
        const auto lowered = lower(needle);
        return lower(pal.name).find(lowered) != std::string::npos
               || lower(pal.name_en).find(lowered) != std::string::npos
               || lower(pal.id).find(lowered) != std::string::npos;
    }

    auto Engine::unique_child(const PalInfo& a, const PalInfo& b) const -> const PalInfo*
    {
        for (std::size_t i = 0; i < kUniqueCount; ++i)
        {
            const auto& u = kUnique[i];
            // a linha pode listar os pais na ordem inversa da escolhida
            const bool direct = equals(u.parent_a, a.tribe) && equals(u.parent_b, b.tribe);
            const bool swapped = equals(u.parent_b, a.tribe) && equals(u.parent_a, b.tribe);
            if (!direct && !swapped)
            {
                continue;
            }
            // genero: "None" aceita qualquer um; o resto tem que bater com o
            // papel do pai correspondente (a = macho, b = femea)
            const char* gender_for_a = direct ? u.gender_a : u.gender_b;
            const char* gender_for_b = direct ? u.gender_b : u.gender_a;
            if (!equals(gender_for_a, "None") && !equals(gender_for_a, "Male"))
            {
                continue;
            }
            if (!equals(gender_for_b, "None") && !equals(gender_for_b, "Female"))
            {
                continue;
            }
            if (const auto* child = find(u.child))
            {
                return child;
            }
        }
        return nullptr;
    }

    auto Engine::nearest(int target_rank) const -> const PalInfo*
    {
        const PalInfo* best = nullptr;
        int best_distance = 0;
        for (const auto* pal : m_pool)
        {
            const int distance = std::abs(pal->combi_rank - target_rank);
            if (best == nullptr || distance < best_distance
                || (distance == best_distance && pal->combi_priority < best->combi_priority))
            {
                best = pal;
                best_distance = distance;
            }
        }
        return best;
    }

    auto Engine::outcome(const PalInfo& male, const PalInfo& female) const -> Outcome
    {
        Outcome out{};
        out.child = unique_child(male, female);
        if (out.child != nullptr)
        {
            out.from_unique = true;
            return out;
        }
        out.target_rank = (male.combi_rank + female.combi_rank + 1) / 2;
        out.child = static_cast<std::size_t>(out.target_rank) < m_by_rank.size()
                        ? m_by_rank[static_cast<std::size_t>(out.target_rank)]
                        : nearest(out.target_rank);
        return out;
    }

    auto Engine::pairs_for(const PalInfo& child) const -> std::vector<ParentPair>
    {
        std::vector<ParentPair> pairs;
        for (std::size_t i = 0; i < m_pool.size(); ++i)
        {
            for (std::size_t j = i; j < m_pool.size(); ++j)
            {
                const PalInfo* a = m_pool[i];
                const PalInfo* b = m_pool[j];
                // as duas ordens porque algumas linhas unicas exigem um genero
                const Outcome ab = outcome(*a, *b);
                const Outcome ba = outcome(*b, *a);
                const bool ab_ok = ab.child == &child;
                const bool ba_ok = ba.child == &child;
                if (!ab_ok && !ba_ok)
                {
                    continue;
                }
                if (ab_ok && ba_ok)
                {
                    pairs.push_back({a, b, false, ab.from_unique});
                }
                else if (ab_ok)
                {
                    pairs.push_back({a, b, true, ab.from_unique});
                }
                else
                {
                    pairs.push_back({b, a, true, ba.from_unique});
                }
            }
        }

        // combinacoes unicas primeiro, depois na ordem do Paldex
        std::sort(pairs.begin(), pairs.end(), [](const ParentPair& x, const ParentPair& y) {
            if (x.from_unique != y.from_unique)
            {
                return x.from_unique;
            }
            if (paldex_order(x.male) != paldex_order(y.male))
            {
                return paldex_order(x.male) < paldex_order(y.male);
            }
            return paldex_order(x.female) < paldex_order(y.female);
        });
        return pairs;
    }

    auto Engine::breed(const PalInfo& male, const PalInfo& female) const -> Result
    {
        const Outcome computed = outcome(male, female);
        Result result{};
        result.child = computed.child;
        result.from_unique = computed.from_unique;
        result.target_rank = computed.target_rank;

        if (result.child == nullptr || result.child->egg == nullptr)
        {
            return result;
        }

        result.egg = egg(result.child->egg);
        for (const auto* pal : m_pool)
        {
            if (equals(pal->egg, result.child->egg))
            {
                result.egg_pool.push_back(pal);
            }
        }
        return result;
    }
} // namespace palbreed
