#include "capture_state.h"

#include <string.h>

#define SUMP_CLOCK_MHZ 100U

static void reverse_samples(uint8_t* buffer, size_t begin, size_t end) {
    while(begin < end) {
        end--;
        if(begin >= end) {
            break;
        }

        uint8_t sample = buffer[begin];
        buffer[begin] = buffer[end];
        buffer[end] = sample;
        begin++;
    }
}

static void capture_state_linearize_pretrigger(CaptureState* state) {
    if(state->pretrigger_count == 0) {
        return;
    }

    if(state->pretrigger_filled < state->pretrigger_count) {
        size_t padding = state->pretrigger_count - state->pretrigger_filled;
        memmove(
            &state->buffer[padding], state->buffer, state->pretrigger_filled * sizeof(uint8_t));
        memset(state->buffer, 0, padding * sizeof(uint8_t));
    } else if(state->pretrigger_head != 0) {
        reverse_samples(state->buffer, 0, state->pretrigger_head);
        reverse_samples(state->buffer, state->pretrigger_head, state->pretrigger_count);
        reverse_samples(state->buffer, 0, state->pretrigger_count);
    }
}

void capture_clock_start(
    CaptureClock* clock,
    uint32_t divider,
    uint32_t cpu_cycles_per_microsecond,
    uint32_t first_sample_cycle) {
    uint64_t period_numerator = (uint64_t)(divider + 1U) * cpu_cycles_per_microsecond;

    clock->deadline = first_sample_cycle;
    clock->whole_cycles = period_numerator / SUMP_CLOCK_MHZ;
    clock->fractional_cycles = period_numerator % SUMP_CLOCK_MHZ;
    clock->fractional_accumulator = 0;
}

uint32_t capture_clock_advance(CaptureClock* clock) {
    clock->deadline += clock->whole_cycles;
    clock->fractional_accumulator += clock->fractional_cycles;
    if(clock->fractional_accumulator >= SUMP_CLOCK_MHZ) {
        clock->deadline++;
        clock->fractional_accumulator -= SUMP_CLOCK_MHZ;
    }

    return clock->deadline;
}

size_t capture_posttrigger_count(
    size_t sample_count,
    size_t requested_posttrigger_count,
    uint8_t trigger_stage_count,
    bool has_trigger) {
    if(!has_trigger) {
        return sample_count;
    }

    size_t stage_adjusted_count = requested_posttrigger_count + trigger_stage_count;
    if(stage_adjusted_count < requested_posttrigger_count || stage_adjusted_count > sample_count) {
        return sample_count;
    }
    return stage_adjusted_count;
}

bool capture_state_init(
    CaptureState* state,
    uint8_t* buffer,
    size_t sample_count,
    size_t posttrigger_count) {
    if(!state || !buffer || sample_count == 0 || posttrigger_count == 0 ||
       posttrigger_count > sample_count) {
        return false;
    }

    *state = (CaptureState){
        .buffer = buffer,
        .sample_count = sample_count,
        .pretrigger_count = sample_count - posttrigger_count,
        .posttrigger_count = posttrigger_count,
    };
    return true;
}

void capture_state_add_pretrigger(CaptureState* state, uint8_t sample) {
    if(state->triggered || state->pretrigger_count == 0) {
        return;
    }

    state->buffer[state->pretrigger_head] = sample;
    state->pretrigger_head = (state->pretrigger_head + 1U) % state->pretrigger_count;
    if(state->pretrigger_filled < state->pretrigger_count) {
        state->pretrigger_filled++;
    }
}

void capture_state_trigger(CaptureState* state) {
    if(state->triggered) {
        return;
    }

    state->triggered = true;
}

bool capture_state_add_posttrigger(CaptureState* state, uint8_t sample) {
    if(!state->triggered || state->posttrigger_filled >= state->posttrigger_count) {
        return state->posttrigger_filled >= state->posttrigger_count;
    }

    state->buffer[state->pretrigger_count + state->posttrigger_filled] = sample;
    state->posttrigger_filled++;
    return state->posttrigger_filled >= state->posttrigger_count;
}

void capture_state_finish(CaptureState* state) {
    if(state->finalized) {
        return;
    }

    if(!state->triggered) {
        capture_state_trigger(state);
    }

    // Rotating the pre-trigger ring while sampling would miss timed samples.
    capture_state_linearize_pretrigger(state);
    size_t unwritten_posttrigger = state->posttrigger_count - state->posttrigger_filled;
    memset(
        &state->buffer[state->pretrigger_count + state->posttrigger_filled],
        0,
        unwritten_posttrigger * sizeof(uint8_t));
    reverse_samples(state->buffer, 0, state->sample_count);
    state->finalized = true;
}

size_t capture_state_progress(const CaptureState* state) {
    return state->pretrigger_filled + state->posttrigger_filled;
}
