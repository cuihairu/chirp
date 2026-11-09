#!/usr/bin/env python3
# Coverage aggregation for scripts/run_coverage.sh: fold the gcov
# JSON intermediates into per-package line/branch/function reports
# and gate on the line threshold. Pin tables live in
# coverage_pins.py next to this file.

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
EXCLUDE = (".pb.cc", "/proto/", "examples/", "/apps/ios/.build/", "/third_party/", "/vcpkg/")

def is_excluded(path):
    if any(tag in path for tag in EXCLUDE):
        return True
    # Service entry points (main.cc, main_enhanced.cc, main_distributed.cc):
    # matched on the basename only. The old substring test also dropped every
    # file under any ancestor directory whose path merely contains "main"
    # (e.g. a worktree at /tmp/chirp-cov-main), silently reporting 0/0 = 100%.
    return os.path.basename(path).startswith("main")


from coverage_pins import (
    KNOWN_UNCOVERABLE,
    KNOWN_UNCOVERABLE_FUNCTIONS,
    KNOWN_UNCOVERABLE_ARMS,
)

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
# file -> line -> {arm_index: [max_count, is_throw_edge]}
branch_data = defaultdict(lambda: defaultdict(dict))
# (file, start_line, function_name) -> max execution count across contexts
func_exec = {}

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
        if is_excluded(name):
            continue
        for l in fc.get("lines", []):
            ln = l["line_number"]
            if l["count"] > data[name][ln]:
                data[name][ln] = l["count"]
            for bi, br in enumerate(l.get("branches", [])):
                cnt = br.get("count", 0)
                thr = bool(br.get("throw", False))
                slot = branch_data[name][ln].get(bi)
                if slot is None:
                    branch_data[name][ln][bi] = [cnt, thr]
                elif cnt > slot[0]:
                    slot[0] = cnt
        for fn in fc.get("functions", []):
            fname = fn.get("name", "")
            if fname.startswith("<") or "artificial" in fname:
                continue
            key = (name, fn.get("start_line", 0), fname)
            cnt = fn.get("execution_count", 0)
            if cnt > func_exec.get(key, -1):
                func_exec[key] = cnt

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

# lcov tracefile for Codecov (and any other lcov-aware consumer).
with open("coverage.lcov", "w") as fh:
    for name, t, c, _ in report:
        fh.write("TN:\n")
        fh.write(f"SF:{os.path.relpath(name, root)}\n")
        for ln in sorted(data[name]):
            if executable_line(name, ln):
                fh.write(f"DA:{ln},{data[name][ln]}\n")
        fh.write(f"LF:{t}\n")
        fh.write(f"LH:{c}\n")
        fh.write("end_of_record\n")

# An empty report (no files matched) is an aggregation failure, never a pass:
# it previously surfaced as 0/0 -> 100% when EXCLUDE swallowed every path.
if total_all == 0:
    print("No coverage data aggregated (0 files) - treating as failure.",
          file=sys.stderr)
    sys.exit(1)

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
# Missing-line diagnostics: CI logs only carry this stdout, so list the
# uncovered lines of every package under the threshold right in the table.
misses = defaultdict(list)
for name, _t, _c, _miss in report:
    pkg = os.path.dirname(os.path.relpath(name, root)) or "."
    for ln in sorted(data[name]):
        if executable_line(name, ln) and data[name][ln] == 0:
            misses[pkg].append(f"{os.path.relpath(name, root)}:{ln}")
for pkg in sorted(pkgs):
    c, t = pkgs[pkg]
    p = 100.0 * c / t if t else 100.0
    status = "OK" if p >= fail_under else "LOW"
    if p < fail_under:
        failed = True
    print(f"{status:3}  {p:6.2f}%  {c:5}/{t:<5}  {pkg}")
    if p < fail_under:
        print(f"      missing lines: {', '.join(misses.get(pkg, []))}")

