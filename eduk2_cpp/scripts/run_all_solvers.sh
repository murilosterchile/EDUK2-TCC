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
OUTPUT_FILE="${OUTPUT_FILE:-${PROJECT_DIR}/results/run_${RUN_ID}.txt}"
BUILD=1

usage() {
    cat <<'EOF'
Usage: scripts/run_all_solvers.sh [options]

Options:
  --data-dir DIR       Directory containing .ukp files (default: data/)
  --build-dir DIR      CMake build directory (default: build/)
  --ocaml-dir DIR      PYAsUKP directory (default: ~/Downloads/pyasukp_mail/pyasukp)
  --output-file FILE   Single text report (default: results/run_<timestamp>.txt)
  --output-dir DIR     Deprecated alias; writes DIR/results.txt
  --build-type TYPE    CMake build type (default: Release)
  --no-build           Reuse existing C++ and OCaml executables
  -h, --help           Show this help

Environment variables DATA_DIR, BUILD_DIR, OCAML_DIR, OUTPUT_FILE and BUILD_TYPE
have the equivalent effect. The script writes one tab-separated text report.
EOF
}

while (($#)); do
    case "$1" in
        --data-dir) DATA_DIR="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --ocaml-dir) OCAML_DIR="$2"; shift 2 ;;
        --output-file) OUTPUT_FILE="$2"; shift 2 ;;
        --output-dir) OUTPUT_FILE="${2%/}/results.txt"; shift 2 ;;
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

mkdir -p "$(dirname "$OUTPUT_FILE")"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

printf '# EDUK2 solver execution report\n# generated_at: %s\n' "$(date --iso-8601=seconds)" > "$OUTPUT_FILE"
printf 'instance\talgorithm\tstatus\telapsed_seconds\tprofit\tweight\toptimal\tverified\tstates_scanned\tpoints_generated\tbb_nodes\tdetails\n' >> "$OUTPUT_FILE"

elapsed_seconds() {
    awk -v begin="$1" -v end="$2" 'BEGIN { printf "%.6f", (end - begin) / 1000000000 }'
}

value_from_cpp_output() {
    local output="$1" key="$2"
    awk -v key="$key" '$1 == key { print $2; exit }' <<< "$output"
}

append_row() {
    local instance="$1" algorithm="$2" status="$3" elapsed="$4" profit="$5" weight="$6"
    local optimal="$7" verified="$8" states="$9" points="${10}" bb_nodes="${11}" details="${12}"
    details="${details//$'\t'/ }"
    details="${details//$'\n'/ }"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$instance" "$algorithm" "$status" "$elapsed" "$profit" "$weight" "$optimal" "$verified" \
        "$states" "$points" "$bb_nodes" "$details" >> "$OUTPUT_FILE"
}

run_cpp() {
    local instance="$1" kind="$2" label="$3"
    local start end output exit_code status
    start="$(date +%s%N)"
    output="$("$FAITHFUL_BIN" "$kind" "$instance" 2>&1)"
    exit_code=$?
    end="$(date +%s%N)"
    status="ok"
    ((exit_code == 0)) || status="failed"
    append_row "$label" "$kind" "$status" "$(elapsed_seconds "$start" "$end")" \
        "$(value_from_cpp_output "$output" profit)" "$(value_from_cpp_output "$output" weight)" \
        "$(value_from_cpp_output "$output" optimal)" "$(value_from_cpp_output "$output" verified)" \
        "$(value_from_cpp_output "$output" states_scanned)" "$(value_from_cpp_output "$output" points_generated)" \
        "$(value_from_cpp_output "$output" bb_nodes)" \
        "$(value_from_cpp_output "$output" stop_reason)"
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
    local start end output exit_code status summary
    start="$(date +%s%N)"
    output="$("$OCAML_BIN" -src "$source" -batch 2>&1)"
    exit_code=$?
    end="$(date +%s%N)"
    status="ok"
    ((exit_code == 0)) || status="failed"
    summary="$(awk 'NF { print; exit }' <<< "$output")"
    # PYAsUKP prefixes the batch fields with a variable-length instance
    # classification. The stable fields are counted from the end: exact flag,
    # profit, internal time, and B&B nodes; critical points precede them.
    append_row "$label" "ocaml_pyasukpbct" "$status" "$(elapsed_seconds "$start" "$end")" \
        "$(awk 'NF { print $(NF - 2); exit }' <<< "$output")" "" \
        "$(awk 'NF { print $(NF - 3); exit }' <<< "$output")" "" "" \
        "$(awk 'NF { print $(NF - 4); exit }' <<< "$output")" \
        "$(awk 'NF { print $NF; exit }' <<< "$output")" "$summary"
}

mapfile -d '' -t instances < <(find "$DATA_DIR" -type f -name '*.ukp' -print0 | sort -z)
if ((${#instances[@]} == 0)); then
    echo "no .ukp instances in ${DATA_DIR}" >&2
    exit 2
fi

for instance in "${instances[@]}"; do
    label="${instance#"${DATA_DIR}"/}"
    echo "running ${label}"
    run_cpp "$instance" faithful "$label"
    run_cpp "$instance" optimized "$label"
    run_ocaml "$instance" "$label"
done

echo "completed ${#instances[@]} instances"
echo "report: ${OUTPUT_FILE}"
