# build to build the compiler
# test to run automated tests
# clean_test to delete only test releated built files
# clean to delete all built files

.PHONY: build test clean_test clean

.DEFAULT_GOAL := build

APP_NAME = orbc

CC = clang++
INC_DIR = include
# TODO: separate release and debug builds
CFLAGS = -g -I$(INC_DIR) `llvm-config --cxxflags --ldflags --system-libs --libs core` -std=c++14

HDR_DIR = include
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests

HDRS = $(wildcard $(HDR_DIR)/*.h)
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

TESTS = $(wildcard $(TEST_DIR)/*.orb)
TEST_BINS = $(TESTS:$(TEST_DIR)/%.orb=$(BIN_DIR)/$(TEST_DIR)/%)
TEST_UTIL = $(OBJ_DIR)/$(TEST_DIR)/utility.o

build: $(BIN_DIR)/$(APP_NAME)

$(BIN_DIR)/$(APP_NAME): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $^ $(CFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(HDRS)
	@mkdir -p $(OBJ_DIR)
	$(CC) -c -o $@ $< $(CFLAGS)

test: $(TEST_BINS)

$(BIN_DIR)/$(TEST_DIR)/%: $(OBJ_DIR)/$(TEST_DIR)/%.o $(TEST_UTIL) $(TEST_DIR)/%.txt
	@mkdir -p $(BIN_DIR)/$(TEST_DIR)
	@$(CC) -o $@ $< $(TEST_UTIL)
# run the binary and verify output
# continue other tests even on fail
	@-$@ | diff $(TEST_DIR)/$*.txt -

$(OBJ_DIR)/$(TEST_DIR)/%.o: $(TEST_DIR)/%.orb build
	@mkdir -p $(OBJ_DIR)/$(TEST_DIR)
	@$(BIN_DIR)/$(APP_NAME) $< $@

$(TEST_UTIL): $(TEST_DIR)/utility.cpp
	@mkdir -p $(OBJ_DIR)/$(TEST_DIR)
	@$(CC) -c -o $(OBJ_DIR)/$(TEST_DIR)/utility.o $(TEST_DIR)/utility.cpp

clean_test:
	rm -rf $(OBJ_DIR)/$(TEST_DIR) $(BIN_DIR)/$(TEST_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
