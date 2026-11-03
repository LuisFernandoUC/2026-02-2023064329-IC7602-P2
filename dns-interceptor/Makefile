CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wpedantic -D_GNU_SOURCE
LDFLAGS += -pthread

LIB_SRC  = src/dns.c src/geo.c src/config.c src/resolver.c
TEST_SRC = $(wildcard tests/test_*.c)

BIN      = build/dns_interceptor
TEST_BIN = build/run_tests
DB_GZ    = data/dbip-country-lite.csv.gz
DB_CSV   = data/dbip-country-lite.csv

.PHONY: all test db clean

all: $(BIN)

$(BIN): $(LIB_SRC) src/main.c $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(LIB_SRC) src/main.c -o $@ $(LDFLAGS)

# Descomprime la base IP to Country (el repositorio solo guarda la versión .gz).
db: $(DB_CSV)

$(DB_CSV): $(DB_GZ)
	gunzip -c $< > $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(LIB_SRC) $(TEST_SRC) $(wildcard src/*.h) tests/test.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(LIB_SRC) $(TEST_SRC) -o $@ $(LDFLAGS)

clean:
	rm -rf build $(DB_CSV)
