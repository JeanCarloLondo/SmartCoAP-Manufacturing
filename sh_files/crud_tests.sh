#!/bin/bash

echo "=== TC-005.1: POST + GET ==="
./tests/client_minimal POST 25
./tests/client_minimal GET

echo "=== TC-005.2: PUT (update) ==="
./tests/client_minimal PUT 30
./tests/client_minimal GET

echo "=== TC-005.3: DELETE ==="
./tests/client_minimal DELETE
./tests/client_minimal GET