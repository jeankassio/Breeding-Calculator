-- =====================================================================
-- A janela da calculadora, 100% Lua (UMG por reflexao via UE4SS). E a
-- interface de TODAS as distribuicoes: nenhum binario, nenhum hook.
--
-- Interacao sem delegates de Blueprint:
--   * cada linha de lista e um CheckBox contendo icone+nome: clicar em
--     qualquer ponto da linha alterna o estado, que fica gravado no widget
--     -- um laco de poll (150 ms) le sem perder cliques;
--   * EditableTextBox (busca) e ScrollBox rolam/digitam sozinhos via Slate.
--
-- Icones: assets do proprio jogo (LoadAsset), anexados aos poucos e com
-- RETENTATIVA -- LoadAsset pode falhar no menu inicial e passar a funcionar
-- dentro do mundo, entao falha nunca e permanente. Toda reflexao passa por
-- try().
-- =====================================================================

local breeding = require("breeding")

local M = {}

local log = function(msg) print(string.format("[PalBreedCalc/ui] %s\n", msg)) end
function M.setLogger(fn) log = fn end

local function try(fn, ...)
    local ok, res = pcall(fn, ...)
    if ok then return res end
    return nil, res
end

-- FString volta como string Lua; FText volta como userdata com ToString().
local function asString(value)
    if value == nil then return "" end
    if type(value) == "string" then return value end
    return try(function() return value:ToString() end) or ""
end

-- ---------------------------------------------------------------- estado
local S = {
    db = nil,
    widget = nil,
    tree = nil,
    refs = {},
    rows = {},              -- rows[key] = { {box, icon, text, pal}, ... }
    sel = {},
    last = {},
    pairRows = {},
    poolCells = {},
    iconQueue = {},         -- {image, path, size, attempts}
    textures = {},          -- asset path -> UTexture2D (apenas sucessos)
    texAttempts = {},       -- asset path -> tentativas de LoadAsset
    visible = false,
    polling = false,
    busy = false,
}

local MAX_PAIR_ROWS = 120
local MAX_POOL_CELLS = 48
local ICONS_PER_TICK = 20
local MAX_TEX_ATTEMPTS = 8

-- ---------------------------------------------------------------- cores
local COLOR = {
    frame   = { R = 0.016, G = 0.018, B = 0.028, A = 1.0 },
    panel   = { R = 0.045, G = 0.050, B = 0.075, A = 1.0 },
    inset   = { R = 0.028, G = 0.030, B = 0.045, A = 1.0 },
    title   = { 0.95, 0.90, 0.70 },
    hint    = { 0.55, 0.56, 0.62 },
    male    = { 0.45, 0.68, 1.00 },
    female  = { 1.00, 0.55, 0.75 },
    child   = { 0.55, 0.95, 0.60 },
    egg     = { 1.00, 0.85, 0.45 },
    text    = { 0.92, 0.93, 0.96 },
    dim     = { 0.66, 0.67, 0.72 },
}

-- ------------------------------------------------------------- reflexao
local function cls(path)
    local obj = StaticFindObject(path)
    if obj and obj:IsValid() then return obj end
    return nil
end

local function construct(classPath, name)
    local class = cls(classPath)
    if class == nil or S.tree == nil then return nil end
    local obj = try(StaticConstructObject, class, S.tree, FName(name or ""))
    if obj and obj:IsValid() then return obj end
    return nil
end

local function addChild(panel, child)
    if panel == nil or child == nil then return nil end
    local slot = try(function() return panel:AddChild(child) end)
    if slot ~= nil then return slot end
    for _, fn in ipairs({ "AddChildToVerticalBox", "AddChildToHorizontalBox",
                          "AddChildToCanvas", "AddChildToWrapBox" }) do
        slot = try(function() return panel[fn](panel, child) end)
        if slot ~= nil then return slot end
    end
    return nil
end

local function setText(widget, text)
    if widget then try(function() widget:SetText(FText(text or "")) end) end
