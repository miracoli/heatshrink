#!/usr/bin/env bash
set -euo pipefail

: "${AVRORA_JAR:=avrora.jar}"

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "FAIL: required tool '$1' not found" >&2
        exit 1
    fi
}

require_tool avr-gcc
require_tool java

if [[ ! -f "$AVRORA_JAR" ]]; then
    echo "FAIL: AVRORA_JAR not found at '$AVRORA_JAR'" >&2
    exit 1
fi

build_dir=".avrora-test"
mkdir -p "$build_dir"
elf="$build_dir/avr_cycle_test.elf"

avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL -std=c99 -Os -g -Wall -Wextra \
    -DHEATSHRINK_DYNAMIC_ALLOC=0 \
    -o "$elf" \
    avr_cycle_test.c heatshrink_encoder.c heatshrink_decoder.c

simulate_help=$(java -jar "$AVRORA_JAR" -help simulate 2>&1)
trip_help=$(java -jar "$AVRORA_JAR" -help trip-time 2>&1)

monitor_opt=$(printf '%s\n' "$simulate_help" | awk '
    /-monitors[ =]/ {print "-monitors"; found=1; exit}
    /-monitor[ =]/ {print "-monitor"; found=1; exit}
    END {if (!found) exit 1}
') || {
    echo "FAIL: could not determine monitor option from -help simulate" >&2
    exit 1
}

stop_opt=$(printf '%s\n' "$simulate_help" | awk '
    /-cycles[ =]/ {print "-cycles"; found=1; exit}
    /-seconds[ =]/ {print "-seconds"; found=1; exit}
    END {if (!found) exit 1}
') || {
    echo "FAIL: could not determine stop option from -help simulate" >&2
    exit 1
}

from_opt=$(printf '%s\n' "$trip_help" | awk '
    /-from[ =]/ {print "-from"; found=1; exit}
    /-start[ =]/ {print "-start"; found=1; exit}
    END {if (!found) exit 1}
') || {
    echo "FAIL: could not determine trip-time start option from -help trip-time" >&2
    exit 1
}

to_opt=$(printf '%s\n' "$trip_help" | awk '
    /-to[ =]/ {print "-to"; found=1; exit}
    /-end[ =]/ {print "-end"; found=1; exit}
    END {if (!found) exit 1}
') || {
    echo "FAIL: could not determine trip-time end option from -help trip-time" >&2
    exit 1
}

run_trip_time() {
    local from_sym="$1"
    local to_sym="$2"
    local out
    out=$(java -jar "$AVRORA_JAR" -action=simulate \
        "${monitor_opt}=trip-time" \
        "${from_opt}=${from_sym}" \
        "${to_opt}=${to_sym}" \
        "${stop_opt}=5" \
        "$elf" 2>&1)
    printf '%s\n' "$out"
}

extract_cycles() {
    printf '%s\n' "$1" | awk '
        match($0, /([0-9]+)[[:space:]]+cycles/, m) {print m[1]; exit}
    '
}

compress_out=$(run_trip_time compress_start compress_end)
decompress_out=$(run_trip_time decompress_start decompress_end)
status_out=$(run_trip_time decompress_end test_ok)

compress_cycles=$(extract_cycles "$compress_out")
decompress_cycles=$(extract_cycles "$decompress_out")
status_trip_cycles=$(extract_cycles "$status_out")

if [[ -z "$compress_cycles" || -z "$decompress_cycles" || -z "$status_trip_cycles" ]]; then
    echo "FAIL: could not parse Avrora trip-time output" >&2
    exit 1
fi

if (( compress_cycles == 0 || decompress_cycles == 0 || status_trip_cycles == 0 )); then
    echo "FAIL: invalid cycle result (zero) or functional status not OK" >&2
    exit 1
fi

echo "compress_cycles=${compress_cycles}"
echo "decompress_cycles=${decompress_cycles}"
