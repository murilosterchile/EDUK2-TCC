#!/usr/bin/env bash
# Build and run faithful against PYAsUKP/OCaml or UKP5 for every .ukp file.
# The input format is shared by all three solvers.

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATA_DIR="${DATA_DIR:-${PROJECT_DIR}/data}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
BENCHMARK_BUILD_DIR="${BENCHMARK_BUILD_DIR:-${BUILD_DIR}}"
OCAML_DIR="${OCAML_DIR:-/home/aprix/Downloads/pyasukp_mail/pyasukp}"
UKP5_DIR="${UKP5_DIR:-${PROJECT_DIR}/../benchmarks/pyasukp_paper/external/masters/codes/cpp}"
COMPARE_WITH="${COMPARE_WITH:-ocaml}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
RUNS="${RUNS:-5}"
RUN_ID="$(date +%Y%m%d_%H%M%S)"
OUTPUT_FILE="${OUTPUT_FILE:-${PROJECT_DIR}/results/run_${RUN_ID}.txt}"
BUILD=1

# Final comparison summary.
FAITHFUL_FASTER_COUNT=0
COMPARED_TIME_COUNT=0
DIFFERENT_OPTIMUM_COUNT=0
declare -a DIFFERENT_OPTIMUM_INSTANCES=()

# Results from the most recently executed solver.
LAST_CPP_STATUS=""
LAST_CPP_INTERNAL=""
LAST_CPP_PROFIT=""
LAST_REFERENCE_STATUS=""
LAST_REFERENCE_INTERNAL=""
LAST_REFERENCE_PROFIT=""

usage() {
    cat <<'EOF'
Usage: scripts/run_all_solvers.sh [options]

Options:
  --data-dir DIR       Directory containing .ukp files (default: data/)
  --build-dir DIR      CMake build directory (default: build/)
  --benchmark-build-dir DIR
                         CMake build directory for faithful (default: build/)
  --compare-with SOLVER Compare faithful with ocaml or ukp5 (default: ocaml)
  --ocaml-dir DIR      PYAsUKP directory (default: ~/Downloads/pyasukp_mail/pyasukp)
  --ukp5-dir DIR       UKP5 C++ source directory (default: repository benchmark copy)
  --output-file FILE   Single text report (default: results/run_<timestamp>.txt)
  --output-dir DIR     Deprecated alias; writes DIR/results.txt
  --build-type TYPE    CMake build type (default: Release)
  --runs N             Executions per instance and solver (default: 5)
  --no-build           Reuse existing solver executables
  -h, --help           Show this help

Environment variables DATA_DIR, BUILD_DIR, BENCHMARK_BUILD_DIR, COMPARE_WITH,
OCAML_DIR, UKP5_DIR, UKP5_BIN, OUTPUT_FILE, BUILD_TYPE and RUNS have the
equivalent effect. The script writes one tab-separated text report with
averages over the requested executions.
EOF
}

while (($#)); do
    case "$1" in
        --data-dir) DATA_DIR="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --benchmark-build-dir) BENCHMARK_BUILD_DIR="$2"; shift 2 ;;
        --compare-with) COMPARE_WITH="$2"; shift 2 ;;
        --ocaml-dir) OCAML_DIR="$2"; shift 2 ;;
        --ukp5-dir) UKP5_DIR="$2"; shift 2 ;;
        --output-file) OUTPUT_FILE="$2"; shift 2 ;;
        --output-dir) OUTPUT_FILE="${2%/}/results.txt"; shift 2 ;;
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --runs) RUNS="$2"; shift 2 ;;
        --no-build) BUILD=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$RUNS" =~ ^[1-9][0-9]*$ ]] || {
    echo "--runs must be a positive integer" >&2
    exit 2
}

case "$COMPARE_WITH" in
    ocaml) REFERENCE_LABEL="OCaml" ;;
    ukp5) REFERENCE_LABEL="UKP5" ;;
    *)
        echo "--compare-with must be either ocaml or ukp5" >&2
        exit 2
        ;;
