"""
Teste funcional da janela Lua (ui_umg.lua) fora do jogo: simula a API do
UE4SS e percorre o fluxo inteiro -- abrir, clicar nas linhas, filtrar,
modo reverso, clicar num par, fechar.

    python tools/test_ui_umg.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import lupa

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "mod" / "PalBreedCalc" / "Scripts"

HARNESS = r"""
local widgets = {}

local function newMock(class, name)
    local w = {
        __class = class, __name = name, __children = {},
        __text = "", __checked = false, __visible = 0, __valid = true,
        __tooltip = "", __brush = nil, __filterText = "",
    }
    function w:IsValid() return w.__valid end
    function w:SetText(t) w.__text = tostring(t and t.__s or t or "") end
    function w:GetText() return { ToString = function() return w.__filterText end } end
    function w:SetHintText() end
    function w:SetVisibility(v) w.__visible = v end
    function w:AddChild(c) table.insert(w.__children, c); return { SetPadding = function() end,
        SetAnchors = function() end, SetAlignment = function() end,
        SetAutoSize = function() end, SetSize = function() end } end
    function w:SetContent(c) table.insert(w.__children, c) end
    function w:SetIsChecked(v) w.__checked = v and true or false end
    function w:IsChecked() return w.__checked end
    function w:SetBrushFromTexture(t) w.__brush = t end
    function w:SetDesiredSizeOverride() end
    function w:SetToolTipText(t) w.__tooltip = tostring(t and t.__s or "") end
    function w:SetBrushColor() end
    function w:SetPadding() end
    function w:SetHeightOverride() end
    function w:AddToViewport() w.__inViewport = true end
    if name and name ~= "" then widgets[name] = w end
    return w
end

MOCK = { widgets = widgets, loaded = 0 }

local classes = {}
local textures = {}

function StaticFindObject(path)
    if path:find("^/Script/UMG%.Default__WidgetBlueprintLibrary") then
        return {
            IsValid = function() return true end,
            Create = function()
                local root = newMock("UserWidget", "RootUserWidget")
                root.WidgetTree = newMock("WidgetTree", "WidgetTree")
                return root
            end,
            SetInputMode_GameAndUIEx = function() MOCK.inputMode = "ui" end,
            SetInputMode_GameOnly = function() MOCK.inputMode = "game" end,
        }
    end
    if path:find("^/Script/") then
        if classes[path] == nil then
            classes[path] = { __classPath = path, IsValid = function() return true end }
        end
        return classes[path]
    end
    if path:find("^/Game/") then
        if textures[path] == nil then
            textures[path] = { __tex = path, IsValid = function() return true end }
        end
        return textures[path]
    end
    return nil
end

function StaticConstructObject(class, outer, name)
    return newMock(class.__classPath or "?", tostring(name and name.__n or ""))
end

function FName(n) return { __n = n } end
function FText(s) return { __s = s, ToString = function() return s end } end
function FindFirstOf() return { IsValid = function() return true end } end
function LoadAsset() MOCK.loaded = MOCK.loaded + 1 end
function LoopAsync(ms, fn) MOCK.loop = fn end
function ExecuteInGameThread(fn) fn() end

package.loaded["UEHelpers"] = {
    GetPlayerController = function()
        return { IsValid = function() return true end, bShowMouseCursor = false }
    end,
}

-- clica na linha de lista cujo texto e `name` (ex.: "Lamball")
function CLICK_ROW(prefix, name)
    for i = 1, 400 do
        local t = widgets[prefix .. "Row" .. i .. "Text"]
        if t and t.__text == name then
            widgets[prefix .. "Row" .. i].__checked = true
            return i
        end
    end
    return nil
end

function COUNT_VISIBLE(prefix)
    local n = 0
    for i = 1, 400 do
        local row = widgets[prefix .. "Row" .. i]
        if row and row.__visible == 0 then n = n + 1 end
    end
    return n
