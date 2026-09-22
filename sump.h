#pragma once

#include <stddef.h>
#include <stdint.h>

#define SUMP_MAX_SAMPLE_COUNT    (4UL * ((uint32_t)UINT16_MAX + 1UL))
#define SUMP_CLOCK_HZ            100000000U
#define SUMP_MAX_SAMPLE_RATE_HZ  200000U
#define SUMP_MIN_DIVIDER         ((SUMP_CLOCK_HZ / SUMP_MAX_SAMPLE_RATE_HZ) - 1U)
#define SUMP_TRIGGER_STAGE_COUNT 4U
#define SUMP_TRIGGER_START_MASK  (1UL << 27)
#define SUMP_REPLY_BUFFER_SIZE   128U

typedef enum {
    SUMP_CMD_RESET = 0x00,
    SUMP_CMD_ARM = 0x01,
    SUMP_CMD_QUERY_ID = 0x02,
    SUMP_CMD_SELF_TEST = 0x03,
    SUMP_CMD_GET_METADATA = 0x04,
    SUMP_CMD_FINISH_NOW = 0x05,
    SUMP_CMD_XON = 0x11,
    SUMP_CMD_XOFF = 0x13,
    SUMP_CMD_SET_DIVIDER = 0x80,
    SUMP_CMD_SET_READ_DELAY_COUNT = 0x81,
    SUMP_CMD_SET_FLAGS = 0x82,
    SUMP_CMD_TRIGGER_MASK = 0xC0,
    SUMP_CMD_TRIGGER_VALUES = 0xC1,
    SUMP_CMD_TRIGGER_CONFIG = 0xC2,
} SumpCommands;

typedef enum {
    SumpCaptureCommandNone,
    SumpCaptureCommandArm,
    SumpCaptureCommandFinish,
    SumpCaptureCommandAbort,
} SumpCaptureCommand;

typedef enum {
    SumpReplyNone = 0,
    SumpReplyId = (1 << 0),
    SumpReplyMetadata = (1 << 1),
} SumpReply;

typedef struct {
    size_t consumed;
    SumpCaptureCommand capture_command;
    uint8_t replies;
} SumpHandleResult;

typedef struct {
    uint8_t flags;
    uint32_t divider;
    uint32_t read_count;
    uint32_t max_sample_count;
    uint32_t delay_count;
    uint32_t trig_mask[SUMP_TRIGGER_STAGE_COUNT];
    uint32_t trig_values[SUMP_TRIGGER_STAGE_COUNT];
    uint32_t trig_config[SUMP_TRIGGER_STAGE_COUNT];
} Sump;

Sump* sump_alloc(uint32_t max_sample_count);

void sump_free(Sump* sump);

SumpHandleResult sump_handle(Sump* sump, const uint8_t* data, size_t length);

size_t sump_write_id(uint8_t* buffer, size_t buffer_size);

size_t sump_write_metadata(const Sump* sump, uint8_t* buffer, size_t buffer_size);
