#ifndef __ARHA_FLIPPERAPP_DEMO
#define __ARHA_FLIPPERAPP_DEMO

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <dialogs/dialogs.h>
#include <dolphin/dolphin.h>
#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdlib.h>
#include <storage/storage.h>

#include "capture_state.h"
#include "sump.h"
#include "usb_uart.h"

#define TAG "LogicAnalyzer"

#define TIMER_HZ   50
#define TIMEOUT    3
#define QUEUE_SIZE 32

typedef enum {
    KeyNone,
    KeyUp,
    KeyRight,
    KeyDown,
    KeyLeft,
    KeyOK
} KeyCode;

typedef enum {
    EventKeyPress,
    EventBufferFilled
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
    size_t capture_count;
} AppEvent;

typedef struct {
    size_t sample_count;
    size_t posttrigger_count;
    uint32_t divider;
    uint8_t trigger_stage_count;
    uint8_t trigger_mask[SUMP_TRIGGER_STAGE_COUNT];
    uint8_t trigger_values[SUMP_TRIGGER_STAGE_COUNT];
} CaptureConfig;

typedef enum {
    InputPullFloat,
    InputPullDown,
    InputPullUp,
} InputPullMode;

typedef struct {
    FuriMessageQueue* event_queue;
    NotificationApp* notification;
    Storage* storage;
    ViewPort* view_port;
    Gui* gui;
    DialogsApp* dialogs;
    UsbUart* uart;
    Sump* sump;

    FuriMutex* mutex;
    FuriSemaphore* arm_display_sem;
    bool processing;
    bool capture_active;
    bool test_clock_enabled;
    InputPullMode input_pull;

    FuriThread* capture_thread;
    uint8_t* capture_buffer;
    size_t capture_capacity;
    size_t heap_free_before_capture;
    size_t heap_max_block_before_capture;
    CaptureConfig pending_capture;
    CaptureConfig active_capture;
    size_t last_capture_count;
    uint8_t current_levels;

} AppFSM;

#endif
