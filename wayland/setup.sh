#!/bin/sh
# setup.sh [--check] [--force] [--uninstall] [--purge]
# Install, verify, or roll back the opendwm Wayland (Hyprland+Quickshell)
# configuration. Detects and reports missing packages but never installs
# them. Idempotent; never touches X11 configuration.
set -eu

WAYLAND_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SCRIPTS_DIR="$WAYLAND_DIR/scripts"
CONF_SRC="$WAYLAND_DIR/hyprland/hyprland.lua"
SESSION_SRC="$WAYLAND_DIR/session"
CONFIG_HOME=${XDG_CONFIG_HOME:-$HOME/.config}
HYPR_DIR="$CONFIG_HOME/hypr"
LINK="$HYPR_DIR/hyprland.lua"
LEGACY_LINK="$HYPR_DIR/hyprland.conf"
LEGACY_CONF_TARGET="$WAYLAND_DIR/hyprland/hyprland.conf"
MANIFEST="$HYPR_DIR/.opendwm-session-manifest"
BIN_DIR=${OPENDWM_SETUP_BIN_DIR:-/usr/local/bin}
SESSION_DIR=${OPENDWM_SETUP_SESSION_DIR:-/usr/share/wayland-sessions}

mode=install force=0 purge=0
for arg in "$@"; do
  case $arg in
    --check) mode=check ;;
    --uninstall) mode=uninstall ;;
    --purge) purge=1 ;;
    --force) force=1 ;;
    -h | --help)
      sed -n '2,5p' "$0"
      exit 0
      ;;
    *) echo "setup.sh: unknown option '$arg'" >&2; exit 2 ;;
  esac
done

info() { printf 'setup: %s\n' "$*"; }
warn() { printf 'setup: WARNING: %s\n' "$*" >&2; }
die() { printf 'setup: ERROR: %s\n' "$*" >&2; exit 1; }

confirm() {
  # confirm QUESTION -- ask unless --force or non-interactive
  [ "$force" -eq 1 ] && return 0
  [ -t 0 ] || return 1
  printf 'setup: %s [y/N] ' "$1" >&2
  read -r answer || return 1
  case $answer in y | Y | yes) return 0 ;; esac
  return 1
}

version_ge() {
  # version_ge CURRENT REQUIRED -- numeric major[.minor[.patch]] comparison.
  # Component presence is determined by delimiters; explicitly present but
  # empty components ("0.4.", "0..2") are malformed and rejected.
  cur=$1 req=$2
  c1=${cur%%.*} c2=0 c3=0
  case $cur in
    *.*.*) rest=${cur#*.}; c2=${rest%%.*} c3=${rest#*.}
      [ -n "$c2" ] && [ -n "$c3" ] || return 1 ;;
    *.*) c2=${cur#*.}
      [ -n "$c2" ] || return 1 ;;
  esac
  r1=${req%%.*} r2=0 r3=0
  case $req in
    *.*.*) rest=${req#*.}; r2=${rest%%.*} r3=${rest#*.}
      [ -n "$r2" ] && [ -n "$r3" ] || return 1 ;;
    *.*) r2=${req#*.}
      [ -n "$r2" ] || return 1 ;;
  esac
  [ -n "$c1" ] && [ -n "$r1" ] || return 1
  case $c1$c2$c3$r1$r2$r3 in *[!0-9]*) return 1 ;; esac
  [ "$c1" -gt "$r1" ] && return 0
  [ "$c1" -lt "$r1" ] && return 1
  [ "$c2" -gt "$r2" ] && return 0
  [ "$c2" -lt "$r2" ] && return 1
  [ "$c3" -ge "$r3" ]
}

# --- dependency detection -------------------------------------------------

check_deps() {
  missing=
  for cmd in hyprctl start-hyprland quickshell jq flock timeout python3 wpctl stat readlink; do
    command -v "$cmd" >/dev/null 2>&1 || missing="$missing $cmd"
  done
  if ! command -v Hyprland >/dev/null 2>&1; then
    missing="$missing Hyprland"
  fi
  drun_ok= run_ok=
  if command -v j4-dmenu-desktop >/dev/null 2>&1 && command -v dmenu >/dev/null 2>&1; then
    drun_ok=1
  fi
  command -v tofi-drun >/dev/null 2>&1 && drun_ok=1
  for cmd in dmenu_run bemenu-run tofi-run; do
    command -v "$cmd" >/dev/null 2>&1 && run_ok=1
  done

  [ -z "$missing" ] && return 0
  warn "missing required commands:$missing"
  info "on Arch, install with approximately:"
  info "  sudo pacman -S hyprland quickshell wireplumber jq util-linux python dmenu j4-dmenu-desktop"
  return 1
}

