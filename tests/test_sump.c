#include "sump.h"

#include <assert.h>

static void ignore_tx(void* context, uint8_t* data, size_t length) {
    (void)context;
    (void)data;
    (void)length;
}

static void test_configuration_and_arm_are_reported_together(void) {
    Sump* sump = sump_alloc();
    assert(sump);
    sump->tx_data = ignore_tx;

    uint8_t commands[] = {
        SUMP_CMD_SET_READ_DELAY_COUNT,
        0x01,
        0x00,
        0x00,
        0x00,
        SUMP_CMD_ARM,
    };
    SumpHandleResult result = sump_handle(sump, commands, sizeof(commands));

    assert(result.consumed == sizeof(commands));
    assert(result.capture_command == SumpCaptureCommandArm);
    assert(sump->read_count == 8);
    assert(sump->delay_count == 4);
    assert(sump->armed);
    sump_free(sump);
}

static void test_lifecycle_command_uses_last_command_in_packet(void) {
    Sump* sump = sump_alloc();
    assert(sump);
    sump->tx_data = ignore_tx;

    uint8_t commands[] = {SUMP_CMD_RESET, SUMP_CMD_ARM, SUMP_CMD_FINISH_NOW};
    SumpHandleResult result = sump_handle(sump, commands, sizeof(commands));

    assert(result.capture_command == SumpCaptureCommandFinish);
    assert(!sump->armed);
    sump_free(sump);
}

static void test_rate_and_buffer_bounds_are_enforced(void) {
    Sump* sump = sump_alloc();
    assert(sump);
    sump->tx_data = ignore_tx;

    uint8_t commands[] = {
        SUMP_CMD_SET_DIVIDER,
        0x00,
        0x00,
        0x00,
        0x00,
        SUMP_CMD_SET_READ_DELAY_COUNT,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
    };
    SumpHandleResult result = sump_handle(sump, commands, sizeof(commands));

    assert(result.consumed == sizeof(commands));
    assert(sump->divider == SUMP_MIN_DIVIDER);
    assert(sump->read_count == MAX_SAMPLE_MEM);
    assert(sump->delay_count == 4U * (UINT16_MAX + 1UL));
    sump_free(sump);
}

static void test_all_trigger_stages_are_parsed(void) {
    Sump* sump = sump_alloc();
    assert(sump);
    sump->tx_data = ignore_tx;

    uint8_t commands[] = {
        0xC4,
        0x0F,
        0x00,
        0x00,
        0x00,
        0xC5,
        0x05,
        0x00,
        0x00,
        0x00,
        0xC6,
        0x00,
        0x00,
        0x01,
        0x08,
    };
    SumpHandleResult result = sump_handle(sump, commands, sizeof(commands));

    assert(result.consumed == sizeof(commands));
    assert(sump->trig_mask[1] == 0x0F);
    assert(sump->trig_values[1] == 0x05);
    assert(sump->trig_config[1] == 0x08010000);
    sump_free(sump);
}

int main(void) {
    test_configuration_and_arm_are_reported_together();
    test_lifecycle_command_uses_last_command_in_packet();
    test_rate_and_buffer_bounds_are_enforced();
    test_all_trigger_stages_are_parsed();
    return 0;
}
