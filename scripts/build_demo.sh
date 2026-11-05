#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/config.sh"
"${SCRIPT_DIR}/ensure_bob.sh"
commands=(resolve build)
if [[ "${1:-}" == bundle ]]; then
    commands+=(bundle)
elif [[ $# -ne 0 ]]; then
    echo "Usage: $0 [bundle]" >&2
    exit 2
fi
exec "${JAVA_BIN}" -jar "${BOB_JAR}" --root "${REPO_ROOT}" \
    --platform "${PLATFORM:-arm64-macos}" --variant debug --archive \
    --defoldsdk "${DEFOLD_SDK_SHA}" --build-server "${BUILD_SERVER:-${HOSTED_EXTENDER_URL}}" \
    "${commands[@]}"
