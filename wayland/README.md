# Wayland Port: Hyprland + Quickshell

This directory ports the opendwm desktop to Wayland:

- **Hyprland** replaces the X11 window management in `x11/opendwm.c`.
- **Quickshell** replaces the built-in X11 bar.
- Bash, POSIX shell, and Python helpers in `wayland/scripts/` cover behavior Hyprland
  cannot express natively (scratchpads, tag+follow, gap clamping).

The X11 build is untouched and remains available as a fallback.

## Layout

```
wayland/
  hyprland/hyprland.lua         compositor configuration (Lua)
  quickshell/shell.qml          bar (workspaces, clock, status)
  scripts/                      integration helpers (see hyprland.lua)
  session/opendwm-wayland.desktop  display-manager session entry
```

## Dependencies

Required:

- `Hyprland` **>= 0.56** (the configuration is Lua; Hyprlang `.conf`
  support is deprecated in 0.56 and removed in 0.57)
- `quickshell` **>= 0.3.1** (built with Hyprland and PipeWire support;
  Lua-aware `Hyprland.usingLua` IPC is required)
- `start-hyprland` (session wrapper, shipped with Hyprland)
- `wireplumber` / `wpctl`, `playerctl`
- `jq` (used by the scratchpad and gap scripts)
- `flock` (util-linux; serializes the helper scripts)
- Bash, Python 3, and GNU coreutils (`timeout`, `stat`, `readlink`)
- `dmenu` + `j4-dmenu-desktop` for the launcher bindings (runs through
  Xwayland; tofi/bemenu are fallback backends)

The compatibility baseline was source-checked against Hyprland 0.56.2 and
Quickshell 0.2.1/0.3.1. In Lua mode `hyprctl keyword` is rejected — the
helpers use `hyprctl eval`/`hl.dsp.*` expressions instead.

Recommended:

- `xdg-desktop-portal-hyprland` (screen sharing/recording)
- `xdg-desktop-portal-gtk` (file dialogs)
- a polkit authentication agent
- `hyprlock` + `hypridle` or `swaylock` + `swayidle` (session locking)
- `ghostty` (terminal; its `--class` flag sets the Wayland `app_id`)

Existing helper commands (`record-menu`, `voxtype`, `dictation`,
`notes-menu`, `sshot-menu`, `power-menu`, `links-menu`) are reused as-is.
**Audit each for Wayland support** — they are outside this repo and were
written for X11. Screen capture helpers in particular need PipeWire/portal
support (e.g. `grim`/`slurp`, `wf-recorder`).

## Setup

The installer detects and reports missing packages (it never installs
them), backs up existing configuration instead of overwriting it, links
this repo's config, and installs the session files:

```sh
wayland/setup.sh            # check dependencies, then configure everything
wayland/setup.sh --check    # verify-only, changes nothing
wayland/setup.sh --force    # skip confirmation prompts (still backs up)
```

It refuses to run as root and uses `sudo` only for the two session files
(credentials are validated before anything changes). All confirmations
happen before any mutation; a declined answer changes nothing. An
existing `~/.config/hypr/hyprland.lua` is moved to a timestamped `.bak`
before linking; foreign files are never deleted. A stale `hyprland.conf`
symlink from an older opendwm install is removed; unrelated links are
left alone. Session files are tracked with a content manifest: managed
older versions are updated in place, while modified or unmanaged files
require confirmation and are backed up first. If any install step fails,
replacements are rolled back — session files from their collision-free
backups, and the configuration (link, legacy link, OPENDWM edit) to its
previous state; rollback problems are reported loudly, never swallowed.
Re-running is idempotent. The installer does not run the development
test suite; run it manually with
`python3 -B -m unittest discover -s wayland/tests -v` if needed.

Keep the `wayland/` tree together: `start-bar` explicitly loads its
sibling `quickshell/shell.qml`, so no Quickshell config symlink is needed.
If you move the repo, re-run `setup.sh --force` so `local OPENDWM` is
updated (or edit it at the top of `hyprland.lua` manually).

### Manual setup (fallback)

If you prefer not to run the script, the equivalent steps are: link
`wayland/hyprland/hyprland.lua` to `~/.config/hypr/hyprland.lua`
(backing up any existing file first), point `local OPENDWM` in that file
at `wayland/scripts/`, and install the session files:

```sh
sudo install -m755 wayland/session/start-opendwm-wayland /usr/local/bin/start-opendwm-wayland
sudo install -Dm644 wayland/session/opendwm-wayland.desktop \
  /usr/share/wayland-sessions/opendwm-wayland.desktop
```

### First run

