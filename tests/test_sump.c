#include "sump.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

static void test_configuration_and_arm_are_reported_together(void) {
    Sump* sump = sump_alloc(85664);
    assert(sump);

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
    sump_free(sump);
}

static void test_lifecycle_command_uses_last_command_in_packet(void) {
    Sump* sump = sump_alloc(85664);
    assert(sump);

    uint8_t commands[] = {SUMP_CMD_RESET, SUMP_CMD_ARM, SUMP_CMD_FINISH_NOW};
    SumpHandleResult result = sump_handle(sump, commands, sizeof(commands));

    assert(result.capture_command == SumpCaptureCommandFinish);
    sump_free(sump);
}

static void test_rate_and_buffer_bounds_are_enforced(void) {
    Sump* sump = sump_alloc(85664);
    assert(sump);

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
    assert(SUMP_MAX_SAMPLE_RATE_HZ == 200000U);
    assert(SUMP_MIN_DIVIDER == 499U);
    assert(sump->divider == SUMP_MIN_DIVIDER);
    assert(sump->read_count == 85664);
    assert(sump->delay_count == 4U * (UINT16_MAX + 1UL));
    sump_free(sump);
}

static void test_all_trigger_stages_are_parsed(void) {
    Sump* sump = sump_alloc(85664);
    assert(sump);

    uint8_t commands[] = {
        0xC0, 0x01, 0x00, 0x00, 0x00, 0xC4, 0x02, 0x00, 0x00, 0x00,
        0xC8, 0x04, 0x00, 0x00, 0x00, 0xCC, 0x08, 0x00, 0x00, 0x00,
    };
    SumpHandleResult result = sump_handle(sump, commands, sizeof(commands));

    assert(result.consumed == sizeof(commands));
    assert(sump->trig_mask[0] == 0x01);
    assert(sump->trig_mask[1] == 0x02);
    assert(sump->trig_mask[2] == 0x04);
    assert(sump->trig_mask[3] == 0x08);
    sump_free(sump);
}

static void test_invalid_trigger_fields_are_ignored(void) {
    Sump* sump = sump_alloc(85664);
    assert(sump);

    uint8_t commands[] = {
        0xC3,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0xCF,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
    };
    SumpHandleResult result = sump_handle(sump, commands, sizeof(commands));

    assert(result.consumed == sizeof(commands));
    for(size_t stage = 0; stage < SUMP_TRIGGER_STAGE_COUNT; stage++) {
        assert(sump->trig_mask[stage] == 0);
        assert(sump->trig_values[stage] == 0);
        assert(sump->trig_config[stage] == 0);
    }
    sump_free(sump);
}

static void test_replies_are_deferred(void) {
    Sump* sump = sump_alloc(85664);
    assert(sump);

    uint8_t commands[] = {SUMP_CMD_QUERY_ID, SUMP_CMD_GET_METADATA};
    SumpHandleResult result = sump_handle(sump, commands, sizeof(commands));
    assert(result.replies == (SumpReplyId | SumpReplyMetadata));

    uint8_t reply[SUMP_REPLY_BUFFER_SIZE];
    assert(sump_write_id(reply, sizeof(reply)) == 4);
    assert(memcmp(reply, "1ALS", 4) == 0);
    size_t reply_size = sump_write_metadata(sump, reply, sizeof(reply));
    assert(reply_size > 4);

    bool found_sample_rate = false;
    for(size_t pos = 0; pos + 4U < reply_size; pos++) {
        if(reply[pos] != 0x23) continue;
        assert(reply[pos + 1U] == 0x00);
        assert(reply[pos + 2U] == 0x03);
        assert(reply[pos + 3U] == 0x0D);
        assert(reply[pos + 4U] == 0x40);
        found_sample_rate = true;
        break;
    }
    assert(found_sample_rate);
    sump_free(sump);
}

int main(void) {
    test_configuration_and_arm_are_reported_together();
    test_lifecycle_command_uses_last_command_in_packet();
    test_rate_and_buffer_bounds_are_enforced();
    test_all_trigger_stages_are_parsed();
    test_invalid_trigger_fields_are_ignored();
    test_replies_are_deferred();
    return 0;
}
