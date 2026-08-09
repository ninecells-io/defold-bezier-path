#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/config.sh
source "${SCRIPT_DIR}/config.sh"

command -v curl >/dev/null 2>&1 || {
    echo "curl is required to download Bob." >&2
    exit 1
}
[[ -n "${JAVA_BIN}" && -x "${JAVA_BIN}" ]] || {
    echo "Java 25 was not found; install Defold 1.13.0 or set JAVA_BIN." >&2
    exit 1
}
JAVA_VERSION_OUTPUT="$(${JAVA_BIN} -version 2>&1)"
JAVA_VERSION="${JAVA_VERSION_OUTPUT%%$'\n'*}"
[[ "${JAVA_VERSION}" == *'"25'* ]] || {
    echo "Java 25 is required, found: ${JAVA_VERSION}" >&2
    exit 1
}

mkdir -p "${LOCAL_BUILD_ROOT}"
if [[ ! -f "${BOB_JAR}" ]]; then
    BOB_TEMP="${BOB_JAR}.download"
    cleanup_download() {
        rm -f "${BOB_TEMP}"
    }
    trap cleanup_download EXIT
    curl --fail --location --silent --show-error \
        "https://d.defold.com/archive/stable/${DEFOLD_SDK_SHA}/bob/bob.jar" \
        --output "${BOB_TEMP}"
    mv "${BOB_TEMP}" "${BOB_JAR}"
fi

BOB_VERSION_OUTPUT="$(${JAVA_BIN} -jar "${BOB_JAR}" --version 2>&1)"
BOB_VERSION="${BOB_VERSION_OUTPUT%%$'\n'*}"
[[ "${BOB_VERSION}" == *"${DEFOLD_SDK_SHA}"* ]] || {
    echo "Bob does not match SDK ${DEFOLD_SDK_SHA}: ${BOB_VERSION}" >&2
    exit 1
}
echo "${BOB_VERSION}"
