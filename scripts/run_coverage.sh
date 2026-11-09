#!/usr/bin/env bash
# Run unit tests with coverage instrumentation and produce per-package
# line-coverage statistics.
#
# Usage:
#   CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" \
#     scripts/run_coverage.sh [--fail-under N] [--skip-build] [--fresh]
#
# CMAKE_ARGS is passed straight to the configure step; CI sets it to the
# vcpkg toolchain (see .github/workflows/ci.yml). Without it the configure
# falls back to pre-generated protos and a half-successful system
# FindProtobuf can leave a protobuf::libprotobuf import target pointing at
# NOTFOUND, which fails at generate time.
#
# --fresh wipes the coverage build dir first. Needed after sources move or
# get deleted: incremental builds leave orphaned .gcda files for removed
# objects behind, and gcovr happily merges that stale data into the report.
#
# Requires: cmake + ninja, a GNU/Clang toolchain and gcov (from gcc).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# COVERAGE_BUILD_DIR isolates the gate from other actors configuring into the
# default build-cov directory concurrently (a second agent or editor task on
# the same checkout wipes test binaries and .gcda mid-run otherwise).
BUILD_DIR="${COVERAGE_BUILD_DIR:-${ROOT_DIR}/build-cov}"
FAIL_UNDER=100
SKIP_BUILD=0
FRESH=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fail-under)
      FAIL_UNDER="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --fresh)
      FRESH=1
      shift
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ${FRESH} -eq 1 && ${SKIP_BUILD} -eq 0 ]]; then
  rm -rf "${BUILD_DIR}"
fi

if [[ ${SKIP_BUILD} -eq 0 ]]; then
  # Same cache variables as the "coverage" preset (see CMakePresets.json), but
  # passed explicitly so COVERAGE_BUILD_DIR can relocate the binary dir; the
  # preset hard-codes ${sourceDir}/build-cov.
  # shellcheck disable=SC2086
  cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DCHIRP_ENABLE_COVERAGE=ON \
    ${CMAKE_ARGS:-}
  # Build everything: a fixed target list here would silently skip targets
  # whose source list changed (or new ones), reporting stale coverage.
  cmake --build "${BUILD_DIR}"
fi

ctest --test-dir "${BUILD_DIR}" --output-on-failure

# ---------------------------------------------------------------------------
# Coverage aggregation.
#
# gcov is run directly over every .gcda and the JSON intermediates are folded
# in Python: one entry per (file, line) with the maximum hit count (several
# compilation contexts -- library objects, test objects and gcov function
# thunks -- can emit separate counters for the same physical line). Blank,
# brace-only, comment and signature-continuation lines carry no executable
# semantics and are dropped from the statistics, as are the lines listed in
# KNOWN_UNCOVERABLE (defensive branches that cannot be reached through the
# public APIs).
# ---------------------------------------------------------------------------
mkdir -p coverage_html
# Keep the intermediates out of /tmp: temp cleaners running on the host have
# been observed deleting files under /tmp mid-run, which silently drops
# packages from the aggregated report. Under the build dir we own the data.
WORK_DIR="$(mktemp -d "${BUILD_DIR}/.gcov-work.XXXXXX")"
trap 'rm -rf "${WORK_DIR}"' EXIT

# gcov writes its JSON intermediates into the current directory and names
# them after the source file, so every gcda is processed in its own
# subdirectory to prevent clobbering between compilation contexts.
i=0
gcov_failed=0
while IFS= read -r -d '' gcda; do
  mkdir -p "${WORK_DIR}/${i}"
  if (cd "${WORK_DIR}/${i}" && gcov -j -b -c "${gcda}" >/dev/null 2>&1); then
    :
  else
    gcov_failed=$((gcov_failed + 1))
    echo "WARNING: gcov failed on ${gcda} (its lines count as uncovered)" >&2
  fi
  i=$((i + 1))
done < <(find "${BUILD_DIR}" -name '*.gcda' -print0)
if [[ ${gcov_failed} -gt 0 ]]; then
  echo "WARNING: ${gcov_failed} gcov invocation(s) failed; report may be incomplete" >&2
fi

# Fold the gcov JSON intermediates into the per-package report and gate
# on the line threshold (pin tables in scripts/coverage_pins.py).
python3 "${ROOT_DIR}/scripts/coverage_report.py" \
  "${ROOT_DIR}" "${WORK_DIR}" "${FAIL_UNDER}"
