-- =====================================================================
-- Motor de reproducao (espelha tools/breeding.py e a logica do jogo em
-- UPalCombiMonsterParameter::FindChildCharacterID).
--
-- Entrada: a base montada por gamedata.lua
--   pals[id]   = { id, tribe, zukan, name, combi_rank, combi_priority,
--                  ignore_combi, element1, size, is_boss, egg }
--   unique[i]  = { parent_a, gender_a, parent_b, gender_b, child }
-- =====================================================================

local M = {}

-- Menor = mais "canonica": linha cujo id e o proprio nome da tribo e que nao
-- seja alfa. Linhas Quest_/SUMMON_/BOSS_ so entram se nao houver outra.
local function canonicalScore(pal)
    local sameName = (pal.id:lower() == (pal.tribe or ""):lower()) and 0 or 2
    return sameName + (pal.is_boss and 1 or 0)
end

-- Uma linha por tribo: a DT tem varias linhas para a mesma especie (BOSS_,
-- Quest_, ...) todas com o mesmo CombiRank, mas so a normal pode nascer.
function M.buildPool(pals)
    local best = {}
    for _, pal in pairs(pals) do
        if not pal.ignore_combi and pal.tribe then
            local cur = best[pal.tribe]
            if cur == nil then
                best[pal.tribe] = pal
            else
                local a, b = canonicalScore(pal), canonicalScore(cur)
                if a < b or (a == b and (pal.zukan or -1) > (cur.zukan or -1)) then
                    best[pal.tribe] = pal
                end
            end
        end
    end
    local pool = {}
    for _, pal in pairs(best) do pool[#pool + 1] = pal end
    table.sort(pool, function(x, y) return x.id < y.id end)
    return pool
end

-- Lista de escolha (pais): o pool + as especies IgnoreCombi (lendarias e
-- afins), que so nascem de auto-cruzamento mas sao pais validos. Todos os
-- Pals capturaveis ficam selecionaveis.
function M.buildSpecies(pals, pool)
    local species, byTribe = {}, {}
    local poolTribes = {}
    for _, p in ipairs(pool) do
        species[#species + 1] = p
        byTribe[p.tribe] = p
        poolTribes[p.tribe] = true
    end
    -- melhor linha por tribo fora do pool (IgnoreCombi): so especies reais
    local best = {}
    for _, pal in pairs(pals) do
        if pal.tribe and not poolTribes[pal.tribe]
           and (pal.zukan or -1) > 0 and not pal.is_boss then
            local cur = best[pal.tribe]
            if cur == nil then
                best[pal.tribe] = pal
            else
                local a, b = canonicalScore(pal), canonicalScore(cur)
                if a < b or (a == b and (pal.zukan or -1) > (cur.zukan or -1)) then
                    best[pal.tribe] = pal
                end
            end
        end
    end
    for _, pal in pairs(best) do
        species[#species + 1] = pal
        byTribe[pal.tribe] = pal
    end
    table.sort(species, function(x, y) return x.id < y.id end)
    return species, byTribe
end

-- Indexa as combinacoes unicas pelo par de tribos (sem ordem).
function M.indexUnique(unique)
    local idx = {}
    for _, u in ipairs(unique) do
        local a, b = u.parent_a, u.parent_b
        local key = (a < b) and (a .. "\1" .. b) or (b .. "\1" .. a)
        idx[key] = idx[key] or {}
        table.insert(idx[key], u)
    end
    return idx
end

local function uniqueChild(idx, a, b, genderA, genderB)
    local ta, tb = a.tribe, b.tribe
    local key = (ta < tb) and (ta .. "\1" .. tb) or (tb .. "\1" .. ta)
    for _, u in ipairs(idx[key] or {}) do
        -- a linha pode estar em qualquer ordem em relacao aos pais informados
        local orders = {
            { u.parent_a, u.gender_a, u.parent_b, u.gender_b },
            { u.parent_b, u.gender_b, u.parent_a, u.gender_a },
        }
        for _, o in ipairs(orders) do
            local pa, ga, pb, gb = o[1], o[2], o[3], o[4]
            if pa == ta and pb == tb
               and (ga == "None" or ga == genderA)
               and (gb == "None" or gb == genderB) then
                return u.child
            end
        end
    end
    return nil
end

-- UPalDatabaseCharacterParameter::FindNearestCombiRank
local function nearest(pool, target)
    local best, bestDist, bestPrio = nil, math.huge, math.huge
    for _, pal in ipairs(pool) do
        local dist = math.abs((pal.combi_rank or 0) - target)
        local prio = pal.combi_priority or 0
        if dist < bestDist or (dist == bestDist and prio < bestPrio) then
            best, bestDist, bestPrio = pal, dist, prio
        end
    end
    return best
end

-- Tabela rank alvo -> filhote: a regra 2 so depende do rank, entao cabe num
-- vetor e cada consulta vira um indice (essencial para a busca reversa, que
-- avalia ~70 mil pares).
local function rankTable(db)
    if db.byRank == nil then
        local maxRank = 0
        for _, p in ipairs(db.pool) do
            if (p.combi_rank or 0) > maxRank then maxRank = p.combi_rank end
        end
        local t = {}
        for r = 0, maxRank do t[r] = nearest(db.pool, r) end
        db.byRank = t
        db.maxRank = maxRank
    end
    return db.byRank
end

local function nearestFast(db, target)
    local t = rankTable(db)
    if target >= 0 and target <= db.maxRank then return t[target] end
    return nearest(db.pool, target)
end

-- db = { pals = {...}, pool = {...}, uniqueIndex = {...} }
-- Retorna { child = pal, rule = "unique"|"rank", target_rank = n }
function M.breed(db, parentA, parentB, genderA, genderB)
    genderA = genderA or "Male"
    genderB = genderB or "Female"

    local childId = uniqueChild(db.uniqueIndex, parentA, parentB, genderA, genderB)
    if childId and db.pals[childId] then
        return { child = db.pals[childId], rule = "unique" }
    end

    -- mesma especie sempre gera ela mesma (regra do jogo; unico caminho para as
    -- IgnoreCombi/lendarias, que estao fora do pool de rank)
    local sameTribe = db.speciesByTribe and db.speciesByTribe[parentA.tribe]
    if parentA.tribe == parentB.tribe and sameTribe then
        return { child = sameTribe, rule = "rank" }
    end

    local target = math.floor(((parentA.combi_rank or 0) + (parentB.combi_rank or 0) + 1) / 2)
    local child = nearestFast(db, target)
    return { child = child, rule = "rank", target_rank = target }
end

-- Caminho inverso: todos os pares (macho, femea) que geram `child`.
-- Cada linha: { male, female, from_unique, gender_specific } -- este ultimo
-- marca pares que so funcionam na ordem mostrada (linhas de DT_PalCombiUnique
-- que exigem um genero).
function M.pairsFor(db, child)
    local list = db.species or db.pool     -- pais podem ser qualquer especie
    local out = {}
    for i = 1, #list do
        for j = i, #list do
            local a, b = list[i], list[j]
            local ab = M.breed(db, a, b, "Male", "Female")
            local ba = M.breed(db, b, a, "Male", "Female")
            local abOk = ab.child and ab.child.id == child.id
            local baOk = ba.child and ba.child.id == child.id
            if abOk and baOk then
                out[#out + 1] = { male = a, female = b,
                                  from_unique = ab.rule == "unique", gender_specific = false }
            elseif abOk then
                out[#out + 1] = { male = a, female = b,
                                  from_unique = ab.rule == "unique", gender_specific = true }
            elseif baOk then
                out[#out + 1] = { male = b, female = a,
                                  from_unique = ba.rule == "unique", gender_specific = true }
            end
        end
    end

    local function paldex(p)
        local z = p.zukan or -1
        return (z > 0) and z or 9999
    end
    table.sort(out, function(x, y)
        if x.from_unique ~= y.from_unique then return x.from_unique end
        if paldex(x.male) ~= paldex(y.male) then return paldex(x.male) < paldex(y.male) end
        return paldex(x.female) < paldex(y.female)
    end)
    return out
end

-- Todos os Pals que saem de um ovo com a mesma aparencia do resultado.
function M.eggPool(db, eggId)
    local out = {}
    for _, pal in ipairs(db.pool) do
        if pal.egg == eggId then out[#out + 1] = pal end
    end
    -- ordem do Paldex; quem nao tem numero (alfa/variante) vai para o fim
    local function key(p)
        local z = p.zukan or -1
        return (z > 0) and z or 9999
    end
    table.sort(out, function(x, y)
        local kx, ky = key(x), key(y)
        if kx ~= ky then return kx < ky end
        return x.id < y.id
    end)
    return out
end

return M
