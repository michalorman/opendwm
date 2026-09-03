# OpenDWM

OpenDWM is a small, fast, tiling window manager for X11 with a built-in status bar,
simple layouts, and a source-configurable workflow inspired by dwm.

## Features
- Tiling and monocle layouts
- Minimal, readable C code
- Configurable keybindings and commands via a header file
- Built-in status bar with clock and system info
- Floating scratchpads that stay running while hidden

## Dependencies
- X11 development headers
- Xft development headers
- pkg-config

On Debian/Ubuntu, for example:

```sh
sudo apt install build-essential pkg-config libx11-dev libxft-dev
```

## Build

```sh
make
```

To install system-wide:

```sh
sudo make install
```

## Configuration

The default configuration lives in `config.def.h`. Your local overrides go in
`config.h`, which is ignored by git.

Recommended workflow:

```sh
make
```

This creates `config.h` from `config.def.h` if it does not exist. Then edit
`config.h` and rebuild:

```sh
make
```

Alternatively, you can copy manually:

```sh
cp config.def.h config.h
```

### Scratchpads

Scratchpads are configured with a unique X11 class and instance plus the command
that creates the window. The first press of a scratchpad binding launches and
shows its command; later presses hide or show that same window. Hidden
scratchpads are moved off-screen rather than closed, so their processes continue
to run. They are floating, start centered at the terminal's configured size, and
can be moved with `Mod+Button1`.

The local example defines these bindings:

- `Mod+Shift+u`: Alacritty running `cliamp`
- `Mod+Shift+i`: an Alacritty shell

Each scratchpad's class and instance must match the command that launches it.

## Testing

Xephyr runs a nested, visible X server and is useful for testing OpenDWM without
replacing the active window manager. Xvfb provides a headless X server for
automated tests. On Arch Linux, install them with:

```sh
sudo pacman -S xorg-server-xephyr xorg-server-xvfb
```

To start a nested test session, run Xephyr and OpenDWM from separate terminals:

```sh
Xephyr :1 -screen 1280x720
DISPLAY=:1 ./opendwm
```

## Running

Add this to your `.xinitrc` (or configure your display manager):

```sh
exec opendwm
```

Then start X with `startx`.
