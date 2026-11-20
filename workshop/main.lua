-- More Save Slots - Workshop companion
-- The save slot pages are added by a native REPENTOGON plugin (zhlMoreSaveSlots.dll), which the Steam
-- Workshop cannot install. This companion only checks that everything is in place and tells the
-- player what is missing.

local mod = RegisterMod("More Save Slots", 1)

local DOWNLOAD_URL = "github.com/maflatv/more-save-slots"
local REPENTOGON_URL = "repentogon.com"

local function GetProblem()
	if not REPENTOGON then
		return {
			"More Save Slots needs REPENTOGON.",
			"Install it from " .. REPENTOGON_URL .. ", then install the plugin",
			"from " .. DOWNLOAD_URL,
		}
	end
	if not MoreSaveSlots then
		return {
			"More Save Slots: the plugin is not installed.",
			"Download the installer from " .. DOWNLOAD_URL,
		}
	end
	if not MoreSaveSlots.Active then
		return {
			"More Save Slots " .. tostring(MoreSaveSlots.Version) .. " is turned off:",
			tostring(MoreSaveSlots.Reason),
			"Check " .. DOWNLOAD_URL .. " for an update.",
		}
	end
	return nil
end

local font = Font()
font:Load("font/pftempestasevencondensed.fnt")

local function DrawLines(lines, y)
	local width = Isaac.GetScreenWidth()
	for i, line in ipairs(lines) do
		local x = (width - font:GetStringWidth(line)) / 2
		font:DrawString(line, x, y + (i - 1) * 11, KColor(1, 0.25, 0.25, 1), 0, false)
	end
end

local logged = false
local function LogStatus()
	if logged then return end
	logged = true
	local problem = GetProblem()
	if problem then
		Isaac.DebugString("[More Save Slots] " .. table.concat(problem, " "))
	else
		Isaac.DebugString("[More Save Slots] plugin " .. tostring(MoreSaveSlots.Version) .. " active ("
			.. tostring(MoreSaveSlots.REPENTOGONVersion) .. ")")
	end
end

if REPENTOGON then
	-- Main menu warning (the plugin registers its global once the menu is running).
	mod:AddCallback(ModCallbacks.MC_MAIN_MENU_RENDER, function()
		LogStatus()
		local problem = GetProblem()
		if problem then DrawLines(problem, 4) end
	end)
end

-- Without REPENTOGON there is no main menu callback: warn for a few seconds at the start of each run.
local runStartFrame = nil
mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
	LogStatus()
	runStartFrame = Isaac.GetFrameCount()
end)
mod:AddCallback(ModCallbacks.MC_POST_RENDER, function()
	if not runStartFrame or Isaac.GetFrameCount() - runStartFrame > 60 * 8 then return end
	local problem = GetProblem()
	if problem then DrawLines(problem, Isaac.GetScreenHeight() - 40) end
end)
