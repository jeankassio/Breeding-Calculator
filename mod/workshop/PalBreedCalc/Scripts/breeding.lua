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

-- db = { pals = {...}, pool = {...}, uniqueIndex = {...} }
-- Retorna { child = pal, rule = "unique"|"rank", target_rank = n }
function M.breed(db, parentA, parentB, genderA, genderB)
    genderA = genderA or "Male"
    genderB = genderB or "Female"

    local childId = uniqueChild(db.uniqueIndex, parentA, parentB, genderA, genderB)
    if childId and db.pals[childId] then
        return { child = db.pals[childId], rule = "unique" }
    end

    local target = math.floor(((parentA.combi_rank or 0) + (parentB.combi_rank or 0) + 1) / 2)
    local child = nearest(db.pool, target)
    return { child = child, rule = "rank", target_rank = target }
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
