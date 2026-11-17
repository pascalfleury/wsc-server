#!/usr/bin/env python3
"""Fails when line coverage of a source scope is below its threshold.

Reads the JSON written by `llvm-cov export -summary-only`. Thresholds are
defined in docs/CODING.org.

  check_coverage.py coverage.json --root . --min libwsc/=90 --min src/=80 \
      --exclude src/platform/ --exclude src/discovery/
"""

import argparse
import json
import os
import sys


def scope_coverage(files, root, prefix, excludes):
    """Returns (covered, total) lines of non-excluded files under `prefix`."""
    covered = total = 0
    for entry in files:
        rel = os.path.relpath(entry["filename"], root).replace(os.sep, "/")
        if not rel.startswith(prefix) or any(rel.startswith(e) for e in excludes):
            continue
        lines = entry["summary"]["lines"]
        covered += lines["covered"]
        total += lines["count"]
    return covered, total


def check(files, root, minimums, excludes):
    """Returns a report line per scope and whether all thresholds are met."""
    report, ok = [], True
    for prefix, minimum in minimums:
        covered, total = scope_coverage(files, root, prefix, excludes)
        if total == 0:
            report.append(f"{prefix}: no code yet, skipped")
            continue
        percent = 100.0 * covered / total
        passed = percent >= minimum
        ok = ok and passed
        report.append(
            f"{prefix}: {percent:.1f} % of {total} lines "
            f"(minimum {minimum:g} %) {'ok' if passed else 'TOO LOW'}"
        )
    return report, ok


def self_test():
    def entry(name, covered, count):
        return {"filename": f"/r/{name}",
                "summary": {"lines": {"covered": covered, "count": count}}}

    files = [entry("src/core/a.cc", 9, 10), entry("src/platform/p.cc", 0, 90),
             entry("other/x.cc", 0, 50)]
    assert scope_coverage(files, "/r", "src/", ["src/platform/"]) == (9, 10)
    assert check(files, "/r", [("src/", 80.0)], ["src/platform/"])[1]
    assert not check(files, "/r", [("src/", 95.0)], ["src/platform/"])[1]
    assert not check(files, "/r", [("src/", 80.0)], [])[1]  # 9 of 100
    assert check(files, "/r", [("libwsc/", 90.0)], [])[1]  # nothing there yet
    print("self-test ok")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("summary", nargs="?", help="llvm-cov export JSON")
    parser.add_argument("--root", default=".", help="repository root")
    parser.add_argument("--min", action="append", default=[],
                        metavar="PREFIX=PERCENT")
    parser.add_argument("--exclude", action="append", default=[],
                        metavar="PREFIX")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.summary or not args.min:
        parser.error("a summary file and at least one --min are required")

    minimums = []
    for item in args.min:
        prefix, _, percent = item.partition("=")
        minimums.append((prefix, float(percent)))
    with open(args.summary, encoding="utf-8") as f:
        files = json.load(f)["data"][0]["files"]
    report, ok = check(files, os.path.abspath(args.root), minimums, args.exclude)
    print("\n".join(report))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
