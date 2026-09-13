-- opendwm -> Hyprland port (Lua configuration)
-- Mirrors bindings/behavior from x11/config.h (X11 opendwm).
-- Requires Hyprland >= 0.56 (Lua config; .conf support is removed in 0.57).
-- Scripts referenced here live in wayland/scripts/ of this repo.
-- Point OPENDWM at the repo checkout, or copy scripts to ~/.local/bin.

local OPENDWM = "/home/snq/Code/opendwm/wayland/scripts"

-- Shell-quote a path for use inside exec_cmd commands (spaces/quotes safe).
local function shq(s)
    return "'" .. s:gsub("'", "'\\''") .. "'"
end

-- Absolute, shell-quoted path to a helper script.
local function script(name)
    return shq(OPENDWM .. "/" .. name)
end

----------------
---- MONITOR --
----------------

-- Output scale approximating the previous X11 session (Xft.dpi = 164).
-- 1.6 yields an integer logical size on 3840x2560 (2400x1600); fractional
-- values must divide both dimensions evenly or Hyprland substitutes them.
hl.monitor({
    output   = "",
    mode     = "preferred",
    position = "auto",
    scale    = 1.6,
})

-----------------
---- AUTOSTART --
-----------------

hl.on("hyprland.start", function()
    hl.exec_cmd("python3 " .. script("start-bar"))
    hl.exec_cmd("change-wallpaper --restore")
    hl.exec_cmd("dunst")
    hl.exec_cmd("udiskie")
end)

-- Optional session services (uncomment if installed):
-- hl.on("hyprland.start", function()
--     hl.exec_cmd("systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP")
--     hl.exec_cmd("dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP=Hyprland")
--     hl.exec_cmd("/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1")
-- end)

-------------
---- INPUT --
-------------

hl.config({
    input = {
        kb_layout    = "pl", -- Polish programmer layout; Right Alt is AltGr
        kb_options   = "ctrl:nocaps", -- Caps Lock acts as an extra Ctrl
        follow_mouse = 0, -- click-to-focus, like opendwm
        repeat_rate  = 40,
        repeat_delay = 300,
        accel_profile = "flat",
    },
})

-----------------------
---- LOOK AND FEEL ----
-----------------------

hl.config({
    general = {
        gaps_in    = 10, -- gappx
        gaps_out   = 10,
        border_size = 2, -- borderpx
        -- Show directional resize cursors near window borders. This also
        -- permits unmodified border dragging; Super+RMB remains available.
        resize_on_border = true,
        hover_icon_on_border = true,
        extend_border_grab_area = 5,
        col = {
            active_border   = "rgb(7aa2f7)",
            inactive_border = "rgb(3b4261)",
        },
        layout = "master",
    },

    master = {
        mfact          = 0.60,
        orientation    = "left",
        new_status     = "slave",
        -- Match X11 attach behavior: new normal windows are the first
        -- visible stack client instead of landing behind older windows.
        new_on_top     = true,
        smart_resizing = false,
    },

    decoration = {
        rounding = 0,
        blur  = { enabled = false },
        shadow = { enabled = false },
    },

    animations = {
        enabled = true,
    },

    misc = {
        disable_hyprland_logo   = true,
        force_default_wallpaper = 0,
    },
})

-- Restrained window lifecycle animation: only opening and closing windows
-- fade and scale. Workspace switches, tiling geometry, borders, and layers
-- remain instant.
hl.curve("opendwm-window", {
    type = "bezier",
    points = { {0.2, 0}, {0, 1} },
})
hl.animation({ leaf = "global",     enabled = true,  speed = 1, bezier = "opendwm-window" })
hl.animation({ leaf = "windows",    enabled = false })
hl.animation({ leaf = "windowsIn",  enabled = true,  speed = 4, bezier = "opendwm-window", style = "popin 95%" })
hl.animation({ leaf = "windowsOut", enabled = true,  speed = 4, bezier = "opendwm-window", style = "popin 95%" })
hl.animation({ leaf = "fadeIn",     enabled = true,  speed = 4, bezier = "opendwm-window" })
hl.animation({ leaf = "fadeOut",    enabled = true,  speed = 4, bezier = "opendwm-window" })
hl.animation({ leaf = "border",     enabled = false })
hl.animation({ leaf = "layers",     enabled = false })
hl.animation({ leaf = "workspaces", enabled = false })

-------------
---- RULES ---
-------------

-- Scratchpad classes (ghostty --class sets the Wayland app_id)
hl.window_rule({
    name  = "opendwm-scratch-terminal",
    match = { class = [[^(opendwm\.scratch-terminal)$]] },
    float  = true,
    center = true,
    size   = "(monitor_w*0.65) (monitor_h*0.60)",
})

