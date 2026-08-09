#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/config.sh
source "${SCRIPT_DIR}/config.sh"

[[ -f "${BOB_JAR}" ]] || {
    echo "Pinned Bob is missing. Run 'make setup-local-extender' first." >&2
    exit 1
}

"${SCRIPT_DIR}/extender.sh" status >/dev/null
python3 "${SCRIPT_DIR}/stage_one_sudoku.py"

echo "Building staged one-sudoku with local Extender ${LOCAL_EXTENDER_URL}..."
python3 "${SCRIPT_DIR}/run_bob.py" \
    --java "${JAVA_BIN}" \
    --bob "${BOB_JAR}" \
    --root "${APP_STAGE_ROOT}" \
    --output build/default \
    --platform arm64-macos \
    --variant debug \
    --archive \
    --defoldsdk "${DEFOLD_SDK_SHA}" \
    --build-server "${LOCAL_EXTENDER_URL}"

echo "Development build completed: ${APP_STAGE_ROOT}/build"