# ---------------------------------------------------------------------------
# Branch / function coverage. Informational for now: the gate above stays
# line-based; this section inventories which control-flow arms and whole
# functions tests never reach. Throw edges (exception cleanup) are skipped -
# they are unreachable without a throwing path and gcov counts them as
# ordinary arms. KNOWN_UNCOVERABLE lines drop out via executable_line, same
# as the line stats.
# ---------------------------------------------------------------------------
branch_taken = branch_total = 0
func_cov = func_total = 0
branch_gaps = []  # (name, line, taken, total, untaken_arm_indices)
func_gaps = []    # (name, start_line, function_name)
pkg_branch = defaultdict(lambda: [0, 0])
pkg_func = defaultdict(lambda: [0, 0])
exempted_arm_count = 0

for name, _t, _c, _miss in report:
    pkg = os.path.dirname(os.path.relpath(name, root)) or "."
    for ln in sorted(data[name]):
        if not executable_line(name, ln):
            continue
        arms = branch_data.get(name, {}).get(ln)
        if not arms:
            continue
        arm_entry = KNOWN_UNCOVERABLE_ARMS.get((os.path.relpath(name, root), ln))
        exempt_arms = arm_entry[0] if arm_entry else ()
        tot = tak = 0
        untaken = []
        for bi in sorted(arms):
            cnt, thr = arms[bi]
            if thr:
                continue
            if bi in exempt_arms:
                exempted_arm_count += 1
                continue
            tot += 1
            if cnt > 0:
                tak += 1
            else:
                untaken.append(bi)
        branch_total += tot
        branch_taken += tak
        pkg_branch[pkg][0] += tak
        pkg_branch[pkg][1] += tot
        if tak < tot:
            branch_gaps.append((name, ln, tak, tot, untaken))

for (name, sl, fname), cnt in sorted(func_exec.items()):
    if (os.path.relpath(name, root), sl) in KNOWN_UNCOVERABLE_FUNCTIONS:
        continue
    pkg = os.path.dirname(os.path.relpath(name, root)) or "."
    func_total += 1
    pkg_func[pkg][1] += 1
    if cnt > 0:
        func_cov += 1
        pkg_func[pkg][0] += 1
    else:
        func_gaps.append((name, sl, fname))

bpct = 100.0 * branch_taken / branch_total if branch_total else 100.0
fpct = 100.0 * func_cov / func_total if func_total else 100.0

print()
print(f"branches:  {bpct:.1f}% ({branch_taken} of {branch_total} arms, "
      "throw edges excluded)")
print(f"functions: {fpct:.1f}% ({func_cov} of {func_total})")
if KNOWN_UNCOVERABLE_ARMS:
    print(f"exempted branch arms: {exempted_arm_count} (KNOWN_UNCOVERABLE_ARMS)")
    for (arm_path, arm_ln), (arm_idxs, arm_why) in sorted(KNOWN_UNCOVERABLE_ARMS.items()):
        print(f"  {arm_path}:{arm_ln} arms={list(arm_idxs)} - {arm_why}")

print()
print("Per-package branch/function coverage (informational, not gated)")
for pkg in sorted(set(pkg_branch) | set(pkg_func)):
    bt, bb = pkg_branch[pkg]
    fc_, ft = pkg_func[pkg]
    b = 100.0 * bt / bb if bb else 100.0
    f = 100.0 * fc_ / ft if ft else 100.0
    print(f"     branch {b:6.2f}% {bt:5}/{bb:<5}  "
          f"func {f:6.2f}% {fc_:5}/{ft:<5}  {pkg}")

with open("coverage-gaps.txt", "w") as fh:
    fh.write("uncovered branch arms (file:line taken/total untaken=[arm indices])\n")
    for name, ln, tak, tot, untaken in branch_gaps:
        fh.write(f"{os.path.relpath(name, root)}:{ln}  {tak}/{tot} {untaken}\n")
    fh.write("\nuncovered functions (file:start_line name)\n")
    for name, sl, fname in func_gaps:
        fh.write(f"{os.path.relpath(name, root)}:{sl}  {fname}\n")

print()
print(f"branch gaps: {len(branch_gaps)} lines, "
      f"function gaps: {len(func_gaps)} (see coverage-gaps.txt)")

if failed:
    print(f"\nCoverage threshold {fail_under}% not met for some packages.", file=sys.stderr)
    sys.exit(1)
print(f"\nAll packages meet the {fail_under}% line coverage threshold.")
