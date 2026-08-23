#!/usr/bin/env bash
# Run every .ukp instance exactly once with faithful, optionally also PYAsUKP.
#
# Execute from ~/TCC/EDUK2-TCC (the repository root):
#
#   # All .ukp files under eduk2_cpp/data, faithful only.
#   ./eduk2_cpp/scripts/run_faithful_once.sh
#
#   # A different directory; search is recursive and files are sorted by path.
#   ./eduk2_cpp/scripts/run_faithful_once.sh \
#     --data-dir benchmarks/pyasukp_paper/instances/ss
#
#   # Faithful and an already compiled native OCaml PYAsUKP executable.
#   ./eduk2_cpp/scripts/run_faithful_once.sh \
#     --data-dir benchmarks/pyasukp_paper/instances/ss \
#     --with-ocaml \
#     --ocaml-bin benchmarks/pyasukp_paper/external/pyasukp_mail/pyasukp/pyasukpt
#
# The script never builds either solver and never repeats an execution. The
# default faithful executable is eduk2_cpp/build/ukp_solve_benchmark, invoked
# exactly as: build/ukp_solve_benchmark faithful INSTANCE.
#
# Output has no header and uses tabs:
#   faithful only: INSTANCE  FAITHFUL_SECONDS  FAITHFUL_OPTIMAL_VALUE
#   with OCaml:    INSTANCE  FAITHFUL_SECONDS  FAITHFUL_OPTIMAL_VALUE
#                              OCAML_SECONDS     OCAML_OPTIMAL_VALUE
#
# When both solvers are used, a summary lists every instance for which their
# optimal values differ and reports the total number of differences.
#
# Times are external elapsed wall-clock seconds measured around each process.
# On an execution or parsing error, the script writes a diagnostic to stderr
# and exits nonzero instead of printing an invented value.

set -u -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DATA_DIR="${DATA_DIR:-${PROJECT_DIR}/data}"
FAITHFUL_BIN="${FAITHFUL_BIN:-${PROJECT_DIR}/build/ukp_solve_benchmark}"
OCAML_BIN="${OCAML_BIN:-}"
WITH_OCAML=0

usage() {
    sed -n '1,35p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while (($#)); do
    case "$1" in
        --data-dir)
            [[ $# -ge 2 ]] || {
                echo "--data-dir needs a directory" >&2
                exit 2
            }
            DATA_DIR="$2"
            shift 2
            ;;
        --faithful-bin)
            [[ $# -ge 2 ]] || {
                echo "--faithful-bin needs a path" >&2
                exit 2
            }
            FAITHFUL_BIN="$2"
            shift 2
            ;;
        --with-ocaml)
            WITH_OCAML=1
            shift
            ;;
        --ocaml-bin)
            [[ $# -ge 2 ]] || {
                echo "--ocaml-bin needs a path" >&2
                exit 2
            }
            OCAML_BIN="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

[[ -d "$DATA_DIR" ]] || {
    echo "data directory not found: $DATA_DIR" >&2
    exit 2
}

[[ -x "$FAITHFUL_BIN" ]] || {
    echo "faithful executable not found: $FAITHFUL_BIN" >&2
    exit 2
}

if ((WITH_OCAML)); then
    [[ -n "$OCAML_BIN" ]] || {
        echo "--with-ocaml requires --ocaml-bin PATH (or OCAML_BIN)" >&2
        exit 2
    }

    [[ -x "$OCAML_BIN" ]] || {
        echo "OCaml executable not found: $OCAML_BIN" >&2
        exit 2
    }
fi

elapsed_seconds() {
    awk -v begin="$1" -v end="$2" \
        'BEGIN { printf "%.9f", (end - begin) / 1000000000 }'
}

faithful_profit() {
    awk '
        $1 == "profit" {
            print $2
            found = 1
            exit
        }
        END {
            if (!found)
                exit 1
        }
    ' <<< "$1"
}

ocaml_profit() {
    awk '
        /^#The optimal value for the given capacity$/ {
            if (getline && $1 ~ /^-?[0-9]+$/) {
                print $1
                found = 1
                exit
            }
        }
        END {
            if (!found)
                exit 1
        }
    ' <<< "$1"
}

# Results from the latest run_solver call.
RUN_SECONDS=""
RUN_PROFIT=""

run_solver() {
    local solver="$1"
    local instance="$2"
    local start end output
    local -a command

    if [[ "$solver" == "faithful" ]]; then
        command=("$FAITHFUL_BIN" faithful "$instance")
    else
        command=("$OCAML_BIN" -src "$instance")
    fi

    start="$(date +%s%N)"

    if ! output="$("${command[@]}" 2>&1)"; then
        echo "$solver failed for $instance" >&2
        printf '%s\n' "$output" >&2
        return 1
    fi

    end="$(date +%s%N)"
    RUN_SECONDS="$(elapsed_seconds "$start" "$end")"

    if [[ "$solver" == "faithful" ]]; then
        RUN_PROFIT="$(faithful_profit "$output")" || {
            echo "could not parse faithful profit for $instance" >&2
            return 1
        }
    else
        RUN_PROFIT="$(ocaml_profit "$output")" || {
            echo "could not parse OCaml profit for $instance" >&2
            return 1
        }
    fi
}

found=0
different_instances=()

while IFS= read -r -d '' instance; do
    found=1
    relative_instance="${instance#"${DATA_DIR%/}/"}"

    run_solver faithful "$instance" || exit 1
    faithful_seconds="$RUN_SECONDS"
    faithful_value="$RUN_PROFIT"

    printf '%s\t%s\t%s' \
        "$relative_instance" \
        "$faithful_seconds" \
        "$faithful_value"

    if ((WITH_OCAML)); then
        run_solver ocaml "$instance" || exit 1
        ocaml_seconds="$RUN_SECONDS"
        ocaml_value="$RUN_PROFIT"

        printf '\t%s\t%s' "$ocaml_seconds" "$ocaml_value"

        if [[ "$faithful_value" != "$ocaml_value" ]]; then
            different_instances+=("$relative_instance")
        fi
    fi

    printf '\n'
done < <(find "$DATA_DIR" -type f -name '*.ukp' -print0 | sort -z)

((found)) || {
    echo "no .ukp files found under: $DATA_DIR" >&2
    exit 1
}

if ((WITH_OCAML)); then
    printf '\n'
    printf 'Instances with different optimal values:\n'

    if ((${#different_instances[@]} == 0)); then
        printf 'None\n'
    else
        printf '  %s\n' "${different_instances[@]}"
    fi

    printf 'Total instances with different values: %d\n' \
        "${#different_instances[@]}"
fi