check_versions() {
  ok=0
  if command -v Hyprland >/dev/null 2>&1; then
    hv=$(Hyprland --version 2>/dev/null | sed -n 's/^Hyprland \([0-9][0-9.]*\).*/\1/p' | head -1)
    if [ -n "$hv" ] && version_ge "$hv" 0.56; then
      info "Hyprland $hv (>= 0.56 required for the Lua config)"
    else
      warn "Hyprland version '${hv:-unknown}' is older than 0.56 or unparseable"
      ok=1
    fi
  fi
  if command -v quickshell >/dev/null 2>&1; then
    qv=$(quickshell --version 2>/dev/null | sed -n 's/[^0-9]*\([0-9][0-9.]*\).*/\1/p' | head -1)
    if [ -n "$qv" ] && version_ge "$qv" 0.3.1; then
      info "quickshell $qv (>= 0.3.1 required for Lua-aware Hyprland IPC)"
    else
      warn "quickshell version '${qv:-unknown}' is older than 0.3.1 or unparseable"
      ok=1
    fi
  fi
  [ -n "$drun_ok" ] || warn "Mod+p (drun) needs dmenu+j4-dmenu-desktop or tofi (patched dmenu flags; see docs)"
  [ -n "$run_ok" ] || warn "Mod+Shift+p (run) needs dmenu_run, bemenu-run or tofi-run"
  if command -v fc-match >/dev/null 2>&1; then
    fc-match "JetBrainsMono Nerd Font" 2>/dev/null | grep -qi "nerd" \
      || warn "JetBrainsMono Nerd Font not found; bar icons will not render"
  fi
  return $ok
}

# --- configuration state --------------------------------------------------

link_state() {
  # prints: ok | missing | foreign-file | foreign-link
  if [ -L "$LINK" ]; then
    [ "$(readlink -f "$LINK")" = "$CONF_SRC" ] && { echo ok; return; }
    echo foreign-link
  elif [ -e "$LINK" ]; then
    echo foreign-file
  else
    echo missing
  fi
}

