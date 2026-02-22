# AGENTS

This file is for agentic coding tools working in this repository. It documents
how to build, how configuration is intended to work, and the local code style.

## Repository Overview
- Single-binary X11 tiling window manager written in C (C99).
- Source-configured via `config.def.h` (defaults) and `config.h` (local override).
- Build system is a small `Makefile` (no tests/lint targets).

## Build / Lint / Test

### Build
- `make`
  - Builds `opendwm`.
  - Auto-creates `config.h` from `config.def.h` if missing.
- `make install`
  - Installs to `$(PREFIX)/bin` (default `/usr/local/bin`).
- `make clean`
  - Removes the `opendwm` binary.

### Lint
- None. There are no lint targets or scripts in this repo.

### Tests
- None. There are no test targets or scripts in this repo.
- Running a “single test” is not applicable.

## Configuration Workflow
- `config.def.h` is the template/default configuration tracked in git.
- `config.h` is the local override, ignored by git.
- The build includes `config.h` if present, otherwise falls back to
  `config.def.h`.
- `make` will create `config.h` once (it will not overwrite your changes).

Recommended local workflow:
1) `make`
2) Edit `config.h`
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

### `opendwm.c`
- Main X11 event loop and window management logic.
- Avoid heavy refactors without strong reason; keep functions small and direct.
- Follow existing style for conditionals and resource checks.

### `config.def.h` / `config.h`
- `config.def.h` should remain a sane, minimal default.
- `config.h` can override any defaults and contain user-specific commands.
- Keep `rules` sentinel in both files.

## Dependencies
- X11 development headers
- Xft development headers
- `pkg-config`

## Cursor / Copilot Rules
- No Cursor rules found (`.cursor/rules/`, `.cursorrules`).
- No Copilot instructions found (`.github/copilot-instructions.md`).
