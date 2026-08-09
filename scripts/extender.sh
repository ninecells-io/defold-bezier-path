#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/config.sh
source "${SCRIPT_DIR}/config.sh"

SERVICE_SCRIPT="${EXTENDER_SOURCE_DIR}/server/scripts/standalone/service-standalone.sh"
USER_ENV="${EXTENDER_SOURCE_DIR}/server/envs/user.env"
DEPENDENCY_SERVICE="${SCRIPT_DIR}/dependency_server.sh"

require_setup() {
    [[ -x "${SERVICE_SCRIPT}" && -f "${USER_ENV}" && -f "${BOB_JAR}" ]] || {
        echo "Local Extender is not provisioned. Run 'make setup-local-extender' first." >&2
        exit 1
    }
}

is_ready() {
    curl --fail --silent --max-time 2 "${LOCAL_EXTENDER_URL}/" 2>/dev/null | grep -q "Extender"
}

wait_until_ready() {
    local attempt
    for attempt in {1..120}; do
        if is_ready; then
            echo "Extender is ready at ${LOCAL_EXTENDER_URL}."
            return 0
        fi
        sleep 0.5
    done
    echo "Extender did not become ready at ${LOCAL_EXTENDER_URL}." >&2
    echo "Logs: ${EXTENDER_SOURCE_DIR}/server/app/logs" >&2
    tail -n 30 "${EXTENDER_SOURCE_DIR}/server/app/logs/error.log" 2>/dev/null || true
    return 1
}

command_name="${1:-status}"
case "${command_name}" in
    start)
        require_setup
        "${DEPENDENCY_SERVICE}" ensure
        if is_ready; then
            echo "Extender is already ready at ${LOCAL_EXTENDER_URL}."
        else
            (
                cd "${EXTENDER_SOURCE_DIR}"
                "${SERVICE_SCRIPT}" start standalone-dev
            )
            wait_until_ready
        fi
        ;;
    stop)
        require_setup
        dependency_result=0
        "${DEPENDENCY_SERVICE}" stop || dependency_result=$?
        (
            cd "${EXTENDER_SOURCE_DIR}"
            "${SERVICE_SCRIPT}" stop standalone-dev
        )
        exit "${dependency_result}"
        ;;
    status)
        require_setup
        "${DEPENDENCY_SERVICE}" status
        if is_ready; then
            echo "Extender is ready at ${LOCAL_EXTENDER_URL}."
            curl --fail --silent --max-time 5 "${LOCAL_EXTENDER_URL}/health_report" || true
            echo
        else
            echo "Extender is not responding at ${LOCAL_EXTENDER_URL}." >&2
            exit 1
        fi
        ;;
    ensure)
        require_setup
        "${DEPENDENCY_SERVICE}" ensure
        if is_ready; then
            echo "Extender is ready at ${LOCAL_EXTENDER_URL}."
        else
            "$0" start
        fi
        ;;
    *)
        echo "Usage: $0 {start|stop|status|ensure}" >&2
        exit 2
        ;;
esac