lua_escape() {
  # Escape a string for a Lua double-quoted string literal.
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

opendwm_path_ok() {
  [ "$(sed -n 's/^local OPENDWM = "\(.*\)"/\1/p' "$CONF_SRC" | head -1)" = "$(lua_escape "$SCRIPTS_DIR")" ]
}

fix_opendwm_path() {
  # Escape for Lua, then for the sed replacement text (&, |, backslash).
  escaped=$(lua_escape "$SCRIPTS_DIR" | sed 's/[&|\\]/\\&/g')
  sed -i "s|^local OPENDWM = .*|local OPENDWM = \"$escaped\"|" "$CONF_SRC"
}

# Hash via stdin: sha256sum backslash-escapes its output when the FILENAME
# contains special characters.
hash_of() { sha256sum < "$1" 2>/dev/null | awk '{print $1}'; }
# Manifest lines are "<hash><two spaces><path>"; paths may contain spaces.
manifest_hash() { awk -v p="$2" '{h=$1; sub(/^[^ ]+  /, ""); if ($0 == p) print h}' "$1" 2>/dev/null; }

write_manifest() {
  # NOTE: called in condition context, where set -e is disabled; every
  # step needs an explicit status check.
  a="$BIN_DIR/start-opendwm-wayland"
  b="$SESSION_DIR/opendwm-wayland.desktop"
  tmp="$MANIFEST.tmp"
  # Reject ANY preexisting temporary entry: a symlink here could rewrite
  # the current manifest before the atomic rename.
  [ ! -e "$tmp" ] && [ ! -L "$tmp" ] || return 1
  {
    echo "# opendwm wayland session manifest v1"
    printf '%s  %s\n' "$(hash_of "$a")" "$a"
    printf '%s  %s\n' "$(hash_of "$b")" "$b"
  } > "$tmp" || return 1
  mv "$tmp" "$MANIFEST" || return 1
}

session_files_state() {
  # prints: ok | outdated | missing | foreign | partial
  # "managed" means the on-disk files match the install manifest, proving
  # opendwm installed them and they were not modified since.
  a="$BIN_DIR/start-opendwm-wayland"
  b="$SESSION_DIR/opendwm-wayland.desktop"
  { [ -e "$a" ] || [ -L "$a" ] || [ -e "$b" ] || [ -L "$b" ]; } || { echo missing; return; }
  [ -f "$a" ] && [ -f "$b" ] || { echo partial; return; }
  cur_a=$(hash_of "$a") cur_b=$(hash_of "$b")
  repo_a=$(hash_of "$SESSION_SRC/start-opendwm-wayland")
  repo_b=$(hash_of "$SESSION_SRC/opendwm-wayland.desktop")
  managed=0
  if [ -f "$MANIFEST" ]; then
    man_a=$(manifest_hash "$MANIFEST" "$a")
    man_b=$(manifest_hash "$MANIFEST" "$b")
    [ -n "$man_a" ] && [ "$man_a" = "$cur_a" ] && [ -n "$man_b" ] && [ "$man_b" = "$cur_b" ] && managed=1
  elif [ "$cur_a" = "$repo_a" ] && [ "$cur_b" = "$repo_b" ]; then
    managed=1  # no manifest: adopt exact current matches
  fi
  [ "$managed" -eq 1 ] || { echo foreign; return; }
  if [ "$cur_a" = "$repo_a" ] && [ "$cur_b" = "$repo_b" ] && [ -x "$a" ]; then
    echo ok
  else
    echo outdated
  fi
}

# --- modes ----------------------------------------------------------------

run_check() {
  rc=0
  check_deps || rc=1
  check_versions || rc=1
  state=$(link_state)
  case $state in
    ok) info "config link: $LINK -> $CONF_SRC" ;;
    *) warn "config link: $state ($LINK)"; rc=1 ;;
  esac
  opendwm_path_ok && info "\$opendwm path: $SCRIPTS_DIR" \
    || { warn "\$opendwm does not point at $SCRIPTS_DIR"; rc=1; }
  sstate=$(session_files_state)
  [ "$sstate" = ok ] && info "session files: installed" \
    || { warn "session files: $sstate"; rc=1; }
  [ $rc -eq 0 ] && info "check passed" || warn "check found problems"
  return $rc
}

# Allocate a collision-free backup name for a session file.
alloc_backup() {
  candidate="$1.bak-$2"
  n=1
  # -e alone misses occupied names held by dangling symlinks.
  while [ -e "$candidate" ] || [ -L "$candidate" ]; do
    candidate="$1.bak-$2-$n"
    n=$((n + 1))
  done
  printf '%s' "$candidate"
}

# Restore one backup, reporting failure loudly (no silent || true).
restore_one() {
  if sudo mv "$1" "$2"; then
    info "restored $2 from backup"
    return 0
  fi
  warn "ROLLBACK FAILED: could not restore $2 from $1"
  return 1
}