Run `start-opendwm-wayland` from a spare TTY (or select the installed
session in your display manager). It uses
`start-hyprland -- --config ...` to select `hyprland.lua` explicitly, so
an existing `hyprland.conf` or a differently located Lua config cannot
take precedence. Start a fresh session when upgrading from the old
bare-Quickshell startup; unmanaged old bar processes are not adopted or
terminated. Keep your X11 session (`.xinitrc` with `exec opendwm`)
intact until the Wayland session passes the checklist below.

### Display scaling

The config sets `scale = 1.6` on all outputs, approximating the previous
X11 session (`Xft.dpi = 164`). Fractional scales must divide both output
dimensions into integers or Hyprland substitutes another factor and
notifies you; on 3840×2560, 1.6 yields 2400×1600 logical. Qt/Wayland
apps scale natively; Xwayland apps (including dmenu) are scaled by the
compositor. Adjust the value — or add per-output `hl.monitor({...})`
entries — in `wayland/hyprland/hyprland.lua`. Avoid stacking additional
`GDK_SCALE`/`QT_SCALE_FACTOR` overrides on top.

## Behavior mapping

| opendwm (X11)                  | Wayland port                                             |
|--------------------------------|----------------------------------------------------------|
| Tags 1-9, 0                    | Workspaces 1-9, 10 (shown as `0`)                        |
| `Mod+Shift+n` tag + follow     | `movetoworkspace` (switches + focuses by default)        |
| Master/stack tiling            | `master` layout, `mfact = 0.60`, orientation left        |
| Monocle                        | `opendwm_set_layout`: gapless, borderless tiled windows  |
| `Mod+j/k` focus cycling        | `cyclenext` / `cyclenext, prev tiled` (tiled only)       |
| `Mod+h/l` incmfact +-0.05      | `resizeactive -/+40 0` (pixel approximation)             |
| `Mod+-/=` incgaps +-2, max 50  | remembered tile gaps; no-op while monocle is active       |
| Scratchpads (class+instance)   | Special workspaces + `scripts/scratchpad` (class only)   |
| Normal app launches            | `scripts/launch-normal` (never trapped on a special ws)  |
| Float rule for 1Password       | `windowrule` block with `match:class`                    |
| Click-to-focus                 | `input:follow_mouse = 0`                                 |
| Caps Lock                      | extra Ctrl via `input:kb_options = "ctrl:nocaps"`        |
| F9 press/release dictation     | `bind`/`bindrit` (release ignores modifiers + shadowing) |
| Bar (tags, layout, clock, ...) | Quickshell `PanelWindow` per output                      |
| `Mod+b` toggle bar             | Quickshell IPC (`scripts/toggle-bar`)                    |
| dmenu launchers                | dmenu via Xwayland; tofi/bemenu fallback                  |

## Known differences

These are deliberate or unavoidable; revisit only if they matter in
practice:

- **Tags vs workspaces.** dwm tags are bitmasks (a window can have several,
  several can be visible). Hyprland workspaces are exclusive. Nothing in
  the default config used multi-tag features, so this should be invisible.
- **Lua configuration requires Hyprland >= 0.56.** The layout binds call
  `hl.config({general={layout=...}})` directly; the bar polls the layout to
  switch its background between tiling and monocle modes.
- **Gap/layout mutations use `hyprctl eval`**, not `keyword`, which Lua
  mode rejects. Running these helpers in a legacy `.conf` session will
  fail; the two configuration formats are not interchangeable at runtime.
- **Monocle is intentionally gapless and borderless.** `Mod+m` disables
  borders only for non-floating, non-fullscreen windows, sets both gaps to
  zero, and remembers the current tile gap. `Mod+t` restores that gap and
  normal borders. Gap bindings do nothing in monocle, so they cannot replace
  the remembered tile gap with zero.
- **Caps Lock is Ctrl** in this Wayland session (`ctrl:nocaps`). It does not
  swap or disable either physical Ctrl key, and does not change X11 settings.
- **Floating border resize** is enabled with a 5-pixel extended grab area.
  The cursor changes near a resizable edge and dragging that edge works
  without Super; Super+right-drag remains available.
- **Master factor steps** are fixed 40 px instead of 5% of the work area.
- **Resize grip.** opendwm only resized from a 12 px bottom-right corner;
  Hyprland `bindm` resizes from anywhere with `Mod+Button3`.
- **Focus/stack order** on close and after `swapnext` follows Hyprland
  semantics and may differ in corner cases (e.g. transient parents).
- **Scratchpad identity** uses the Wayland `app_id`/class only; the X11
  class+instance pair does not exist on Wayland. The special workspace
  stays visible across workspace switches until toggled, matching dwm.
  A scratchpad moved to a normal workspace is moved back on next toggle.
- **Fullscreen bar hiding** is per output: a bar hides when the workspace
  shown on its own output — including a displayed special (scratchpad)
  workspace — has an actual fullscreen window. Maximized or
  compositor-hidden windows do not suppress the bar. Client and monitor
  IPC is refreshed on relevant events and every two seconds.
