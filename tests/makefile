CC = gcc
CFLAGS = -Wall -pthread

SRC_COAP = ../src/coap.c
OBJ_COAP = ../src/coap.o

all: test_client client_minimal

test_client: test_client.o $(OBJ_COAP)
	$(CC) -o $@ test_client.o $(OBJ_COAP) $(CFLAGS)

client_minimal: client_minimal.o $(OBJ_COAP)
	$(CC) -o $@ client_minimal.o $(OBJ_COAP) $(CFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(OBJ_COAP) test_client client_minimal