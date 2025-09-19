#!/bin/bash
# tests/run_esp_sim.sh
IP=127.0.0.1
PORT=5683

echo "=== TC-003.1: Single sensor sends POST (should be stored) ==="
./clients/esp32_sim $IP $PORT 1 1

echo
echo "=== TC-003.2: Retransmit test (simulate server not replying) ==="
echo "-> To test retransmit you can temporarily stop the server or firewall the port."
echo "   Or run esp32_sim while server is down to see retransmit attempts."
echo

echo "=== TC-003.3: Malformed packet test ==="
./clients/malformed_sender $IP $PORT