hl.window_rule({
    name  = "opendwm-cliamp",
    match = { class = [[^(opendwm\.cliamp)$]] },
    float  = true,
    center = true,
    size   = "(monitor_w*0.65) (monitor_h*0.60)",
})

-- Floating rules from x11/config.h
hl.window_rule({
  name  = "opendwm-float-1password",
    match = { class = [[^(1[Pp]assword)$]] },
    float = true,
})

-- Native file chooser observed from Helium. It is already floating, but
-- defaults to the full work area in monocle; give new portal dialogs a
-- useful centered size while keeping them manually resizable.
hl.window_rule({
    name = "opendwm-gtk-portal-file-chooser",
    match = { class = [[^xdg-desktop-portal-gtk$]] },
    float = true,
    center = true,
    size = "(monitor_w*0.60) (monitor_h*0.60)",
})

-- Xwayland modal dialogs can otherwise enter the tiled monocle stack and
-- fill the workspace. Keep them floating and centered in every layout.
hl.window_rule({
    name = "opendwm-xwayland-modal-dialogs",
    match = {
        xwayland = true,
        modal = true,
    },
    float = true,
    center = true,
})

-- Floating windows keep rounded corners in every layout, including
-- scratchpads and portal dialogs. Fullscreen windows stay square.
hl.window_rule({
    name = "opendwm-floating-rounding",
    match = {
        float = true,
        fullscreen = false,
    },
    rounding = 8,
})

-- Layout policy. Monocle is gapless and borderless for tiled windows only;
-- floating scratchpads and normal fullscreen handling keep their own rules.
local tile_gaps = 10
local monocle_active = false
local monocle_border = hl.window_rule({
    name = "opendwm-monocle-border",
    match = {
        float = false,
        fullscreen = false,
    },
    border_size = 0,
})
monocle_border:set_enabled(false)

-- Rounded corners are a tiling-only treatment. Monocle intentionally stays
-- square and gapless; floating windows retain their own appearance.
local tiled_rounding = hl.window_rule({
    name = "opendwm-tiled-rounding",
    match = {
        float = false,
        fullscreen = false,
    },
    rounding = 8,
})
tiled_rounding:set_enabled(true)

function opendwm_set_layout(layout)
    if layout == "monocle" then
        monocle_active = true
        monocle_border:set_enabled(true)
        tiled_rounding:set_enabled(false)
        hl.config({
            general = {
                layout = "monocle",
                gaps_in = 0,
                gaps_out = 0,
            },
        })
    else
        monocle_active = false
        monocle_border:set_enabled(false)
        tiled_rounding:set_enabled(true)
        hl.config({
            general = {
                layout = "master",
                gaps_in = tile_gaps,
                gaps_out = tile_gaps,
            },
        })
    end

    -- Rule state and general layout settings schedule property refreshes
    -- independently. Flush only after all final values are in place so
    -- clients receive one final, coherent configure after a mode switch.
    hl.exec_scheduled_prop_refresh_immediately()
end

-- Called by scripts/adjust-gaps through hyprctl eval. Gap bindings are a
-- no-op in monocle so zero applied gaps cannot overwrite the tile baseline.
function opendwm_adjust_gaps(delta)
    if monocle_active then
        return
    end
    tile_gaps = math.max(0, math.min(50, tile_gaps + delta))
    hl.config({
        general = {
            gaps_in = tile_gaps,
            gaps_out = tile_gaps,
        },
    })
end

-------------
---- BINDS ---
-------------

local mod = "SUPER"

-- Workspaces 1-9, 0 (0 = workspace 10), like dwm tags.
-- Move window to workspace and follow (tag + view).
for i = 1, 10 do
    local key = i % 10
    hl.bind(mod .. " + " .. key, hl.dsp.focus({ workspace = i }))
    hl.bind(mod .. " + SHIFT + " .. key,
            hl.dsp.exec_cmd(script("move-to-workspace") .. " " .. i))
end

-- Focus cycling (tiled only; in monocle this cycles the monocle stack)
hl.bind(mod .. " + j", hl.dsp.window.cycle_next({ next = true, tiled = true }))
hl.bind(mod .. " + k", hl.dsp.window.cycle_next({ next = false, tiled = true }))

-- Stack movement
hl.bind(mod .. " + SHIFT + j", hl.dsp.window.swap({ next = true }))
hl.bind(mod .. " + SHIFT + k", hl.dsp.window.swap({ prev = true }))

-- Terminal / close / quit / promote master
hl.bind(mod .. " + Return", hl.dsp.exec_cmd(script("launch-normal") .. " ghostty"))
hl.bind(mod .. " + c", hl.dsp.window.close())
hl.bind(mod .. " + SHIFT + q", hl.dsp.exit())
hl.bind(mod .. " + SHIFT + Return", hl.dsp.layout("swapwithmaster"))

