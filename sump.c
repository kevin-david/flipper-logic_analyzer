
#include <furi.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "sump.h"

size_t sump_write_id(uint8_t* buffer, size_t buffer_size) {
    if(!buffer || buffer_size < 4U) return 0;
    memcpy(buffer, "1ALS", 4U);
    return 4U;
}

size_t sump_write_metadata(const Sump* sump, uint8_t* buffer, size_t buffer_size) {
    if(!sump || !buffer || buffer_size < SUMP_REPLY_BUFFER_SIZE) return 0;

    uint8_t* buf = buffer;
    size_t pos = 0;

    const char* name = "Flipper LogicAnalyzer v1.0 (originally by g3gg0.de)";
    const char* fpga = "(none)";
    const char* firmware = "v0.99.1";
    const uint8_t probes = 8;
    uint32_t max_sample_rate = SUMP_MAX_SAMPLE_RATE_HZ;
    uint32_t max_sample_mem = sump->max_sample_count;

    /* 0x01 	device name (e.g. "Openbench Logic Sniffer v1.0", "Bus Pirate
   * v3b"  */
    buf[pos++] = 0x01;
    strcpy((char*)&buf[pos], name);
    pos += strlen(name) + 1;

    /* 0x02 	Version of the FPGA firmware */
    buf[pos++] = 0x02;
    strcpy((char*)&buf[pos], fpga);
    pos += strlen(fpga) + 1;

    /* 0x03 	Ancillary version (PIC firmware) */
    buf[pos++] = 0x03;
    strcpy((char*)&buf[pos], firmware);
    pos += strlen(firmware) + 1;

    /* 0x40	Number of usable probes (short) */
    buf[pos++] = 0x40;
    buf[pos++] = probes;

    /* 0x41 	Protocol version (short) */
    buf[pos++] = 0x41;
    buf[pos++] = 0x02;

    /* 0x21 	Amount of sample memory available (bytes) */
    buf[pos++] = 0x21;
    buf[pos++] = (max_sample_mem >> 24) & 0xFF;
    buf[pos++] = (max_sample_mem >> 16) & 0xFF;
    buf[pos++] = (max_sample_mem >> 8) & 0xFF;
    buf[pos++] = (max_sample_mem >> 0) & 0xFF;

    /* 0x23 	Maximum sample rate (hz)  */
    buf[pos++] = 0x23;
    buf[pos++] = (max_sample_rate >> 24) & 0xFF;
    buf[pos++] = (max_sample_rate >> 16) & 0xFF;
    buf[pos++] = (max_sample_rate >> 8) & 0xFF;
    buf[pos++] = (max_sample_rate >> 0) & 0xFF;

    /* 0x00 	not used, key means end of metadata*/
    buf[pos++] = 0x00;

    return pos;
}

static uint32_t get_word(const uint8_t* data) {
    return ((uint32_t)data[3] << 24) | ((uint32_t)data[2] << 16) | ((uint32_t)data[1] << 8) |
           (uint32_t)data[0];
}

static bool sump_handle_trigger_command(Sump* sump, uint8_t command, uint32_t extra) {
    if(command < SUMP_CMD_TRIGGER_MASK || command > 0xCE) {
        return false;
    }

    uint8_t offset = command - SUMP_CMD_TRIGGER_MASK;
    uint8_t stage = offset / 4U;
    uint8_t field = offset % 4U;
    if(stage >= SUMP_TRIGGER_STAGE_COUNT || field > 2U) {
        return false;
    }

    if(field == 0U) {
        sump->trig_mask[stage] = extra;
    } else if(field == 1U) {
        sump->trig_values[stage] = extra;
    } else {
        sump->trig_config[stage] = extra;
    }
    return true;
}

SumpHandleResult sump_handle(Sump* sump, const uint8_t* data, size_t length) {
    size_t pos = 0;
    SumpCaptureCommand capture_command = SumpCaptureCommandNone;
    uint8_t replies = SumpReplyNone;

    while(pos < length) {
        uint8_t command = data[pos];
        uint32_t extra = 0;

        if(command & 0x80) {
            if(length - pos < 5) {
                return (SumpHandleResult){
                    .consumed = pos,
                    .capture_command = capture_command,
                    .replies = replies,
                };
            }
            pos++;
            extra = get_word(&data[pos]);
            pos += 4;
        } else {
            pos++;
        }

        if(sump_handle_trigger_command(sump, command, extra)) {
            continue;
        }

        switch(command) {
        case SUMP_CMD_RESET:
            memset(sump->trig_mask, 0, sizeof(sump->trig_mask));
            memset(sump->trig_values, 0, sizeof(sump->trig_values));
            memset(sump->trig_config, 0, sizeof(sump->trig_config));
            capture_command = SumpCaptureCommandAbort;
            break;

        case SUMP_CMD_ARM:
            capture_command = SumpCaptureCommandArm;
            break;

        case SUMP_CMD_QUERY_ID:
            replies |= SumpReplyId;
            break;

        case SUMP_CMD_SELF_TEST:
            break;

        case SUMP_CMD_GET_METADATA:
            replies |= SumpReplyMetadata;
            break;

        case SUMP_CMD_FINISH_NOW:
            capture_command = SumpCaptureCommandFinish;
            break;

        case SUMP_CMD_XON:
            break;

        case SUMP_CMD_XOFF:
            break;

        case SUMP_CMD_SET_READ_DELAY_COUNT:
            sump->read_count = 4 * ((extra & 0xFFFF) + 1);
            if(sump->read_count > sump->max_sample_count) {
                sump->read_count = sump->max_sample_count;
            }
            sump->delay_count = 4 * ((extra >> 16) + 1);
            break;

        case SUMP_CMD_SET_FLAGS:
            sump->flags = extra & 0xFF;
            break;

        case SUMP_CMD_SET_DIVIDER:
            sump->divider = extra & 0xFFFFFF;
            if(sump->divider < SUMP_MIN_DIVIDER) {
                sump->divider = SUMP_MIN_DIVIDER;
            }
            break;

        default:
            break;
        }
    }

    return (SumpHandleResult){
        .consumed = pos,
        .capture_command = capture_command,
        .replies = replies,
    };
}

Sump* sump_alloc(uint32_t max_sample_count) {
    if(max_sample_count < 4U || max_sample_count > SUMP_MAX_SAMPLE_COUNT ||
       (max_sample_count & 0x3U) != 0U) {
        return NULL;
    }

    Sump* sump = calloc(1, sizeof(Sump));
    if(!sump) {
        return NULL;
    }

    sump->max_sample_count = max_sample_count;
    sump->read_count = max_sample_count;
    sump->delay_count = max_sample_count;
    sump->divider = SUMP_MIN_DIVIDER;

    return sump;
}

void sump_free(Sump* sump) {
    free(sump);
}
