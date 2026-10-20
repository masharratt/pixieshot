#!/usr/bin/env bash
# Makes the README demo GIF (docs/assets/pixieshot-demo.gif) with
# Pixieshot's own renderer: builds tools/demogif and runs it offscreen.
# Your own Pixieshot settings are never read or written (the generator uses
# a temp settings folder).
#
# Usage: scripts/make-demo-gif.sh [--samples <dir>] [build-dir]
#        (default build dir: build-demo)
#
# Exit codes: 0 written, 1 build failed, 2 the generator failed, 3 the GIF
# is over the size budget.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/docs/assets/pixieshot-demo.gif"
MAX_BYTES=$((6 * 1024 * 1024))
SAMPLES=()
BUILD_DIR="build-demo"
while [ $# -gt 0 ]; do
    case "$1" in
        --samples) SAMPLES=(--samples "$2"); shift 2 ;;
        *) BUILD_DIR="$1"; shift ;;
    esac
done
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$ROOT/$BUILD_DIR" ;;
esac

QT6_DIR="$(brew --prefix qt6)/lib/cmake/Qt6"

# Reuse dependency sources already fetched by the normal build so this
# works offline (PIXIE_DEPS_DIR points elsewhere); otherwise CMake fetches them.
DEPS="${PIXIE_DEPS_DIR:-$ROOT/build/_deps}"
DEP_ARGS=()
for pair in qtcolorwidgets:QTCOLORWIDGETS kdsingleapplication:KDSINGLEAPPLICATION qhotkey:QHOTKEY; do
    dir="$DEPS/${pair%%:*}-src"
    if [ -d "$dir" ]; then
        DEP_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_${pair##*:}=$dir")
    fi
done

echo "==> Building the demo generator in $BUILD_DIR"
if ! cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
        -DQt6_DIR="$QT6_DIR" -DPIXIE_BUILD_DEMO=ON -DBUILD_TESTING=OFF \
        ${DEP_ARGS[@]+"${DEP_ARGS[@]}"} >/dev/null \
    || ! cmake --build "$BUILD_DIR" --target pixie_demogif; then
    echo "Build failed." >&2
    exit 1
fi

echo "==> Rendering $OUT"
if ! QT_QPA_PLATFORM=offscreen "$BUILD_DIR/tools/demogif/pixie_demogif" \
        "$OUT" ${SAMPLES[@]+"${SAMPLES[@]}"}; then
    echo "The generator failed. Inside a sandboxed shell the GIF encoder can" >&2
    echo "be unavailable; run this script from a normal Terminal." >&2
    exit 2
fi

BYTES=$(stat -f %z "$OUT")
if [ "$BYTES" -gt "$MAX_BYTES" ]; then
    echo "The GIF is $BYTES bytes, over the $MAX_BYTES budget." >&2
    exit 3
fi
echo "==> Done: $OUT ($BYTES bytes)"
