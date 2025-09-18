CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g
INCLUDE = -I../src
BUILD   = build

SRC     = ../src/coap.c
OBJS    = $(BUILD)/coap.o

CLIENTS = esp32_sim malformed_sender

# Default target
all: $(CLIENTS)

# Build directory
$(BUILD):
	mkdir -p $(BUILD)

# Compile coap.c
$(BUILD)/coap.o: $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

esp32_sim: esp32_sim.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@

malformed_sender: malformed_sender.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDE) $^ -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD) $(CLIENTS)