# Compiler and Flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O3 -g -IInclude -MMD -MP
LDFLAGS = 

# Directories
SRC_DIR = Source
INC_DIR = Include
BUILD_DIR = Build
TARGET = vanarize
TEST_TARGET = run_tests

# Source Files
# Recursively find all .c files in Source/
SRCS := $(shell find $(SRC_DIR) -name '*.c')

# Object Files
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# Main Target
all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Compilation Rule
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Test Target (Placeholder)
test: $(TEST_TARGET)
	./$(TEST_TARGET)

# Clean
clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TEST_TARGET)

# Phony Targets
.PHONY: all clean test

# Include Dependencies
-include $(DEPS)