-- Layouts: tile (master) / monocle
hl.bind(mod .. " + t", function() opendwm_set_layout("master") end)
hl.bind(mod .. " + m", function() opendwm_set_layout("monocle") end)

-- Toggle bar (Quickshell IPC; starts the shell if not running)
hl.bind(mod .. " + b", hl.dsp.exec_cmd(script("toggle-bar")))

-- Master factor (approximation of incmfact: resize in 40px steps)
hl.bind(mod .. " + h", hl.dsp.window.resize({ x = -40, y = 0, relative = true }))
hl.bind(mod .. " + l", hl.dsp.window.resize({ x = 40, y = 0, relative = true }))

-- Gaps (incgaps +-2, clamped to 0-50 like maxgaps)
hl.bind(mod .. " + minus",           hl.dsp.exec_cmd(script("adjust-gaps") .. " -2"))
hl.bind(mod .. " + KP_Subtract",     hl.dsp.exec_cmd(script("adjust-gaps") .. " -2"))
hl.bind(mod .. " + equal",           hl.dsp.exec_cmd(script("adjust-gaps") .. " 2"))
hl.bind(mod .. " + SHIFT + equal",   hl.dsp.exec_cmd(script("adjust-gaps") .. " 2"))
hl.bind(mod .. " + KP_Add",          hl.dsp.exec_cmd(script("adjust-gaps") .. " 2"))

-- Scratchpads (first use launches, then show/hide)
hl.bind(mod .. " + SHIFT + u", hl.dsp.exec_cmd(
    script("scratchpad") .. " cliamp opendwm.cliamp ghostty --class=opendwm.cliamp --window-width=120 --window-height=35 -e cliamp"))
hl.bind(mod .. " + SHIFT + i", hl.dsp.exec_cmd(
    script("scratchpad") .. " scratch-terminal opendwm.scratch-terminal ghostty --class=opendwm.scratch-terminal --window-width=120 --window-height=35"))

-- Menus and launchers (dmenu via Xwayland; helpers must be Wayland-compatible)
hl.bind(mod .. " + CTRL + b", hl.dsp.exec_cmd(script("launch-normal") .. " helium-browser"))
hl.bind(mod .. " + p",        hl.dsp.exec_cmd(script("launch-normal") .. " " .. script("launcher") .. " drun"))
hl.bind(mod .. " + SHIFT + p", hl.dsp.exec_cmd(script("launch-normal") .. " " .. script("launcher") .. " run"))
hl.bind(mod .. " + r",        hl.dsp.exec_cmd(script("launch-normal") .. " record-menu"))
hl.bind(mod .. " + SHIFT + r", hl.dsp.exec_cmd(script("launch-normal") .. " record-menu stop"))
hl.bind(mod .. " + s",        hl.dsp.exec_cmd(script("launch-normal") .. " sshot-menu"))
hl.bind(mod .. " + x",        hl.dsp.exec_cmd(script("launch-normal") .. " power-menu"))
hl.bind(mod .. " + n",        hl.dsp.exec_cmd(script("launch-normal") .. " notes-menu"))
hl.bind("ALT + l",            hl.dsp.exec_cmd(script("launch-normal") .. " links-menu"))
hl.bind(mod .. " + CTRL + p", hl.dsp.exec_cmd(script("launch-normal") .. " 1password"))

-- Volume / media
hl.bind("XF86AudioRaiseVolume", hl.dsp.exec_cmd("wpctl set-volume --limit 1.0 @DEFAULT_AUDIO_SINK@ 5%+"), { repeating = true })
hl.bind("XF86AudioLowerVolume", hl.dsp.exec_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-"),             { repeating = true })
hl.bind("XF86AudioMute",        hl.dsp.exec_cmd("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"))
hl.bind("XF86AudioPlay",        hl.dsp.exec_cmd("playerctl play-pause"))
hl.bind("XF86AudioNext",        hl.dsp.exec_cmd("playerctl next"))
hl.bind("XF86AudioPrev",        hl.dsp.exec_cmd("playerctl previous"))

-- Dictation: press F9 to start, release to stop.
-- release fires on release; ignore_mods ignores modifiers held at release
-- time; transparent prevents keybind shadowing from swallowing the stop.
-- No autorepeat by default, so holding F9 does not retrigger start.
hl.bind("F9", hl.dsp.exec_cmd("dictation start"))
hl.bind("F9", hl.dsp.exec_cmd("dictation stop"),
        { release = true, ignore_mods = true, transparent = true })

-- Mouse: move/resize floating windows
hl.bind(mod .. " + mouse:272", hl.dsp.window.drag(),   { mouse = true })
hl.bind(mod .. " + mouse:273", hl.dsp.window.resize(), { mouse = true })
