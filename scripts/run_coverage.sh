#!/usr/bin/env bash
# Run unit tests with coverage instrumentation and produce per-package
# line-coverage statistics with gcovr.
#
# Usage:
#   scripts/run_coverage.sh [--fail-under N] [--skip-build]
#
# Requires: cmake + ninja, a GNU/Clang toolchain and gcovr.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-cov"
FAIL_UNDER=98
SKIP_BUILD=0

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
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ${SKIP_BUILD} -eq 0 ]]; then
  cmake --preset coverage
  cmake --build --preset coverage --target \
    common_tests network_tests chat_validation_tests gateway_session_registry_tests \
    2>/dev/null || cmake --build --preset coverage
fi

ctest --preset coverage --output-on-failure

GCOVR_BIN="${GCOVR_BIN:-gcovr}"
if ! command -v "${GCOVR_BIN}" >/dev/null 2>&1; then
  for candidate in gcovr python3 -m gcovr; do
    if command -v python3 >/dev/null 2>&1 && python3 -m gcovr --version >/dev/null 2>&1; then
      GCOVR_BIN="python3 -m gcovr"
      break
    fi
  done
fi

mkdir -p coverage_html

# Aggregate coverage over the libraries and services compiled into the unit
# tests. Generated protobuf sources and application entry points are excluded
# because they are not exercised by unit tests.
"${GCOVR_BIN}" \
  --root "${ROOT_DIR}" \
  --filter "${ROOT_DIR}/libs/" \
  --filter "${ROOT_DIR}/services/" \
  --filter "${ROOT_DIR}/sdks/core/src/" \
  --exclude ".*\.pb\.cc" \
  --exclude ".*/proto/" \
  --exclude ".*main.*\.cc" \
  --exclude ".*examples/.*" \
  --object-directory "${BUILD_DIR}" \
  "${BUILD_DIR}" \
  --txt "coverage.txt" \
  --txt-metric line \
  --json "coverage.json" \
  --json-summary "coverage-summary.json" \
  --csv "coverage.csv" \
  --html-details "coverage_html/index.html" \
  --print-summary

# Per-package (directory) breakdown, failing when any package is below the
# threshold.
echo
echo "Per-package line coverage (threshold: ${FAIL_UNDER}%)"
"${GCOVR_BIN}" \
  --root "${ROOT_DIR}" \
  --filter "${ROOT_DIR}/libs/" \
  --filter "${ROOT_DIR}/services/" \
  --filter "${ROOT_DIR}/sdks/core/src/" \
  --exclude ".*\.pb\.cc" \
  --exclude ".*/proto/" \
  --exclude ".*main.*\.cc" \
  --exclude ".*examples/.*" \
  --object-directory "${BUILD_DIR}" \
  "${BUILD_DIR}" \
  --csv "coverage-packages.csv" >/dev/null

python3 - "${FAIL_UNDER}" <<'PYEOF'
import csv
import sys
from collections import defaultdict

fail_under = float(sys.argv[1])
rows = defaultdict(lambda: [0, 0])  # dir -> [covered, total]

with open("coverage-packages.csv", newline="") as fh:
    for row in csv.DictReader(fh):
        covered = int(row["line_covered"])
        total = int(row["line_total"])
        path = row["filename"]
        parts = path.split("/")
        # Group by the deepest directory that contains the file.
        package = "/".join(parts[:-1]) if len(parts) > 1 else "."
        rows[package][0] += covered
        rows[package][1] += total

failed = False
for package in sorted(rows):
    covered, total = rows[package]
    pct = 100.0 * covered / total if total else 100.0
    status = "OK" if pct >= fail_under else "LOW"
    if pct < fail_under:
        failed = True
    print(f"{status:3}  {pct:6.2f}%  {covered:5}/{total:<5}  {package}")

if failed:
    print(f"\nCoverage threshold {fail_under}% not met for some packages.", file=sys.stderr)
    sys.exit(1)
print(f"\nAll packages meet the {fail_under}% line coverage threshold.")
PYEOF
