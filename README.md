# OpenDWM

OpenDWM is a keyboard-driven tiling desktop inspired by dwm. It favors a
small, predictable workspace over a full desktop environment: numbered
workspaces, a master/stack layout, monocle, persistent floating scratchpads,
and a compact status bar.

It began as a standalone X11 window manager written in C. The Wayland version
recreates the same workflow with Hyprland for window management and
Quickshell for the bar, rather than implementing a second compositor.

## Features

- Master/stack tiling and gapless, borderless monocle.
- Ten numbered workspaces with move-and-follow navigation.
- Floating rules, fullscreen handling, and persistent scratchpads.
- Keyboard-first focus, stack movement, master promotion, and gap controls.
- Compact status bar with workspace state, layout, clock, audio, memory,
  recording, and dictation indicators.
- Source configuration for X11; live Lua/QML configuration for Wayland.

Some integrations, including recording, screenshots, notes, links, and
dictation, delegate to external helper commands. See the backend documentation
for their requirements.

## Backends

- [X11](x11/README.md): the original C99 tiling window manager with its own
  built-in bar and source configuration.
- [Wayland](wayland/README.md): a Hyprland Lua configuration, Quickshell bar,
  and helper scripts that recreate the OpenDWM workflow.

## Quick Start

Build the X11 window manager from the repository root:

```sh
make
sudo make install
```

The root Makefile forwards these commands to `x11/` for compatibility.

Set up the Wayland configuration separately:

```sh
wayland/setup.sh --check
wayland/setup.sh
```

The two backends are independent. Installing or running the Wayland setup
does not modify the X11 source, local X11 configuration, or installed X11
binary.

## Configuration

X11 configuration lives in `x11/config.h` and takes effect after rebuilding
with `make`. Wayland configuration lives in `wayland/hyprland/hyprland.lua`;
apply changes in a running session with:

```sh
hyprctl reload
```

Quickshell reloads its QML bar configuration automatically. See the backend
READMEs for installation, session startup, testing, and rollback details.
