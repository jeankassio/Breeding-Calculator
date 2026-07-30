-- =====================================================================
-- Leitura das DataTables do proprio jogo (UE4SS expoe UDataTable com
-- FindRow / ForEachRow / GetRowNames).
--
-- Fontes:
--   DT_PalMonsterParameter  -> especie, CombiRank, elemento, tamanho, genero
--   DT_PalCombiUnique       -> combinacoes especiais (pai+mae -> filhote fixo)
--   DT_ItemDataTable        -> itens de ovo (PalEgg_<elemento>_<tamanho>)
--
-- Nada aqui pode derrubar o mod: toda leitura e protegida e, se o jogo ainda
-- nao carregou as tabelas, a base fica vazia e tentamos de novo depois.
-- =====================================================================

local M = {}

local TABLES = {
    monster = "/Game/Pal/DataTable/Character/DT_PalMonsterParameter.DT_PalMonsterParameter",
    unique  = "/Game/Pal/DataTable/Character/DT_PalCombiUnique.DT_PalCombiUnique",
    item    = "/Game/Pal/DataTable/Item/DT_ItemDataTable.DT_ItemDataTable",
}

-- EPalElementType -> nome usado no id do item de ovo
local EGG_ELEMENT = {
    Normal = "Normal", Fire = "Fire", Water = "Water", Leaf = "Leaf",
    Electricity = "Electricity", Ice = "Ice", Earth = "Earth",
    Dark = "Dark", Dragon = "Dragon",
}
local EGG_SIZE = { XS = "01", S = "02", M = "03", L = "04", XL = "05" }

local log = function(msg) print(string.format("[PalBreedCalc] %s\n", msg)) end

function M.setLogger(fn) log = fn end

-- --------------------------------------------------------------- helpers
local function try(fn, ...)
    local ok, res = pcall(fn, ...)
    if ok then return res end
    return nil, res
end

local function toStr(value)
    if value == nil then return nil end
    if type(value) == "string" then return value end
    if type(value) == "number" then return tostring(value) end
    -- FName / FString / enum vindos do UE4SS
    local s = try(function() return value:ToString() end)
    if s then return s end
    s = try(function() return value:GetFName():ToString() end)
    if s then return s end
    return tostring(value)
end

local function toNum(value)
    if type(value) == "number" then return value end
    local n = tonumber(toStr(value))
    return n
end

local function toBool(value)
    if type(value) == "boolean" then return value end
    local n = toNum(value)
    if n ~= nil then return n ~= 0 end
    return value == "true" or value == "True"
end

-- Carrega (ou encontra ja carregada) uma DataTable pelo caminho do asset.
local function findTable(path)
    local obj = try(StaticFindObject, path)
    if obj and try(function() return obj:IsValid() end) then return obj end
    try(LoadAsset, path)
    obj = try(StaticFindObject, path)
    if obj and try(function() return obj:IsValid() end) then return obj end
    return nil
end

-- Percorre as linhas. ForEachRow e a via rapida; GetRowNames + FindRow e o
-- plano B para builds do UE4SS que nao expoem ForEachRow.
local function eachRow(table_, fn)
    local ok = pcall(function()
        table_:ForEachRow(function(name, row)
            fn(toStr(name), row)
        end)
    end)
    if ok then return true end

    local names = try(function() return table_:GetRowNames() end)
    if not names then return false end
    local count = try(function() return #names end) or 0
    for i = 1, count do
        local name = toStr(names[i])
        local row = try(function() return table_:FindRow(names[i]) end)
        if row then fn(name, row) end
    end
    return count > 0
end

local function eggItemId(element, size)
    local e, s = EGG_ELEMENT[element or "Normal"], EGG_SIZE[size or ""]
    if not e or not s then return nil end
    return string.format("PalEgg_%s_%s", e, s)
end

-- ------------------------------------------------------------- leitura
local function readPals(db)
    local dt = findTable(TABLES.monster)
    if not dt then return 0 end
    local n = 0
    eachRow(dt, function(id, row)
        if not toBool(try(function() return row.IsPal end)) then return end
        local element = toStr(try(function() return row.ElementType1 end))
        local size = toStr(try(function() return row.Size end))
        db.pals[id] = {
            id = id,
            tribe = toStr(try(function() return row.Tribe end)),
            zukan = toNum(try(function() return row.ZukanIndex end)) or -1,
            name = id,                                   -- traduzido em names.lua
            combi_rank = toNum(try(function() return row.CombiRank end)) or 9999,
            combi_priority = toNum(try(function() return row.CombiDuplicatePriority end)) or 0,
            ignore_combi = toBool(try(function() return row.IgnoreCombi end)) or false,
            male_probability = toNum(try(function() return row.MaleProbability end)) or 50,
            element1 = element,
            element2 = toStr(try(function() return row.ElementType2 end)),
            size = size,
            rarity = toNum(try(function() return row.Rarity end)) or 0,
            is_boss = toBool(try(function() return row.IsBoss end)) or false,
            is_tower_boss = toBool(try(function() return row.IsTowerBoss end)) or false,
            egg = eggItemId(element, size),
        }
        n = n + 1
    end)
    return n
end

local function readUnique(db)
    local dt = findTable(TABLES.unique)
    if not dt then return 0 end
    local n = 0
    eachRow(dt, function(_, row)
        local entry = {
            parent_a = toStr(try(function() return row.ParentTribeA end)),
            gender_a = toStr(try(function() return row.ParentGenderA end)) or "None",
            parent_b = toStr(try(function() return row.ParentTribeB end)),
            gender_b = toStr(try(function() return row.ParentGenderB end)) or "None",
            child    = toStr(try(function() return row.ChildCharacterID end)),
        }
        if entry.parent_a and entry.parent_b and entry.child then
            db.unique[#db.unique + 1] = entry
            n = n + 1
        end
    end)
    return n
end

local function readEggs(db)
    local dt = findTable(TABLES.item)
    if not dt then return 0 end
    local n = 0
    eachRow(dt, function(id, row)
        if id:sub(1, 7) ~= "PalEgg_" then return end
        db.eggs[id] = { id = id, icon = toStr(try(function() return row.IconName end)) }
        n = n + 1
    end)
    return n
end

-- Monta a base completa. Retorna nil enquanto o jogo nao tiver as tabelas.
function M.load()
    local db = { pals = {}, unique = {}, eggs = {} }
    local nPals = readPals(db)
    if nPals == 0 then
        log("DataTables not available yet (load into a world and try again)")
        return nil
    end
    local nUnique = readUnique(db)
    local nEggs = readEggs(db)
    log(string.format("loaded from the game: %d Pals, %d unique combinations, %d eggs",
                      nPals, nUnique, nEggs))
    return db
end

return M