# Install both session files with backups and rollback. The two
# destinations are handled explicitly (no word-splitting over paths).
# Returns nonzero on failure; rollback is best-effort but never silent.
install_session_files() {
  stamp=$(date +%Y%m%d-%H%M%S)
  la="$BIN_DIR/start-opendwm-wayland"
  de="$SESSION_DIR/opendwm-wayland.desktop"
  la_bak= de_bak=
  # -e alone misses dangling symlinks: they are real directory entries
  # that must be backed up before replacement.
  if [ -e "$la" ] || [ -L "$la" ]; then la_bak=$(alloc_backup "$la" "$stamp"); fi
  if [ -e "$de" ] || [ -L "$de" ]; then de_bak=$(alloc_backup "$de" "$stamp"); fi

  if [ -n "$la_bak" ] && ! sudo mv "$la" "$la_bak"; then
    warn "backup of $la failed; installation aborted, nothing replaced"
    return 1
  fi
  if [ -n "$de_bak" ] && ! sudo mv "$de" "$de_bak"; then
    rb=0
    [ -z "$la_bak" ] || restore_one "$la_bak" "$la" || rb=1
    warn "backup of $de failed; installation aborted"
    [ "$rb" -eq 0 ] || warn "ROLLBACK INCOMPLETE"
    return 1
  fi
  [ -z "$la_bak$de_bak" ] || info "previous session files backed up"

  rb=0
  if ! sudo install -m755 "$SESSION_SRC/start-opendwm-wayland" "$la"; then
    # No predecessor may exist: remove a partial destination.
    [ -n "$la_bak" ] || { sudo rm -f "$la" || { warn "ROLLBACK: could not remove partial $la"; rb=1; }; }
    [ -z "$la_bak" ] || restore_one "$la_bak" "$la" || rb=1
    [ -z "$de_bak" ] || restore_one "$de_bak" "$de" || rb=1
    warn "failed to install session launcher"
    [ "$rb" -eq 0 ] || warn "ROLLBACK INCOMPLETE"
    return 1
  fi
  if ! sudo install -Dm644 "$SESSION_SRC/opendwm-wayland.desktop" "$de"; then
    sudo rm -f "$la" || { warn "ROLLBACK: could not remove $la"; rb=1; }
    [ -n "$de_bak" ] || { sudo rm -f "$de" || { warn "ROLLBACK: could not remove partial $de"; rb=1; }; }
    [ -z "$la_bak" ] || restore_one "$la_bak" "$la" || rb=1
    [ -z "$de_bak" ] || restore_one "$de_bak" "$de" || rb=1
    warn "failed to install session entry"
    [ "$rb" -eq 0 ] || warn "ROLLBACK INCOMPLETE"
    return 1
  fi
  if ! write_manifest; then
    sudo rm -f "$la" "$de" || { warn "ROLLBACK: could not remove new session files"; rb=1; }
    [ -z "$la_bak" ] || restore_one "$la_bak" "$la" || rb=1
    [ -z "$de_bak" ] || restore_one "$de_bak" "$de" || rb=1
    warn "failed to write session manifest"
    [ "$rb" -eq 0 ] || warn "ROLLBACK INCOMPLETE"
    return 1
  fi
  return 0
}

