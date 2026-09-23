#include "capture_state.h"

#include <assert.h>
#include <string.h>

static void test_clock_uses_absolute_fractional_deadlines(void) {
    CaptureClock clock;
    capture_clock_start(&clock, 1000, 64, 100);

    assert(capture_clock_advance(&clock) == 740);
    assert(capture_clock_advance(&clock) == 1381);
    assert(capture_clock_advance(&clock) == 2021);
}

static void test_clock_waits_after_an_overrun(void) {
    CaptureClock clock;
    capture_clock_start(&clock, 999, 64, 100);

    assert(capture_clock_advance(&clock) == 740);
    assert(capture_clock_restart_after_overrun(&clock, 800));
    assert(clock.deadline == 1440);
    assert(!capture_clock_restart_after_overrun(&clock, 1000));
}

static void test_trigger_stages_align_the_host_marker(void) {
    assert(capture_posttrigger_count(100, 40, true) == 40);
    assert(capture_posttrigger_count(100, 101, true) == 100);
    assert(capture_posttrigger_count(100, 40, false) == 100);
}

static void test_immediate_capture_is_reversed_for_sump(void) {
    uint8_t buffer[4] = {0};
    CaptureState state;
    assert(capture_state_init(&state, buffer, sizeof(buffer), sizeof(buffer)));

    capture_state_trigger(&state);
    assert(!capture_state_add_posttrigger(&state, 1));
    assert(!capture_state_add_posttrigger(&state, 2));
    assert(!capture_state_add_posttrigger(&state, 3));
    assert(capture_state_add_posttrigger(&state, 4));
    capture_state_finish(&state);

    const uint8_t expected[] = {4, 3, 2, 1};
    assert(memcmp(buffer, expected, sizeof(expected)) == 0);
}

static void test_pretrigger_ring_keeps_latest_samples(void) {
    uint8_t guarded[7] = {0xA5, 0, 0, 0, 0, 0, 0x5A};
    CaptureState state;
    assert(capture_state_init(&state, &guarded[1], 5, 2));

    capture_state_add_pretrigger(&state, 1);
    capture_state_add_pretrigger(&state, 2);
    capture_state_add_pretrigger(&state, 3);
    capture_state_add_pretrigger(&state, 4);
    capture_state_add_pretrigger(&state, 5);
    capture_state_trigger(&state);
    const uint8_t unrotated[] = {4, 5, 3};
    assert(memcmp(&guarded[1], unrotated, sizeof(unrotated)) == 0);
    assert(!capture_state_add_posttrigger(&state, 6));
    assert(capture_state_add_posttrigger(&state, 7));
    capture_state_finish(&state);

    const uint8_t expected[] = {7, 6, 5, 4, 3};
    assert(memcmp(&guarded[1], expected, sizeof(expected)) == 0);
    assert(guarded[0] == 0xA5);
    assert(guarded[6] == 0x5A);
}

static void test_partial_pretrigger_is_zero_padded(void) {
    uint8_t buffer[5] = {0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    CaptureState state;
    assert(capture_state_init(&state, buffer, sizeof(buffer), 2));

    capture_state_add_pretrigger(&state, 1);
    capture_state_add_pretrigger(&state, 2);
    capture_state_trigger(&state);
    assert(!capture_state_add_posttrigger(&state, 3));
    assert(capture_state_add_posttrigger(&state, 4));
    capture_state_finish(&state);

    const uint8_t expected[] = {4, 3, 2, 1, 0};
    assert(memcmp(buffer, expected, sizeof(expected)) == 0);
}

static void test_manual_finish_pads_unsampled_tail(void) {
    uint8_t buffer[5] = {0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    CaptureState state;
    assert(capture_state_init(&state, buffer, sizeof(buffer), sizeof(buffer)));

    capture_state_trigger(&state);
    assert(!capture_state_add_posttrigger(&state, 1));
    assert(!capture_state_add_posttrigger(&state, 2));
    capture_state_finish(&state);

    const uint8_t expected[] = {0, 0, 0, 2, 1};
    assert(memcmp(buffer, expected, sizeof(expected)) == 0);
    assert(capture_state_progress(&state) == 2);
}

int main(void) {
    test_clock_uses_absolute_fractional_deadlines();
    test_clock_waits_after_an_overrun();
    test_trigger_stages_align_the_host_marker();
    test_immediate_capture_is_reversed_for_sump();
    test_pretrigger_ring_keeps_latest_samples();
    test_partial_pretrigger_is_zero_padded();
    test_manual_finish_pads_unsampled_tail();
    return 0;
}