esac

require_dir() {
    [[ -d "$1" ]] || {
        echo "directory not found: $1" >&2
        exit 2
    }
}

require_dir "$DATA_DIR"

if [[ "$COMPARE_WITH" == "ocaml" ]]; then
    require_dir "$OCAML_DIR"
else
    require_dir "$UKP5_DIR"
fi

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
OCAML_RUNTIME_DIR="$OCAML_DIR"

if ((BUILD)); then
    cmake -S "$PROJECT_DIR" -B "$BENCHMARK_BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$BENCHMARK_BUILD_DIR" --parallel

    if [[ "$COMPARE_WITH" == "ocaml" ]]; then
        # Build PYAsUKP from its .ml sources as a native executable so its timing
        # is comparable to the C++ executables. Build in a clean temporary copy,
        # avoiding stale artifacts and leaving the checked-out OCaml tree intact.
        OCAML_RUNTIME_DIR="${TEMP_DIR}/pyasukp"
        cp -a "$OCAML_DIR" "$OCAML_RUNTIME_DIR"
        make -C "$OCAML_RUNTIME_DIR" allclean optt
    else
        # This is the Makefile target whose main is mains/run_ukp5.cpp.
        make -C "$UKP5_DIR" bin/run_ukp5.out
    fi
fi

FAITHFUL_BIN="${BENCHMARK_BUILD_DIR}/ukp_solve_benchmark"
OCAML_BIN="${OCAML_BIN:-${OCAML_RUNTIME_DIR}/pyasukpt}"
UKP5_BIN="${UKP5_BIN:-${UKP5_DIR}/bin/run_ukp5.out}"

if [[ ! -x "$FAITHFUL_BIN" ]]; then
    echo "C++ faithful benchmark executable not found: ${FAITHFUL_BIN}" >&2
    exit 2
fi

if [[ "$COMPARE_WITH" == "ocaml" ]]; then
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
elif [[ ! -x "$UKP5_BIN" ]]; then
    echo "UKP5 executable not found: ${UKP5_BIN}" >&2
    exit 2
fi

mkdir -p "$(dirname "$OUTPUT_FILE")"

printf '# EDUK2 solver execution report\n# generated_at: %s\n' \
    "$(date --iso-8601=seconds)" > "$OUTPUT_FILE"
printf '# primary_time: internal_seconds (average over successful runs)\n' \
    >> "$OUTPUT_FILE"

printf 'instance\talgorithm\tstatus\truns\tinternal_seconds\tprofit\tweight\toptimal\tverified\tstates_scanned\tpoints_generated\tbb_nodes\tdetails\n' \
    >> "$OUTPUT_FILE"

elapsed_seconds() {
    LC_ALL=C awk -v begin="$1" -v end="$2" \
        'BEGIN { printf "%.6f", (end - begin) / 1000000000 }'
}

value_from_cpp_output() {
    local output="$1" key="$2"
    awk -v key="$key" '$1 == key { print $2; exit }' <<< "$output"
}

average_values() {
    LC_ALL=C awk '{
        gsub(",", ".", $1)
        sum += $1
        count += 1
    }
    END {
        if (count > 0)
            printf "%.6f", sum / count
    }'
}

