# Zonal Cache - GNU C
#
#   make          compila bin/zonal-cache
#   make test     compila y ejecuta las pruebas unitarias
#   make clean    elimina los artefactos de compilación
#
# Dependencias (Debian/Ubuntu): build-essential libcurl4-openssl-dev libssl-dev libcjson-dev

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-format-truncation -pthread -D_GNU_SOURCE
LDLIBS  += -lcurl -lssl -lcrypto -lcjson -pthread

SRC_DIR   := src
TEST_DIR  := tests
BUILD_DIR := build
BIN_DIR   := bin

LIB_SRCS  := $(filter-out $(SRC_DIR)/main.c,$(wildcard $(SRC_DIR)/*.c))
LIB_OBJS  := $(LIB_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS := $(TEST_SRCS:$(TEST_DIR)/%.c=$(BUILD_DIR)/tests/%.o)

.PHONY: all test clean

all: $(BIN_DIR)/zonal-cache

$(BIN_DIR)/zonal-cache: $(LIB_OBJS) $(BUILD_DIR)/main.o | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BIN_DIR)/unit-tests: $(LIB_OBJS) $(TEST_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: $(BIN_DIR)/unit-tests
	./$(BIN_DIR)/unit-tests

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/tests/%.o: $(TEST_DIR)/%.c $(wildcard $(SRC_DIR)/*.h) $(TEST_DIR)/test.h | $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN_DIR) $(BUILD_DIR) $(BUILD_DIR)/tests:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
