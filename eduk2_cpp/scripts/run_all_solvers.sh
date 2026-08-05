#!/usr/bin/env bash
# Build and run faithful, optimized, and PYAsUKP/OCaml for every .ukp file.
# The input format is shared by the C++ reader and PYAsUKP's -src option.

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATA_DIR="${DATA_DIR:-${PROJECT_DIR}/data}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
OCAML_DIR="${OCAML_DIR:-/home/aprix/Downloads/pyasukp_mail/pyasukp}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTPUT_DIR="${OUTPUT_DIR:-${PROJECT_DIR}/results/${RUN_ID}}"
BUILD=1

usage() {
    cat <<'EOF'
Usage: scripts/run_all_solvers.sh [options]

Options:
  --data-dir DIR       Directory containing .ukp files (default: data/)
  --build-dir DIR      CMake build directory (default: build/)
  --ocaml-dir DIR      PYAsUKP directory (default: ~/Downloads/pyasukp_mail/pyasukp)
  --output-dir DIR     Directory for CSV and per-run logs
  --build-type TYPE    CMake build type (default: Release)
  --no-build           Reuse existing C++ and OCaml executables
  -h, --help           Show this help

Environment variables DATA_DIR, BUILD_DIR, OCAML_DIR, OUTPUT_DIR and BUILD_TYPE
have the equivalent effect. The script writes results.csv and one log per solver/run.
EOF
}

while (($#)); do
    case "$1" in
        --data-dir) DATA_DIR="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --ocaml-dir) OCAML_DIR="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --no-build) BUILD=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

require_dir() {
    [[ -d "$1" ]] || { echo "directory not found: $1" >&2; exit 2; }
}

require_dir "$DATA_DIR"
require_dir "$OCAML_DIR"

if ((BUILD)); then
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$BUILD_DIR" --parallel
fi

FAITHFUL_BIN="${BUILD_DIR}/ukp_solve"
OPTIMIZED_BIN="${BUILD_DIR}/ukp_solve"
OCAML_BIN="${OCAML_BIN:-${OCAML_DIR}/pyasukpbct}"

if [[ ! -x "$FAITHFUL_BIN" || ! -x "$OPTIMIZED_BIN" ]]; then
    echo "C++ executable not found: ${BUILD_DIR}/ukp_solve" >&2
    exit 2
fi

if [[ ! -x "$OCAML_BIN" ]]; then
    if (( ! BUILD )); then
        echo "OCaml executable not found: ${OCAML_BIN}" >&2
        exit 2
    fi
    make -C "$OCAML_DIR" bct
fi
if [[ ! -x "$OCAML_BIN" ]]; then
    echo "PYAsUKP build did not produce ${OCAML_BIN}" >&2
    exit 2
fi

mkdir -p "$OUTPUT_DIR"/{faithful,optimized,ocaml}
CSV="${OUTPUT_DIR}/results.csv"
printf 'instance,solver,status,elapsed_seconds,log\n' > "$CSV"

append_result() {
    local instance="$1" solver="$2" status="$3" time_file="$4" log="$5"
    local elapsed=""
    [[ -f "$time_file" ]] && elapsed="$(tail -n 1 "$time_file")"
    printf '%s,%s,%s,%s,%s\n' "$instance" "$solver" "$status" "$elapsed" "$log" >> "$CSV"
    rm -f "$time_file"
}

run_cpp() {
    local instance="$1"
    local kind="$2"
    local log="$3"
    local time_file="${log}.time"
    if /usr/bin/time -f '%e' -o "$time_file" "$FAITHFUL_BIN" "$kind" "$instance" > "$log" 2>&1; then
        append_result "$(basename "$instance")" "$kind" "ok" "$time_file" "$log"
    else
        append_result "$(basename "$instance")" "$kind" "failed" "$time_file" "$log"
    fi
}

run_ocaml() {
    local instance="$1"
    local log="$2"
    local time_file="${log}.time"
    local source="$instance"
    local converted="${log%.log}.input.ukp"
    # PYAsUKP accepts its own n:/c:/begin-data format. Convert only the
    # compact C++ legacy format, whose item order is profit then weight.
    if ! grep -qE '^[[:space:]]*(n|m):|^[[:space:]]*begin data' "$instance"; then
        awk '
            /^[[:space:]]*#/ { next }
            { for (i = 1; i <= NF; ++i) values[++count] = $i }
            END {
                n = values[1]; capacity = values[2];
                if (n == "" || capacity == "") exit 2;
                print "n: " n;
                print "c: " capacity;
                print "begin data";
                for (i = 0; i < n; ++i) print values[4 + 2 * i] " " values[3 + 2 * i];
                print "end data";
            }
        ' "$instance" > "$converted" || return 1
        source="$converted"
    fi
    if /usr/bin/time -f '%e' -o "$time_file" "$OCAML_BIN" -src "$source" -batch > "$log" 2>&1; then
        append_result "$(basename "$instance")" "ocaml_pyasukpbct" "ok" "$time_file" "$log"
    else
        append_result "$(basename "$instance")" "ocaml_pyasukpbct" "failed" "$time_file" "$log"
    fi
}

shopt -s nullglob
instances=("${DATA_DIR}"/*.ukp)
if ((${#instances[@]} == 0)); then
    echo "no .ukp instances in ${DATA_DIR}" >&2
    exit 2
fi

for instance in "${instances[@]}"; do
    stem="$(basename "${instance%.ukp}")"
    echo "running ${stem}"
    run_cpp "$instance" faithful "${OUTPUT_DIR}/faithful/${stem}.log"
    run_cpp "$instance" optimized "${OUTPUT_DIR}/optimized/${stem}.log"
    run_ocaml "$instance" "${OUTPUT_DIR}/ocaml/${stem}.log"
done

echo "completed ${#instances[@]} instances"
echo "CSV: ${CSV}"