average_array() {
    (($# > 0)) || return
    printf '%s\n' "$@" | average_values
}

append_row() {
    local instance="$1"
    local algorithm="$2"
    local status="$3"
    local runs="$4"
    local elapsed="$5"
    local profit="$6"
    local weight="$7"
    local optimal="$8"
    local verified="$9"
    local states="${10}"
    local points="${11}"
    local bb_nodes="${12}"
    local details="${13}"

    details="${details//$'\t'/ }"
    details="${details//$'\n'/ }"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$instance" \
        "$algorithm" \
        "$status" \
        "$runs" \
        "$elapsed" \
        "$profit" \
        "$weight" \
        "$optimal" \
        "$verified" \
        "$states" \
        "$points" \
        "$bb_nodes" \
        "$details" \
        >> "$OUTPUT_FILE"
}

run_cpp() {
    local binary="$1"
    local instance="$2"
    local kind="$3"
    local label="$4"

    local start end output exit_code status="ok" stop_reason=""
    local -a elapsed_values=()
    local -a internal_values=()
    local -a profit_values=()
    local -a weight_values=()
    local -a optimal_values=()
    local -a verified_values=()
    local -a states_values=()
    local -a points_values=()
    local -a bb_nodes_values=()

    local run

    for ((run = 1; run <= RUNS; ++run)); do
        start="$(date +%s%N)"

        output="$("$binary" "$kind" "$instance" 2>&1)"
        exit_code=$?

        end="$(date +%s%N)"

        elapsed_values+=(
            "$(elapsed_seconds "$start" "$end")"
        )

        if ((exit_code != 0)); then
            status="failed"
            continue
        fi

        internal_values+=(
            "$(LC_ALL=C awk -v us="$(value_from_cpp_output "$output" time_us)" \
                'BEGIN { printf "%.9f", us / 1000000 }')"
        )

        profit_values+=(
            "$(value_from_cpp_output "$output" profit)"
        )

        weight_values+=(
            "$(value_from_cpp_output "$output" weight)"
        )

        optimal_values+=(
            "$(value_from_cpp_output "$output" optimal)"
        )

        verified_values+=(
            "$(value_from_cpp_output "$output" verified)"
        )

        states_values+=(
            "$(value_from_cpp_output "$output" states_scanned)"
        )

        points_values+=(
            "$(value_from_cpp_output "$output" points_generated)"
        )

        bb_nodes_values+=(
            "$(value_from_cpp_output "$output" bb_nodes)"
        )

        stop_reason="$(
            value_from_cpp_output "$output" stop_reason
        )"
    done

    local avg_elapsed avg_internal avg_profit avg_weight avg_optimal avg_verified
    local avg_states avg_points avg_bb_nodes

    avg_elapsed="$(average_array "${elapsed_values[@]}")"
    avg_internal="$(average_array "${internal_values[@]}")"
    avg_profit="$(average_array "${profit_values[@]}")"
    avg_weight="$(average_array "${weight_values[@]}")"
    avg_optimal="$(average_array "${optimal_values[@]}")"
    avg_verified="$(average_array "${verified_values[@]}")"
    avg_states="$(average_array "${states_values[@]}")"
    avg_points="$(average_array "${points_values[@]}")"
    avg_bb_nodes="$(average_array "${bb_nodes_values[@]}")"

    append_row \
        "$label" \
        "$kind" \
        "$status" \
        "$RUNS" \
        "$avg_internal" \
        "$avg_profit" \
        "$avg_weight" \
        "$avg_optimal" \
        "$avg_verified" \
        "$avg_states" \
        "$avg_points" \
        "$avg_bb_nodes" \
        "elapsed_seconds=${avg_elapsed} last_stop_reason=${stop_reason}"

    LAST_CPP_STATUS="$status"
    LAST_CPP_INTERNAL="$avg_internal"
    LAST_CPP_PROFIT="$avg_profit"
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

            {
                for (i = 1; i <= NF; ++i)
                    values[++count] = $i
            }

            END {
                n = values[1]
                capacity = values[2]

                if (n == "" || capacity == "")
                    exit 2

                print "n: " n
                print "c: " capacity
                print "begin data"

                for (i = 0; i < n; ++i)
                    print values[4 + 2 * i] " " values[3 + 2 * i]

                print "end data"
            }
        ' "$instance" > "$converted" || return 1

        source="$converted"
    fi

    local start end output exit_code status="ok" summary=""
    local -a elapsed_values=()
    local -a internal_values=()
    local -a profit_values=()
    local -a optimal_values=()
    local -a points_values=()
    local -a bb_nodes_values=()

    local run

    # PYAsUKP prefixes the batch fields with a variable-length instance
    # classification. The stable fields are counted from the end: exact flag,
    # profit, internal time, and B&B nodes; critical points precede them.
    for ((run = 1; run <= RUNS; ++run)); do
        start="$(date +%s%N)"

        output="$("$OCAML_BIN" -src "$source" -batch 2>&1)"
        exit_code=$?

        end="$(date +%s%N)"

        elapsed_values+=(
            "$(elapsed_seconds "$start" "$end")"
        )

        if ((exit_code != 0)); then
            status="failed"
            continue
        fi

        summary="$(
            awk 'NF { print; exit }' <<< "$output"
        )"

        internal_values+=(
            "$(awk 'NF { print $(NF - 1); exit }' <<< "$output")"
        )

        profit_values+=(
            "$(awk 'NF { print $(NF - 2); exit }' <<< "$output")"
        )

        optimal_values+=(
            "$(awk 'NF { print $(NF - 3); exit }' <<< "$output")"
        )

        points_values+=(
            "$(awk 'NF { print $(NF - 4); exit }' <<< "$output")"
        )

        bb_nodes_values+=(
            "$(awk 'NF { print $NF; exit }' <<< "$output")"
        )
    done

    local avg_elapsed avg_internal avg_profit avg_optimal avg_points avg_bb_nodes

    avg_elapsed="$(average_array "${elapsed_values[@]}")"
    avg_internal="$(average_array "${internal_values[@]}")"
    avg_profit="$(average_array "${profit_values[@]}")"
    avg_optimal="$(average_array "${optimal_values[@]}")"
    avg_points="$(average_array "${points_values[@]}")"
    avg_bb_nodes="$(average_array "${bb_nodes_values[@]}")"

    append_row \
        "$label" \
        "ocaml_pyasukpt" \
        "$status" \
        "$RUNS" \
        "$avg_internal" \
        "$avg_profit" \
        "" \
        "$avg_optimal" \
        "" \
        "" \
        "$avg_points" \
        "$avg_bb_nodes" \
        "elapsed_seconds=${avg_elapsed} ${summary}"

    LAST_REFERENCE_STATUS="$status"
    LAST_REFERENCE_INTERNAL="$avg_internal"
    LAST_REFERENCE_PROFIT="$avg_profit"
}

