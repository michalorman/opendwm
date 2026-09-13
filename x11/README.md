# OpenDWM X11

OpenDWM is a small, source-configured X11 tiling window manager with a
built-in status bar and a workflow inspired by dwm.

## Dependencies

- X11 development headers
- Xft development headers
- pkg-config

On Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config libx11-dev libxft-dev
```

## Build

From the repository root:

```sh
make
```

Or explicitly:

```sh
make -C x11
```

The first build creates `x11/config.h` from `x11/config.def.h` if it is
missing. `x11/config.h` is local and ignored by Git. Edit it, rebuild, and
install when ready:

```sh
make
sudo make install
```

The installed executable remains `/usr/local/bin/opendwm` by default.

## Configuration

- `x11/config.def.h`: tracked default configuration.
- `x11/config.h`: ignored local configuration.
- `x11/examples/config.h`: richer reference configuration.

Rules must retain the `{ NULL, 0 }` sentinel. Scratchpads match a unique
X11 class and instance; later toggles hide/show the same running process.

## Testing

There is no automated X11 test suite. Test safely in Xephyr:

```sh
Xephyr :1 -screen 1280x720
DISPLAY=:1 ./x11/opendwm
```

## Running

Add the installed binary to `.xinitrc` or configure an X11 display-manager
session:

```sh
exec opendwm
```
