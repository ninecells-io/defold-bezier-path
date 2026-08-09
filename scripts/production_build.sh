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
PRODUCTION_SETTINGS="${SCRIPT_DIR}/production.dependencies.settings"
[[ -f "${PRODUCTION_SETTINGS}" ]] || {
    echo "Production dependency settings were not found at ${PRODUCTION_SETTINGS}." >&2
    exit 1
}

EXPECTED_DEPENDENCIES=(
    'dependencies#0 = https://corneliusbehrend:__GITHUB_TOKEN__@github.com/ninecells-io/defold-sudoku-playboard/archive/refs/tags/0.1.7.zip'
    'dependencies#1 = https://corneliusbehrend:__GITHUB_TOKEN__@github.com/ninecells-io/defold-controls/archive/refs/tags/0.2.1.zip'
    'dependencies#2 = https://corneliusbehrend:__GITHUB_TOKEN__@github.com/ninecells-io/defold-bezier-path/archive/refs/tags/0.2.2.zip'
    'dependencies#3 = https://github.com/ninecells-io/defold-helpers/archive/refs/tags/0.1.5.zip'
)
[[ "$(grep -Ec '^dependencies#[0-9]+[[:space:]]*=' "${PRODUCTION_SETTINGS}")" -eq "${#EXPECTED_DEPENDENCIES[@]}" ]] || {
    echo "Production build refused: production settings must contain exactly four dependencies." >&2
    exit 1
}
for dependency in "${EXPECTED_DEPENDENCIES[@]}"; do
    grep -Fxq "${dependency}" "${PRODUCTION_SETTINGS}" || {
        echo "Production build refused: missing exact pin ${dependency}." >&2
        exit 1
    }
done
if grep -Eq 'localhost|127\.0\.0\.1' "${PRODUCTION_SETTINGS}"; then
    echo "Production build refused: production settings contain a local dependency URL." >&2
    exit 1
fi
for dependency in defold-sudoku-playboard defold-controls defold-bezier-path defold-helpers; do
    grep -Fq "${LOCAL_DEPENDENCY_URL}/${dependency}.zip" "${ONE_SUDOKU_ROOT}/game.project" || {
        echo "Production build refused: one-sudoku development dependency ${dependency} is not local." >&2
        exit 1
    }
done

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
    --build-server "${HOSTED_EXTENDER_URL}" \
    --settings "${PRODUCTION_SETTINGS}"
