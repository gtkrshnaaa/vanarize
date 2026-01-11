# Compiler and Flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O3 -g -IInclude -MMD -MP
LDFLAGS = -lm

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

# Install
install: clean all
	@echo "Installing $(TARGET) to /usr/local/bin..."
	@cp $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Optimization installed successfully. Run '$(TARGET) -v' to verify."

# Uninstall
uninstall:
	@echo "Uninstalling $(TARGET) from /usr/local/bin..."
	@rm -f /usr/local/bin/$(TARGET)
	@echo "Uninstalled successfully."

# Phony Targets
.PHONY: all clean test install uninstall

# Include Dependencies
-include $(DEPS)

# Source Listing
source_list:
	@mkdir -p ZListing
	@echo "Generating source listing to ZListing/Listing.txt..."
	@rm -f ZListing/Listing.txt
	@echo "VANARIZE PROJECT SOURCE LISTING" >> ZListing/Listing.txt
	@echo "================================================================================" >> ZListing/Listing.txt
	@echo "Contains: Source/, Include/, Examples/, ZDocs/, README.md, and Makefile." >> ZListing/Listing.txt
	@echo "Generated on $$(date)" >> ZListing/Listing.txt
	@echo "================================================================================" >> ZListing/Listing.txt
	@echo "" >> ZListing/Listing.txt
	@for file in Makefile README.md $$(find Source Include Examples ZDocs -type f 2>/dev/null); do \
		echo "================================================================================" >> ZListing/Listing.txt; \
		echo "FILE: $$file" >> ZListing/Listing.txt; \
		echo "================================================================================" >> ZListing/Listing.txt; \
		cat "$$file" >> ZListing/Listing.txt; \
		echo "" >> ZListing/Listing.txt; \
		echo "" >> ZListing/Listing.txt; \
	done
	@echo "Done."
