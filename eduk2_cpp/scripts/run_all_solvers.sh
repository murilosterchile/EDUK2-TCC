#!/usr/bin/env bash
# Build and run faithful, optimized, and PYAsUKP/OCaml for every .ukp file.
# The input format is shared by the C++ reader and PYAsUKP's -src option.

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATA_DIR="${DATA_DIR:-${PROJECT_DIR}/data}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
BENCHMARK_BUILD_DIR="${BENCHMARK_BUILD_DIR:-${PROJECT_DIR}/build-benchmark}"
OCAML_DIR="${OCAML_DIR:-/home/aprix/Downloads/pyasukp_mail/pyasukp}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
RUNS="${RUNS:-5}"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTPUT_FILE="${OUTPUT_FILE:-${PROJECT_DIR}/results/run_${RUN_ID}.txt}"
BUILD=1

usage() {
    cat <<'EOF'
Usage: scripts/run_all_solvers.sh [options]

Options:
  --data-dir DIR       Directory containing .ukp files (default: data/)
  --build-dir DIR      CMake build directory for optimized (default: build/)
  --benchmark-build-dir DIR
                         CMake build directory for faithful (default: build-benchmark/)
  --ocaml-dir DIR      PYAsUKP directory (default: ~/Downloads/pyasukp_mail/pyasukp)
  --output-file FILE   Single text report (default: results/run_<timestamp>.txt)
  --output-dir DIR     Deprecated alias; writes DIR/results.txt
  --build-type TYPE    CMake build type (default: Release)
  --runs N             Executions per instance and solver (default: 5)
  --no-build           Reuse existing C++ and native OCaml executables
  -h, --help           Show this help

Environment variables DATA_DIR, BUILD_DIR, BENCHMARK_BUILD_DIR, OCAML_DIR,
OUTPUT_FILE, BUILD_TYPE and RUNS have the equivalent effect. The script writes
one tab-separated text report with averages over the requested executions.
EOF
}

while (($#)); do
    case "$1" in
        --data-dir) DATA_DIR="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --benchmark-build-dir) BENCHMARK_BUILD_DIR="$2"; shift 2 ;;
        --ocaml-dir) OCAML_DIR="$2"; shift 2 ;;
        --output-file) OUTPUT_FILE="$2"; shift 2 ;;
        --output-dir) OUTPUT_FILE="${2%/}/results.txt"; shift 2 ;;
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --runs) RUNS="$2"; shift 2 ;;
        --no-build) BUILD=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$RUNS" =~ ^[1-9][0-9]*$ ]] || { echo "--runs must be a positive integer" >&2; exit 2; }

require_dir() {
    [[ -d "$1" ]] || { echo "directory not found: $1" >&2; exit 2; }
}

require_dir "$DATA_DIR"
require_dir "$OCAML_DIR"

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
OCAML_RUNTIME_DIR="$OCAML_DIR"

if ((BUILD)); then
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$BUILD_DIR" --parallel
    cmake -S "$PROJECT_DIR" -B "$BENCHMARK_BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$BENCHMARK_BUILD_DIR" --parallel
    # Build PYAsUKP from its .ml sources as a native executable so its timing
    # is comparable to the C++ executables. Build in a clean temporary copy,
    # avoiding stale artifacts and leaving the checked-out OCaml tree intact.
    OCAML_RUNTIME_DIR="${TEMP_DIR}/pyasukp"
    cp -a "$OCAML_DIR" "$OCAML_RUNTIME_DIR"
    make -C "$OCAML_RUNTIME_DIR" allclean optt
fi

FAITHFUL_BIN="${BENCHMARK_BUILD_DIR}/ukp_solve"
OPTIMIZED_BIN="${BUILD_DIR}/ukp_solve"
OCAML_BIN="${OCAML_BIN:-${OCAML_RUNTIME_DIR}/pyasukpt}"

if [[ ! -x "$FAITHFUL_BIN" || ! -x "$OPTIMIZED_BIN" ]]; then
    echo "C++ executable not found: faithful=${FAITHFUL_BIN}, optimized=${OPTIMIZED_BIN}" >&2
    exit 2
fi

if [[ ! -x "$OCAML_BIN" ]]; then
    if (( ! BUILD )); then
        echo "OCaml executable not found: ${OCAML_BIN}" >&2
        exit 2
    fi
fi
if [[ ! -x "$OCAML_BIN" ]]; then
    echo "native PYAsUKP build did not produce ${OCAML_BIN}" >&2
    exit 2
fi

mkdir -p "$(dirname "$OUTPUT_FILE")"

printf '# EDUK2 solver execution report\n# generated_at: %s\n' "$(date --iso-8601=seconds)" > "$OUTPUT_FILE"
printf 'instance\talgorithm\tstatus\truns\telapsed_seconds\tprofit\tweight\toptimal\tverified\tstates_scanned\tpoints_generated\tbb_nodes\tdetails\n' >> "$OUTPUT_FILE"

elapsed_seconds() {
    awk -v begin="$1" -v end="$2" 'BEGIN { printf "%.6f", (end - begin) / 1000000000 }'
}

value_from_cpp_output() {
    local output="$1" key="$2"
    awk -v key="$key" '$1 == key { print $2; exit }' <<< "$output"
}

