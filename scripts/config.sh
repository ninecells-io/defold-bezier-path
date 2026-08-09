#!/usr/bin/env bash

# Version pins are intentionally not inherited from the environment. Updating
# the local toolchain should be an explicit repository change.
export DEFOLD_VERSION="1.13.0"
export DEFOLD_SDK_SHA="f735c12192bf95684e6ae1ae27c400b8170fc6d8"
export EXTENDER_TAG="extender-v2.12.4"
export XCODE_VERSION="26.2"
export MACOS_SDK_VERSION="26.2"
export IOS_SDK_VERSION="26.2"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
export LOCAL_BUILD_ROOT="${LOCAL_BUILD_ROOT:-${REPO_ROOT}/.local-build}"
export XCODE_APP="${XCODE_APP:-/Applications/Xcode_26.2.app}"
export DEFOLD_CONTROLS_ROOT="${DEFOLD_CONTROLS_ROOT:-$(cd "${REPO_ROOT}/../defold-controls" 2>/dev/null && pwd || true)}"
export DEFOLD_SUDOKU_PLAYBOARD_ROOT="${DEFOLD_SUDOKU_PLAYBOARD_ROOT:-$(cd "${REPO_ROOT}/../defold-sudoku-playboard" 2>/dev/null && pwd || true)}"
export DEFOLD_HELPERS_ROOT="${DEFOLD_HELPERS_ROOT:-$(cd "${REPO_ROOT}/../defold-helpers" 2>/dev/null && pwd || true)}"
export ONE_SUDOKU_ROOT="${ONE_SUDOKU_ROOT:-$(cd "${REPO_ROOT}/../one-sudoku" 2>/dev/null && pwd || true)}"
export CONSUMERS="${CONSUMERS:-defold-controls defold-sudoku-playboard one-sudoku}"
export LOCAL_EXTENDER_HOST="127.0.0.1"
export LOCAL_EXTENDER_PORT="9010"
export LOCAL_EXTENDER_URL="http://${LOCAL_EXTENDER_HOST}:${LOCAL_EXTENDER_PORT}"
export LOCAL_DEPENDENCY_HOST="127.0.0.1"
export LOCAL_DEPENDENCY_PORT="${LOCAL_DEPENDENCY_PORT:-9012}"
export LOCAL_DEPENDENCY_URL="http://${LOCAL_DEPENDENCY_HOST}:${LOCAL_DEPENDENCY_PORT}"
export HOSTED_EXTENDER_URL="${HOSTED_EXTENDER_URL:-https://build.defold.com}"
export PACKAGE_SERVER_PORT="${PACKAGE_SERVER_PORT:-9011}"

export DEFOLD_SOURCE_DIR="${LOCAL_BUILD_ROOT}/defold-${DEFOLD_VERSION}"
export EXTENDER_SOURCE_DIR="${LOCAL_BUILD_ROOT}/extender-${EXTENDER_TAG}"
export BOB_JAR="${LOCAL_BUILD_ROOT}/bob-${DEFOLD_VERSION}.jar"
export DEFOLD_CONTROLS_STAGE_ROOT="${DEFOLD_CONTROLS_STAGE_ROOT:-${LOCAL_BUILD_ROOT}/defold-controls-stage}"
export DEFOLD_SUDOKU_PLAYBOARD_STAGE_ROOT="${DEFOLD_SUDOKU_PLAYBOARD_STAGE_ROOT:-${LOCAL_BUILD_ROOT}/defold-sudoku-playboard-stage}"
export APP_STAGE_ROOT="${LOCAL_BUILD_ROOT}/one-sudoku-stage"

if [[ -n "${JAVA_BIN:-}" ]]; then
    export JAVA_BIN
elif [[ -x "/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home/bin/java" ]]; then
    export JAVA_BIN="/opt/homebrew/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home/bin/java"
elif [[ -x "/usr/local/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home/bin/java" ]]; then
    export JAVA_BIN="/usr/local/opt/openjdk@25/libexec/openjdk.jdk/Contents/Home/bin/java"
elif [[ -n "${JAVA_HOME:-}" && -x "${JAVA_HOME}/bin/java" ]]; then
    export JAVA_BIN="${JAVA_HOME}/bin/java"
elif [[ -x "/Applications/Defold.app/Contents/Resources/packages/jdk-25+36/bin/java" ]]; then
    # Defold ships a trimmed Java 25 runtime. It can run Bob, but the Extender
    # bootstrap will replace it with a full JDK because Gradle needs
    # java.instrument and javac.
    export JAVA_BIN="/Applications/Defold.app/Contents/Resources/packages/jdk-25+36/bin/java"
else
    export JAVA_BIN="$(command -v java 2>/dev/null || true)"
fi

if [[ -n "${JAVA_BIN}" ]]; then
    export JAVA_HOME="$(cd "$(dirname "${JAVA_BIN}")/.." && pwd)"
fi