end

local function setColor(widget, rgb)
    if widget and rgb then
        try(function()
            widget:SetColorAndOpacity({
                SpecifiedColor = { R = rgb[1], G = rgb[2], B = rgb[3], A = 1.0 },
                ColorUseRule = 0,
            })
        end)
    end
end

local function setVisible(widget, on)
    if widget then try(function() widget:SetVisibility(on and 0 or 1) end) end
end

local function setChecked(widget, on)
    if widget then try(function() widget:SetIsChecked(on and true or false) end) end
end

local function isChecked(widget)
    return widget and try(function() return widget:IsChecked() end) or false
end

local function setPadding(slot, l, t, r, b)
    if slot then
        try(function() slot:SetPadding({ Left = l, Top = t, Right = r, Bottom = b }) end)
    end
end

-- ------------------------------------------------------ icones com retry
local function tryLoadTexture(path)
    local cached = S.textures[path]
    if cached ~= nil and cached:IsValid() then return cached end
    local attempts = S.texAttempts[path] or 0
    if attempts >= MAX_TEX_ATTEMPTS then return nil end
    S.texAttempts[path] = attempts + 1
    try(LoadAsset, path)
    local tex = cls(path)
    if tex then S.textures[path] = tex end
    return tex
end

local function attach(image, tex, size)
    try(function() image:SetBrushFromTexture(tex, false) end)
    try(function() image:SetDesiredSizeOverride({ X = size, Y = size }) end)
    setVisible(image, true)
end

