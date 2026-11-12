#!/usr/bin/env bash
# Runs the test suite (Swift Testing; XCTest is not available with Command Line Tools only).
# Under Command Line Tools the Swift Testing macro plugin is sometimes reported as "not found"
# (nondeterministic, seen on a busy machine); the same command then succeeds on a retry.
set -uo pipefail
cd "$(dirname "$0")/.."
for attempt in 1 2 3 4 5; do
  out="$(swift test 2>&1)"; status=$?
  if [ $status -eq 0 ]; then echo "$out" | grep -E "✘|Test run|passed|failed" | tail -5; exit 0; fi
  if echo "$out" | grep -q "plugin for module 'TestingMacros' not found"; then
    echo "attempt $attempt: Swift Testing macro plugin not found (CLT quirk), retrying..." >&2
    continue
  fi
  echo "$out" | tail -40; exit $status
done
echo "giving up after 5 attempts" >&2; exit 1
