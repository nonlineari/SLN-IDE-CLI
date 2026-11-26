#!/usr/bin/env bash
# SLN-IDE-CLI Pico 2 helper. The pico window execs this file with one
# fixed argument: info or volume. It reads the BOOTSEL board. It does
# not copy a UF2 and it does not evaluate typed text.

set -o pipefail

IDE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG="${SLN_PICO_LOG:-${IDE_ROOT}/build/pico-last.log}"
cmd="${1:-}"

mkdir -p "$(dirname "$LOG")"

find_rp2350() {
    local d vendor product
    RP_SYS=""
    RP_VENDOR=""
    RP_PRODUCT=""
    RP_SERIAL=""
    for d in /sys/bus/usb/devices/*; do
        [[ -f "${d}/idVendor" ]] || continue
        vendor="$(tr -d '[:space:]' < "${d}/idVendor")"
        [[ "$vendor" == "2e8a" ]] || continue
        product="$(tr -d '\r\n' < "${d}/product" 2>/dev/null || true)"
        case "$product" in
            *RP2350*|*Pico\ 2*|*Pico2*)
                RP_SYS="$d"
                RP_VENDOR="$vendor:$(tr -d '[:space:]' < "${d}/idProduct")"
                RP_PRODUCT="$product"
                RP_SERIAL="$(tr -d '[:space:]' < "${d}/serial" 2>/dev/null || true)"
                return 0
                ;;
        esac
    done
    return 1
}

find_volume() {
    local link dev mountpoint rest
    RP_DISK=""
    RP_MOUNT=""
    for link in /dev/disk/by-id/usb-RPI_RP2350_*-part1; do
        [[ -e "$link" ]] || continue
        dev="$(readlink -f "$link" 2>/dev/null || true)"
        [[ -n "$dev" ]] || continue
        RP_DISK="$dev"
        while read -r dev mountpoint rest; do
            if [[ "$dev" == "$RP_DISK" ]]; then
                RP_MOUNT="${mountpoint//\\040/ }"
                return 0
            fi
        done < /proc/mounts
        return 0
    done
    return 1
}

{
    case "$cmd" in
        info|volume)
            ;;
        *)
            echo "usage: pico.sh info|volume"
            exit 2
            ;;
    esac

    echo "tool: pico2"
    if find_rp2350; then
        echo "usb: ${RP_VENDOR} ${RP_PRODUCT}"
        echo "serial: ${RP_SERIAL:-unknown}"
        echo "port: $(basename "$RP_SYS")"
    else
        echo "board: absent"
        echo "expected: 2e8a:000f RP2350 Boot"
        exit 1
    fi

    if find_volume; then
        echo "disk: ${RP_DISK:-none}"
        echo "volume: ${RP_MOUNT:-unmounted}"
    else
        echo "disk: none"
        echo "volume: unmounted"
    fi

    if [[ "$cmd" == "info" && -n "$RP_MOUNT" && -f "${RP_MOUNT}/INFO_UF2.TXT" ]]; then
        echo "uf2:"
        sed -n '1,6p' "${RP_MOUNT}/INFO_UF2.TXT"
    fi

    if [[ "$cmd" == "volume" ]]; then
        if [[ -z "$RP_MOUNT" || ! -d "$RP_MOUNT" ]]; then
            echo "volume: unmounted"
            exit 1
        fi
        echo "files:"
        found=0
        for f in "$RP_MOUNT"/*; do
            [[ -f "$f" ]] || continue
            echo "  $(basename "$f")"
            found=1
        done
        if [[ "$found" -eq 0 ]]; then
            echo "  (empty)"
        fi
    fi
} 2>&1 | tee "$LOG"
exit "${PIPESTATUS[0]}"
