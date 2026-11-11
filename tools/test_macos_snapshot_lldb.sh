#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Collect a native backtrace for one fake-device regression after CTest fails.
# A successful diagnostic rerun never replaces the original CTest outcome.
set -uo pipefail

if [ "$(uname -s)" != Darwin ]; then
    echo "This diagnostic requires the macOS CI runner." >&2
    exit 2
fi

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
test_binary="$repo_root/build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests"
evidence="$repo_root/build/test-results/macos-snapshot-diagnostic"
mkdir -p "$evidence" || exit 2

if [ ! -x "$test_binary" ]; then
    echo "Required host-test executable is missing: $test_binary" | tee "$evidence/error.txt" >&2
    exit 2
fi

{
    echo "Exact fake-device filter: AudioEngineTest.SavedDriverSnapshotDoesNotUseAReplacementEndpoint"
    git -C "$repo_root" rev-parse HEAD
    uname -m
    xcrun --find lldb
    file "$test_binary"
    xcrun lipo -info "$test_binary"
    xcrun dwarfdump --uuid "$test_binary"
} > "$evidence/identity.log" 2>&1

# The workflow bounds this diagnostic step to three minutes. This launches only
# the named fake-device test, with no DirectPipe application or physical device.
(
    cd "$repo_root/build/tests" || exit 2
    xcrun lldb --batch \
        --one-line run \
        --one-line-on-crash 'thread backtrace all' \
        --one-line-on-crash 'quit 1' \
        --one-line 'script import lldb; p = lldb.debugger.GetSelectedTarget().GetProcess(); lldb.debugger.HandleCommand("quit {}".format(p.GetExitStatus() if p.GetState() == lldb.eStateExited else 1))' \
        -- "$test_binary" \
        --gtest_filter=AudioEngineTest.SavedDriverSnapshotDoesNotUseAReplacementEndpoint
) 2>&1 | tee "$evidence/lldb.log"
pipeline_status=("${PIPESTATUS[@]}")
diagnostic_status=${pipeline_status[0]}
if [ "$diagnostic_status" -eq 0 ] && [ "${pipeline_status[1]}" -ne 0 ]; then
    diagnostic_status=2
fi
printf '%s\n' "$diagnostic_status" > "$evidence/exit-code.txt"

# Collect only this test binary's diagnostic reports, including the original
# CTest crash. Never upload other applications' reports from the runner.
report_root="$HOME/Library/Logs/DiagnosticReports"
if [ -d "$report_root" ]; then
    mkdir -p "$evidence/crash-reports"
    while IFS= read -r -d '' report; do
        cp "$report" "$evidence/crash-reports/" || echo "Could not copy host-test crash report: $report" >&2
    done < <(find "$report_root" -maxdepth 1 -type f \
        \( -name 'directpipe-host-tests*.ips' -o -name 'directpipe-host-tests*.crash' \
           -o -name 'DirectPipeHostTests*.ips' -o -name 'DirectPipeHostTests*.crash' \) -print0)
fi

# Preserve matching debug symbols when the build emitted them; Release may not
# contain a dSYM, which remains an explicit evidence limitation.
symbols_found=false
while IFS= read -r -d '' symbols; do
    symbols_found=true
    ditto "$symbols" "$evidence/$(basename "$symbols")" \
        || echo "Could not copy host-test debug symbols: $symbols" >&2
done < <(find "$repo_root/build/tests" -type d \
    \( -name 'directpipe-host-tests.dSYM' -o -name 'DirectPipeHostTests.dSYM' \) -prune -print0)
if [ "$symbols_found" = false ]; then
    echo "No host-test dSYM was produced by this build." > "$evidence/symbols.txt"
fi

exit "$diagnostic_status"
