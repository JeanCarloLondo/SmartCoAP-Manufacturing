# Unified Makefile (SmartCoAP-Manufacturing)
# Binaries are built into build/bin but then copied to the project root
# so you can see them easily as ./coap_server, ./coap_client, ./client.
#
# Usage examples:
#   make                 # build everything (server, client, tests)
#   make server          # build and run ./coap_server $(PORT) $(LOG)
#   make server PORT=5683 LOG=mylog.log     # run with custom port/log
#   make server RUN_BG=1 PORT=5683 LOG=server.log  # run server in background
#   make coap_client     # build client simulation and copy ./coap_client
#   make esp32_sim       # build ESP32 simulator
#   make test            # build tests (test_*.c)
#   make run TEST=test_req001        # execute a specific test binary
#   make run TEST=test_client        # run stress test via sh_files/run_stress.sh
#   make run TEST=esp32_cases
#   make clean           # remove build/ and root copies
#
# NOTE: Running `make server` in foreground will block your terminal.
# Set RUN_BG=1 to run it in background (Make will return immediately).

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -g -I./src -pthread -D_POSIX_C_SOURCE=200809L

OBJDIR := build/obj
BINDIR := build/bin

SRCDIR := src
SERVER_DIR := server
CLIENT_DIR := coap_client/main
TESTDIR := tests

# Sources
COAP_SRC := $(SRCDIR)/coap.c
SERVER_SRC := $(wildcard $(SERVER_DIR)/*.c)
COAP_CLIENT_SRC := $(CLIENT_DIR)/coap_client.c
CLIENT_UTIL_SRC := $(TESTDIR)/client.c   # optional util (tests/client.c)

# Objects
COAP_OBJ := $(OBJDIR)/coap.o
SERVER_OBJS := $(patsubst $(SERVER_DIR)/%.c,$(OBJDIR)/%.o,$(SERVER_SRC))
COAP_CLIENT_OBJ := $(OBJDIR)/coap_client.o
CLIENT_UTIL_OBJ := $(OBJDIR)/client.o

# Tests (exclude tests/client.c to avoid name collisions)
ALL_TEST_SRCS := $(wildcard $(TESTDIR)/*.c)
TEST_SRCS := $(filter-out $(CLIENT_UTIL_SRC),$(ALL_TEST_SRCS))
TEST_OBJS := $(patsubst $(TESTDIR)/%.c,$(OBJDIR)/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TESTDIR)/%.c,$(BINDIR)/%,$(TEST_SRCS))

# Binaries (primary outputs)
SERVER_BIN := $(BINDIR)/coap_server
COAP_CLIENT_BIN := $(BINDIR)/coap_client
CLIENT_UTIL_BIN := $(BINDIR)/client
ESP32_SRC := clients/esp32_sim.c
ESP32_OBJ := $(OBJDIR)/esp32_sim.o
ESP32_BIN := $(BINDIR)/esp32_sim

# Defaults for server runtime
PORT ?= 5683
LOG ?= server.log
RUN_BG ?= 0

.PHONY: all clean test run help server coap_client client_util esp32_sim expose

all: server coap_client esp32_sim test
	@echo "Build completed: server, coap_client, esp32_sim and tests compiled."

# -----------------------
# Link rules
# -----------------------
$(SERVER_BIN): $(COAP_OBJ) $(SERVER_OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

server: $(SERVER_BIN) expose
	@echo "=> Running server: ./coap_server $(PORT) $(LOG)"
ifeq ($(RUN_BG),1)
	@nohup ./coap_server $(PORT) $(LOG) >/dev/null 2>&1 & echo $$! > server.pid && echo "Server started in background (pid saved to server.pid)"
else
	@./coap_server $(PORT) $(LOG)
endif

# -----------------------
# ESP32 simulator
# -----------------------
$(ESP32_OBJ): $(ESP32_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(ESP32_BIN): $(COAP_OBJ) $(ESP32_OBJ) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

esp32_sim: $(ESP32_BIN) expose
	@echo "ESP32 simulator ready -> ./esp32_sim"

# -----------------------
# Client utilities
# -----------------------
$(CLIENT_UTIL_OBJ): $(CLIENT_UTIL_SRC) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT_UTIL_BIN): $(COAP_OBJ) $(CLIENT_UTIL_OBJ) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

client: $(CLIENT_UTIL_BIN) expose
	@echo "Robust client ready -> ./client"

$(COAP_CLIENT_BIN): $(COAP_OBJ) $(COAP_CLIENT_OBJ) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

coap_client: $(COAP_CLIENT_BIN) expose
	@echo "Client (minimal simulation) built and exposed as ./coap_client"

# -----------------------
# Tests
# -----------------------
$(OBJDIR)/%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/%: $(COAP_OBJ) $(OBJDIR)/%.o | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^

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
	elif [ "$(TEST)" = "esp32_cases" ] && [ -x sh_files/run_esp32_cases.sh ]; then \
		$(MAKE) esp32_sim; \
		echo "Executing ESP32 integration cases..."; \
		./sh_files/run_esp32_cases.sh $(TEST_ARGS); \
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
	@if [ -f $(COAP_CLIENT_BIN) ]; then install -m755 $(COAP_CLIENT_BIN) ./coap_client; fi
	@if [ -f $(CLIENT_UTIL_BIN) ]; then install -m755 $(CLIENT_UTIL_BIN) ./client; fi
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
	@echo "  make coap_client     -> build minimal client ./coap_client"
	@echo "  make client          -> build robust client ./client"
	@echo "  make esp32_sim       -> build ESP32 simulator ./esp32_sim"
	@echo "  make test            -> build tests (test_*.c)"
	@echo "  make run TEST=<name> -> run test (special cases: test_client, esp32_sim)"
	@echo "  make clean           -> remove build/ and top-level binaries"