run_install() {
  [ "$(id -u)" -ne 0 ] || die "do not run as root (sudo is used only for session files)"
  case $CONFIG_HOME in /*) ;; *) die "XDG_CONFIG_HOME must be an absolute path" ;; esac
  check_deps || die "install the missing packages above, then re-run setup.sh"
  check_versions || die "upgrade to Hyprland >= 0.56 and Quickshell >= 0.3.1 first"

  mkdir -p "$HYPR_DIR"

  # Gather state and obtain ALL confirmations before mutating anything:
  # a declined answer must leave the existing installation untouched.
  state=$(link_state)
  sstate=$(session_files_state)
  opendwm_fix=0
  opendwm_path_ok || opendwm_fix=1

  case $state in
    foreign-file | foreign-link)
      backup="$LINK.bak-$(date +%Y%m%d-%H%M%S)"
      [ ! -e "$backup" ] || backup="$backup-$$"
      confirm "existing $LINK ($state) will be moved to $backup. Continue?" \
        || die "aborted; nothing was changed"
      ;;
  esac
  if [ "$opendwm_fix" -eq 1 ]; then
    current=$(sed -n 's/^local OPENDWM = "\(.*\)"/\1/p' "$CONF_SRC" | head -1)
    confirm "OPENDWM is '$current'; point it at $SCRIPTS_DIR?" \
      || die "aborted; nothing was changed (edit local OPENDWM manually and re-run)"
  fi
  case $sstate in
    foreign | partial)
      confirm "session files exist but are not managed by opendwm; back up and replace them?" \
        || die "aborted; nothing was changed"
      ;;
  esac

  # Privileged preflight BEFORE any mutation: without working sudo there
  # is no safe way to proceed, so configuration must not be activated yet.
  if [ "$sstate" != ok ]; then
    command -v sudo >/dev/null 2>&1 || die "sudo is required to install session files"
    sudo -v 2>/dev/null || die "sudo authentication unavailable; nothing was changed"
  fi

  # --- mutations (tracked for rollback) ---
  legacy_removed=0
  conf_tmp=
  [ "$opendwm_fix" -eq 1 ] && { conf_tmp="$HYPR_DIR/.opendwm-lua-presed"; cp "$CONF_SRC" "$conf_tmp"; }

  # Idempotent: safe to run twice. Restores the OPENDWM edit, the config
  # link (or its backup), and the legacy link to their previous state.
  rollback_config() {
    trap - ERR INT TERM
    if [ -n "$conf_tmp" ] && [ -f "$conf_tmp" ]; then
      cp "$conf_tmp" "$CONF_SRC" && rm -f "$conf_tmp" \
        && info "restored repository config before OPENDWM edit" \
        || warn "ROLLBACK: could not restore $CONF_SRC (backup: $conf_tmp)"
      conf_tmp=
    fi
    case $state in
      missing)
        [ -L "$LINK" ] && [ "$(readlink -f "$LINK")" = "$CONF_SRC" ] && rm -f "$LINK"
        ;;
      foreign-file | foreign-link)
        [ -L "$LINK" ] && rm -f "$LINK"
        # -e alone is false for a backed-up DANGLING symlink.
        if [ -n "${backup:-}" ] && { [ -e "$backup" ] || [ -L "$backup" ]; }; then
          mv "$backup" "$LINK" || warn "ROLLBACK: could not restore $LINK (backup: $backup)"
        fi
        ;;
    esac
    if [ "$legacy_removed" -eq 1 ] && [ ! -e "$LEGACY_LINK" ] && [ ! -L "$LEGACY_LINK" ]; then
      ln -s "$LEGACY_CONF_TARGET" "$LEGACY_LINK" 2>/dev/null \
        || warn "ROLLBACK: could not restore legacy link"
    fi
  }

  on_mutation_error() {
    rollback_config
    die "installation failed; configuration restored to previous state"
  }
  trap 'on_mutation_error' ERR
  trap 'rollback_config; exit 1' INT TERM

  # Remove a stale .conf link from a previous OPENDWM install. Compare the
  # raw link target textually so a dangling link is still identified;
  # anything not positively ours is left alone.
  if [ -L "$LEGACY_LINK" ] && [ "$(readlink "$LEGACY_LINK")" = "$LEGACY_CONF_TARGET" ]; then
    rm "$LEGACY_LINK"
    legacy_removed=1
    info "removed stale hyprland.conf link (superseded by hyprland.lua)"
  elif [ -L "$LEGACY_LINK" ]; then
    info "leaving foreign hyprland.conf link untouched"
  fi
  case $state in
    ok) info "config link already in place" ;;
    missing)
      ln -s "$CONF_SRC" "$LINK"
      info "linked $LINK -> $CONF_SRC"
      ;;
    foreign-file | foreign-link)
      mv "$LINK" "$backup"
      ln -s "$CONF_SRC" "$LINK"
      info "backed up old config to $backup and linked ours"
      ;;
  esac

  if [ "$opendwm_fix" -eq 1 ]; then
    fix_opendwm_path
    info "OPENDWM now points at $SCRIPTS_DIR"
  fi

  case $sstate in
    ok)
      [ -f "$MANIFEST" ] || write_manifest
      info "session files already installed"
      ;;
    outdated | missing | foreign | partial)
      # Condition context: the function's own rollback runs; a nonzero
      # result then rolls back the configuration too (idempotently).
      if install_session_files; then
        [ "$sstate" = outdated ] && info "updated session files" \
          || info "installed session launcher and desktop entry"
      else
        rollback_config
        die "session installation failed; configuration restored to previous state"
      fi
      ;;
  esac
  trap - ERR INT TERM
  [ -z "$conf_tmp" ] || rm -f "$conf_tmp"

  info "done. Next steps:"
  info "  1. Smoke-test from a spare TTY: start-opendwm-wayland"
  info "  2. Full checklist and rollback: wayland/README.md"
  info "  3. Rollback anytime: $0 --uninstall"
}

run_uninstall() {
  [ "$(id -u)" -ne 0 ] || die "do not run as root"
  case $CONFIG_HOME in /*) ;; *) die "XDG_CONFIG_HOME must be an absolute path" ;; esac
  removed=0

  # Purge eligibility BEFORE any uninstall mutation: a refused purge must
  # not leave a half-uninstalled desktop. pgrep semantics: 0 = match,
  # 1 = no match, anything else = error (fail closed).
  if [ "$purge" -eq 1 ]; then
    command -v pgrep >/dev/null 2>&1 || die "--purge needs pgrep (procps-ng)"
    if pgrep -x Hyprland >/dev/null 2>&1; then
      die "--purge refused: a Hyprland session is running; log out of all Hyprland sessions first"
    else
      pgrep_rc=$?
      [ "$pgrep_rc" -eq 1 ] || die "--purge refused: cannot determine compositor state (pgrep error $pgrep_rc)"
    fi
    runtime=${XDG_RUNTIME_DIR:-}
    [ -n "$runtime" ] && [ -d "$runtime" ] && [ -O "$runtime" ] \
      || die "--purge needs a user-owned XDG_RUNTIME_DIR"
    [ "$(stat -c %a -- "$runtime")" = 700 ] || die "--purge needs XDG_RUNTIME_DIR mode 0700"
  fi

  # Remove only links positively identified as opendwm's.
  if [ -L "$LEGACY_LINK" ] && [ "$(readlink "$LEGACY_LINK")" = "$LEGACY_CONF_TARGET" ]; then
    rm "$LEGACY_LINK"
    removed=1
    info "removed legacy hyprland.conf link"
  elif [ -L "$LEGACY_LINK" ]; then
    info "leaving foreign hyprland.conf link untouched"
  fi
  if [ -L "$LINK" ] && [ "$(readlink -f "$LINK")" = "$CONF_SRC" ]; then
    rm "$LINK"
    removed=1
    info "removed config link"
  elif [ -e "$LINK" ]; then
    warn "leaving foreign $LINK untouched"
  fi

  # Restore backups for BOTH config generations, only when nothing
  # occupies the destination. Backup NAMES carry the creation timestamp;
  # sort by name because mv preserves the original file's mtime.
  for cfg in "$LINK" "$LEGACY_LINK"; do
    [ -e "$cfg" ] || [ -L "$cfg" ] && continue
    backup=$(ls -1d "$cfg".bak-* 2>/dev/null | sort | tail -n 1 || true)
    if [ -n "$backup" ]; then
      mv "$backup" "$cfg"
      info "restored backup $backup -> $cfg"
    fi
  done

  # Remove session files only while they still match the install manifest
  # (managed and unmodified); otherwise leave them with a warning.
  # Removal failures are fatal and preserve the manifest.
  a="$BIN_DIR/start-opendwm-wayland"
  b="$SESSION_DIR/opendwm-wayland.desktop"
  failed=0 leftovers=0
  if [ -f "$MANIFEST" ]; then
    for f in "$a" "$b"; do
      man=$(manifest_hash "$MANIFEST" "$f")
      if [ -f "$f" ] && [ -n "$man" ] && [ "$man" = "$(hash_of "$f")" ]; then
        if sudo rm -f "$f"; then
          removed=1
          info "removed $f"
        else
          warn "failed to remove $f"
          failed=1 leftovers=1
        fi
      elif [ -e "$f" ]; then
        warn "leaving modified or unmanaged $f in place"
        leftovers=1
      fi
    done
    if [ "$leftovers" -eq 0 ]; then
      rm -f "$MANIFEST"
    else
      info "kept $MANIFEST for remaining files"
    fi
  else
    # No manifest (pre-manifest install): remove only exact current matches.
    for f in "$a" "$b"; do
      src="$SESSION_SRC/$(basename "$f")"
      if [ -f "$f" ] && cmp -s "$f" "$src"; then
        if sudo rm -f "$f"; then
          removed=1
          info "removed $f"
        else
          warn "failed to remove $f"
          failed=1
        fi
      elif [ -e "$f" ]; then
        warn "leaving unmanaged $f in place (no manifest)"
      fi
    done
  fi

  if [ "$purge" -eq 1 ]; then
    for path in "$runtime"/opendwm-bar-* "$runtime"/opendwm-scratchpad-* "$runtime"/opendwm-gaps-*; do
      [ -e "$path" ] || continue
      rm -rf "$path"
      info "purged $path"
    done
  fi

  [ "$failed" -eq 0 ] || die "uninstall incomplete; see warnings above"
  [ "$removed" -eq 1 ] || warn "nothing to remove"
  info "uninstall done. Your X11 session was never modified."
}

case $mode in
  check) run_check ;;
  install) [ "$purge" -eq 0 ] || die "--purge only applies to --uninstall"; run_install ;;
  uninstall) run_uninstall ;;
esac
