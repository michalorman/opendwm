"""Exercise opendwm's Lua layout policy with a stub Hyprland API."""
from pathlib import Path
import shutil
import subprocess
import unittest


CONFIG = Path(__file__).resolve().parents[1] / "hyprland/hyprland.lua"


@unittest.skipUnless(shutil.which("lua"), "Lua required")
class HyprlandConfigTests(unittest.TestCase):
    def test_monocle_policy_and_capslock_mapping(self):
        script = r'''
local configs, rule_states, animations = {}, {}, {}
local function dispatcher() return {} end
hl = {
  monitor = function(_) end,
  on = function(_, _) end,
  exec_cmd = dispatcher,
  curve = function(_, _) end,
  animation = function(value) table.insert(animations, value) end,
  config = function(value) table.insert(configs, value) end,
  window_rule = function(_) return {set_enabled = function(_, value) table.insert(rule_states, value) end} end,
  bind = function(...) end,
  dsp = {
    focus = dispatcher,
    exec_cmd = dispatcher,
    layout = dispatcher,
    exit = dispatcher,
    window = {
      cycle_next = dispatcher,
      swap = dispatcher,
      close = dispatcher,
      resize = dispatcher,
      drag = dispatcher,
    },
  },
}
        dofile(os.getenv("OPENDWM_HYPRLAND_CONFIG"))
local input
local general
for _, value in ipairs(configs) do
  if value.input then input = value.input end
  if value.general then general = value.general end
end
assert(input.kb_options == "ctrl:nocaps")
assert(general.resize_on_border == true)
assert(general.hover_icon_on_border == true)
assert(general.extend_border_grab_area == 5)
local by_leaf = {}
for _, animation in ipairs(animations) do by_leaf[animation.leaf] = animation end
assert(by_leaf.global.enabled == true)
assert(by_leaf.windows.enabled == false)
assert(by_leaf.windowsIn.enabled == true and by_leaf.windowsIn.style == "popin 95%")
assert(by_leaf.windowsOut.enabled == true and by_leaf.windowsOut.style == "popin 95%")
assert(by_leaf.fadeIn.enabled == true and by_leaf.fadeOut.enabled == true)
assert(by_leaf.workspaces.enabled == false and by_leaf.layers.enabled == false)
configs = {}
opendwm_set_layout("monocle")
assert(rule_states[#rule_states] == true)
assert(configs[#configs].general.layout == "monocle")
assert(configs[#configs].general.gaps_in == 0)
assert(configs[#configs].general.gaps_out == 0)
local count = #configs
opendwm_adjust_gaps(2)
assert(#configs == count)
opendwm_set_layout("master")
assert(rule_states[#rule_states] == false)
assert(configs[#configs].general.layout == "master")
assert(configs[#configs].general.gaps_in == 10)
opendwm_adjust_gaps(2)
assert(configs[#configs].general.gaps_in == 12)
assert(configs[#configs].general.gaps_out == 12)
'''
        env = {"OPENDWM_HYPRLAND_CONFIG": str(CONFIG)}
        subprocess.run(["lua", "-e", script], check=True, capture_output=True,
                       env=env)


if __name__ == "__main__":
    unittest.main()