run_ukp5() {
    local instance="$1" label="$2"
    local source="$instance"
    local converted

    # run_ukp5.out accepts n:/c:/begin-data input. Convert the compact legacy
    # format, whose item order is profit then weight, before invoking it.
    if ! grep -qE '^[[:space:]]*(n|m):|^[[:space:]]*begin data' "$instance"; then
        converted="$(mktemp "${TEMP_DIR}/ukp5-input.XXXXXX.ukp")"

        awk '
            /^[[:space:]]*#/ { next }

            {
                for (i = 1; i <= NF; ++i)
                    values[++count] = $i
            }

            END {
                n = values[1]
                capacity = values[2]

                if (n == "" || capacity == "")
                    exit 2

                print "n: " n
                print "c: " capacity
                print "begin data"

                for (i = 0; i < n; ++i)
                    print values[4 + 2 * i] " " values[3 + 2 * i]

                print "end data"
            }
        ' "$instance" > "$converted" || return 1

        source="$converted"
    fi

    local start end output exit_code status="ok"
    local -a elapsed_values=()
    local -a internal_values=()
    local -a profit_values=()
    local -a weight_values=()
    local run

    for ((run = 1; run <= RUNS; ++run)); do
        start="$(date +%s%N)"
        output="$("$UKP5_BIN" "$source" 2>&1)"
        exit_code=$?
        end="$(date +%s%N)"

        elapsed_values+=("$(elapsed_seconds "$start" "$end")")

        if ((exit_code != 0)); then
            status="failed"
            continue
        fi

        profit_values+=("$(value_from_cpp_output "$output" 'opt:')")
        weight_values+=("$(value_from_cpp_output "$output" 'y_opt:')")
        internal_values+=("$(value_from_cpp_output "$output" 'Seconds:')")
    done

    local avg_elapsed avg_internal avg_profit avg_weight
    avg_elapsed="$(average_array "${elapsed_values[@]}")"
    avg_internal="$(average_array "${internal_values[@]}")"
    avg_profit="$(average_array "${profit_values[@]}")"
    avg_weight="$(average_array "${weight_values[@]}")"

    append_row \
        "$label" \
        "ukp5" \
        "$status" \
        "$RUNS" \
        "$avg_internal" \
        "$avg_profit" \
        "$avg_weight" \
        "1" \
        "" \
        "" \
        "" \
        "" \
        "elapsed_seconds=${avg_elapsed}"

    LAST_REFERENCE_STATUS="$status"
    LAST_REFERENCE_INTERNAL="$avg_internal"
    LAST_REFERENCE_PROFIT="$avg_profit"
}

