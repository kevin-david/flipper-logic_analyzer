

#include "logic_analyzer_app.h"

#define COUNT(x)                 ((size_t)(sizeof(x) / sizeof((x)[0])))
#define SUMP_CLOCK_MHZ           100U
#define SUMP_CLOCK_HZ            100000000U
#define CAPTURE_MIN_SAMPLE_COUNT 16384U
#define CAPTURE_HEAP_RESERVE     (32U * 1024U)
#define CAPTURE_ALLOCATION_STEP  (4U * 1024U)

static void render_callback(Canvas* const canvas, void* cb_ctx);

static bool capture_buffer_alloc(AppFSM* app) {
    app->heap_free_before_capture = memmgr_get_free_heap();
    app->heap_max_block_before_capture = memmgr_heap_get_max_free_block();

    size_t target = 0;
    if(app->heap_max_block_before_capture > CAPTURE_HEAP_RESERVE) {
        target = app->heap_max_block_before_capture - CAPTURE_HEAP_RESERVE;
    }
    if(target > SUMP_MAX_SAMPLE_COUNT) {
        target = SUMP_MAX_SAMPLE_COUNT;
    }
    target &= ~(size_t)0x3U;

    while(target >= CAPTURE_MIN_SAMPLE_COUNT) {
        app->capture_buffer = malloc(target);
        if(app->capture_buffer) {
            app->capture_capacity = target;
            FURI_LOG_I(
                TAG,
                "Capture heap: free=%lu max_block=%lu reserve=%u allocated=%lu",
                (unsigned long)app->heap_free_before_capture,
                (unsigned long)app->heap_max_block_before_capture,
                CAPTURE_HEAP_RESERVE,
                (unsigned long)app->capture_capacity);
            return true;
        }

        if(target < CAPTURE_MIN_SAMPLE_COUNT + CAPTURE_ALLOCATION_STEP) {
            break;
        }
        target = (target - CAPTURE_ALLOCATION_STEP) & ~(size_t)0x3U;
    }

    FURI_LOG_E(
        TAG,
        "Capture allocation failed: free=%lu max_block=%lu reserve=%u minimum=%u",
        (unsigned long)app->heap_free_before_capture,
        (unsigned long)app->heap_max_block_before_capture,
        CAPTURE_HEAP_RESERVE,
        CAPTURE_MIN_SAMPLE_COUNT);
    return false;
}

static const GpioPin* gpios[] = {
    &gpio_ext_pc0,
    &gpio_ext_pc1,
    &gpio_ext_pc3,
    &gpio_ext_pb2,
    &gpio_ext_pb3,
    &gpio_ext_pa4,
    &gpio_ext_pa6,
    &gpio_ext_pa7};

