-- =====================================================================
-- PalBreedCalc - parte Lua
--
-- Duas interfaces possiveis para o F6 (escolhida em uiconfig.lua):
--   * overlay ImGui do mod C++ (dlls/main.dll) - pacote Workshop/manual;
--   * janela UMG 100%% Lua (ui_umg.lua)         - pacote Nexus, sem DLL.
--
-- E, pelo console do UE4SS, sempre:
--   PalBreedCalc("Lamball", "Cattiva")   consulta rapida
--   PalBreedCalcCheck()                  compara os dados embutidos com as
--                                        DataTables do jogo em execucao
--   PalBreedCalcUI()                     forca a janela Lua, qualquer config
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
    local pool = breeding.buildPool(pals)
    local species, speciesByTribe = breeding.buildSpecies(pals, pool)
    return {
        pals = pals,
        pool = pool,
        -- resultados possiveis de um cruzamento por rank (sem os filhos de
        -- combinacao unica, que so nascem da combinacao deles)
        rankPool = breeding.buildRankPool(pals, pool, baseData.unique),
        species = species,
        speciesByTribe = speciesByTribe,
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

-- ------------------------------------------------------------ janela Lua
-- A pasta do proprio mod, tirada do package.path que o UE4SS monta
-- (".../Mods/PalBreedCalc/Scripts/?.lua"). E o unico caminho que vale para
-- qualquer instalacao; os relativos abaixo sao so reforco.
local function modDirs()
    local dirs = {}
    for entry in string.gmatch(package.path or "", "[^;]+") do
        local dir = entry:match("^(.*)[/\\][Ss]cripts[/\\]%?%.lua$")
        if dir then dirs[#dirs + 1] = dir end
    end
    return dirs
end

local function dllPresent()
    local candidates = {}
    for _, dir in ipairs(modDirs()) do
        candidates[#candidates + 1] = dir .. "/dlls/main.dll"
    end
    -- cwd do jogo = Pal\Binaries\Win64; cobre a instalacao padrao do UE4SS e
    -- a gerenciada da Workshop
    for _, path in ipairs({
        "ue4ss/Mods/PalBreedCalc/dlls/main.dll",
        "Mods/PalBreedCalc/dlls/main.dll",
        "../../../Mods/NativeMods/UE4SS/Mods/PalBreedCalc/dlls/main.dll",
    }) do
        candidates[#candidates + 1] = path
    end

    for _, path in ipairs(candidates) do
        local f = io.open(path, "rb")
        if f then f:close() return true end
    end
    return false
end

local function openLuaUi()
    if not ensureDb() then
        log("database unavailable")
        return
    end
    local ui = require("ui_umg")
    ui.setLogger(log)
    ui.toggle(db)
end

function PalBreedCalcUI()
    guarded("ui_umg", openLuaUi)
end

-- Nome de tecla -> constante do UE4SS. Os digitos tem nome por extenso no
-- enum, o resto bate direto ("F6" -> Key.F6, "h" -> Key.H).
local digitKeys = {
    ["0"] = "ZERO", ["1"] = "ONE", ["2"] = "TWO", ["3"] = "THREE", ["4"] = "FOUR",
    ["5"] = "FIVE", ["6"] = "SIX", ["7"] = "SEVEN", ["8"] = "EIGHT", ["9"] = "NINE",
}

local function resolveKey(name)
    if type(name) ~= "string" or name == "" then return nil end
    local upper = name:upper():gsub("[%s%-]", "_")
    return Key[digitKeys[upper] or upper]
end

local cfgOk, uiconfig = pcall(require, "uiconfig")
local luaUi = cfgOk and uiconfig and uiconfig.lua_ui or false
if luaUi == "auto" then
    luaUi = not dllPresent()
end
if luaUi == true then
    -- A janela da DLL le o atalho de config.ini; a janela Lua le daqui, para
    -- as duas interfaces responderem a mesma tecla.
    local wanted = cfgOk and uiconfig and uiconfig.hotkey or "F6"
    local key = resolveKey(wanted)
    if key == nil then
        log(string.format("unknown hotkey %q in uiconfig.lua -- using F6", tostring(wanted)))
        wanted, key = "F6", Key.F6
    end
    RegisterKeyBind(key, function()
        guarded("ui_umg", openLuaUi)
    end)
    log(string.format("loaded (pure Lua UI). %s opens the calculator.", wanted:upper()))
else
    log("loaded. Console: PalBreedCalc(\"Lamball\", \"Cattiva\") / PalBreedCalcCheck()")
end
