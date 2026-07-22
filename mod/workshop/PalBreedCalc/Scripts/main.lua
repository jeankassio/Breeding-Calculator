-- =====================================================================
-- PalBreedCalc - parte Lua (opcional)
--
-- A janela (F6) e a conta vivem no mod C++ (dlls/main.dll), com os dados
-- extraidos do pak compilados junto. Este script serve para duas coisas:
--
--   PalBreedCalc("Lamball", "Cattiva")   consulta rapida pelo console do UE4SS
--   PalBreedCalcCheck()                  compara os dados embutidos com as
--                                        DataTables do jogo em execucao e
--                                        aponta o que um patch mudou
--
-- Nao registra atalho nenhum: o F6 e do mod C++.
-- =====================================================================

local breeding = require("breeding")
local gamedata = require("gamedata")
local baseData = require("data")

local function log(msg)
    print(string.format("[PalBreedCalc/lua] %s\n", msg))
end

gamedata.setLogger(log)

local function guarded(label, fn, ...)
    local ok, res = pcall(fn, ...)
    if not ok then
        log("error in " .. label .. " (ignored): " .. tostring(res))
        return nil
    end
    return res
end

-- ------------------------------------------------------------------ base
local db = nil

local function buildDb()
    local pals = {}
    for id, p in pairs(baseData.pals) do
        pals[id] = {
            id = p.id, tribe = p.tribe, zukan = p.zukan,
            name = p.name, name_en = p.name_en,
            combi_rank = p.combi_rank, combi_priority = p.combi_priority,
            ignore_combi = p.ignore_combi, male_probability = p.male_probability,
            element1 = p.element1, element2 = p.element2, size = p.size,
            rarity = p.rarity, is_boss = p.is_boss, egg = p.egg,
        }
    end
    return {
        pals = pals,
        pool = breeding.buildPool(pals),
        uniqueIndex = breeding.indexUnique(baseData.unique),
        uniqueCount = #baseData.unique,
        eggs = baseData.eggs,
    }
end

local function ensureDb()
    if db == nil then
        db = guarded("buildDb", buildDb)
    end
    return db
end

-- Aceita id da linha ("SheepBall") ou nome traduzido ("Lamball").
local function findPal(needle)
    if db.pals[needle] then return db.pals[needle] end
    local lower = needle:lower()
    for _, pal in pairs(db.pals) do
        if pal.id:lower() == lower
           or (pal.name and pal.name:lower() == lower)
           or (pal.name_en and pal.name_en:lower() == lower) then
            return pal
        end
    end
    return nil
end

-- ------------------------------------------------------------- consulta
function PalBreedCalc(maleName, femaleName)
    if not ensureDb() then
        log("database unavailable")
        return
    end
    local male, female = findPal(maleName or ""), findPal(femaleName or "")
    if not male then log("Pal not found: " .. tostring(maleName)) return end
    if not female then log("Pal not found: " .. tostring(femaleName)) return end

    local result = breeding.breed(db, male, female, "Male", "Female")
    local child = result.child
    local egg = child and db.eggs[child.egg]

    log(string.format("%s (male, rank %d)  x  %s (female, rank %d)",
        male.name, male.combi_rank, female.name, female.combi_rank))
    log(string.format("  child: %s%s", child.name,
        result.rule == "unique" and "   [unique combination]"
                                 or string.format("   [target rank %d]", result.target_rank or 0)))
    if egg then
        local names = {}
        for _, p in ipairs(breeding.eggPool(db, child.egg)) do names[#names + 1] = p.name end
        log(string.format("  egg: %s", egg.name))
        log(string.format("  hatches from this egg (%d): %s", #names, table.concat(names, ", ")))
    end
    return result
end

-- --------------------------------------------------------- verificacao
-- Le as DataTables do jogo em execucao e compara com o que foi compilado no
-- mod. Divergencia = o jogo recebeu patch; rode tools/extract_game_data.py e
-- recompile.
function PalBreedCalcCheck()
    if not ensureDb() then
        log("database unavailable")
        return
    end
    local live = guarded("gamedata.load", gamedata.load)
    if not live then
        log("could not read the DataTables (load into a world and try again)")
        return
    end

    local diffs, missing, extra = 0, 0, 0
    for id, p in pairs(live.pals) do
        local base = db.pals[id]
        if base == nil then
            extra = extra + 1
            if extra <= 10 then log("  only in the game: " .. id) end
        elseif base.combi_rank ~= p.combi_rank
            or base.combi_priority ~= p.combi_priority
            or base.ignore_combi ~= p.ignore_combi
            or base.egg ~= p.egg then
            diffs = diffs + 1
            if diffs <= 10 then
                log(string.format("  changed: %s (rank %s->%s, egg %s->%s)",
                    id, tostring(base.combi_rank), tostring(p.combi_rank),
                    tostring(base.egg), tostring(p.egg)))
            end
        end
    end
    for id, _ in pairs(db.pals) do
        if live.pals[id] == nil then
            missing = missing + 1
            if missing <= 10 then log("  gone from the game: " .. id) end
        end
    end

    local liveUnique = #live.unique
    if liveUnique ~= db.uniqueCount then
        log(string.format("  unique combinations: %d bundled vs %d in the game",
                          db.uniqueCount, liveUnique))
    end

    if diffs == 0 and missing == 0 and extra == 0 and liveUnique == db.uniqueCount then
        log("bundled data matches the game.")
    else
        log(string.format("%d changed, %d missing, %d new -- run tools/extract_game_data.py",
                          diffs, missing, extra))
    end
end

log("loaded. Console: PalBreedCalc(\"Lamball\", \"Cattiva\") / PalBreedCalcCheck()")
