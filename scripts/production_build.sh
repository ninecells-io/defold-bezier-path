#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/config.sh
source "${SCRIPT_DIR}/config.sh"

[[ -f "${BOB_JAR}" ]] || {
    echo "Pinned Bob is missing. Run 'make setup-local-extender' first." >&2
    exit 1
}
[[ -f "${ONE_SUDOKU_ROOT}/game.project" ]] || {
    echo "one-sudoku was not found at ${ONE_SUDOKU_ROOT}." >&2
    exit 1
}

DEPENDENCY_LINE="$(grep -E '^dependencies#[0-9]+[[:space:]]*=.*defold-bezier-path/archive/refs/tags/0\.2\.2\.zip' "${ONE_SUDOKU_ROOT}/game.project" || true)"
[[ -n "${DEPENDENCY_LINE}" ]] || {
    echo "Production build refused: one-sudoku is not pinned to bezier-path 0.2.2." >&2
    exit 1
}
if grep -q 'localhost\|127\.0\.0\.1' "${ONE_SUDOKU_ROOT}/game.project"; then
    echo "Production build refused: game.project contains a local dependency URL." >&2
    exit 1
fi

PLATFORM="${PRODUCTION_PLATFORM:-arm64-macos}"
VARIANT="${PRODUCTION_VARIANT:-release}"
echo "Building production one-sudoku (${PLATFORM}, ${VARIANT}) with hosted Extender ${HOSTED_EXTENDER_URL}..."
python3 "${SCRIPT_DIR}/run_bob.py" \
    --java "${JAVA_BIN}" \
    --bob "${BOB_JAR}" \
    --root "${ONE_SUDOKU_ROOT}" \
    --output build/default \
    --platform "${PLATFORM}" \
    --variant "${VARIANT}" \
    --archive \
    --defoldsdk "${DEFOLD_SDK_SHA}" \
    --build-server "${HOSTED_EXTENDER_URL}"
