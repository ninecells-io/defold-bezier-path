#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/config.sh
source "${SCRIPT_DIR}/config.sh"

PID_FILE="${LOCAL_BUILD_ROOT}/dependency-server.pid"
LOG_FILE="${LOCAL_BUILD_ROOT}/dependency-server.log"

is_ready() {
    curl --fail --silent --max-time 2 "${LOCAL_DEPENDENCY_URL}/health" 2>/dev/null \
        | grep -q '^local Defold dependencies ready$'
}

running_pid() {
    [[ -f "${PID_FILE}" ]] || return 1
    local pid
    pid="$(<"${PID_FILE}")"
    [[ "${pid}" =~ ^[0-9]+$ ]] || return 1
    kill -0 "${pid}" >/dev/null 2>&1 || return 1
    ps -p "${pid}" -o command= 2>/dev/null | grep -q '[d]ependency_server.py' || return 1
    echo "${pid}"
}

wait_until_ready() {
    local attempt
    for attempt in {1..40}; do
        if is_ready; then
            echo "Dependency server is ready at ${LOCAL_DEPENDENCY_URL}."
            return 0
        fi
        sleep 0.25
    done
    echo "Dependency server did not become ready at ${LOCAL_DEPENDENCY_URL}." >&2
    echo "Log: ${LOG_FILE}" >&2
    tail -n 30 "${LOG_FILE}" 2>/dev/null || true
    return 1
}

command_name="${1:-status}"
case "${command_name}" in
    start)
        if is_ready; then
            echo "Dependency server is already ready at ${LOCAL_DEPENDENCY_URL}."
            exit 0
        fi
        if pid="$(running_pid)"; then
            echo "Dependency server process ${pid} is running but not ready." >&2
            exit 1
        fi
        mkdir -p "${LOCAL_BUILD_ROOT}"
        rm -f "${PID_FILE}"
        nohup python3 "${SCRIPT_DIR}/dependency_server.py" \
            --host "${LOCAL_DEPENDENCY_HOST}" \
            --port "${LOCAL_DEPENDENCY_PORT}" \
            >"${LOG_FILE}" 2>&1 &
        echo "$!" >"${PID_FILE}"
        wait_until_ready
        ;;
    stop)
        if ! pid="$(running_pid)"; then
            rm -f "${PID_FILE}"
            echo "Dependency server is not running."
            exit 0
        fi
        kill "${pid}"
        for _ in {1..50}; do
            if ! kill -0 "${pid}" >/dev/null 2>&1; then
                rm -f "${PID_FILE}"
                echo "Dependency server stopped."
                exit 0
            fi
            sleep 0.1
        done
        echo "Dependency server ${pid} did not stop cleanly." >&2
        exit 1
        ;;
    status)
        if is_ready; then
            echo "Dependency server is ready at ${LOCAL_DEPENDENCY_URL}."
        else
            echo "Dependency server is not responding at ${LOCAL_DEPENDENCY_URL}." >&2
            exit 1
        fi
        ;;
    ensure)
        if is_ready; then
            echo "Dependency server is ready at ${LOCAL_DEPENDENCY_URL}."
        else
            "$0" start
        fi
        ;;
    *)
        echo "Usage: $0 {start|stop|status|ensure}" >&2
        exit 2
        ;;
esac
