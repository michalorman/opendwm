# OpenDWM

OpenDWM is a small, fast, tiling window manager for X11 with a built-in status bar,
simple layouts, and a source-configurable workflow inspired by dwm.

## Features
- Tiling and monocle layouts
- Minimal, readable C code
- Configurable keybindings and commands via a header file
- Built-in status bar with clock and system info

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

## Running

Add this to your `.xinitrc` (or configure your display manager):

```sh
exec opendwm
```

Then start X with `startx`.
