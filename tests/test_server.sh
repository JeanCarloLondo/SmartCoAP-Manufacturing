#!/bin/bash
SERVER="127.0.0.1"
PORT=5683

echo "=== Test 1: GET (lectura de temperatura y humedad) ==="
coap-client -m get coap://$SERVER:$PORT/

echo -e "\n=== Test 2: POST (agregar valor nuevo) ==="
coap-client -m post coap://$SERVER:$PORT/ -e "nuevo_valor"

echo -e "\n=== Test 3: PUT (actualizar valor existente) ==="
coap-client -m put coap://$SERVER:$PORT/ -e "valor_actualizado"

echo -e "\n=== Test 4: GET (lectura después de actualización) ==="
coap-client -m get coap://$SERVER:$PORT/

echo -e "\n=== Test 5: DELETE (eliminar valor) ==="
coap-client -m delete coap://$SERVER:$PORT/

echo -e "\n=== Test 6: GET (lectura después de delete) ==="
coap-client -m get coap://$SERVER:$PORT/