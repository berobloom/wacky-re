#!/bin/sh
# Check (and where it needs no root, set up) everything a DREA project needs.
#
#   ./setup.sh            check tools; create .work/venv with the Python packages
#   ./setup.sh --suite    also fetch and build dos-re-suite (DOSBox-X + MCP server)
#
# It never runs sudo. For missing system packages it prints the install command
# (Debian/Ubuntu/Pop!_OS names; on other systems install the same tools).
# Run it again until it says "ready".
set -u
ROOT=$(cd "$(dirname "$0")" && pwd)
SUITE=${DOS_RE_SUITE:-$(dirname "$ROOT")/dos-re-suite}
SUITE_REPO=${DOS_RE_SUITE_REPO:-berobloom/dos-re-suite}
WANT_SUITE=0
[ "${1:-}" = "--suite" ] && WANT_SUITE=1

missing_pkgs=""
problems=0
ok()   { printf '  ok       %s\n' "$1"; }
miss() { printf '  MISSING  %s  (apt: %s)\n' "$1" "$2"; missing_pkgs="$missing_pkgs $2"; problems=1; }
note() { printf '  note     %s\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

echo "== system tools"
for spec in \
    "python3:python3" "gcc:build-essential" "make:build-essential" \
    "cmake:cmake" "pkg-config:pkg-config" "sdl2-config:libsdl2-dev" \
    "git:git" "curl:curl" "unzip:unzip" "7z:p7zip-full" \
    "gdb:gdb" "Xvfb:xvfb" "xdotool:xdotool" "import:imagemagick"; do
    cmd=${spec%%:*}; pkg=${spec#*:}
    if have "$cmd"; then ok "$cmd"; else miss "$cmd" "$pkg"; fi
done
if have python3 && ! python3 -c 'import venv, ensurepip' 2>/dev/null; then
    miss "python3 venv" "python3-venv"
fi
if have python3 && ! python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))'; then
    note "python3 is older than 3.10; the tools expect 3.10+"
    problems=1
fi
have ffmpeg && ok "ffmpeg (optional: films, sound)" || note "ffmpeg missing (optional: apt ffmpeg)"
have gh && ok "gh (optional: GitHub)" || note "gh missing (optional: apt gh)"

echo "== python tools (.work/venv)"
if have python3 && python3 -c 'import venv, ensurepip' 2>/dev/null; then
    if [ ! -x "$ROOT/.work/venv/bin/python" ]; then
        python3 -m venv "$ROOT/.work/venv" && echo "  created .work/venv"
    fi
    if "$ROOT/.work/venv/bin/python" -c 'import capstone, unicorn, PIL' 2>/dev/null; then
        ok "capstone, unicorn, pillow"
    else
        "$ROOT/.work/venv/bin/pip" install -q capstone unicorn pillow \
            && ok "capstone, unicorn, pillow (installed)" \
            || { note "pip install failed (network?)"; problems=1; }
    fi
else
    note "skipped: needs python3 with venv"
fi

echo "== dos-re-suite ($SUITE)"
suite_bin="$SUITE/.work/dosbox-x-remotedebug/src/dosbox-x"
if [ ! -d "$SUITE" ]; then
    if [ $WANT_SUITE = 1 ]; then
        if have gh; then gh repo clone "$SUITE_REPO" "$SUITE"
        else git clone "https://github.com/$SUITE_REPO.git" "$SUITE"; fi
    else
        note "not found: run ./setup.sh --suite (clones $SUITE_REPO), or set DOS_RE_SUITE"
        problems=1
    fi
fi
if [ -d "$SUITE" ]; then
    if [ -x "$suite_bin" ]; then
        ok "patched DOSBox-X built"
    else
        # the bundled SDL 1.x needs the X11 headers, or it builds without video
        for spec in "autoconf:autoconf" "automake:automake" "libtool:libtool"; do
            have "${spec%%:*}" || miss "${spec%%:*}" "${spec#*:}"
        done
        for h in X11/Xlib.h X11/extensions/Xrandr.h X11/Xcursor/Xcursor.h \
                 X11/extensions/XInput.h X11/extensions/Xinerama.h \
                 X11/extensions/xf86vmode.h alsa/asoundlib.h png.h zlib.h; do
            [ -f "/usr/include/$h" ] && continue
            case $h in
                X11/Xlib.h) p=libx11-dev ;; *Xrandr*) p=libxrandr-dev ;;
                *Xcursor*) p=libxcursor-dev ;; *XInput*) p=libxi-dev ;;
                *Xinerama*) p=libxinerama-dev ;; *xf86vmode*) p=libxxf86vm-dev ;;
                alsa/*) p=libasound2-dev ;; png.h) p=libpng-dev ;; zlib.h) p=zlib1g-dev ;;
            esac
            miss "$h" "$p"
        done
        if [ $WANT_SUITE = 1 ] && [ -z "$missing_pkgs" ]; then
            echo "  building DOSBox-X (a few minutes, ~1.4 GB)"
            "$SUITE/backend/build.sh" || { note "build failed: see its output"; problems=1; }
        else
            note "DOSBox-X not built: after the packages above, ./setup.sh --suite"
            problems=1
        fi
    fi
    if [ -x "$SUITE/.venv/bin/python" ] && "$SUITE/.venv/bin/python" -c 'import mcp, pexpect' 2>/dev/null; then
        ok "MCP server venv"
    elif [ $WANT_SUITE = 1 ] && have python3; then
        python3 -m venv "$SUITE/.venv" && "$SUITE/.venv/bin/pip" install -q -r "$SUITE/mcp/requirements.txt" \
            && ok "MCP server venv (installed)" || { note "MCP venv failed"; problems=1; }
    else
        note "MCP server venv missing: ./setup.sh --suite"
        problems=1
    fi
fi

echo "== optional"
if [ -n "${GHIDRA_INSTALL_DIR:-}" ] && [ -x "$GHIDRA_INSTALL_DIR/support/analyzeHeadless" ]; then
    ok "Ghidra ($GHIDRA_INSTALL_DIR)"
else
    note "Ghidra not configured (optional: set GHIDRA_INSTALL_DIR for tools/ghidra/)"
fi

echo
if [ -n "$missing_pkgs" ]; then
    pkgs=$(echo $missing_pkgs | tr ' ' '\n' | sort -u | tr '\n' ' ')
    echo "Install the missing system packages, then run ./setup.sh again:"
    echo "  sudo apt install $pkgs"
fi
if [ $problems = 0 ]; then echo "ready"; exit 0; fi
exit 1
