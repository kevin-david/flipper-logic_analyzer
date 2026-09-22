

#include "logic_analyzer_app.h"

#include <furi_hal_cortex.h>

#define COUNT(x)      ((size_t)(sizeof(x) / sizeof((x)[0])))
#define SUMP_CLOCK_HZ 100000000U

typedef enum {
    CaptureEventStop = (1 << 0),
    CaptureEventArm = (1 << 1),
    CaptureEventFinish = (1 << 2),
    CaptureEventAbort = (1 << 3),
} CaptureEventFlags;

#define CAPTURE_ALL_EVENTS \
    (CaptureEventStop | CaptureEventArm | CaptureEventFinish | CaptureEventAbort)

static void render_callback(Canvas* const canvas, void* cb_ctx);
static uint8_t levels_get(AppFSM* app);

static const GpioPin* gpios[] = {
    &gpio_ext_pc0,
    &gpio_ext_pc1,
    &gpio_ext_pc3,
    &gpio_ext_pb2,
    &gpio_ext_pb3,
    &gpio_ext_pa4,
    &gpio_ext_pa6,
    &gpio_ext_pa7};

static void render_callback(Canvas* const canvas, void* cb_ctx) {
    AppFSM* app = cb_ctx;
    if(app == NULL) {
        return;
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    if(!app->processing) {
        furi_mutex_release(app->mutex);
        return;
    }

    char buffer[64];
    app->current_levels = levels_get(app);
    canvas_draw_frame(canvas, 0, 0, 128, 64);
    canvas_set_font(canvas, FontKeyboard);
    snprintf(
        buffer,
        sizeof(buffer),
        "A7:%u A6:%u A4:%u B3:%u",
        (app->current_levels >> 7) & 1,
        (app->current_levels >> 6) & 1,
        (app->current_levels >> 5) & 1,
        (app->current_levels >> 4) & 1);
    canvas_draw_str_aligned(canvas, 3, 9, AlignLeft, AlignBottom, buffer);
    snprintf(
        buffer,
        sizeof(buffer),
        "B2:%u C3:%u C1:%u C0:%u",
        (app->current_levels >> 3) & 1,
        (app->current_levels >> 2) & 1,
        (app->current_levels >> 1) & 1,
        (app->current_levels >> 0) & 1);
    canvas_draw_str_aligned(canvas, 3, 18, AlignLeft, AlignBottom, buffer);

    uint32_t rx_count = 0;
    uint32_t tx_count = 0;
    if(app->uart) {
        UsbUartState st;
        usb_uart_get_state(app->uart, &st);
        rx_count = st.rx_cnt;
        tx_count = st.tx_cnt;
    }
    snprintf(
        buffer,
        sizeof(buffer),
        "G:%02X RX:%04lX TX:%04lX",
        app->current_levels,
        (unsigned long)(rx_count & 0xFFFF),
        (unsigned long)(tx_count & 0xFFFF));
    canvas_draw_str_aligned(canvas, 3, 28, AlignLeft, AlignBottom, buffer);

    if(app->sump) {
        if(app->capture_active) {
            if(app->triggered) {
                snprintf(
                    buffer,
                    sizeof(buffer),
                    "CAP %u/%lu F:%02X",
                    app->capture_pos,
                    (unsigned long)app->active_capture.sample_count,
                    app->sump->flags);
            } else {
                snprintf(buffer, sizeof(buffer), "WAIT TRIG OK:END");
            }
        } else if(app->last_capture_count) {
            snprintf(
                buffer,
                sizeof(buffer),
                "SENT %u F:%02X",
                (unsigned int)app->last_capture_count,
                app->sump->flags);
        } else {
            snprintf(buffer, sizeof(buffer), "READY F:%02X", app->sump->flags);
        }
        canvas_draw_str_aligned(canvas, 3, 38, AlignLeft, AlignBottom, buffer);

        uint32_t sample_rate = SUMP_CLOCK_HZ / (app->sump->divider + 1U);
        if(sample_rate >= 1000) {
            snprintf(
                buffer,
                sizeof(buffer),
                "RATE:%lukHz N:%lu",
                (unsigned long)(sample_rate / 1000),
                (unsigned long)app->sump->read_count);
        } else {
            snprintf(
                buffer,
                sizeof(buffer),
                "RATE:%luHz N:%lu",
                (unsigned long)sample_rate,
                (unsigned long)app->sump->read_count);
        }
        canvas_draw_str_aligned(canvas, 3, 48, AlignLeft, AlignBottom, buffer);

        snprintf(
            buffer,
            sizeof(buffer),
            "D:%06lX M:%02lX V:%02lX",
            (unsigned long)(app->sump->divider & 0xFFFFFF),
            (unsigned long)(app->sump->trig_mask[0] & 0xFF),
            (unsigned long)(app->sump->trig_values[0] & 0xFF));
        canvas_draw_str_aligned(canvas, 3, 58, AlignLeft, AlignBottom, buffer);
    }

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input_event, void* event_queue) {
    furi_assert((FuriMessageQueue*)event_queue);

    /* better skip than sorry */
    if(furi_message_queue_get_count((FuriMessageQueue*)event_queue) < QUEUE_SIZE) {
        AppEvent event = {.type = EventKeyPress, .input = *input_event};
        furi_message_queue_put((FuriMessageQueue*)event_queue, &event, 100);
    }
}

static bool message_process(AppFSM* app) {
    bool processing = true;
    AppEvent event;
    FuriStatus event_status = furi_message_queue_get(app->event_queue, &event, 100);

    if(event_status != FuriStatusOk) {
        return true;
    }

    switch(event.type) {
    case EventKeyPress: {
        if(event.input.type != InputTypePress) {
            break;
        }

        switch(event.input.key) {
        case InputKeyUp:
            break;

        case InputKeyDown:
            break;

        case InputKeyRight:
            break;

        case InputKeyLeft:
            break;

        case InputKeyOk:
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            bool capture_active = app->capture_active;
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            if(capture_active) {
                furi_thread_flags_set(furi_thread_get_id(app->capture_thread), CaptureEventFinish);
            }
            break;

        case InputKeyBack:
            processing = false;
            break;

        default:
            break;
        }

        break;
    }

    case EventBufferFilled: {
        usb_uart_tx_data(app->uart, app->capture_buffer, event.capture_count);
        break;
    }

    default: {
        break;
    }
    }

    return processing;
}

size_t data_received(void* ctx, uint8_t* data, size_t length) {
    AppFSM* app = (AppFSM*)ctx;

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    SumpHandleResult result = sump_handle(app->sump, data, length);

    if(result.capture_command == SumpCaptureCommandArm) {
        size_t sample_count = app->sump->read_count;
        size_t posttrigger_count = sample_count;
        uint8_t trigger_stage_count = 1;
        bool has_trigger = false;
        for(uint8_t stage = 0; stage < SUMP_TRIGGER_STAGE_COUNT; stage++) {
            if(app->sump->trig_config[stage] & SUMP_TRIGGER_START_MASK) {
                trigger_stage_count = stage + 1U;
                break;
            }
        }
        for(uint8_t stage = 0; stage < trigger_stage_count; stage++) {
            has_trigger = has_trigger || (app->sump->trig_mask[stage] != 0);
        }

        posttrigger_count = capture_posttrigger_count(
            sample_count, app->sump->delay_count, trigger_stage_count, has_trigger);

        app->pending_capture = (CaptureConfig){
            .sample_count = sample_count,
            .posttrigger_count = posttrigger_count,
            .divider = app->sump->divider,
            .trigger_stage_count = trigger_stage_count,
        };
        for(uint8_t stage = 0; stage < trigger_stage_count; stage++) {
            app->pending_capture.trigger_mask[stage] = app->sump->trig_mask[stage] & 0xFF;
            app->pending_capture.trigger_values[stage] = app->sump->trig_values[stage] & 0xFF;
        }
        app->last_capture_count = 0;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    switch(result.capture_command) {
    case SumpCaptureCommandArm:
        furi_thread_flags_set(furi_thread_get_id(app->capture_thread), CaptureEventArm);
        break;
    case SumpCaptureCommandFinish:
        furi_thread_flags_set(furi_thread_get_id(app->capture_thread), CaptureEventFinish);
        break;
    case SumpCaptureCommandAbort:
        furi_thread_flags_set(furi_thread_get_id(app->capture_thread), CaptureEventAbort);
        break;
    case SumpCaptureCommandNone:
        break;
    }

    return result.consumed;
}

void tx_sump_tx(void* ctx, uint8_t* data, size_t length) {
    AppFSM* app = (AppFSM*)ctx;

    usb_uart_tx_data(app->uart, data, length);
}

static uint8_t levels_get(AppFSM* app) {
    UNUSED(app);
    uint32_t port_a = GPIOA->IDR;
    uint32_t port_b = GPIOB->IDR;
    uint32_t port_c = GPIOC->IDR;

    /*   7  6  5  4  3  2  1  0
      A7 A6 A4 B3 B2 C3 C1 C0 */

    uint8_t ret = (port_a & 0xC0) | ((port_a & 0x10) << 1) | ((port_b & 0x0C) << 1) |
                  ((port_c & 0x08) >> 1) | (port_c & 0x03);

    return ret;
}

static void capture_set_inactive(AppFSM* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->capture_active = false;
    app->triggered = false;
    app->capture_pos = 0;
    app->sump->armed = false;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void capture_complete(AppFSM* app, CaptureState* capture) {
    size_t captured_count = capture_state_progress(capture);
    capture_state_finish(capture);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->capture_active = false;
    app->triggered = capture->triggered;
    app->capture_pos = captured_count;
    app->last_capture_count = captured_count;
    app->sump->armed = false;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    AppEvent event = {
        .type = EventBufferFilled,
        .capture_count = capture->sample_count,
    };
    furi_message_queue_put(app->event_queue, &event, 100);
}

static int32_t capture_thread_worker(void* context) {
    AppFSM* app = (AppFSM*)context;
    CaptureState capture = {0};
    CaptureClock clock = {0};
    bool active = false;
    size_t status_counter = 0;
    uint8_t trigger_stage = 0;

    while(true) {
        uint32_t events = furi_thread_flags_wait(
            CAPTURE_ALL_EVENTS, FuriFlagWaitAny, active ? 0 : FuriWaitForever);
        if(events == FuriFlagErrorTimeout) {
            events = 0;
        } else {
            furi_check(!(events & FuriFlagError));
        }

        if(events & CaptureEventStop) {
            break;
        }
        if(events & CaptureEventAbort) {
            active = false;
            capture_set_inactive(app);
            continue;
        }
        if((events & CaptureEventFinish) && active) {
            capture_complete(app, &capture);
            active = false;
            continue;
        }
        if(events & CaptureEventArm) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            app->active_capture = app->pending_capture;
            CaptureConfig config = app->active_capture;
            app->capture_active = true;
            app->triggered = false;
            app->capture_pos = 0;
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

            furi_check(capture_state_init(
                &capture, app->capture_buffer, config.sample_count, config.posttrigger_count));
            capture_clock_start(
                &clock,
                config.divider,
                furi_hal_cortex_instructions_per_microsecond(),
                DWT->CYCCNT);
            active = true;
            status_counter = 0;
            trigger_stage = 0;
        }

        if(!active) {
            continue;
        }

        uint8_t sample = levels_get(app);
        uint8_t trigger_mask = app->active_capture.trigger_mask[trigger_stage];
        bool trigger_matches =
            (trigger_mask == 0) ||
            ((sample & trigger_mask) ==
             (app->active_capture.trigger_values[trigger_stage] & trigger_mask));

        if(!capture.triggered) {
            if(trigger_matches) {
                trigger_stage++;
                if(trigger_stage >= app->active_capture.trigger_stage_count) {
                    capture_state_trigger(&capture);
                    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
                    app->triggered = true;
                    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
                } else {
                    capture_state_add_pretrigger(&capture, sample);
                }
            } else {
                capture_state_add_pretrigger(&capture, sample);
            }
        }

        bool completed = capture.triggered && capture_state_add_posttrigger(&capture, sample);
        status_counter++;
        if((status_counter & 0x3FFU) == 0) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            app->capture_pos = capture_state_progress(&capture);
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        }
        if(completed) {
            capture_complete(app, &capture);
            active = false;
            continue;
        }

        uint32_t deadline = capture_clock_advance(&clock);
        while((int32_t)(DWT->CYCCNT - deadline) < 0) {
            __NOP();
        }
    }

    return 0;
}

static bool app_init(AppFSM* const app) {
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    if(!app->mutex) {
        FURI_LOG_E(TAG, "cannot create mutex\r\n");
        return false;
    }

    app->processing = true;

    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);

    app->view_port = view_port_alloc();
    app->event_queue = furi_message_queue_alloc(QUEUE_SIZE, sizeof(AppEvent));
    if(!app->view_port || !app->event_queue) {
        return false;
    }

    view_port_draw_callback_set(app->view_port, render_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app->event_queue);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->sump = sump_alloc();
    if(!app->sump) {
        return false;
    }
    app->sump->tx_data = tx_sump_tx;
    app->sump->tx_data_ctx = app;

    app->capture_buffer = malloc(MAX_SAMPLE_MEM);
    if(!app->capture_buffer) {
        return false;
    }

    for(size_t io = 0; io < COUNT(gpios); io++) {
        furi_hal_gpio_init(gpios[io], GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    }

    app->capture_thread = furi_thread_alloc_ex("capture_thread", 1024, capture_thread_worker, app);
    if(!app->capture_thread) {
        return false;
    }
    furi_thread_start(app->capture_thread);

    UsbUartConfig uart_config = {
        .rx_data = &data_received,
        .rx_data_ctx = app,
    };
    app->uart = usb_uart_enable(&uart_config);
    if(!app->uart) {
        return false;
    }

    return true;
}

static void app_deinit(AppFSM* const app) {
    if(app->mutex) {
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        app->processing = false;
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    }
    if(app->view_port) {
        view_port_enabled_set(app->view_port, false);
        if(app->gui) {
            gui_remove_view_port(app->gui, app->view_port);
        }
    }

    if(app->uart) {
        usb_uart_disable(app->uart);
    }

    if(app->capture_thread) {
        furi_thread_flags_set(furi_thread_get_id(app->capture_thread), CaptureEventStop);
        furi_thread_join(app->capture_thread);
        furi_thread_free(app->capture_thread);
    }

    if(app->view_port) view_port_free(app->view_port);
    if(app->event_queue) furi_message_queue_free(app->event_queue);
    if(app->mutex) furi_mutex_free(app->mutex);
    free(app->capture_buffer);
    if(app->sump) sump_free(app->sump);

    if(app->storage) furi_record_close(RECORD_STORAGE);
    if(app->dialogs) furi_record_close(RECORD_DIALOGS);
    if(app->gui) furi_record_close(RECORD_GUI);
    if(app->notification) furi_record_close(RECORD_NOTIFICATION);
}

int32_t logic_analyzer_app_main(void* p) {
    UNUSED(p);

    AppFSM* app = calloc(1, sizeof(AppFSM));
    if(!app) {
        return -1;
    }
    if(!app_init(app)) {
        app_deinit(app);
        free(app);
        return -1;
    }

    dolphin_deed(DolphinDeedPluginGameStart);
    notification_message_block(app->notification, &sequence_display_backlight_enforce_on);

    bool processing = true;
    while(processing) {
        processing = message_process(app);
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        app->processing = processing;
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

        if(processing) view_port_update(app->view_port);
    }

    notification_message_block(app->notification, &sequence_display_backlight_enforce_auto);

    app_deinit(app);
    free(app);

    return 0;
}
