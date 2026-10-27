local addonName = ...
local frame
local openButton
local targetDropdown
local levelBox

local function SaveFramePosition()
    local point, _, relativePoint, x, y = frame:GetPoint(1)
    ZPBE_UI_DB.point = point
    ZPBE_UI_DB.relativePoint = relativePoint
    ZPBE_UI_DB.x = x
    ZPBE_UI_DB.y = y
end

local function PositionOpenButton()
    local position = ZPBE_UI_DB
    openButton:ClearAllPoints()
    openButton:SetPoint(position.point or "CENTER", UIParent, position.relativePoint or "CENTER", position.x or 0, position.y or 140)
end

local function HideToolbar()
    SaveFramePosition()
    ZPBE_UI_DB.hidden = true
    frame:Hide()
    PositionOpenButton()
    openButton:Show()
end

local function ShowToolbar()
    ZPBE_UI_DB.hidden = false
    openButton:Hide()
    frame:Show()
end

local function ExecuteCommand(command)
    local editBox = ChatFrame1EditBox
    if not editBox then
        UIErrorsFrame:AddMessage("Chat-Eingabefeld nicht gefunden.", 1, 0.2, 0.2, 1, UIERRORS_HOLD_TIME)
        return
    end

    editBox:SetText(command)
    if ChatEdit_SendText then
        ChatEdit_SendText(editBox, 1)
        if ChatEdit_OnEscapePressed then
            ChatEdit_OnEscapePressed(editBox)
        end
    elseif ChatFrame_OpenChat then
        ChatFrame_OpenChat(command)
    else
        editBox:Show()
        editBox:SetFocus()
    end
end

local function GetArguments()
    local target = tonumber(UIDropDownMenu_GetSelectedValue(targetDropdown)) or 5
    if target ~= 5 and target ~= 10 and target ~= 25 and target ~= 40 then
        UIErrorsFrame:AddMessage("Gruppengröße muss 5, 10, 25 oder 40 sein.", 1, 0.2, 0.2, 1, UIERRORS_HOLD_TIME)
        return nil
    end

    local levelText = levelBox:GetText()
    if levelText == "" then
        return tostring(target)
    end

    local level = tonumber(levelText)
    if not level or level < 1 or level > 80 or level ~= math.floor(level) then
        UIErrorsFrame:AddMessage("Level muss eine ganze Zahl zwischen 1 und 80 sein.", 1, 0.2, 0.2, 1, UIERRORS_HOLD_TIME)
        return nil
    end

    return tostring(target) .. " " .. tostring(level)
end

local function AddButton(parent, text, width, onClick)
    local button = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    button:SetSize(width, 22)
    button:SetText(text)
    button:SetScript("OnClick", onClick)
    return button
end

local function CreateToolbar()
    frame = CreateFrame("Frame", "ZPBE_Toolbar", UIParent)
    frame:SetWidth(270)
    frame:SetHeight(128)
    frame:SetFrameStrata("DIALOG")
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function(self) self:StartMoving() end)
    frame:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        SaveFramePosition()
    end)
    frame:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 32,
        edgeSize = 12,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    frame:SetBackdropColor(0.02, 0.03, 0.05, 0.94)

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 14, -10)
    title:SetText("ZPBE  |  Botgruppe")

    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -3, -3)
    close:SetScript("OnClick", HideToolbar)

    local sizeLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    sizeLabel:SetPoint("TOPLEFT", frame, "TOPLEFT", 16, -36)
    sizeLabel:SetText("Größe")

    targetDropdown = CreateFrame("Frame", "ZPBE_TargetSize", frame, "UIDropDownMenuTemplate")
    targetDropdown:SetPoint("LEFT", sizeLabel, "RIGHT", -8, -2)
    UIDropDownMenu_SetWidth(targetDropdown, 66)
    UIDropDownMenu_Initialize(targetDropdown, function(self, level)
        local sizes = { 5, 10, 25, 40 }
        for _, size in ipairs(sizes) do
            local info = UIDropDownMenu_CreateInfo()
            info.text = size .. "er"
            info.value = size
            info.func = function()
                UIDropDownMenu_SetSelectedValue(targetDropdown, size)
                UIDropDownMenu_SetText(targetDropdown, size .. "er")
                CloseDropDownMenus()
            end
            UIDropDownMenu_AddButton(info, level)
        end
    end)
    UIDropDownMenu_SetSelectedValue(targetDropdown, 5)
    UIDropDownMenu_SetText(targetDropdown, "5er")

    local levelLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    levelLabel:SetPoint("LEFT", targetDropdown, "RIGHT", 0, 0)
    levelLabel:SetText("Level")

    levelBox = CreateFrame("EditBox", "ZPBE_TargetLevel", frame, "InputBoxTemplate")
    levelBox:SetSize(35, 20)
    levelBox:SetPoint("LEFT", levelLabel, "RIGHT", 7, 0)
    levelBox:SetAutoFocus(false)
    levelBox:SetNumeric(true)
    levelBox:SetMaxLetters(2)

    local populate = AddButton(frame, "Gruppe auffüllen", 230, function()
        local args = GetArguments()
        if args then ExecuteCommand(".zpbe populate " .. args) end
    end)
    populate:SetPoint("BOTTOM", frame, "BOTTOM", 0, 37)
    populate:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_TOP")
        GameTooltip:SetText("Nutzt vorhandene Bots und relevelt oder erstellt bei Bedarf AddClass-Bots.")
        GameTooltip:Show()
    end)
    populate:SetScript("OnLeave", function() GameTooltip:Hide() end)

    local status = AddButton(frame, "Status", 68, function() ExecuteCommand(".zpbe status") end)
    status:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 38, 9)

    local dismiss = AddButton(frame, "Bots entlassen", 120, function() ExecuteCommand(".zpbe dismiss") end)
    dismiss:SetPoint("LEFT", status, "RIGHT", 6, 0)

    local position = ZPBE_UI_DB
    frame:SetPoint(position.point or "CENTER", UIParent, position.relativePoint or "CENTER", position.x or 0, position.y or 140)

    openButton = CreateFrame("Button", "ZPBE_OpenToolbar", UIParent, "UIPanelButtonTemplate")
    openButton:SetSize(38, 38)
    openButton:SetFrameStrata("DIALOG")
    openButton:SetClampedToScreen(true)
    openButton:SetNormalTexture("Interface\\Icons\\INV_Misc_GroupNeedMore")
    openButton:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    openButton:SetScript("OnClick", ShowToolbar)
    openButton:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText("ZPBE Toolbar öffnen")
        GameTooltip:Show()
    end)
    openButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
    PositionOpenButton()

    if position.hidden then
        frame:Hide()
        openButton:Show()
    else
        openButton:Hide()
        frame:Show()
    end
end

local loader = CreateFrame("Frame")
loader:RegisterEvent("ADDON_LOADED")
loader:SetScript("OnEvent", function(_, _, loadedAddon)
    if loadedAddon ~= addonName then return end

    ZPBE_UI_DB = ZPBE_UI_DB or {}
    CreateToolbar()

    SLASH_ZPBEUI1 = "/zpbeui"
    SlashCmdList.ZPBEUI = function()
        if frame:IsShown() then
            HideToolbar()
        else
            ShowToolbar()
        end
    end
end)
