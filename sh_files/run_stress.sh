#!/bin/bash
# run_stress.sh - run the test_client stress runner
# Usage:
#   ./sh_files/run_stress.sh [path-to-test_binary] [IP] [PORT] [THREADS] [REQS]
#
# Defaults:
#   path-to-test_binary -> build/bin/test_client
#   IP -> 127.0.0.1
#   PORT -> 5683
#   THREADS -> 100
#   REQS -> 20

BINARY="${1:-build/bin/test_client}"
IP="${2:-127.0.0.1}"
PORT="${3:-5683}"
THREADS="${4:-100}"
REQS="${5:-20}"

OUTFILE="tests/stress_out.txt"

if [ ! -x "$BINARY" ]; then
  echo "Error: test binary '$BINARY' not found or not executable"
  echo "Build it with 'make test' or 'make' if needed."
  exit 1
fi

echo "Running stress test:"
echo "  binary: $BINARY"
echo "  target: $IP:$PORT"
echo "  threads: $THREADS, requests/thread: $REQS"
echo "Output -> $OUTFILE"

"$BINARY" "$IP" "$PORT" "$THREADS" "$REQS" > "$OUTFILE" 2>&1
rc=$?
if [ $rc -eq 0 ]; then
  echo "Stress test completed. Output saved to $OUTFILE"
else
  echo "Stress test finished with exit code $rc. See $OUTFILE for details."
fi
exit $rc