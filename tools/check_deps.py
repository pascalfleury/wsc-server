#!/usr/bin/env python3
"""Fails when the project's CMake targets depend on each other in a cycle (D-32).

A target depends on another when it links it, or when one of its files
includes a header that the other target lists as a source. Targets and their
sources come from the CMake File API: create the query before configuring.

  mkdir -p build/x/.cmake/api/v1/query && touch build/x/.cmake/api/v1/query/codemodel-v2
  cmake --preset x
  check_deps.py build/x --root .
"""

import argparse
import glob
import json
import os
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def load_targets(build_dir):
    """Returns {target: {"sources": [repo-relative paths], "links": set()}}."""
    reply = os.path.join(build_dir, ".cmake", "api", "v1", "reply")
    indexes = sorted(glob.glob(os.path.join(reply, "index-*.json")))
    if not indexes:
        sys.exit(f"no CMake File API reply in {reply}; see --help")
    with open(indexes[-1]) as f:
        index = json.load(f)
    codemodel = next(o for o in index["objects"] if o["kind"] == "codemodel")
    with open(os.path.join(reply, codemodel["jsonFile"])) as f:
        model = json.load(f)
    refs = model["configurations"][0]["targets"]
    names = {t["id"]: t["name"] for t in refs}
    targets = {}
    for ref in refs:
        with open(os.path.join(reply, ref["jsonFile"])) as f:
            target = json.load(f)
        targets[target["name"]] = {
            "sources": [s["path"].replace(os.sep, "/") for s in target.get("sources", [])],
            "links": {names[d["id"]] for d in target.get("dependencies", [])},
        }
    return targets


def edges(targets, read_file):
    """Returns {(from, to): reason}; read_file(path) gives a source's lines."""
    owner = {}
    for name, target in targets.items():
        for path in target["sources"]:
            owner.setdefault(path, name)
    result = {}
    for name, target in targets.items():
        for dep in target["links"]:
            result.setdefault((name, dep), "links it")
        for path in target["sources"]:
            for number, line in enumerate(read_file(path), 1):
                match = INCLUDE.match(line)
                dep = owner.get(match.group(1)) if match else None
                if dep is not None and dep != name:
                    result.setdefault((name, dep), f"{path}:{number} includes {match.group(1)}")
    return result


def cycles(nodes, edge_map):
    """Strongly connected components with more than one node (Tarjan)."""
    graph = {n: sorted(b for (a, b) in edge_map if a == n) for n in nodes}
    index, low, stack, on_stack, found = {}, {}, [], set(), []

    def visit(n):
        index[n] = low[n] = len(index)
        stack.append(n)
        on_stack.add(n)
        for m in graph[n]:
            if m not in index:
                visit(m)
                low[n] = min(low[n], low[m])
            elif m in on_stack:
                low[n] = min(low[n], index[m])
        if low[n] == index[n]:
            component = []
            while True:
                m = stack.pop()
                on_stack.discard(m)
                component.append(m)
                if m == n:
                    break
            if len(component) > 1:
                found.append(sorted(component))

    for n in sorted(graph):
        if n not in index:
            visit(n)
    return found


def report(targets, edge_map):
    """Returns the report lines and whether the graph is acyclic."""
    found = cycles(targets.keys(), edge_map)
    lines = []
    for component in found:
        lines.append("cycle between " + ", ".join(component) + ":")
        members = set(component)
        for (a, b), reason in sorted(edge_map.items()):
            if a in members and b in members:
                lines.append(f"  {a} -> {b}: {reason}")
    if not found:
        lines.append(f"no dependency cycles among {len(targets)} targets")
    return lines, not found


def self_test():
    targets = {
        "low": {"sources": ["low/a.h", "low/a.cc"], "links": set()},
        "high": {"sources": ["high/b.h", "high/b.cc"], "links": {"low"}},
    }
    files = {"low/a.cc": ['#include "low/a.h"'], "high/b.cc": ['#include "low/a.h"']}
    read = lambda p: files.get(p, [])
    assert report(targets, edges(targets, read))[1]

    files["low/a.cc"].append('  #  include "high/b.h"')  # low now needs high
    lines, ok = report(targets, edges(targets, read))
    assert not ok
    assert "cycle between high, low:" in lines
    assert "  low -> high: low/a.cc:2 includes high/b.h" in lines
    assert "  high -> low: links it" in lines
    print("self-test ok")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build_dir", nargs="?")
    parser.add_argument("--root", default=".", help="repository root")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.build_dir is None:
        parser.error("build_dir is required")

    def read_file(path):
        full = path if os.path.isabs(path) else os.path.join(args.root, path)
        with open(full, encoding="utf-8") as f:
            return f.readlines()

    targets = load_targets(args.build_dir)
    lines, ok = report(targets, edges(targets, read_file))
    print("\n".join(lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
