#!/usr/bin/env bash
# SLN-IDE-CLI ESP32 helper. The TUI execs this file with one fixed
# argument: build, flash, or monitor. It does not evaluate typed text.
#
# set -u is intentionally off. ESP-IDF export.sh reads optional variables.
# set -e is off so the pipeline status stays in PIPESTATUS.

set -o pipefail

IDE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SLN_ROOT="$(cd "${IDE_ROOT}/.." && pwd)"
LOG="${SLN_ESP_LOG:-${IDE_ROOT}/build/esp-last.log}"
PORT="/dev/ttyUSB0"
IDF_EXPORT="${HOME}/esp/esp-idf/export.sh"
FW="${SLN_ROOT}/ESPWROOM32_wifi/firmware"
cmd="${1:-}"

mkdir -p "$(dirname "$LOG")"

{
    case "$cmd" in
        build|flash|monitor)
            ;;
        *)
            echo "usage: esp.sh build|flash|monitor"
            exit 2
            ;;
    esac

    if [[ "$cmd" == "flash" || "$cmd" == "monitor" ]]; then
        if [[ ! -r "$PORT" || ! -w "$PORT" ]]; then
            echo "cannot open /dev/ttyUSB0 (group uucp required)"
            exit 1
        fi
    fi

    if [[ ! -f "$IDF_EXPORT" ]]; then
        echo "missing ${IDF_EXPORT}"
        exit 1
    fi
    if [[ ! -f "${FW}/CMakeLists.txt" ]]; then
        echo "missing ${FW}/CMakeLists.txt"
        exit 1
    fi

    export PATH="${HOME}/.local/bin:${PATH}"
    export PYTHONUNBUFFERED=1
    # shellcheck disable=SC1090
    . "$IDF_EXPORT"
    cd "$FW" || {
        echo "missing ${FW}"
        exit 1
    }
    echo "project: ${FW}"

    case "$cmd" in
        build)
            idf.py -C "$FW" -B "${FW}/build" build
            ;;
        flash)
            idf.py -C "$FW" -B "${FW}/build" -p "$PORT" flash
            ;;
        monitor)
            idf.py -C "$FW" -B "${FW}/build" -p "$PORT" monitor
            ;;
    esac
} 2>&1 | tee "$LOG"

exit "${PIPESTATUS[0]}"
