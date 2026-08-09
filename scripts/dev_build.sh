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

read -r -a CONSUMER_NAMES <<< "${CONSUMERS}"
[[ "${#CONSUMER_NAMES[@]}" -gt 0 ]] || {
    echo "No development consumers selected. Set CONSUMERS to one or more consumer identifiers." >&2
    exit 1
}

python3 "${SCRIPT_DIR}/stage_consumers.py" "${CONSUMER_NAMES[@]}"

for consumer in "${CONSUMER_NAMES[@]}"; do
    case "${consumer}" in
        defold-controls)
            stage_root="${DEFOLD_CONTROLS_STAGE_ROOT}"
            ;;
        defold-sudoku-playboard)
            stage_root="${DEFOLD_SUDOKU_PLAYBOARD_STAGE_ROOT}"
            ;;
        one-sudoku)
            stage_root="${APP_STAGE_ROOT}"
            ;;
        *)
            echo "Unknown development consumer: ${consumer}" >&2
            exit 1
            ;;
    esac

    echo "Building staged ${consumer} with local Extender ${LOCAL_EXTENDER_URL}..."
    python3 "${SCRIPT_DIR}/run_bob.py" \
        --java "${JAVA_BIN}" \
        --bob "${BOB_JAR}" \
        --root "${stage_root}" \
        --output build/default \
        --platform arm64-macos \
        --variant debug \
        --archive \
        --defoldsdk "${DEFOLD_SDK_SHA}" \
        --build-server "${LOCAL_EXTENDER_URL}"

    if ! compgen -G "${stage_root}/build/arm64-osx/libBezierPath_*.a" >/dev/null; then
        echo "Development build did not produce a local BezierPath native library for ${consumer}." >&2
        exit 1
    fi
    echo "Development build completed: ${stage_root}/build"
done
