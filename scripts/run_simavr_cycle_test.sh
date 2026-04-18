#!/usr/bin/env bash
set -euo pipefail

: "${AVR_CC:=avr-gcc}"
: "${SIMAVR:=simavr}"
: "${SIMAVR_INCLUDE:=/usr/include/simavr}"
: "${AVR_CYCLE_MCU:=atmega328p}"
: "${AVR_CYCLE_HZ:=8000000}"
: "${AVR_CYCLE_TIMEOUT:=10s}"
: "${AVR_CYCLE_OPTIMIZE:=-Os}"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "required tool '$1' not found"
}

require_tool "$AVR_CC"
require_tool "$SIMAVR"
require_tool timeout

if [[ ! -f "$SIMAVR_INCLUDE/avr/avr_mcu_section.h" ]]; then
    fail "could not find simavr headers under '$SIMAVR_INCLUDE'"
fi

if (( 1000000000 % AVR_CYCLE_HZ != 0 )); then
    fail "AVR_CYCLE_HZ=$AVR_CYCLE_HZ does not yield an integral ns-per-cycle value"
fi

ns_per_cycle=$((1000000000 / AVR_CYCLE_HZ))

build_dir=".simavr-test"
metrics_file="avr_cycle_metrics.txt"
trap 'rm -rf "$build_dir"' EXIT
mkdir -p "$build_dir"

elf="$build_dir/avr_cycle_test.elf"
log="$build_dir/simavr.log"
vcd="$build_dir/avr_cycle_trace.vcd"

"$AVR_CC" -mmcu="$AVR_CYCLE_MCU" -DF_CPU="${AVR_CYCLE_HZ}UL" \
    -std=c99 "$AVR_CYCLE_OPTIMIZE" -g -Wall -Wextra -pedantic \
    -I"$SIMAVR_INCLUDE" \
    -DHEATSHRINK_DYNAMIC_ALLOC=0 \
    -o "$elf" \
    avr_cycle_test.c heatshrink_encoder.c heatshrink_decoder.c

rm -f "$log" "$vcd"

set +e
(
    cd "$build_dir"
    timeout "$AVR_CYCLE_TIMEOUT" \
        "$SIMAVR" -m "$AVR_CYCLE_MCU" -f "$AVR_CYCLE_HZ" -v "$(basename "$elf")" \
        >"$(basename "$log")" 2>&1
)
simavr_status=$?
set -e

case "$simavr_status" in
    0|124)
        ;;
    *)
        cat "$log" >&2 || true
        fail "simavr exited with status $simavr_status"
        ;;
esac

if [[ ! -f "$vcd" ]]; then
    cat "$log" >&2 || true
    fail "simavr did not produce VCD trace '$vcd'"
fi

extract_phase_timestamps() {
    awk '
        function bin2dec(bits,    i, v) {
            v = 0;
            for (i = 1; i <= length(bits); i++) {
                v = (v * 2) + substr(bits, i, 1);
            }
            return v;
        }

        BEGIN {
            sym = "";
            now = 0;
        }

        $1 == "$var" && $(NF-1) == "phase_marker" {
            sym = $4;
            next;
        }

        /^#/ {
            now = substr($0, 2) + 0;
            next;
        }

        sym != "" && $1 ~ /^b[01]+$/ && $2 == sym {
            val = bin2dec(substr($1, 2));
            if (!(val in seen)) {
                seen[val] = now;
            }
        }

        END {
            if (!(1 in seen) || !(2 in seen) || !(3 in seen) || !(4 in seen) || !(5 in seen)) {
                exit 1;
            }
            printf "%s %s %s %s %s\n", seen[1], seen[2], seen[3], seen[4], seen[5];
        }
    ' "$vcd"
}

extract_vcd_timescale_ns() {
    awk '
        function unit_scale(unit) {
            if (unit == "s") return 1000000000;
            if (unit == "ms") return 1000000;
            if (unit == "us") return 1000;
            if (unit == "ns") return 1;
            return 0;
        }

        /^\$timescale$/ {
            mode = 1;
            next;
        }

        mode && /^\$end$/ {
            mode = 0;
            if (value > 0 && scale > 0) {
                print value * scale;
                exit 0;
            }
            exit 1;
        }

        mode && value == 0 {
            token = $1;
            if (token ~ /^[0-9]+$/) {
                value = token + 0;
                if (NF >= 2) {
                    scale = unit_scale($2);
                }
                next;
            }
            if (token ~ /^[0-9]+[a-z]+$/) {
                unit = token;
                gsub(/^[0-9]+/, "", unit);
                value = token + 0;
                scale = unit_scale(unit);
                next;
            }
        }

        mode && value > 0 && scale == 0 {
            scale = unit_scale($1);
        }

        END {
            if (value > 0 && scale > 0) {
                print value * scale;
                exit 0;
            }
            exit 1;
        }
    ' "$vcd"
}

timestamps=$(extract_phase_timestamps) || {
    cat "$log" >&2 || true
    fail "did not observe a complete phase_marker trace in '$vcd'"
}

vcd_tick_ns=$(extract_vcd_timescale_ns) || {
    fail "could not parse VCD \$timescale from '$vcd'"
}

read -r t1 t2 t3 t4 _ <<< "$timestamps"

compress_ticks=$((t2 - t1))
decompress_ticks=$((t4 - t3))

compress_ns=$((compress_ticks * vcd_tick_ns))
decompress_ns=$((decompress_ticks * vcd_tick_ns))

if (( compress_ns <= 0 || decompress_ns <= 0 )); then
    fail "non-positive timing interval in VCD trace"
fi

if (( compress_ns % ns_per_cycle != 0 || decompress_ns % ns_per_cycle != 0 )); then
    fail "VCD timestamps are not aligned to full AVR cycles"
fi

compress_cycles=$((compress_ns / ns_per_cycle))
decompress_cycles=$((decompress_ns / ns_per_cycle))

if (( compress_cycles == 0 || decompress_cycles == 0 )); then
    fail "invalid zero cycle result"
fi

cat > "$metrics_file" <<EOF2
compress_cycles=$compress_cycles
decompress_cycles=$decompress_cycles
avr_cycle_mcu=$AVR_CYCLE_MCU
avr_cycle_hz=$AVR_CYCLE_HZ
avr_cycle_optimize=$AVR_CYCLE_OPTIMIZE
EOF2

cat "$metrics_file"
