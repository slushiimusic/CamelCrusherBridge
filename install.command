#!/bin/bash
# Installs CamelCrusher Native (arm64 VST2 + Audio Unit) into your plug-ins
# folders. No sudo required. Double-click in Finder, or run from Terminal.
#
# Two modes, picked automatically:
#   * Ready-built  — a "Plugins" folder sits next to this script (the release
#                    disk image). Nothing is compiled; the bundles are copied
#                    into place.
#   * From source  — the bridge source sits next to this script (a git clone).
#                    Needs the Xcode Command Line Tools, and takes the original
#                    Intel plugin and skin art from your own installation.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RED=$'\033[31m'; GRN=$'\033[32m'; YEL=$'\033[33m'; BLD=$'\033[1m'; OFF=$'\033[0m'

say()  { printf '%s\n' "$*"; }
step() { printf '\n%s==>%s %s\n' "$BLD" "$OFF" "$*"; }
ok()   { printf '  %s✓%s %s\n' "$GRN" "$OFF" "$*"; }
warn() { printf '  %s!%s %s\n' "$YEL" "$OFF" "$*"; }

CANON_SUPPORT="/Library/Application Support/Camel Audio"
CANON_ORIG="$CANON_SUPPORT/CamelCrusherOriginal.vst"
CANON_SKIN="$CANON_SUPPORT/CamelCrusherData/Skins"

WORK=""
finish() { [ -n "$WORK" ] && rm -rf "$WORK"; WORK=""; }
die() {
    printf '\n%sInstallation failed.%s %s\n' "$RED" "$OFF" "$*" >&2
    finish
    if [ -t 0 ]; then printf '\nPress return to close this window.\n'; read -r _ 2>/dev/null || true; fi
    exit 1
}

cat <<'BANNER'

  CamelCrusher Native — installer
  arm64 VST2 + Audio Unit for Apple Silicon
  ------------------------------------------------------------

BANNER

# ---------------------------------------------------------------- preflight
step "Checking this machine"

[ "$(uname -s)" = "Darwin" ] || die "This installer is for macOS."

if [ "$(uname -m)" != "arm64" ]; then
    warn "This Mac is not Apple Silicon ($(uname -m))."
    warn "The bridge is built and tested for arm64 only."
    die "Nothing was changed."
fi
ok "Apple Silicon, macOS $(sw_vers -productVersion)"

# ------------------------------------------------------------- pick a mode
PREBUILT=""
[ -d "$HERE/Plugins/CamelCrusher.vst" ] && PREBUILT="$HERE/Plugins"

SRC=""
for cand in "$HERE/Source" "$HERE"; do
    if [ -f "$cand/plugin.c" ] && [ -f "$cand/build.sh" ]; then SRC="$cand"; break; fi
done

if [ -n "$PREBUILT" ]; then
    ok "mode: install ready-built plugins"
elif [ -n "$SRC" ]; then
    ok "mode: build from source"
else
    die "Found neither ready-built plugins nor bridge source next to this installer."
fi

# ------------------------------------------------------------- destinations
# Prefer the system folder when it is actually writable — that is where the
# Camel Audio installer put things, and reusing it avoids a duplicate entry in
# the host's plugin list. Otherwise fall back to the user folder. Never sudo.
pick_dest() {
    local sys="$1" usr="$2"
    if [ -e "$sys" ]; then
        if [ -w "$sys" ]; then printf '%s' "$sys"; return; fi
    elif [ -w "$(dirname "$sys")" ]; then
        printf '%s' "$sys"; return
    fi
    printf '%s' "$usr"
}

VST_DEST="${CC_VST_DEST:-$(pick_dest "/Library/Audio/Plug-Ins/VST/CamelCrusher.vst" \
                                     "$HOME/Library/Audio/Plug-Ins/VST/CamelCrusher.vst")}"
AU_DEST="${CC_AU_DEST:-$(pick_dest "/Library/Audio/Plug-Ins/Components/CamelCrusher.component" \
                                   "$HOME/Library/Audio/Plug-Ins/Components/CamelCrusher.component")}"
mkdir -p "$(dirname "$VST_DEST")" "$(dirname "$AU_DEST")" 2>/dev/null

