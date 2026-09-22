#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t deadline;
    uint32_t whole_cycles;
    uint32_t fractional_cycles;
    uint32_t fractional_accumulator;
} CaptureClock;

typedef struct {
    uint8_t* buffer;
    size_t sample_count;
    size_t pretrigger_count;
    size_t posttrigger_count;
    size_t pretrigger_head;
    size_t pretrigger_filled;
    size_t posttrigger_filled;
    bool triggered;
    bool finalized;
} CaptureState;

void capture_clock_start(
    CaptureClock* clock,
    uint32_t divider,
    uint32_t cpu_cycles_per_microsecond,
    uint32_t first_sample_cycle);

uint32_t capture_clock_advance(CaptureClock* clock);

bool capture_clock_restart_after_overrun(CaptureClock* clock, uint32_t current_cycle);

size_t capture_posttrigger_count(
    size_t sample_count,
    size_t requested_posttrigger_count,
    bool has_trigger);

bool capture_state_init(
    CaptureState* state,
    uint8_t* buffer,
    size_t sample_count,
    size_t posttrigger_count);

void capture_state_add_pretrigger(CaptureState* state, uint8_t sample);

void capture_state_trigger(CaptureState* state);

bool capture_state_add_posttrigger(CaptureState* state, uint8_t sample);

void capture_state_finish(CaptureState* state);

size_t capture_state_progress(const CaptureState* state);