-- Tenta na hora; se o asset ainda nao carrega (menu inicial), entra na fila
-- e o poll tenta de novo -- os icones aparecem quando o jogo deixar.
local function setIcon(image, path, size)
    if image == nil then return end
    if path == nil or path == "" then
        setVisible(image, false)
        return
    end
    local tex = S.textures[path]
    if tex ~= nil and tex:IsValid() then
        attach(image, tex, size)
        return
    end
    setVisible(image, false)
    S.iconQueue[#S.iconQueue + 1] = { image = image, path = path, size = size, attempts = 0 }
end

local function drainIconQueue()
    local n = 0
    local requeue = {}
    while n < ICONS_PER_TICK and #S.iconQueue > 0 do
        local item = table.remove(S.iconQueue, 1)
        local tex = tryLoadTexture(item.path)
        if tex then
            attach(item.image, tex, item.size)
        else
            item.attempts = item.attempts + 1
            if item.attempts < MAX_TEX_ATTEMPTS then
                requeue[#requeue + 1] = item
            end
        end
        n = n + 1
    end
    for _, item in ipairs(requeue) do
        S.iconQueue[#S.iconQueue + 1] = item
    end
end

-- --------------------------------------------------------------- idioma
local usePortuguese = nil
local function portuguese()
    if usePortuguese == nil then
        usePortuguese = false
        local ok, cfg = pcall(require, "uiconfig")
        local forced = ok and cfg and cfg.language or "auto"
        if forced == "pt-BR" then
            usePortuguese = true
            return true
        elseif forced == "en" then
            return false
        end
        local f = io.open("../../../../../appmanifest_1623730.acf", "r")
        if f then
            local text = f:read("*a") or ""
            f:close()
            local lang = text:match('"language"%s*"([^"]+)"')
            if lang and (lang:find("brazilian") or lang:find("portuguese")) then
                usePortuguese = true
            end
        end
    end
    return usePortuguese
end

local function displayName(entry)
    if entry == nil then return "?" end
    if portuguese() then return entry.name or entry.name_en or entry.id end
    return entry.name_en or entry.name or entry.id
end

-- ------------------------------------------------------------ construcao
local function paldexOrder(p)
    local z = p.zukan or -1
    return (z > 0) and z or 9999
end

local function sortedPool()
    if S.sortedPool == nil then
        local list = {}
        for _, p in ipairs(S.db.species or S.db.pool) do list[#list + 1] = p end
        table.sort(list, function(a, b)
            if paldexOrder(a) ~= paldexOrder(b) then
                return paldexOrder(a) < paldexOrder(b)
            end
            return a.id < b.id
        end)
        S.sortedPool = list
    end
    return S.sortedPool
end

local function label(parent, name, text, rgb)
    local block = construct("/Script/UMG.TextBlock", name)
    setText(block, text)
    setColor(block, rgb or COLOR.text)
    addChild(parent, block)
    return block
end

-- Painel com fundo solido: e o que da estrutura visual a janela.
local function panel(parent, name, color, pad)
    local border = construct("/Script/UMG.Border", name)
    local slot = addChild(parent, border)
    try(function() border:SetBrushColor(color) end)
    try(function() border:SetPadding({ Left = pad, Top = pad, Right = pad, Bottom = pad }) end)
    return border, slot
end

-- Linha clicavel: CheckBox contendo [icone + texto].
local function makeRow(parent, name, iconSize, rgb)
    local box = construct("/Script/UMG.CheckBox", name)
    setPadding(addChild(parent, box), 2, 1, 2, 1)
    local content = construct("/Script/UMG.HorizontalBox", name .. "H")
    try(function() box:SetContent(content) end)
    local icon = construct("/Script/UMG.Image", name .. "Icon")
    setPadding(addChild(content, icon), 0, 0, 6, 0)
    setVisible(icon, false)
    local text = construct("/Script/UMG.TextBlock", name .. "Text")
    setText(text, "")
    setColor(text, rgb or COLOR.text)
    addChild(content, text)
    return { box = box, icon = icon, text = text }
end

local function buildPicker(parent, key, title, rgb, listHeight)
    local card, cardSlot = panel(parent, key .. "Card", COLOR.panel, 10)
    setPadding(cardSlot, 6, 4, 6, 4)
    try(function() cardSlot:SetSize({ SizeRule = 1, Value = 1.0 }) end)   -- Fill

    local box = construct("/Script/UMG.VerticalBox", key .. "Box")
    try(function() card:SetContent(box) end)

    label(box, key .. "Title", title, rgb)

    local summary = construct("/Script/UMG.HorizontalBox", key .. "Summary")
    setPadding(addChild(box, summary), 0, 6, 0, 6)
    local icon = construct("/Script/UMG.Image", key .. "SelIcon")
    setPadding(addChild(summary, icon), 0, 0, 10, 0)
    setVisible(icon, false)
    local info = construct("/Script/UMG.TextBlock", key .. "SelInfo")
    setText(info, "click a Pal below")
    setColor(info, COLOR.dim)
    addChild(summary, info)
    S.refs[key .. "SelIcon"] = icon
    S.refs[key .. "SelInfo"] = info

    local filter = construct("/Script/UMG.EditableTextBox", key .. "Filter")
    try(function() filter:SetHintText(FText("search...")) end)
    setPadding(addChild(box, filter), 0, 2, 0, 6)
    S.refs[key .. "Filter"] = filter

    local inset, _ = panel(box, key .. "Inset", COLOR.inset, 4)
    local sizeBox = construct("/Script/UMG.SizeBox", key .. "Size")
    try(function() inset:SetContent(sizeBox) end)
    try(function() sizeBox:SetHeightOverride(listHeight) end)
    local scroll = construct("/Script/UMG.ScrollBox", key .. "Scroll")
    try(function() sizeBox:SetContent(scroll) end)
    try(function() scroll:SetClipping(1) end)      -- ClipToBounds: nada vaza
    local host = scroll or box

    S.rows[key] = {}
    for i, pal in ipairs(sortedPool()) do
        local row = makeRow(host, key .. "Row" .. i, 28)
        setText(row.text, displayName(pal))
        setIcon(row.icon, pal.icon_asset, 28)
        row.pal = pal
        S.rows[key][i] = row
    end
    return card
end

local function build()
    local UEHelpers = require("UEHelpers")
    local pc = try(UEHelpers.GetPlayerController)
    if pc == nil or not pc:IsValid() then
        log("no player controller yet -- load into a world and press F6 again")
        return false
    end

    local wbl = cls("/Script/UMG.Default__WidgetBlueprintLibrary")
    local userWidgetClass = cls("/Script/UMG.UserWidget")
    local gameInstance = FindFirstOf("GameInstance")
    if wbl == nil or userWidgetClass == nil or gameInstance == nil then
        log("UMG classes unavailable")
        return false
    end

    local widget = try(function() return wbl:Create(gameInstance, userWidgetClass, pc) end)
    if widget == nil or not widget:IsValid() then
        log("could not create the root widget")
        return false
    end
    S.widget = widget
    S.tree = widget.WidgetTree
    if S.tree == nil or not S.tree:IsValid() then
        log("root widget has no WidgetTree")
        return false
    end

    local canvas = construct("/Script/UMG.CanvasPanel", "Root")
    S.tree.RootWidget = canvas

    local frame = construct("/Script/UMG.Border", "Frame")
    local slot = addChild(canvas, frame)
    try(function() frame:SetBrushColor(COLOR.frame) end)
    try(function() frame:SetPadding({ Left = 18, Top = 14, Right = 18, Bottom = 14 }) end)
    if slot then
        try(function()
            slot:SetAnchors({ Minimum = { X = 0.5, Y = 0.5 }, Maximum = { X = 0.5, Y = 0.5 } })
            slot:SetAlignment({ X = 0.5, Y = 0.5 })
            slot:SetAutoSize(false)
            slot:SetSize({ X = 1120, Y = 780 })
        end)
    end

    local main = construct("/Script/UMG.VerticalBox", "Main")
    try(function() frame:SetContent(main) end)

    -- titulo + dica na mesma linha
    local titleRow = construct("/Script/UMG.HorizontalBox", "TitleRow")
    setPadding(addChild(main, titleRow), 4, 0, 4, 4)
    label(titleRow, "Title", "Breeding Calculator", COLOR.title)
    label(titleRow, "TitleHint", "      F6 closes  ·  results update as you click", COLOR.hint)

    local modeRow = construct("/Script/UMG.HorizontalBox", "ModeRow")
    setPadding(addChild(main, modeRow), 4, 2, 4, 8)
    local modeCheck = construct("/Script/UMG.CheckBox", "ModeCheck")
    addChild(modeRow, modeCheck)
    label(modeRow, "ModeLabel", "  Reverse mode (Child -> Parents)", COLOR.dim)
    S.refs.modeCheck = modeCheck

    -- ------------------------------------------------ pais -> filhote
    local forward = construct("/Script/UMG.VerticalBox", "Forward")
    addChild(main, forward)
    S.refs.forward = forward

    local pickers = construct("/Script/UMG.HorizontalBox", "Pickers")
    addChild(forward, pickers)
    buildPicker(pickers, "male", "MALE", COLOR.male, 250)
    buildPicker(pickers, "female", "FEMALE", COLOR.female, 250)

    -- resultado: ovo -> filhote num painel proprio
    local resultCard, resultSlot = panel(forward, "ResultCard", COLOR.panel, 10)
    setPadding(resultSlot, 6, 8, 6, 4)
    local resultBox = construct("/Script/UMG.VerticalBox", "ResultBox")
    try(function() resultCard:SetContent(resultBox) end)

    local resultRow = construct("/Script/UMG.HorizontalBox", "ResultRow")
    addChild(resultBox, resultRow)
    local eggIcon = construct("/Script/UMG.Image", "EggIcon")
    setPadding(addChild(resultRow, eggIcon), 0, 0, 8, 0)
    setVisible(eggIcon, false)
    label(resultRow, "Arrow", "  ->  ", COLOR.hint)
    local childIcon = construct("/Script/UMG.Image", "ChildIcon")
    setPadding(addChild(resultRow, childIcon), 0, 0, 12, 0)
    setVisible(childIcon, false)
    local resultText = construct("/Script/UMG.TextBlock", "ResultText")
    setText(resultText, "Pick a male and a female.")
    setColor(resultText, COLOR.text)
    addChild(resultRow, resultText)
    S.refs.eggIcon = eggIcon
    S.refs.childIcon = childIcon
    S.refs.resultText = resultText

    S.refs.poolTitle = label(resultBox, "PoolTitle", "", COLOR.dim)
    local wrap = construct("/Script/UMG.WrapBox", "PoolWrap")
    setPadding(addChild(resultBox, wrap), 0, 6, 0, 2)
    for i = 1, MAX_POOL_CELLS do
        -- largura fixa por celula para a grade alinhar
        local cellSize = construct("/Script/UMG.SizeBox", "PoolSize" .. i)
        addChild(wrap, cellSize)
        try(function() cellSize:SetWidthOverride(96) end)
        local cell = construct("/Script/UMG.VerticalBox", "PoolCell" .. i)
        try(function() cellSize:SetContent(cell) end)
        local img = construct("/Script/UMG.Image", "PoolIcon" .. i)
        setPadding(addChild(cell, img), 20, 2, 20, 0)
        local name = construct("/Script/UMG.TextBlock", "PoolName" .. i)
        setText(name, "")
        setColor(name, COLOR.dim)
        setPadding(addChild(cell, name), 2, 0, 2, 4)
        setVisible(cellSize, false)
        S.poolCells[i] = { cell = cellSize, icon = img, text = name }
    end

    -- ------------------------------------------------ filhote -> pais
    local reverse = construct("/Script/UMG.VerticalBox", "Reverse")
    addChild(main, reverse)
    setVisible(reverse, false)
    S.refs.reverse = reverse

    local reverseSplit = construct("/Script/UMG.HorizontalBox", "ReverseSplit")
    addChild(reverse, reverseSplit)
    buildPicker(reverseSplit, "child", "CHILD", COLOR.child, 380)

    local pairsCard, pairsSlot = panel(reverseSplit, "PairsCard", COLOR.panel, 10)
    setPadding(pairsSlot, 6, 4, 6, 4)
    try(function() pairsSlot:SetSize({ SizeRule = 1, Value = 1.4 }) end)
    local pairsSide = construct("/Script/UMG.VerticalBox", "PairsSide")
    try(function() pairsCard:SetContent(pairsSide) end)
    S.refs.pairsTitle = label(pairsSide, "PairsTitle", "Pick the child you want.", COLOR.dim)
    local pairsInset, _ = panel(pairsSide, "PairsInset", COLOR.inset, 4)
    local pairsSize = construct("/Script/UMG.SizeBox", "PairsSize")
    try(function() pairsInset:SetContent(pairsSize) end)
    try(function() pairsSize:SetHeightOverride(430) end)
    local pairsScroll = construct("/Script/UMG.ScrollBox", "PairsScroll")
    try(function() pairsSize:SetContent(pairsScroll) end)
    try(function() pairsScroll:SetClipping(1) end)
    local host = pairsScroll or pairsSide
    for i = 1, MAX_PAIR_ROWS do
        local row = makeRow(host, "Pair" .. i, 24)
        setVisible(row.box, false)
        S.pairRows[i] = row
    end

    widget:AddToViewport(200)
    log(string.format("window built (pure Lua / UMG, %d species)", #sortedPool()))
    return true
end

-- ------------------------------------------------------------------ poll
local function updateSummary(key, rgb)
    local pal = S.sel[key]
    setIcon(S.refs[key .. "SelIcon"], pal and pal.icon_asset, 64)
    local info = S.refs[key .. "SelInfo"]
    setText(info, pal and string.format(
        "%s\n%s  ·  %s\nrank %d  ·  %d%% male",
        displayName(pal), pal.element1 or "?", pal.size or "?",
        pal.combi_rank or 0, pal.male_probability or 50) or "click a Pal below")
    setColor(info, pal and COLOR.text or COLOR.dim)
end

local function pollSelection(key, rgb)
    local rows = S.rows[key] or {}
    local current = S.sel[key]
    local picked = nil
    for _, row in ipairs(rows) do
        if row.pal ~= current and isChecked(row.box) then
            picked = row.pal
            break
        end
    end
    if picked then
        S.sel[key] = picked
        for _, row in ipairs(rows) do
            local mine = row.pal == picked
            setChecked(row.box, mine)
            setColor(row.text, mine and rgb or COLOR.text)
        end
        updateSummary(key, rgb)
    elseif current then
        for _, row in ipairs(rows) do
            if row.pal == current and not isChecked(row.box) then
                setChecked(row.box, true)
            end
        end
    end
end

local function pollFilter(key)
    local filter = S.refs[key .. "Filter"]
    local text = asString(filter and try(function() return filter:GetText() end))
    if S.last[key .. "Filter"] == text then return end
    S.last[key .. "Filter"] = text
    local lower = text:lower()
    for _, row in ipairs(S.rows[key] or {}) do
        local name = displayName(row.pal)
        local show = lower == "" or name:lower():find(lower, 1, true) ~= nil
                     or row.pal.id:lower():find(lower, 1, true) ~= nil
        setVisible(row.box, show)
    end
end

local function updateForward()
    local male, female = S.sel.male, S.sel.female
    local key = (male and male.id or "") .. "|" .. (female and female.id or "")
    if S.last.forward == key then return end
    S.last.forward = key

    if male == nil or female == nil then
        setText(S.refs.resultText, "Pick a male and a female.")
        setColor(S.refs.resultText, COLOR.dim)
        setVisible(S.refs.eggIcon, false)
        setVisible(S.refs.childIcon, false)
        setText(S.refs.poolTitle, "")
        for _, cell in ipairs(S.poolCells) do setVisible(cell.cell, false) end
        return
    end

    local r = breeding.breed(S.db, male, female, "Male", "Female")
    local child = r.child
    local egg = child and S.db.eggs[child.egg]

    setIcon(S.refs.eggIcon, egg and egg.icon_asset, 80)
    setIcon(S.refs.childIcon, child and child.icon_asset, 80)
    setText(S.refs.resultText, string.format(
        "%s  ×  %s\nChild: %s   (%s)\nEgg: %s",
        displayName(male), displayName(female), displayName(child),
        r.rule == "unique" and "unique combination" or "closest rank to the average",
        egg and displayName(egg) or "?"))
    setColor(S.refs.resultText, COLOR.text)

    local pool = breeding.eggPool(S.db, child.egg)
    setText(S.refs.poolTitle,
            string.format("Pals that can hatch from this same egg (%d):", #pool))
    for i = 1, MAX_POOL_CELLS do
        local cell = S.poolCells[i]
        local pal = pool[i]
        if pal then
            setVisible(cell.cell, true)
            setIcon(cell.icon, pal.icon_asset, 52)
            local name = displayName(pal)
            local mine = pal.id == child.id
            setText(cell.text, name)
            setColor(cell.text, mine and COLOR.child or COLOR.dim)
            try(function() cell.icon:SetToolTipText(FText(name)) end)
        else
            setVisible(cell.cell, false)
        end
    end
end

local function updateReverse()
    local child = S.sel.child
    local key = child and child.id or ""
    if S.last.reverse == key then return end
    S.last.reverse = key

    if child == nil then
        setText(S.refs.pairsTitle, "Pick the child you want.")
        for _, row in ipairs(S.pairRows) do setVisible(row.box, false) end
        return
    end

    local pairs_ = breeding.pairsFor(S.db, child)
    local shown = math.min(#pairs_, MAX_PAIR_ROWS)
    setText(S.refs.pairsTitle, string.format(
        "%d pairings produce %s%s -- click one to load it:",
        #pairs_, displayName(child),
        #pairs_ > shown and string.format(" (showing the first %d)", shown) or ""))
    for i = 1, MAX_PAIR_ROWS do
        local row = S.pairRows[i]
        local p = pairs_[i]
        row.pair = p
        if p and i <= shown then
            local tags = {}
            if p.from_unique then tags[#tags + 1] = "unique" end
            if p.gender_specific then tags[#tags + 1] = "genders as shown" end
            setText(row.text, string.format("%s (M)  ×  %s (F)%s",
                displayName(p.male), displayName(p.female),
                #tags > 0 and ("   [" .. table.concat(tags, ", ") .. "]") or ""))
            setColor(row.text, p.from_unique and COLOR.egg or COLOR.text)
            setIcon(row.icon, p.male.icon_asset, 24)
            setChecked(row.box, false)
            setVisible(row.box, true)
        else
            setVisible(row.box, false)
        end
    end
end

local function pollPairClick()
    for _, row in ipairs(S.pairRows) do
        if row.pair and isChecked(row.box) then
            setChecked(row.box, false)
            S.sel.male = row.pair.male
            S.sel.female = row.pair.female
            for _, entry in ipairs({ { "male", COLOR.male }, { "female", COLOR.female } }) do
                local k, rgb = entry[1], entry[2]
                for _, r in ipairs(S.rows[k] or {}) do
                    local mine = r.pal == S.sel[k]
                    setChecked(r.box, mine)
                    setColor(r.text, mine and rgb or COLOR.text)
                end
                updateSummary(k, rgb)
            end
            S.last.forward = nil
            setChecked(S.refs.modeCheck, false)
            return
        end
    end
end

local function poll()
    if not S.visible or S.widget == nil then return end
    if not S.widget:IsValid() then
        S.widget = nil
        S.visible = false
        return
    end

    drainIconQueue()

    local reverseMode = isChecked(S.refs.modeCheck)
    if S.last.mode ~= reverseMode then
        S.last.mode = reverseMode
        setVisible(S.refs.forward, not reverseMode)
        setVisible(S.refs.reverse, reverseMode)
    end

    if reverseMode then
        pollFilter("child")
        pollSelection("child", COLOR.child)
        updateReverse()
        pollPairClick()
    else
        pollFilter("male")
        pollFilter("female")
        pollSelection("male", COLOR.male)
        pollSelection("female", COLOR.female)
        updateForward()
    end
end

local function startPolling()
    if S.polling then return end
    S.polling = true
    LoopAsync(150, function()
        if S.visible and not S.busy then
            S.busy = true
            ExecuteInGameThread(function()
                local ok, err = pcall(poll)
                if not ok then log("poll error (ignored): " .. tostring(err)) end
                S.busy = false
            end)
        end
        return false
    end)
end

-- ------------------------------------------------------------- interface
local function applyInputMode(open)
    local UEHelpers = require("UEHelpers")
    local pc = try(UEHelpers.GetPlayerController)
    if pc == nil or not pc:IsValid() then return end
    local wbl = cls("/Script/UMG.Default__WidgetBlueprintLibrary")
    if open then
        try(function() wbl:SetInputMode_GameAndUIEx(pc, S.widget, 0, false) end)
        try(function() pc.bShowMouseCursor = true end)
    else
        try(function() wbl:SetInputMode_GameOnly(pc) end)
        try(function() pc.bShowMouseCursor = false end)
    end
end

function M.toggle(db)
    S.db = db
    if S.widget == nil or not S.widget:IsValid() then
        S.widget = nil
        S.refs, S.rows, S.sel, S.last = {}, {}, {}, {}
        S.pairRows, S.poolCells, S.iconQueue = {}, {}, {}
        S.sortedPool = nil
        if not build() then return end
        S.visible = false
    end

    S.visible = not S.visible
    setVisible(S.widget, S.visible)
    applyInputMode(S.visible)
    if S.visible then
        S.texAttempts = {}          -- reabrir da nova chance aos icones que falharam
        startPolling()
    end
end

return M
