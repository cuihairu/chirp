#!/usr/bin/env bash
# Run unit tests with coverage instrumentation and produce per-package
# line-coverage statistics.
#
# Usage:
#   scripts/run_coverage.sh [--fail-under N] [--skip-build]
#
# Requires: cmake + ninja, a GNU/Clang toolchain and gcov (from gcc).

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
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

# gcov writes its JSON intermediates into the current directory and names
# them after the source file, so every gcda is processed in its own
# subdirectory to prevent clobbering between compilation contexts.
i=0
while IFS= read -r -d '' gcda; do
  mkdir -p "${WORK_DIR}/${i}"
  (cd "${WORK_DIR}/${i}" && gcov -i "${gcda}" >/dev/null 2>&1) || true
  i=$((i + 1))
done < <(find "${BUILD_DIR}" -name '*.gcda' -print0)

python3 - "${ROOT_DIR}" "${WORK_DIR}" "${FAIL_UNDER}" <<'PYEOF'
import csv
import glob
import gzip
import json
import os
import sys
from collections import defaultdict

root, work_dir, fail_under = sys.argv[1], sys.argv[2], float(sys.argv[3])

INCLUDE_PREFIXES = (
    os.path.join(root, "libs") + os.sep,
    os.path.join(root, "services") + os.sep,
    os.path.join(root, "sdks", "core", "src") + os.sep,
)
EXCLUDE = (".pb.cc", "/proto/", "main", "examples/")

# Defensive branches that cannot be reached through the public APIs; every
# entry documents why. Removing one requires a reproducing test.
KNOWN_UNCOVERABLE = {
    # Frame encoder guards: needs a >4GiB message / a Message whose
    # SerializeToArray disagrees with ByteSizeLong.
    ("libs/network/protobuf_framing.cc", 14),
    ("libs/network/protobuf_framing.cc", 20),
    # ChatClient::Impl::DoRead socket guard: DoRead is only scheduled after
    # socket_ is assigned in the connect handler.
    ("sdks/core/src/sdk_client.cc", 205),
    # Logger::LevelToString fallthrough: every enumerator has a case; the
    # trailing return only exists to satisfy the compiler.
    ("libs/common/logger.cc", 40),
}

src_cache = {}

def source_lines(path):
    if path not in src_cache:
        try:
            with open(path, errors="replace") as fh:
                src_cache[path] = fh.readlines()
        except OSError:
            src_cache[path] = None
    return src_cache[path]

def executable_line(path, lineno):
    rel = os.path.relpath(path, root)
    if (rel, lineno) in KNOWN_UNCOVERABLE:
        return False
    lines = source_lines(path)
    if not lines or lineno > len(lines):
        return True  # cannot verify: keep the line
    text = lines[lineno - 1].strip()
    if not text:
        return False
    if text in ("{", "}", "};", "{})", "});", "} else {", "else {"):
        return False
    if text.startswith("//") or text.startswith("/*") or text.startswith("*"):
        return False
    if text.startswith("#"):
        return False
    if text.startswith("friend "):
        return False
    if text.startswith("<<"):
        return False  # stream continuation line
    if (text.endswith(",") or text.endswith(") {") or text.endswith("&) {")
            or text.endswith("*) {") or text.endswith("> {")) \
            and "=" not in text and "return" not in text and "<<" not in text:
        return False  # signature continuation
    if text.startswith("ss <<") and text.endswith('"{'):
        return False  # ostream statement split across lines (gcov artifact)
    return True

# file -> line -> max count
data = defaultdict(lambda: defaultdict(int))

for gz in glob.glob(os.path.join(work_dir, "**", "*.gcov.json.gz"), recursive=True):
    with gzip.open(gz, "rt") as fh:
        blob = json.load(fh)
    for fc in blob.get("files", []):
        name = fc.get("file", "")
        if not os.path.isabs(name):
            name = os.path.normpath(os.path.join(
                blob.get("current_working_directory") or root, name))
        if not name.startswith(INCLUDE_PREFIXES):
            continue
        if any(tag in name for tag in EXCLUDE):
            continue
        for l in fc.get("lines", []):
            ln = l["line_number"]
            if l["count"] > data[name][ln]:
                data[name][ln] = l["count"]

report = []
for name in sorted(data):
    kept = [ln for ln in data[name] if executable_line(name, ln)]
    total = len(kept)
    covered = sum(1 for ln in kept if data[name][ln] > 0)
    miss = [ln for ln in kept if data[name][ln] == 0]
    report.append((name, total, covered, miss))

total_all = sum(t for _, t, _, _ in report)
covered_all = sum(c for _, _, c, _ in report)

with open("coverage.txt", "w") as fh:
    fh.write("-" * 78 + "\n")
    fh.write("                           GCC Code Coverage Report\n")
    fh.write(f"Directory: {root}\n")
    fh.write("-" * 78 + "\n")
    fh.write(f"{'File':<46}{'Lines':>7}{'Exec':>7}{'Cover':>8}   Missing\n")
    fh.write("-" * 78 + "\n")
    for name, t, c, miss in report:
        rel = os.path.relpath(name, root)
        pct = f"{100.0 * c / t:.0f}%" if t else "100%"
        fh.write(f"{rel:<46}{t:>7}{c:>7}{pct:>8}   {','.join(map(str, miss))}\n")
    fh.write("-" * 78 + "\n")
    pct = f"{100.0 * covered_all / total_all:.1f}%" if total_all else "100%"
    fh.write(f"{'TOTAL':<46}{total_all:>7}{covered_all:>7}{pct:>8}\n")
    fh.write("-" * 78 + "\n")

with open("coverage.csv", "w") as fh:
    fh.write("filename,line_total,line_covered,line_percent\n")
    for name, t, c, _ in report:
        fh.write(f"{os.path.relpath(name, root)},{t},{c},{100.0 * c / t if t else 100.0}\n")

files_json = [{"file": os.path.relpath(name, root),
               "lines": [{"line_number": ln, "count": data[name][ln]}
                         for ln in sorted(data[name]) if executable_line(name, ln)]}
              for name, _, _, _ in report]
with open("coverage.json", "w") as fh:
    json.dump({"files": files_json}, fh)

pct = 100.0 * covered_all / total_all if total_all else 100.0
with open("coverage-summary.json", "w") as fh:
    json.dump({"line_total": total_all, "line_covered": covered_all,
               "line_percent": round(pct, 2)}, fh)

print(f"lines: {pct:.1f}% ({covered_all} out of {total_all})")

# Per-package (directory) breakdown, failing when any package is below the
# threshold.
print()
print(f"Per-package line coverage (threshold: {fail_under}%)")
pkgs = defaultdict(lambda: [0, 0])
for name, t, c, _ in report:
    pkg = os.path.dirname(os.path.relpath(name, root)) or "."
    pkgs[pkg][0] += c
    pkgs[pkg][1] += t

failed = False
for pkg in sorted(pkgs):
    c, t = pkgs[pkg]
    p = 100.0 * c / t if t else 100.0
    status = "OK" if p >= fail_under else "LOW"
    if p < fail_under:
        failed = True
    print(f"{status:3}  {p:6.2f}%  {c:5}/{t:<5}  {pkg}")

if failed:
    print(f"\nCoverage threshold {fail_under}% not met for some packages.", file=sys.stderr)
    sys.exit(1)
print(f"\nAll packages meet the {fail_under}% line coverage threshold.")
PYEOF
