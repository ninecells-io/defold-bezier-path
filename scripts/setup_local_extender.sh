#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/config.sh
source "${SCRIPT_DIR}/config.sh"

CHECK_ONLY=0
if [[ "${1:-}" == "--check-only" ]]; then
    CHECK_ONLY=1
elif [[ $# -ne 0 ]]; then
    echo "Usage: $0 [--check-only]" >&2
    exit 2
fi

fail() {
    echo "setup error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "required command '$1' was not found"
}

[[ "$(uname -s)" == "Darwin" ]] || fail "standalone Extender setup is supported on macOS only"
[[ "$(uname -m)" == "arm64" ]] || fail "this pipeline currently targets Apple Silicon (arm64)"

for command_name in brew curl git python3 rsync sed; do
    require_command "${command_name}"
done

[[ -n "${JAVA_BIN}" && -x "${JAVA_BIN}" ]] || fail "Java 25 was not found; install Defold 1.13.0 or set JAVA_BIN"
JAVA_VERSION_OUTPUT="$(${JAVA_BIN} -version 2>&1)"
JAVA_VERSION="${JAVA_VERSION_OUTPUT%%$'\n'*}"
[[ "${JAVA_VERSION}" == *'"25'* ]] || fail "Java 25 is required, found: ${JAVA_VERSION}"

[[ -d "${XCODE_APP}/Contents/Developer" ]] || fail "Xcode ${XCODE_VERSION} is required at ${XCODE_APP}"
export DEVELOPER_DIR="${XCODE_APP}/Contents/Developer"
XCODE_VERSION_OUTPUT="$(/usr/bin/xcodebuild -version)"
XCODE_VERSION_LINE="${XCODE_VERSION_OUTPUT%%$'\n'*}"
INSTALLED_XCODE_VERSION="${XCODE_VERSION_LINE#Xcode }"
[[ "${INSTALLED_XCODE_VERSION}" == "${XCODE_VERSION}" ]] || fail "expected Xcode ${XCODE_VERSION} at ${XCODE_APP}, found ${INSTALLED_XCODE_VERSION:-unknown}"
if ! /usr/bin/xcodebuild -checkFirstLaunchStatus >/dev/null 2>&1; then
    fail "Xcode ${XCODE_VERSION} first-launch tasks are incomplete; open ${XCODE_APP} and install its required components"
fi

MACOS_SDK_PATH="$(/usr/bin/xcrun --sdk macosx --show-sdk-path)"
IOS_SDK_PATH="$(/usr/bin/xcrun --sdk iphoneos --show-sdk-path)"
IOS_SIMULATOR_SDK_PATH="$(/usr/bin/xcrun --sdk iphonesimulator --show-sdk-path)"
[[ "${MACOS_SDK_PATH}" == *"MacOSX${MACOS_SDK_VERSION}.sdk" ]] || fail "Xcode does not provide MacOSX${MACOS_SDK_VERSION}.sdk"
[[ "${IOS_SDK_PATH}" == *"iPhoneOS${IOS_SDK_VERSION}.sdk" ]] || fail "Xcode does not provide iPhoneOS${IOS_SDK_VERSION}.sdk"
[[ "${IOS_SIMULATOR_SDK_PATH}" == *"iPhoneSimulator${IOS_SDK_VERSION}.sdk" ]] || fail "Xcode does not provide iPhoneSimulator${IOS_SDK_VERSION}.sdk"

has_full_jdk() {
    [[ -x "${JAVA_HOME}/bin/javac" ]] || return 1
    local java_modules
    java_modules="$(${JAVA_BIN} --list-modules 2>/dev/null)" || return 1
    [[ "${java_modules}" == *"java.instrument@"* ]]
}

if ! has_full_jdk; then
    if [[ ${CHECK_ONLY} -eq 1 ]]; then
        fail "a full JDK 25 is required; Defold's bundled Java runtime omits java.instrument (install with 'brew install openjdk@25')"
    fi
    echo "Installing full OpenJDK 25 for Gradle and Extender..."
    brew install openjdk@25
    JAVA_HOME="$(brew --prefix openjdk@25)/libexec/openjdk.jdk/Contents/Home"
    JAVA_BIN="${JAVA_HOME}/bin/java"
    export JAVA_HOME JAVA_BIN
    has_full_jdk || fail "Homebrew openjdk@25 is missing javac or the java.instrument module"
    JAVA_VERSION_OUTPUT="$(${JAVA_BIN} -version 2>&1)"
    JAVA_VERSION="${JAVA_VERSION_OUTPUT%%$'\n'*}"
fi

echo "Prerequisites OK: Defold ${DEFOLD_VERSION}, Extender ${EXTENDER_TAG}, Xcode ${XCODE_VERSION}, ${JAVA_VERSION}"
if [[ ${CHECK_ONLY} -eq 1 ]]; then
    exit 0
fi

mkdir -p "${LOCAL_BUILD_ROOT}"

if ! brew list gnu-tar >/dev/null 2>&1; then
    echo "Installing GNU tar (required to preserve Apple SDK links and metadata)..."
    brew install gnu-tar
fi
GTAR_BIN_DIR="$(brew --prefix gnu-tar)/libexec/gnubin"
export PATH="${JAVA_HOME}/bin:${GTAR_BIN_DIR}:${PATH}"

clone_pinned() {
    local url="$1"
    local revision="$2"
    local destination="$3"
    if [[ ! -d "${destination}/.git" ]]; then
        git clone --depth 1 --branch "${revision}" "${url}" "${destination}"
    else
        local current_revision
        current_revision="$(git -C "${destination}" describe --tags --exact-match 2>/dev/null || true)"
        [[ "${current_revision}" == "${revision}" ]] || fail "${destination} is not at ${revision}; remove it and rerun setup"
    fi
}

clone_pinned "https://github.com/defold/defold.git" "${DEFOLD_VERSION}" "${DEFOLD_SOURCE_DIR}"
clone_pinned "https://github.com/defold/extender.git" "${EXTENDER_TAG}" "${EXTENDER_SOURCE_DIR}"

"${SCRIPT_DIR}/ensure_bob.sh"

PACKAGE_SCRIPT_SOURCE="${DEFOLD_SOURCE_DIR}/scripts/package/package_xcode_and_sdks.sh"
PACKAGE_SCRIPT="${LOCAL_BUILD_ROOT}/package_xcode_and_sdks-${XCODE_VERSION}.sh"
python3 - "${PACKAGE_SCRIPT_SOURCE}" "${PACKAGE_SCRIPT}" "${XCODE_APP}" <<'PY'
from pathlib import Path
import sys

source, destination, xcode_app = map(Path, sys.argv[1:])
text = source.read_text()
old = 'XCODE_CONTENTS="/Applications/Xcode.app/Contents"'
new = f'XCODE_CONTENTS="{xcode_app}/Contents"'
if old not in text:
    raise SystemExit(f"cannot patch unexpected package script: {source}")
destination.write_text(text.replace(old, new, 1))
destination.chmod(0o755)
PY

echo "Packaging Xcode ${XCODE_VERSION} SDKs and toolchain. This can take several minutes and multiple GB of disk space..."
(
    cd "${DEFOLD_SOURCE_DIR}"
    "${PACKAGE_SCRIPT}"
)

# Xcode 26.2 exposes MacOSX26.sdk as its canonical SDK symlink, while the
# pinned Extender environment requests an archive named MacOSX26.2.sdk. Keep
# the official package intact and add the filename alias expected by Extender.
SDK_PACKAGE_DIR="${DEFOLD_SOURCE_DIR}/local_sdks"
EXPECTED_MACOS_PACKAGE="${SDK_PACKAGE_DIR}/MacOSX${MACOS_SDK_VERSION}.sdk.tar.gz"
if [[ ! -e "${EXPECTED_MACOS_PACKAGE}" ]]; then
    PACKAGED_MACOS_SDK="$(find "${SDK_PACKAGE_DIR}" -maxdepth 1 -type f -name 'MacOSX*.sdk.tar.gz' -print -quit)"
    [[ -n "${PACKAGED_MACOS_SDK}" ]] || fail "packaging did not produce a macOS SDK archive"
    ln -s "$(basename "${PACKAGED_MACOS_SDK}")" "${EXPECTED_MACOS_PACKAGE}"
fi

echo "Building pinned Extender and manifest merger..."
(
    cd "${EXTENDER_SOURCE_DIR}"
    ./gradlew :server:bootJar :manifestmergetool:mainJar
)

# Extender 2.12.4 emits optional user.env entries prefixed with ';'. That is
# INI comment syntax, not valid Bash syntax, even though the file is sourced by
# its service scripts. Regenerate with the selected full JDK and normalize
# those optional lines to shell comments.
(
    cd "${EXTENDER_SOURCE_DIR}"
    export JAVA_HOME PATH
    ./server/envs/generate_user_env.sh
)
python3 - "${EXTENDER_SOURCE_DIR}/server/envs/user.env" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
lines = path.read_text().splitlines(keepends=True)
path.write_text("".join("#" + line[1:] if line.startswith(";") else line for line in lines))
PY

PACKAGE_SERVER_LOG="${LOCAL_BUILD_ROOT}/package-server.log"
python3 -m http.server "${PACKAGE_SERVER_PORT}" \
    --bind 127.0.0.1 \
    --directory "${DEFOLD_SOURCE_DIR}/local_sdks" \
    >"${PACKAGE_SERVER_LOG}" 2>&1 &
PACKAGE_SERVER_PID=$!
cleanup_package_server() {
    kill "${PACKAGE_SERVER_PID}" >/dev/null 2>&1 || true
    wait "${PACKAGE_SERVER_PID}" 2>/dev/null || true
}
trap cleanup_package_server EXIT INT TERM

for _ in {1..40}; do
    if curl --fail --silent "http://127.0.0.1:${PACKAGE_SERVER_PORT}/" >/dev/null; then
        break
    fi
    sleep 0.25
done
curl --fail --silent "http://127.0.0.1:${PACKAGE_SERVER_PORT}/" >/dev/null \
    || fail "temporary package server failed; see ${PACKAGE_SERVER_LOG}"

echo "Installing packaged SDKs into Extender..."
(
    cd "${EXTENDER_SOURCE_DIR}"
    export JAVA_HOME
    export PATH
    DM_PACKAGES_URL="http://127.0.0.1:${PACKAGE_SERVER_PORT}" \
        ./server/scripts/standalone/setup-standalone-env.sh
)

echo "Local Extender setup complete. Start it with: make extender-start"
