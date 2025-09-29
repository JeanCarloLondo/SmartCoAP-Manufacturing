#!/bin/bash
# run_esp32_cases.sh - Executes test cases for the ESP32 simulator against the CoAP server
# Usage: ./run_esp32_cases.sh [SERVER_IP] [PORT]

# --- Resolve project root ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
cd "$ROOT_DIR"

# --- Defaults ---
IP=${1:-127.0.0.1}
PORT=${2:-5683}
BIN=build/bin/esp32_sim
LOG=tests/esp32_cases.log

echo "[esp32_test] Running integration tests (server=$IP:$PORT)"
echo "--------------------------------------------------------"

# Ensure log folder exists
mkdir -p "$(dirname "$LOG")"

# Write header to log
echo "Test run at $(date)" > "$LOG"
echo "Server: $IP:$PORT" >> "$LOG"

# --- TC-003.1: Normal POST with valid data ---
echo -e "\n[TC-003.1] Normal POST with temperature/humidity" | tee -a "$LOG"
$BIN $IP $PORT sensor 2 1 | tee -a "$LOG"

# --- TC-003.2: Retransmission due to missing ACK ---
echo -e "\n[TC-003.2] Retransmission (simulate unavailable server)" | tee -a "$LOG"
# Send to a wrong port to force timeouts/retransmissions
$BIN $IP 9999 sensor 1 1 | tee -a "$LOG"

# --- TC-003.3: Malformed packet ---
echo -e "\n[TC-003.3] Send invalid UDP packet (malformed)" | tee -a "$LOG"
echo "garbage" | nc -u -w1 $IP $PORT
echo "[manual UDP sent: 'garbage']" | tee -a "$LOG"

echo -e "\n[esp32_test] All test cases executed. Check $LOG for details."