//static const char* gpio_names[] = {"PC0", "PC1", "PC3", "PB2", "PB3", "PA4", "PA6", "PA7"};

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
        if(app->sump->armed) {
            if(app->triggered) {
                snprintf(
                    buffer,
                    sizeof(buffer),
                    "CAP %u/%lu F:%02X",
                    app->capture_pos,
                    (unsigned long)app->sump->read_count,
                    app->sump->flags);
            } else {
                snprintf(buffer, sizeof(buffer), "WAIT TRIGGER F:%02X", app->sump->flags);
            }
        } else if(app->last_capture_count) {
            snprintf(
                buffer,
                sizeof(buffer),
                "SENT %u F:%02X",
                (unsigned int)app->last_capture_count,
                app->sump->flags);
        } else {
            snprintf(
                buffer,
                sizeof(buffer),
                "READY M:%luK F:%02X",
                (unsigned long)(app->capture_capacity / 1024U),
                app->sump->flags);
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
            (unsigned long)(app->sump->trig_mask & 0xFF),
            (unsigned long)(app->sump->trig_values & 0xFF));
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
            /* when armed, trigger by pressing the button */
            if(app->sump->armed) {
                for(size_t pos = app->capture_pos; pos < app->sump->read_count; pos++) {
                    app->capture_buffer[app->sump->read_count - 1 - pos] = 0;
                }
                app->sump->armed = false;
                AppEvent event = {.type = EventBufferFilled};
                furi_message_queue_put(app->event_queue, &event, 100);
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
        usb_uart_tx_data(app->uart, app->capture_buffer, app->sump->read_count);
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

    snprintf(
        app->state_string,
        sizeof(app->state_string),
        "Rx: %02x '%c' (total %u)",
        data[0],
        data[0],
        length);

    return sump_handle(app->sump, data, length);
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

static int32_t capture_thread_worker(void* context) {
    AppFSM* app = (AppFSM*)context;
    uint8_t prev_levels = 0;

    while(app->processing) {
        app->current_levels = levels_get(app);

        if(app->sump->armed) {
            uint8_t trigger_mask = app->sump->trig_mask & 0xFF;

            if(!app->triggered) {
                app->triggered =
                    (trigger_mask == 0) ||
                    ((app->current_levels & trigger_mask) != (prev_levels & trigger_mask));
                prev_levels = app->current_levels;

                if(!app->triggered) {
                    furi_delay_us(10);
                    continue;
                }
            }

            uint32_t sample_period_us =
                (app->sump->divider + SUMP_CLOCK_MHZ) / SUMP_CLOCK_MHZ;

            app->capture_buffer[app->sump->read_count - 1 - app->capture_pos++] =
                app->current_levels;

            if(app->capture_pos >= app->sump->read_count) {
                app->last_capture_count = app->capture_pos;
                app->sump->armed = false;
                AppEvent event = {.type = EventBufferFilled};
                furi_message_queue_put(app->event_queue, &event, 100);
            } else {
                furi_delay_us(sample_period_us);
            }
        } else {
            prev_levels = app->current_levels;
            app->capture_pos = 0;
            app->triggered = false;
            furi_delay_ms(50);
        }
    }

    return 0;
}

static bool app_init(AppFSM* const app) {
    strcpy(app->state_string, "none");
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
        FURI_LOG_E(TAG, "cannot allocate UI resources\r\n");
        if(app->view_port) {
            view_port_free(app->view_port);
            app->view_port = NULL;
        }
        if(app->event_queue) {
            furi_message_queue_free(app->event_queue);
            app->event_queue = NULL;
        }
        return false;
    }

    view_port_draw_callback_set(app->view_port, render_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app->event_queue);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    UsbUartConfig uart_config;

    uart_config.vcp_ch = 0;
    uart_config.rx_data = &data_received;
    uart_config.rx_data_ctx = app;

    if(!capture_buffer_alloc(app)) {
        return false;
    }

    app->sump = sump_alloc(app->capture_capacity);
    if(!app->sump) {
        FURI_LOG_E(TAG, "cannot allocate SUMP state\r\n");
        return false;
    }
    app->sump->tx_data = tx_sump_tx;
    app->sump->tx_data_ctx = app;

    app->uart = usb_uart_enable(&uart_config);
    if(!app->uart) {
        FURI_LOG_E(TAG, "cannot enable USB UART\r\n");
        return false;
    }

    for(size_t io = 0; io < COUNT(gpios); io++) {
        furi_hal_gpio_init(gpios[io], GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    }

    app->capture_thread = furi_thread_alloc_ex("capture_thread", 1024, capture_thread_worker, app);
    if(!app->capture_thread) {
        FURI_LOG_E(TAG, "cannot allocate capture thread\r\n");
        return false;
    }
    furi_thread_start(app->capture_thread);

    return true;
}

static void app_deinit(AppFSM* const app) {
    app->processing = false;

    if(app->view_port) {
        view_port_enabled_set(app->view_port, false);
        if(app->gui) {
            gui_remove_view_port(app->gui, app->view_port);
        }
    }

    if(app->capture_thread) {
        furi_thread_join(app->capture_thread);
        furi_thread_free(app->capture_thread);
    }

    if(app->uart) {
        usb_uart_disable(app->uart);
    }

    if(app->view_port) {
        view_port_free(app->view_port);
    }
    if(app->event_queue) {
        furi_message_queue_free(app->event_queue);
    }
    if(app->mutex) {
        furi_mutex_free(app->mutex);
    }

    free(app->capture_buffer);

    if(app->sump) {
        sump_free(app->sump);
    }

    if(app->storage) {
        furi_record_close(RECORD_STORAGE);
    }
    if(app->dialogs) {
        furi_record_close(RECORD_DIALOGS);
    }
    if(app->gui) {
        furi_record_close(RECORD_GUI);
    }
    if(app->notification) {
        furi_record_close(RECORD_NOTIFICATION);
    }
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

    while(app->processing) {
        app->processing = message_process(app);

        view_port_update(app->view_port);
    }

    notification_message_block(app->notification, &sequence_display_backlight_enforce_auto);

    app_deinit(app);
    free(app);

    return 0;
}
