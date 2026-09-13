# AGENTS

This file is for agentic coding tools working in this repository. It documents
how to build, how configuration is intended to work, and the local code style.

## Repository Overview
- `x11/`: C99 X11 tiling window manager, source-configured through headers.
- `wayland/`: Hyprland Lua configuration, Quickshell bar, and helper scripts.
- Root `Makefile` forwards X11 build commands to `x11/`; it never runs the
  Wayland setup. Backend guides are `x11/README.md` and `wayland/README.md`.

## Build / Lint / Test

### Build
- `make`
  - Forwards to `make -C x11`; builds `x11/opendwm`.
  - Auto-creates `x11/config.h` from `x11/config.def.h` if missing.
- `make install`
  - Installs to `$(PREFIX)/bin` (default `/usr/local/bin`).
- `make clean`
  - Removes `x11/opendwm`.

### Lint
- None. There are no lint targets or scripts in this repo.

### Tests
- The X11 C build has no automated tests.
- Wayland helpers: `python3 -B -m unittest discover -s wayland/tests -v`.
  These use mocked compositor/shell processes, not a live desktop. Requires
  Python 3, Bash, jq and coreutils; Node.js enables the QML JavaScript test.
- Run one module with `python3 -B -m unittest discover -s wayland/tests -p test_bar.py -v`.

## Configuration Workflow
- `x11/config.def.h` is the template/default configuration tracked in git.
- `x11/config.h` is the local override, ignored by git.
- The build includes `x11/config.h` if present, otherwise falls back to
  `x11/config.def.h`.
- `make` creates `x11/config.h` once (it will not overwrite your changes).

Recommended local workflow:
1) `make`
2) Edit `x11/config.h`
3) `make` again (then `make install` if needed)

## Code Style (C)

### Formatting
- Indentation: 2 spaces.
- Braces: K&R style, opening brace on the same line.
- Line length: keep to a readable width; wrap long conditionals logically.
- One statement per line (avoid comma expressions).
- Use blank lines to separate logical blocks.

### Naming Conventions
- Functions: lower_snake_case (e.g., `updateclock`, `movestack`).
- Types: `typedef struct` and `typedef union` with capitalized names
  (`Client`, `Arg`, `Rule`, `Key`).
- Globals: `static` globals in lowercase with underscores when needed.
- Constants/macros: uppercase (`TAGMASK`, `LENGTH`, `TAGKEYS`).

### Types / Const
- Use `static` for file-local functions and globals.
- Prefer `const` for config values and read-only pointers.
- Use `unsigned int` for masks and sizes as in existing code.

### Error Handling
- Early-return for recoverable errors and invalid inputs.
- Fatal conditions call `die("message")`.
- Avoid silent failures in core paths; log to `stderr` when needed.

### Memory / Resources
- Free X11 resources where appropriate (e.g., `XFree`, `XftColorFree`).
- Clean up in `cleanup()` on exit.

### Pattern Usage
- Macros `LENGTH` and `TAGKEYS` used for arrays and tag bindings.
- Config arrays (`rules`, `keys`) are defined in config headers.
- `rules` uses a sentinel entry `{ NULL, 0 }` and is iterated
  until `rules[i].class == NULL`.

## File-Specific Notes

### `x11/opendwm.c`
- Main X11 event loop and window management logic.
- Avoid heavy refactors without strong reason; keep functions small and direct.
- Follow existing style for conditionals and resource checks.

### `x11/config.def.h` / `x11/config.h`
- `x11/config.def.h` should remain a sane, minimal default.
- `x11/config.h` can override any defaults and contain user-specific commands.
- Keep `rules` sentinel in both files.

## Dependencies
- X11 development headers
- Xft development headers
- `pkg-config`

## Cursor / Copilot Rules
- No Cursor rules found (`.cursor/rules/`, `.cursorrules`).
- No Copilot instructions found (`.github/copilot-instructions.md`).