- **Layout/gaps** are global (as in opendwm), but Hyprland applies layout
  per workspace when set per workspace; this port switches the global
  layout key to keep bar state simple.
- **Dialogs/transients** rely on Hyprland defaults rather than the C
  code's explicit floating rules for `_NET_WM_WINDOW_TYPE` hints.

## Testing

Run the mocked helper regression suite without changing the current desktop:

```sh
python3 -B -m unittest discover -s wayland/tests -v
```

These tests do not replace live Quickshell loading or compositor testing.
Node.js enables the optional QML JavaScript logic test. The QML file also
passes the local `qmlformat` parser, including the typed string returns
used by the IPC acknowledgement protocol.

Early iteration can run nested:

```sh
start-opendwm-wayland
```

Acceptance checklist (from the migration plan):

- Workspaces: empty, single-window, crowded; `0` maps to workspace 10.
- Master/monocle switch (`Mod+t`/`Mod+m`); the bar background is transparent
  in tiling and opaque in monocle.
- In monocle, tiled windows have no border or gaps; floating scratchpads
  retain borders; returning to master restores the adjusted tile gap.
- Caps Lock behaves as Ctrl in native Wayland and Xwayland applications.
- Focus cycling `Mod+j/k`, stack swap `Mod+Shift+j/k`, `Mod+Shift+Return`.
- Scratchpads: first launch, repeat toggling, survive workspace changes,
  close then relaunch.
- F9 press/release starts and stops dictation; holding F9 does not
  retrigger start.
- Bar: workspace colors reflect empty, occupied, and active states; clock,
  volume (after `wpctl` changes), RAM, recording and dictation states;
  `Mod+b` toggles it.
- Fullscreen hides the bar and restores it afterwards.
- Volume/media keys; volume shown updates.
- Native Wayland and Xwayland clients; clipboard between them.
- Output hotplug, suspend/resume.
- Missing helpers (e.g. `record-menu` absent): bar stays up, status shows
  empty state, and restarts when the helper returns.

## Failure Recovery

- Helpers require the session's `HYPRLAND_INSTANCE_SIGNATURE` and a private,
  user-owned `XDG_RUNTIME_DIR` (mode 0700). Locks and bar state are scoped
  to the compositor instance, including nested sessions.
- An unacknowledged bar command reports failure and retains the recorded
  process. It does not launch a duplicate or retry a possibly applied toggle.
  Quickshell's own crash recovery keeps the same PID; bar control survives
  it because identity is bound to the session environment and the IPC
  handshake, not the original argument vector.
- The scratchpad helper queries mapped clients only. This avoids a Hyprland
  0.56.2 crash while serializing an unmapped client with an idle inhibitor.
- A scratchpad launch whose outcome is uncertain leaves a `.pending` marker
  whose path appears in subsequent errors. A mapped client clears it, as do
  outcomes known to be safe (dispatch never sent, or explicitly rejected).
  Only remove the reported marker manually after confirming the attempted
  launch is no longer running; otherwise another activation could duplicate it.
  Launch and recovery ensure visibility instead of toggling, so an already
  open special workspace is never closed by activating its scratchpad.
- Bar startup registers the new shell before returning. Errors and catchable
  signals during registration terminate the new child rather than leaving an
  unmanaged duplicate; cleanup is shielded from repeated signals and always
  completes (SIGTERM, then SIGKILL + reap). A SIGKILL to the helper in that
  narrow window can still orphan a bar, which a later invocation will not
  adopt (it starts a new one).
- Gap adjustment delegates to the Lua layout policy. It adjusts the
  remembered tile gap atomically within the configuration state and becomes
  a safe no-op while monocle is active.

## Rollback

Log out and log back into X11 (`startx` with `exec opendwm`, or the X11
session entry). Nothing in this port modifies the X11 build or
`x11/config.h`.

To remove the Wayland setup itself:

```sh
wayland/setup.sh --uninstall          # links + session files, restores backup
wayland/setup.sh --uninstall --purge  # also clears runtime state/markers
```

It only removes repo-owned artifacts: the config symlink (if it points
here), session files that still match the install manifest (unmodified
since install), and — with `--purge` — `$XDG_RUNTIME_DIR/opendwm-*`
locks, state, and pending markers. `--purge` is refused while any
Hyprland session is running, because live helpers hold locks and state
there. Config backups made during install are restored automatically for
both the Lua and legacy `.conf` generations (chosen by backup-name
timestamp). Backups of replaced session files (`*.bak-<timestamp>` next
to the originals) are never deleted. If a removal fails, uninstall exits
nonzero and keeps the manifest so a later run can finish the job.
