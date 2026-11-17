#!/usr/bin/env bash
# Builds the `coverage` preset, runs the tests and enforces the coverage
# thresholds of docs/CODING.org. Needs clang++, llvm-profdata and llvm-cov
# (set LLVM_SUFFIX=-18 if they are only installed with a version suffix).
set -euo pipefail
cd "$(dirname "$0")/.."

build=build/coverage
suffix="${LLVM_SUFFIX:-}"

cmake --preset coverage
cmake --build --preset coverage
rm -rf "$build/profiles"
LLVM_PROFILE_FILE="$PWD/$build/profiles/%p-%m.profraw" ctest --preset coverage
"llvm-profdata$suffix" merge -o "$build/coverage.profdata" "$build"/profiles/*.profraw

# llvm-cov takes the first binary as-is and every other one with -object.
objects=()
while IFS= read -r binary; do
  if [ ${#objects[@]} -eq 0 ]; then objects+=("$binary"); else objects+=(-object "$binary"); fi
done < <(find "$build" -type f -name '*_test' -perm -u+x | sort)

# Tests, fakes, the entry point and dependencies are not measured.
"llvm-cov$suffix" export -summary-only \
  -instr-profile "$build/coverage.profdata" \
  -ignore-filename-regex '(_test\.cc$|/testing/|/src/main\.cc$|/build/|vcpkg)' \
  "${objects[@]}" > "$build/coverage.json"

python3 tools/check_coverage.py "$build/coverage.json" --root . \
  --min libwsc/=90 --min src/=80 \
  --exclude src/platform/ --exclude src/discovery/
