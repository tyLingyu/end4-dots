require("hyprland/lib")

hl.on("hyprland.start", function()
   local homeDir = os.getenv("HOME")
   if string.len(homeDir) == 0 then
      return
   end
   local baseCustomDir = homeDir .. "/.config/hypr/custom"
   local files = {
      baseCustomDir .. "/env.lua",
      baseCustomDir .. "/execs.lua",
      baseCustomDir .. "/general.lua",
      baseCustomDir .. "/keybinds.lua",
      baseCustomDir .. "/rules.lua",
      baseCustomDir .. "/variables.lua"
   }
   for _, file in ipairs(files) do
      if not is_file_exists(file) then
         create_if_not_exists(file)
      end
   end
end)
