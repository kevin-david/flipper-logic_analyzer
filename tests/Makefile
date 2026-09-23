CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
BUILD_DIR := .build

.PHONY: test clean

test: $(BUILD_DIR)/capture_state $(BUILD_DIR)/sump
	$(BUILD_DIR)/capture_state
	$(BUILD_DIR)/sump

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/capture_state: ../capture_state.c test_capture_state.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. $^ -o $@

$(BUILD_DIR)/sump: ../sump.c test_sump.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I.. -Istubs $^ -o $@

clean:
	rm -rf $(BUILD_DIR)