# A plug-in folder's parent is often root-owned, so the bundle directory itself
# may resist removal even when its contents are ours. Clear it in place.
clear_bundle() {
    if [ -d "$1" ]; then
        rm -rf "$1"/* 2>/dev/null
        rm -rf "$1"/.[!.]* 2>/dev/null
    else
        mkdir -p "$1" || return 1
    fi
    return 0
}

install_bundle() {   # install_bundle <src bundle> <dest bundle> <label>
    local src="$1" dest="$2" label="$3"
    say "  → $dest"
    clear_bundle "$dest" || die "Cannot write to $dest"
    ( cd "$src" && /usr/bin/tar cf - . ) | ( cd "$dest" && /usr/bin/tar xf - ) \
        || die "Could not copy the $label into place."
    xattr -cr "$dest" 2>/dev/null
    [ -f "$dest/Contents/MacOS/CamelCrusherHelper" ] && \
        codesign --force --sign - "$dest/Contents/MacOS/CamelCrusherHelper" >/dev/null 2>&1
    codesign --force --sign - "$dest" >/dev/null 2>&1
    ok "$label installed"
}

# ==========================================================================
if [ -n "$PREBUILT" ]; then
# ------------------------------------------------- mode 1: ready-built copy
    step "Installing the VST2"
    install_bundle "$PREBUILT/CamelCrusher.vst" "$VST_DEST" "VST2"

    if [ -d "$PREBUILT/CamelCrusher.component" ]; then
        step "Installing the Audio Unit"
        install_bundle "$PREBUILT/CamelCrusher.component" "$AU_DEST" "Audio Unit"
    else
        AU_DEST=""
        warn "No Audio Unit in this package; installed the VST2 only."
    fi

    # Leave a pristine original where build.sh expects it, so the plugin can be
    # rebuilt from source later without hunting for a copy.
    if [ ! -d "$CANON_ORIG" ] && [ -d "$VST_DEST/Contents/Resources/CamelCrusher.vst" ]; then
        if mkdir -p "$CANON_SUPPORT" 2>/dev/null && \
           cp -R "$VST_DEST/Contents/Resources/CamelCrusher.vst" "$CANON_ORIG" 2>/dev/null; then
            ok "archived a pristine original to $CANON_ORIG"
        fi
    fi

else
# --------------------------------------------------- mode 2: build from src
    step "Checking the toolchain"
    if ! xcrun --find clang >/dev/null 2>&1; then
        say ""
        say "  The Xcode Command Line Tools are needed to build from source."
        say "  Install them by running:"
        say ""
        say "      xcode-select --install"
        say ""
        die "Command Line Tools not found."
    fi
    ok "compiler: $(clang --version | head -1 | sed 's/ (.*//')"

    # The bridge hosts Camel Audio's own x86_64 binary, so a genuine copy has to
    # already be on this Mac. A previously installed bridge carries one inside
    # its Resources, so that counts as a source too.
    step "Looking for an original CamelCrusher"

    # Genuine original == a VST2 bundle whose executable is x86_64 and not arm64.
    is_original() {
        local exe
        exe="$(find "$1/Contents/MacOS" -maxdepth 1 -type f ! -name '*Helper*' 2>/dev/null | head -1)"
        [ -n "$exe" ] || return 1
        local archs; archs="$(lipo -archs "$exe" 2>/dev/null)" || return 1
        case " $archs " in *" arm64 "*) return 1 ;; esac
        case " $archs " in *" x86_64 "*) return 0 ;; esac
        return 1
    }

    ORIG_SRC=""
    for cand in \
        "$CANON_ORIG" \
        "/Library/Audio/Plug-Ins/VST/CamelCrusher.vst" \
        "$HOME/Library/Audio/Plug-Ins/VST/CamelCrusher.vst" \
        "/Library/Audio/Plug-Ins/VST/CamelCrusher.vst/Contents/Resources/CamelCrusher.vst" \
        "$HOME/Library/Audio/Plug-Ins/VST/CamelCrusher.vst/Contents/Resources/CamelCrusher.vst" \
        "/Library/Audio/Plug-Ins/Components/CamelCrusher.component/Contents/Resources/CamelCrusher.vst" \
        "$HOME/Library/Audio/Plug-Ins/Components/CamelCrusher.component/Contents/Resources/CamelCrusher.vst"
    do
        if [ -d "$cand" ] && is_original "$cand"; then ORIG_SRC="$cand"; break; fi
    done

    if [ -z "$ORIG_SRC" ]; then
        say ""
        say "  ${BLD}No original CamelCrusher found on this Mac.${OFF}"
        say ""
        say "  Building from source wraps a copy of Camel Audio's own plugin, so"
        say "  one has to be installed already. Either install the original"
        say "  freeware release (its VST2 is enough), or use the ready-built"
        say "  disk image from the Releases page, which includes it."
        say ""
        die "Original plugin not found."
    fi
    ok "original: $ORIG_SRC"

    SKIN_SRC=""
    for cand in \
        "$CANON_SKIN" \
        "/Library/Audio/Plug-Ins/VST/CamelCrusher.vst/Contents/Resources/Skins" \
        "$HOME/Library/Audio/Plug-Ins/VST/CamelCrusher.vst/Contents/Resources/Skins" \
        "/Library/Audio/Plug-Ins/Components/CamelCrusher.component/Contents/Resources/Skins" \
        "$HOME/Library/Audio/Plug-Ins/Components/CamelCrusher.component/Contents/Resources/Skins"
    do
        if [ -f "$cand/default/Background.png" ]; then SKIN_SRC="$cand"; break; fi
    done
    if [ -n "$SKIN_SRC" ]; then
        ok "skin art: $SKIN_SRC"
    else
        warn "Skin art not found — the plugin will run, but its interface will not draw."
    fi

    # Always work from a private copy. A destination we are about to overwrite
    # can be the very bundle the original was found inside, and clobbering that
    # would destroy the last genuine copy on this machine.
    step "Staging"
    WORK="$(mktemp -d /tmp/ccnative.XXXXXX)" || die "Could not create a temporary build directory."
    mkdir -p "$WORK/build"
    cp "$SRC"/*.c "$SRC"/*.h "$SRC"/*.m "$WORK/build/" 2>/dev/null
    cp "$SRC"/build.sh "$SRC"/build_au.sh "$WORK/build/" || die "Could not copy the build scripts."
    chmod +x "$WORK/build"/*.sh

    cp -R "$ORIG_SRC" "$WORK/CamelCrusherOriginal.vst" || die "Could not copy the original plugin."
    export CC_ORIG="$WORK/CamelCrusherOriginal.vst"
    if [ -n "$SKIN_SRC" ]; then
        cp -R "$SKIN_SRC" "$WORK/Skins" || die "Could not copy the skin art."
        export CC_SKIN="$WORK/Skins"
    else
        export CC_SKIN="$WORK/none"
    fi
    ok "staged a private copy of the original"

    if [ ! -d "$CANON_ORIG" ]; then
        if mkdir -p "$CANON_SUPPORT" 2>/dev/null && \
           cp -R "$WORK/CamelCrusherOriginal.vst" "$CANON_ORIG" 2>/dev/null; then
            ok "archived a pristine original to $CANON_ORIG"
        else
            warn "Could not archive a pristine original to $CANON_SUPPORT (not writable)."
        fi
    fi

    step "Building and installing the VST2"
    say "  → $VST_DEST"
    "$WORK/build/build.sh" "$VST_DEST" 2>&1 | sed 's/^/  /' || die "The VST2 build did not complete."
    [ -x "$VST_DEST/Contents/MacOS/CamelCrusher" ] || die "VST2 bundle was not produced."
    ok "VST2 installed"

    step "Building and installing the Audio Unit"
    say "  → $AU_DEST"
    "$WORK/build/build_au.sh" "$AU_DEST" 2>&1 | sed 's/^/  /' || die "The Audio Unit build did not complete."
    [ -x "$AU_DEST/Contents/MacOS/CamelCrusher" ] || die "Audio Unit bundle was not produced."
    ok "Audio Unit installed"
fi
# ==========================================================================

# -------------------------------------------------------------------- verify
step "Verifying"
say "  VST2 front-end: $(lipo -archs "$VST_DEST/Contents/MacOS/CamelCrusher" 2>&1)"
say "  helper:         $(lipo -archs "$VST_DEST/Contents/MacOS/CamelCrusherHelper" 2>&1)"
if [ -n "$AU_DEST" ]; then
    say "  AU front-end:   $(lipo -archs "$AU_DEST/Contents/MacOS/CamelCrusher" 2>&1)"
    if command -v auval >/dev/null 2>&1; then
        if auval -v aumf CaCr CamA >/dev/null 2>&1; then
            ok "auval passed"
        else
            warn "auval did not pass — run 'auval -v aumf CaCr CamA' to see why."
        fi
    fi
fi

finish

printf '\n  %s%sDone.%s\n\n' "$GRN" "$BLD" "$OFF"
printf '  VST2   %s\n' "$VST_DEST"
[ -n "$AU_DEST" ] && printf '  AU     %s\n' "$AU_DEST"
cat <<'EOF'

  Restart your DAW and rescan plugins. Both formats present the original's
  plugin identity, so existing projects re-link to this build on their own —
  there is nothing to re-dial.

EOF

if [ -t 0 ]; then
    printf '  Press return to close this window.\n'
    read -r _ 2>/dev/null || true
fi
