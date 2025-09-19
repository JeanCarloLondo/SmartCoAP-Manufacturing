#!/bin/bash
IP=127.0.0.1
PORT=5683

# parameters
THREADS=100
REQS=20

if [ ! -x ./tests/test_client ]; then
  echo "Error: ./tests/test_client not found or not executable"
  exit 1
fi

./tests/test_client $IP $PORT $THREADS $REQS > tests/stress_out.txt
echo "Stress test completed. Output saved to tests/stress_out.txt"