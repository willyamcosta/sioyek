#!/usr/bin/env bash
set -euo pipefail

PREFIX="${1:-./result}"
SIOYEK_BIN="$PREFIX/bin/sioyek"

echo "=== Sioyek Regression & Smoke Test Suite ==="
echo "Target Prefix: $PREFIX"

# 1. Verify binary exists and is executable
if [[ ! -x "$SIOYEK_BIN" ]]; then
    echo "FAIL: $SIOYEK_BIN is missing or not executable"
    exit 1
fi
echo "[PASS] Binary exists: $SIOYEK_BIN"

# 2. Verify share/ directory files required by upstream path changes
REQUIRED_FILES=(
    "$PREFIX/share/prefs.config"
    "$PREFIX/share/keys.config"
    "$PREFIX/share/tutorial.pdf"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [[ ! -f "$file" ]]; then
        echo "FAIL: Required file missing: $file"
        exit 1
    fi
    if [[ ! -s "$file" ]]; then
        echo "FAIL: Required file is empty: $file"
        exit 1
    fi
    echo "[PASS] Verified file: $file"
done

if [[ ! -d "$PREFIX/share/shaders" ]]; then
    echo "FAIL: Required shaders directory missing: $PREFIX/share/shaders"
    exit 1
fi
echo "[PASS] Verified directory: $PREFIX/share/shaders"

# 3. Verify keybindings content has core bindings
if ! grep -q "^quit[[:space:]]\+q" "$PREFIX/share/keys.config"; then
    echo "FAIL: $PREFIX/share/keys.config is missing 'quit q' binding"
    exit 1
fi
echo "[PASS] Verified 'quit q' is defined in $PREFIX/share/keys.config"

# 4. Verify CLI flags (--version, --help)
VERSION_OUTPUT=$("$SIOYEK_BIN" --version 2>&1)
if [[ "$VERSION_OUTPUT" != *"sioyek 2.0.0"* ]]; then
    echo "FAIL: Unexpected version output: $VERSION_OUTPUT"
    exit 1
fi
echo "[PASS] CLI --version check: $VERSION_OUTPUT"

HELP_OUTPUT=$("$SIOYEK_BIN" --help 2>&1)
if [[ "$HELP_OUTPUT" != *"Usage:"* ]]; then
    echo "FAIL: Unexpected help output: $HELP_OUTPUT"
    exit 1
fi
echo "[PASS] CLI --help check succeeded"

# 5. Run built-in regression tests (database concurrency, continuous scroll gap math, keybinds, typo check)
echo "Running internal regression tests via --run-tests..."
export HOME="$(mktemp -d)"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CONFIG_HOME="$HOME/.config"
export QT_QPA_PLATFORM=offscreen
unset QT_QPA_PLATFORMTHEME || true
unset QT_STYLE_OVERRIDE || true
TEST_OUTPUT=$("$SIOYEK_BIN" --run-tests 2>&1)
echo "$TEST_OUTPUT"

if [[ "$TEST_OUTPUT" != *"ALL REGRESSION TESTS PASSED"* ]]; then
    echo "FAIL: Internal regression tests did not pass"
    exit 1
fi

if echo "$TEST_OUTPUT" | grep -qi "Error: default_config_path"; then
    echo "FAIL: Output reported missing default_config_path"
    exit 1
fi

if echo "$TEST_OUTPUT" | grep -qi "Error: default_keys_path"; then
    echo "FAIL: Output reported missing default_keys_path"
    exit 1
fi

echo "[PASS] Internal regression tests and startup checks passed completely"

echo ""
echo "=== All Sioyek Regression & Smoke Tests Passed Successfully ==="
exit 0
