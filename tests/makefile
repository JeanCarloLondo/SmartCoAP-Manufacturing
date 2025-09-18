CC=gcc
CFLAGS=-Wall -pthread
SRC=../src/coap.c test_client.c
OBJ=$(SRC:.c=.o)

test_client: $(OBJ)
	$(CC) -o $@ $(OBJ) $(CFLAGS)

clean:
	rm -f $(OBJ) test_client