average_values() {
    awk '{ sum += $1; count += 1 } END { if (count > 0) printf "%.6f", sum / count }'
}

average_array() {
    (($# > 0)) || return
    printf '%s\n' "$@" | average_values
}

append_row() {
    local instance="$1" algorithm="$2" status="$3" runs="$4" elapsed="$5" profit="$6" weight="$7"
    local optimal="$8" verified="$9" states="${10}" points="${11}" bb_nodes="${12}" details="${13}"
    details="${details//$'\t'/ }"
    details="${details//$'\n'/ }"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$instance" "$algorithm" "$status" "$runs" "$elapsed" "$profit" "$weight" "$optimal" "$verified" \
        "$states" "$points" "$bb_nodes" "$details" >> "$OUTPUT_FILE"
}

run_cpp() {
    local binary="$1" instance="$2" kind="$3" label="$4"
    local start end output exit_code status="ok" stop_reason=""
    local -a elapsed_values=() profit_values=() weight_values=() optimal_values=()
    local -a verified_values=() states_values=() points_values=() bb_nodes_values=()
    local run
    for ((run = 1; run <= RUNS; ++run)); do
        start="$(date +%s%N)"
        output="$("$binary" "$kind" "$instance" 2>&1)"
        exit_code=$?
        end="$(date +%s%N)"
        elapsed_values+=("$(elapsed_seconds "$start" "$end")")
        if ((exit_code != 0)); then
            status="failed"
            continue
        fi
        profit_values+=("$(value_from_cpp_output "$output" profit)")
        weight_values+=("$(value_from_cpp_output "$output" weight)")
        optimal_values+=("$(value_from_cpp_output "$output" optimal)")
        verified_values+=("$(value_from_cpp_output "$output" verified)")
        states_values+=("$(value_from_cpp_output "$output" states_scanned)")
        points_values+=("$(value_from_cpp_output "$output" points_generated)")
        bb_nodes_values+=("$(value_from_cpp_output "$output" bb_nodes)")
        stop_reason="$(value_from_cpp_output "$output" stop_reason)"
    done
    append_row "$label" "$kind" "$status" "$RUNS" "$(average_array "${elapsed_values[@]}")" \
        "$(average_array "${profit_values[@]}")" "$(average_array "${weight_values[@]}")" \
        "$(average_array "${optimal_values[@]}")" "$(average_array "${verified_values[@]}")" \
        "$(average_array "${states_values[@]}")" "$(average_array "${points_values[@]}")" \
        "$(average_array "${bb_nodes_values[@]}")" "last_stop_reason=${stop_reason}"
}

run_ocaml() {
    local instance="$1" label="$2"
    local source="$instance"
    local converted
    # PYAsUKP accepts its own n:/c:/begin-data format. Convert only the
    # compact C++ legacy format, whose item order is profit then weight.
    if ! grep -qE '^[[:space:]]*(n|m):|^[[:space:]]*begin data' "$instance"; then
        converted="$(mktemp "${TEMP_DIR}/input.XXXXXX.ukp")"
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
    local start end output exit_code status="ok" summary=""
    local -a elapsed_values=() profit_values=() optimal_values=() points_values=() bb_nodes_values=()
    local run
    # PYAsUKP prefixes the batch fields with a variable-length instance
    # classification. The stable fields are counted from the end: exact flag,
    # profit, internal time, and B&B nodes; critical points precede them.
    for ((run = 1; run <= RUNS; ++run)); do
        start="$(date +%s%N)"
        output="$("$OCAML_BIN" -src "$source" -batch 2>&1)"
        exit_code=$?
        end="$(date +%s%N)"
        elapsed_values+=("$(elapsed_seconds "$start" "$end")")
        if ((exit_code != 0)); then
            status="failed"
            continue
        fi
        summary="$(awk 'NF { print; exit }' <<< "$output")"
        profit_values+=("$(awk 'NF { print $(NF - 2); exit }' <<< "$output")")
        optimal_values+=("$(awk 'NF { print $(NF - 3); exit }' <<< "$output")")
        points_values+=("$(awk 'NF { print $(NF - 4); exit }' <<< "$output")")
        bb_nodes_values+=("$(awk 'NF { print $NF; exit }' <<< "$output")")
    done
    append_row "$label" "ocaml_pyasukpt" "$status" "$RUNS" "$(average_array "${elapsed_values[@]}")" \
        "$(average_array "${profit_values[@]}")" "" \
        "$(average_array "${optimal_values[@]}")" "" "" \
        "$(average_array "${points_values[@]}")" \
        "$(average_array "${bb_nodes_values[@]}")" "$summary"
}

mapfile -d '' -t instances < <(find "$DATA_DIR" -type f -name '*.ukp' -print0 | sort -z)
if ((${#instances[@]} == 0)); then
    echo "no .ukp instances in ${DATA_DIR}" >&2
    exit 2
fi

for instance in "${instances[@]}"; do
    label="${instance#"${DATA_DIR}"/}"
    echo "running ${label}"
    run_cpp "$FAITHFUL_BIN" "$instance" faithful "$label"
    run_cpp "$OPTIMIZED_BIN" "$instance" optimized "$label"
    run_ocaml "$instance" "$label"
done

echo "completed ${#instances[@]} instances"
echo "report: ${OUTPUT_FILE}"
