CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -I./src -g
SRC := src/coap.c
TEST := tests/test_req001.c
OBJ := $(SRC:.c=.o)

all: test_req001

src/coap.o: src/coap.c src/coap.h
	$(CC) $(CFLAGS) -c src/coap.c -o src/coap.o

test_req001: src/coap.o $(TEST)
	$(CC) $(CFLAGS) src/coap.o $(TEST) -o test_req001

run: test_req001
	./test_req001

clean:
	rm -f src/*.o test_req001