mapfile -d '' -t instances < <(
    find "$DATA_DIR" \
        -type f \
        -name '*.ukp' \
        -print0 |
    sort -z
)

if ((${#instances[@]} == 0)); then
    echo "no .ukp instances in ${DATA_DIR}" >&2
    exit 2
fi

for instance in "${instances[@]}"; do
    label="${instance#"${DATA_DIR}"/}"

    echo "running ${label}"

    # Reset per-instance results before running both solvers.
    LAST_CPP_STATUS=""
    LAST_CPP_INTERNAL=""
    LAST_CPP_PROFIT=""
    LAST_REFERENCE_STATUS=""
    LAST_REFERENCE_INTERNAL=""
    LAST_REFERENCE_PROFIT=""

    run_cpp "$FAITHFUL_BIN" "$instance" faithful "$label"
    if [[ "$COMPARE_WITH" == "ocaml" ]]; then
        run_ocaml "$instance" "$label"
    else
        run_ukp5 "$instance" "$label"
    fi

    # Compare internal execution time once per instance, using each solver's
    # average over RUNS executions.
    if [[ "$LAST_CPP_STATUS" == "ok" && "$LAST_REFERENCE_STATUS" == "ok" &&
          -n "$LAST_CPP_INTERNAL" && -n "$LAST_REFERENCE_INTERNAL" ]]; then
        ((COMPARED_TIME_COUNT += 1))

        if LC_ALL=C awk -v faithful="$LAST_CPP_INTERNAL" -v reference="$LAST_REFERENCE_INTERNAL" \
            'BEGIN { exit !(faithful < reference) }'; then
            ((FAITHFUL_FASTER_COUNT += 1))
        fi
    fi

    # Compare the optimum once per instance, regardless of RUNS.
    if [[ "$LAST_CPP_STATUS" == "ok" && "$LAST_REFERENCE_STATUS" == "ok" &&
          -n "$LAST_CPP_PROFIT" && -n "$LAST_REFERENCE_PROFIT" ]]; then
        if ! LC_ALL=C awk -v faithful="$LAST_CPP_PROFIT" -v reference="$LAST_REFERENCE_PROFIT" \
            'BEGIN { exit !(faithful == reference) }'; then
            ((DIFFERENT_OPTIMUM_COUNT += 1))
            DIFFERENT_OPTIMUM_INSTANCES+=(
                "${label} (faithful=${LAST_CPP_PROFIT}, ${COMPARE_WITH}=${LAST_REFERENCE_PROFIT})"
            )
        fi
    fi
done

echo
echo "completed ${#instances[@]} instances"
echo "report: ${OUTPUT_FILE}"
echo
echo "=== Final comparison ==="
echo "faithful faster than ${REFERENCE_LABEL} by average internal time: ${FAITHFUL_FASTER_COUNT}/${COMPARED_TIME_COUNT} instances"
echo "instances with different optimum: ${DIFFERENT_OPTIMUM_COUNT}"

if ((DIFFERENT_OPTIMUM_COUNT > 0)); then
    echo "instances with different optimum:"
    for mismatch in "${DIFFERENT_OPTIMUM_INSTANCES[@]}"; do
        echo "  - ${mismatch}"
    done
else
    echo "all compared instances returned the same optimum"
fi
