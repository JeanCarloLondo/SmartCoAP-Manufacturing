# Unified Makefile (SmartCoAP-Manufacturing)
# Binaries are built into build/bin but then copied to the project root
# so you can see them easily as ./coap_server, ./client, ./esp32_sim

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -g -I./src -pthread -D_POSIX_C_SOURCE=200809L

OBJDIR := build/obj
BINDIR := build/bin

SRCDIR := src
SERVER_DIR := server
CLIENT_DIR := clients
TESTDIR := tests
LIBS := -lsqlite3

# Sources
COAP_SRC := $(SRCDIR)/coap.c
CLIENT_SRC := $(CLIENT_DIR)/client.c
SERVER_SRC := $(wildcard $(SERVER_DIR)/*.c)
COAP_CLIENT_SRC := # unused (kept for compatibility)
CLIENT_UTIL_SRC := $(TESTDIR)/client.c   # optional util (tests/client.c)

# Objects
COAP_OBJ := $(OBJDIR)/coap.o
SERVER_OBJS := $(patsubst $(SERVER_DIR)/%.c,$(OBJDIR)/%.o,$(SERVER_SRC))
DB_OBJ := $(OBJDIR)/db.o
CLIENT_UTIL_OBJ := $(OBJDIR)/client.o

# Tests (exclude tests/client.c to avoid name collisions)
ALL_TEST_SRCS := $(wildcard $(TESTDIR)/*.c)
TEST_SRCS := $(filter-out $(CLIENT_UTIL_SRC),$(ALL_TEST_SRCS))
TEST_OBJS := $(patsubst $(TESTDIR)/%.c,$(OBJDIR)/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TESTDIR)/%.c,$(BINDIR)/%,$(TEST_SRCS))

# Binaries (primary outputs)
SERVER_BIN := $(BINDIR)/coap_server
CLIENT_BIN := $(BINDIR)/client
CLIENT_UTIL_BIN := $(BINDIR)/client    # legacy name kept for expose
ESP32_SRC := $(CLIENT_DIR)/esp32_sim.c
ESP32_OBJ := $(OBJDIR)/esp32_sim.o
ESP32_BIN := $(BINDIR)/esp32_sim

# Defaults for server runtime
PORT ?= 5683
LOG ?= server.log
RUN_BG ?= 0
ESP32_IP ?= 127.0.0.1
ESP32_PORT ?= 5683
ESP32_TOPIC ?= sensor
ESP32_INSTANCES ?= 500
ESP32_REQS ?= 4

ESP32_MSGS ?= 200 # messages per instance
ESP32_INTERVAL ?= 4 # seconds between messages

.PHONY: all clean test run help server client esp32_sim expose

all: server esp32_sim test
	@echo "Build completed: server, esp32_sim and tests compiled."

# -----------------------
# Link rules
# -----------------------
$(SERVER_BIN): $(COAP_OBJ) $(SERVER_OBJS) $(DB_OBJ) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

server: $(SERVER_BIN) expose
	@echo "=> Running server: ./coap_server $(PORT) $(LOG)"
ifeq ($(RUN_BG),1)
	@nohup ./coap_server $(PORT) $(LOG) >/dev/null 2>&1 & echo $$! > server.pid && echo "Server started in background (pid saved to server.pid)"
else
	@./coap_server $(PORT) $(LOG)
endif

db_test: tests/db_test.c build/obj/db.o
	$(CC) $(CFLAGS) -I./src -o build/bin/db_test tests/db_test.c build/obj/db.o -lsqlite3

# -----------------------
# ESP32 simulator
# -----------------------
$(ESP32_OBJ): $(ESP32_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(ESP32_BIN): $(COAP_OBJ) $(ESP32_OBJ) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

esp32_sim: $(ESP32_BIN) expose
	@echo "ESP32 simulator built -> $(ESP32_BIN)"
	@echo "Launching esp32_sim with defaults:"
	@echo "  target: $(ESP32_IP):$(ESP32_PORT) topic=$(ESP32_TOPIC) instances=$(ESP32_INSTANCES) reqs=$(ESP32_REQS) msgs=$(ESP32_MSGS) interval=$(ESP32_INTERVAL)s"
	@echo
	@$(ESP32_BIN) $(ESP32_IP) $(ESP32_PORT) $(ESP32_TOPIC) $(ESP32_INSTANCES) $(ESP32_REQS) $(ESP32_MSGS) $(ESP32_INTERVAL)

# -----------------------
# client (console client)
# -----------------------
$(OBJDIR)/client.o: $(CLIENT_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT_BIN): $(COAP_OBJ) $(OBJDIR)/client.o | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

client: $(CLIENT_BIN) expose
	@echo "Client built -> $(CLIENT_BIN)"
	@echo "Launching client with defaults:"
	@# run the root-exposed binary so behavior mirrors server/esp32_sim
	@./client

# -----------------------
# Tests
# -----------------------
$(OBJDIR)/%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/%: $(COAP_OBJ) $(OBJDIR)/%.o | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

build/bin/db_test: build/obj/db_test.o build/obj/db.o
	$(CC) $(CFLAGS) -o $@ $^ -lsqlite3
	
test: $(TEST_BINS)
	@echo "Tests compiled:"
	@ls -1 $(TEST_BINS) 2>/dev/null || true

# -----------------------
# Run logic
# -----------------------
run:

ifndef TEST
	$(error Specify TEST=<test_name> (e.g. TEST=test_req001))
endif
	@if [ "$(TEST)" = "test_client" ] && [ -x sh_files/run_stress.sh ]; then \
		echo "Executing stress test..."; \
		./sh_files/run_stress.sh $(BINDIR)/test_client $(TEST_ARGS); \
	elif [ "$(TEST)" = "esp32_sim" ] && [ -x sh_files/run_esp32_test.sh ]; then \
		$(MAKE) esp32_sim; \
		echo "Executing ESP32 simulator test..."; \
		./sh_files/run_esp32_test.sh $(BINDIR)/esp32_sim $(TEST_ARGS); \
	elif [ "$(TEST)" = "esp32_cases" ]; then \
		$(MAKE) esp32_sim; \
		echo "Executing ESP32 integration cases..."; \
		bash sh_files/run_esp32_cases.sh $(ESP32_IP) $(ESP32_PORT); \
	else \
		if [ -x $(BINDIR)/$(TEST) ]; then \
			echo "Executing $(TEST) $(TEST_ARGS)..."; \
			$(BINDIR)/$(TEST) $(TEST_ARGS); \
		else \
			echo "Binary $(BINDIR)/$(TEST) not found. Compile it with 'make test'."; \
			exit 1; \
		fi; \
	fi

# -----------------------
# Object compilation rules
# -----------------------
$(COAP_OBJ): $(COAP_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SERVER_DIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(CLIENT_DIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# -----------------------
# Expose
# -----------------------
expose: | $(BINDIR)
	@echo "Exposing main binaries to project root..."
	@if [ -f $(SERVER_BIN) ]; then install -m755 $(SERVER_BIN) ./coap_server; fi
	@if [ -f $(CLIENT_BIN) ]; then install -m755 $(CLIENT_BIN) ./client; fi
	@if [ -f $(ESP32_BIN) ]; then install -m755 $(ESP32_BIN) ./esp32_sim; fi

# -----------------------
# Directories
# -----------------------
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

# -----------------------
# Clean
# -----------------------
clean:
	@echo "Cleaning build/ and root copies..."
	-rm -rf $(OBJDIR) $(BINDIR) ./coap_server ./coap_client ./client ./esp32_sim server.pid
	@echo "Done."

# -----------------------
# Help
# -----------------------
help:
	@echo "Available targets:"
	@echo "  make                 -> build server, clients and tests"
	@echo "  make server          -> build and run ./coap_server (set PORT, LOG, RUN_BG)"
	@echo "  make coap_client     -> (not used) kept for compatibility"
	@echo "  make client          -> build robust client ./client and run it"
	@echo "  make esp32_sim       -> build ESP32 simulator ./esp32_sim"
	@echo "  make test            -> build tests (test_*.c)"
	@echo "  make run TEST=<name> -> run test (special cases: test_client, esp32_sim)"
	@echo "  make clean           -> remove build/ and top-level binaries"