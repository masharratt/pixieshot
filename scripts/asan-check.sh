#!/usr/bin/env bash
# Deep check before a release: builds Pixieshot with AddressSanitizer and
# runs every unit test under it, including the toolbar robot that clicks
# every button in the capture overlay and the editor.
#
# Usage: scripts/asan-check.sh [build-dir]   (default: build-asan)
#
# Exit codes: 0 all tests passed, 1 build failed, 2 tests failed or a
# sanitizer reported an error, 3 no tests ran.
#
# The video/OCR tests need the macOS encoder and Vision; they skip (and
# say so) when those are unavailable, e.g. inside a sandboxed shell.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-build-asan}"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$ROOT/$BUILD_DIR" ;;
esac
LOG="${TMPDIR:-/tmp}/pixieshot-asan-$(date +%s).txt"

QT6_DIR="$(brew --prefix qt6)/lib/cmake/Qt6"

# Reuse dependency sources already fetched by the normal build so this
# works offline; otherwise CMake fetches them.
DEPS="$ROOT/build/_deps"
DEP_ARGS=()
for pair in qtcolorwidgets:QTCOLORWIDGETS kdsingleapplication:KDSINGLEAPPLICATION qhotkey:QHOTKEY; do
    dir="$DEPS/${pair%%:*}-src"
    if [ -d "$dir" ]; then
        DEP_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_${pair##*:}=$dir")
    fi
done

SAN_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"

echo "==> Configuring ASan build in $BUILD_DIR"
if ! cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DQt6_DIR="$QT6_DIR" \
    -DCMAKE_C_FLAGS="$SAN_FLAGS" \
    -DCMAKE_CXX_FLAGS="$SAN_FLAGS" \
    -DCMAKE_OBJCXX_FLAGS="$SAN_FLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
    ${DEP_ARGS[@]+"${DEP_ARGS[@]}"} >/dev/null; then
    echo "FAILED: configure" >&2
    exit 1
fi

echo "==> Building"
if ! cmake --build "$BUILD_DIR" 2>&1 | tail -20; then
    echo "FAILED: build" >&2
    exit 1
fi
# tail hides ninja's exit status; check it separately
if ! cmake --build "$BUILD_DIR" >/dev/null 2>&1; then
    echo "FAILED: build (run cmake --build $BUILD_DIR to see errors)" >&2
    exit 1
fi

echo "==> Running tests under AddressSanitizer (full log: $LOG)"
# Leak checking is off: Qt and system frameworks report leaks we don't own
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:halt_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
export QT_QPA_PLATFORM=offscreen

set +e
ctest --test-dir "$BUILD_DIR" --output-on-failure 2>&1 | tee "$LOG"
status=${PIPESTATUS[0]}
set -e

# ctest ends with "100% tests passed out of N" or "... failed out of N"
ran=$(grep -Eo 'tests (passed|failed) out of [0-9]+' "$LOG" | tail -1 | grep -Eo '[0-9]+$' || true)
if [ -z "$ran" ] || [ "$ran" -eq 0 ]; then
    echo "NOTHING CHECKED: no tests ran" >&2
    exit 3
fi
if grep -q -E "ERROR: AddressSanitizer|runtime error:" "$LOG"; then
    echo "FAILED: sanitizer errors found, see $LOG" >&2
    grep -E "ERROR: AddressSanitizer|runtime error:" "$LOG" | sort | uniq -c >&2
    exit 2
fi
if [ "$status" -ne 0 ]; then
    echo "FAILED: some tests failed, see $LOG" >&2
    exit 2
fi
# Timing tests and (in a sandboxed shell) video/OCR tests skip inside a
# passing group; ctest hides that output, so no skip count is shown here.
echo "PASSED: $ran test groups clean under AddressSanitizer"
