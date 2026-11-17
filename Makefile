# MiniSQL - build with plain make + g++ (or clang++).
#   make          audit for STL containers, then build ./minisql
#   make test     build and run the test harness
#   make bench    run the indexed vs full-scan benchmark
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CXXFLAGS += -Isrc

ENGINE_SRC := $(wildcard src/ds/*.cpp src/util/*.cpp src/storage/*.cpp src/index/*.cpp src/sql/*.cpp)
SERVER_SRC := $(wildcard src/server/*.cpp)
APP_SRC    := src/main.cpp src/Cli.cpp

ENGINE_OBJ := $(patsubst src/%.cpp,build/%.o,$(ENGINE_SRC))
SERVER_OBJ := $(patsubst src/%.cpp,build/%.o,$(SERVER_SRC))
APP_OBJ    := $(patsubst src/%.cpp,build/%.o,$(APP_SRC))

BIN  := minisql
TEST := build/run_tests

ifeq ($(OS),Windows_NT)
  BIN    := minisql.exe
  TEST   := build/run_tests.exe
  LDLIBS += -lws2_32
endif

.PHONY: all audit test bench clean

all: audit $(BIN)

$(BIN): $(ENGINE_OBJ) $(SERVER_OBJ) $(APP_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TEST): tests/test_main.cpp $(ENGINE_OBJ)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

audit:
	@sh scripts/audit_stl.sh

test: audit $(TEST)
	./$(TEST)

bench: $(BIN)
	./$(BIN) --bench 10000

clean:
	rm -rf build $(BIN) minisql.exe

-include $(ENGINE_OBJ:.o=.d) $(SERVER_OBJ:.o=.d) $(APP_OBJ:.o=.d)