end
"""


def main() -> int:
    lua = lupa.LuaRuntime(unpack_returned_tuples=True)
    lua.execute(f'package.path = [[{SCRIPTS.as_posix()}/?.lua]] .. ";" .. package.path')
    lua.execute(HARNESS)

    result = lua.execute(r"""
        local breeding = require("breeding")
        local data = require("data")
        local pool = breeding.buildPool(data.pals)
        local species, speciesByTribe = breeding.buildSpecies(data.pals, pool)
        local db = {
            pals = data.pals,
            pool = pool,
            rankPool = breeding.buildRankPool(data.pals, pool, data.unique),
            species = species,
            speciesByTribe = speciesByTribe,
            uniqueIndex = breeding.indexUnique(data.unique),
            eggs = data.eggs,
        }
        local ui = require("ui_umg")
        ui.setLogger(function(m) end)

        local out = {}
        ui.toggle(db)
        out.built = MOCK.widgets.RootUserWidget ~= nil
        out.inViewport = MOCK.widgets.RootUserWidget
                         and MOCK.widgets.RootUserWidget.__inViewport or false
        out.inputMode = MOCK.inputMode
        out.maleRows = COUNT_VISIBLE("male")

        MOCK.loop()                                   -- poll inicial
        out.resultEmpty = MOCK.widgets.ResultText.__text

        -- fila de icones: alguns por tick
        local before = MOCK.loaded
        MOCK.loop(); MOCK.loop()
        out.iconsProgressive = MOCK.loaded > before

        -- clica Lamball (macho) e Cattiva (femea)
        local lamballRow = CLICK_ROW("male", "Lamball")
        MOCK.loop()
        CLICK_ROW("female", "Cattiva")
        MOCK.loop()
        out.resultPair = MOCK.widgets.ResultText.__text
        out.maleSummary = MOCK.widgets.maleSelInfo.__text
        out.poolTitle = MOCK.widgets.PoolTitle.__text
        out.poolCell = nil
        for i = 1, 48 do
            local t = MOCK.widgets["PoolName" .. i]
            if t and t.__text == "Daedream" then out.poolCell = t.__text end
        end

        -- filtro
        MOCK.widgets.maleFilter.__filterText = "anub"
        MOCK.loop()
        out.filtered = COUNT_VISIBLE("male")
        MOCK.widgets.maleFilter.__filterText = ""
        MOCK.loop()

        -- selecao exclusiva: clicar outro macho troca e desmarca o anterior
        CLICK_ROW("male", "Chikipi")
        MOCK.loop()
        out.exclusive = MOCK.widgets["maleRow" .. lamballRow].__checked
        local checkedCount = 0
        for i = 1, 400 do
            local row = MOCK.widgets["maleRow" .. i]
            if row and row.__checked then checkedCount = checkedCount + 1 end
        end
        out.checkedCount = checkedCount

        -- Lyleen (lendaria IgnoreCombi) esta na lista e auto-cruza para si
        out.lyleenRow = CLICK_ROW("male", "Lyleen")
        CLICK_ROW("female", "Lyleen")
        MOCK.loop()
        out.lyleenResult = MOCK.widgets.ResultText.__text

        -- modo reverso
        MOCK.widgets.ModeCheck.__checked = true
        MOCK.loop()
        CLICK_ROW("child", "Anubis")
        MOCK.loop()
        out.pairsTitle = MOCK.widgets.PairsTitle.__text
        out.firstPair = MOCK.widgets.Pair1Text.__text
        out.reverseShown = MOCK.widgets.Reverse.__visible == 0
                           and MOCK.widgets.Forward.__visible == 1

        -- clica o primeiro par -> volta ao modo normal com o par carregado
        MOCK.widgets.Pair1.__checked = true
        MOCK.loop()                                   -- processa o clique
        MOCK.loop()                                   -- troca de modo + recalcula
        out.backToForward = MOCK.widgets.Forward.__visible == 0
        out.loadedPair = MOCK.widgets.ResultText.__text

        ui.toggle(db)
        out.closedMode = MOCK.inputMode
        return out
    """)

    checks = {
        "janela construida": result["built"] is True,
        "no viewport": result["inViewport"] is True,
        "input mode UI ao abrir": result["inputMode"] == "ui",
        # 287 = os 204 do Paldex + 84 variantes - Astralym (sem ovo, nao entra
        # na fazenda). Antes eram 290: entravam tambem Boltmane e Monkey_Ice,
        # que nao existem no jogo 1.0, pela linha do alfa.
        "287 linhas na lista (inclui lendarias)": result["maleRows"] == 287,
        "mensagem inicial": "Pick a male" in str(result["resultEmpty"]),
        "icones progressivos": result["iconsProgressive"] is True,
        # Lamball x Cattiva = Daedream (conferido com o jogo -- ver
        # KNOWN_COMBOS em validate.py; antes o mod dizia Tanzee, por causa do
        # desempate invertido)
        "clique seleciona e calcula": "Daedream" in str(result["resultPair"]),
        "resumo do macho": "Lamball" in str(result["maleSummary"]),
        "titulo da grade (11)": "(11)" in str(result["poolTitle"]),
        "filhote presente na grade": str(result["poolCell"] or "") == "Daedream",
        "filtro reduz a lista": int(result["filtered"]) == 1,
        "selecao exclusiva": result["exclusive"] is False and int(result["checkedCount"]) == 1,
        "aba reversa visivel": result["reverseShown"] is True,
        "Lyleen na lista (era o bug)": result["lyleenRow"] is not None,
        "Lyleen x Lyleen = Lyleen": "Child: Lyleen" in str(result["lyleenResult"]),
        "lista de pares do Anubis": "pairings produce Anubis" in str(result["pairsTitle"]),
        "linha de par com x": "×" in str(result["firstPair"]),
        "clique no par volta ao modo normal": result["backToForward"] is True,
        "par carregado calcula Anubis": "Child: Anubis" in str(result["loadedPair"]),
        "input devolvido ao fechar": result["closedMode"] == "game",
    }

    failed = 0
    for name, ok in checks.items():
        print(("  ok   " if ok else "  FAIL ") + name)
        failed += 0 if ok else 1
    if failed:
        print("\nvalores brutos:")
        for key in ("resultEmpty", "resultPair", "maleSummary", "poolTitle", "poolCell",
                    "pairsTitle", "firstPair", "loadedPair", "maleRows", "filtered",
                    "checkedCount"):
            print(f"  {key} = {result[key]!r}")
    print(f"\n{len(checks) - failed}/{len(checks)} checks ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
