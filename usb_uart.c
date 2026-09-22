
#include <stdlib.h>

#include "furi_hal.h"
#include "usb_cdc.h"
#include "usb_uart.h"
#include <furi_hal_usb_cdc.h>

#define USB_CDC_PKT_LEN          CDC_DATA_SZ
#define USB_MODE_SWITCH_DELAY_MS 500
#define USB_ANALYZER_CDC_CHANNEL 1U

#define USB_CDC_BIT_DTR (1 << 0)
#define USB_CDC_BIT_RTS (1 << 1)

typedef enum {
    WorkerEvtStop = (1 << 0),
    WorkerEvtCdcRx = (1 << 1),
} WorkerEvtFlags;

#define WORKER_ALL_EVENTS (WorkerEvtStop | WorkerEvtCdcRx)

struct UsbUart {
    UsbUartConfig cfg;

    FuriThread* thread;
    FuriMutex* usb_mutex;
    FuriSemaphore* tx_sem;
    UsbUartState st;
};

static void vcp_on_cdc_tx_complete(void* context);
static void vcp_on_cdc_rx(void* context);
static void vcp_state_callback(void* context, CdcState state);
static void vcp_on_cdc_control_line(void* context, CdcCtrlLine state);
static void vcp_on_line_config(void* context, struct usb_cdc_line_coding* config);

static const CdcCallbacks cdc_cb = {
    .tx_ep_callback = &vcp_on_cdc_tx_complete,
    .rx_ep_callback = &vcp_on_cdc_rx,
    .state_callback = &vcp_state_callback,
    .ctrl_line_callback = &vcp_on_cdc_control_line,
    .config_callback = &vcp_on_line_config};

static void usb_uart_vcp_init(UsbUart* usb_uart) {
    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_cdc_dual, NULL) == true);
    furi_hal_cdc_set_callbacks(USB_ANALYZER_CDC_CHANNEL, (CdcCallbacks*)&cdc_cb, usb_uart);
}

static void usb_uart_vcp_deinit(void) {
    furi_hal_cdc_set_callbacks(USB_ANALYZER_CDC_CHANNEL, NULL, NULL);
}

void usb_uart_tx_data(UsbUart* usb_uart, uint8_t* data, size_t length) {
    uint32_t pos = 0;
    while(pos < length) {
        size_t pkt_size = length - pos;

        if(pkt_size > USB_CDC_PKT_LEN) {
            pkt_size = USB_CDC_PKT_LEN;
        }

        if(furi_semaphore_acquire(usb_uart->tx_sem, 100) == FuriStatusOk) {
            furi_check(furi_mutex_acquire(usb_uart->usb_mutex, FuriWaitForever) == FuriStatusOk);
            furi_hal_cdc_send(USB_ANALYZER_CDC_CHANNEL, &data[pos], pkt_size);
            usb_uart->st.tx_cnt += pkt_size;
            furi_check(furi_mutex_release(usb_uart->usb_mutex) == FuriStatusOk);
            pos += pkt_size;
        }
    }
}

static int32_t usb_uart_worker(void* context) {
    UsbUart* usb_uart = (UsbUart*)context;

    // Let the loader acknowledge app startup before changing the USB descriptor.
    furi_delay_ms(USB_MODE_SWITCH_DELAY_MS);
    usb_uart_vcp_init(usb_uart);

    uint8_t data[2 * USB_CDC_PKT_LEN];
    size_t remain = 0;

    while(1) {
        uint32_t events =
            furi_thread_flags_wait(WORKER_ALL_EVENTS, FuriFlagWaitAny, FuriWaitForever);
        furi_check(!(events & FuriFlagError));

        if(events & WorkerEvtStop) {
            break;
        }

        if(events & WorkerEvtCdcRx) {
            furi_check(furi_mutex_acquire(usb_uart->usb_mutex, FuriWaitForever) == FuriStatusOk);
            size_t len =
                furi_hal_cdc_receive(USB_ANALYZER_CDC_CHANNEL, &data[remain], USB_CDC_PKT_LEN);
            usb_uart->st.rx_cnt += len;
            furi_check(furi_mutex_release(usb_uart->usb_mutex) == FuriStatusOk);

            if(len > 0) {
                remain += len;

                size_t handled = usb_uart->cfg.rx_data(usb_uart->cfg.rx_data_ctx, data, remain);

                memcpy(data, &data[handled], remain - handled);
                remain -= handled;
            }
        }
    }
    usb_uart_vcp_deinit();

    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_cdc_single, NULL) == true);

    return 0;
}

/* VCP callbacks */
static void vcp_on_cdc_tx_complete(void* context) {
    UsbUart* usb_uart = (UsbUart*)context;
    furi_semaphore_release(usb_uart->tx_sem);
}

static void vcp_on_cdc_rx(void* context) {
    UsbUart* usb_uart = (UsbUart*)context;
    furi_thread_flags_set(furi_thread_get_id(usb_uart->thread), WorkerEvtCdcRx);
}

static void vcp_state_callback(void* context, CdcState state) {
    UNUSED(context);
    UNUSED(state);
}

static void vcp_on_cdc_control_line(void* context, CdcCtrlLine state) {
    UNUSED(context);
    UNUSED(state);
}

static void vcp_on_line_config(void* context, struct usb_cdc_line_coding* config) {
    UNUSED(context);
    UNUSED(config);
}

UsbUart* usb_uart_enable(UsbUartConfig* cfg) {
    UsbUart* usb_uart = calloc(1, sizeof(UsbUart));
    if(!usb_uart) {
        return NULL;
    }
    memcpy(&usb_uart->cfg, cfg, sizeof(UsbUartConfig));

    usb_uart->tx_sem = furi_semaphore_alloc(1, 1);
    usb_uart->usb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!usb_uart->tx_sem || !usb_uart->usb_mutex) {
        if(usb_uart->usb_mutex) furi_mutex_free(usb_uart->usb_mutex);
        if(usb_uart->tx_sem) furi_semaphore_free(usb_uart->tx_sem);
        free(usb_uart);
        return NULL;
    }

    usb_uart->thread = furi_thread_alloc_ex("UsbUartWorker", 1024, usb_uart_worker, usb_uart);
    if(!usb_uart->thread) {
        furi_mutex_free(usb_uart->usb_mutex);
        furi_semaphore_free(usb_uart->tx_sem);
        free(usb_uart);
        return NULL;
    }
    furi_thread_start(usb_uart->thread);
    return usb_uart;
}

void usb_uart_disable(UsbUart* usb_uart) {
    furi_assert(usb_uart);
    furi_thread_flags_set(furi_thread_get_id(usb_uart->thread), WorkerEvtStop);
    furi_thread_join(usb_uart->thread);
    furi_thread_free(usb_uart->thread);
    furi_mutex_free(usb_uart->usb_mutex);
    furi_semaphore_free(usb_uart->tx_sem);
    free(usb_uart);
}

void usb_uart_get_state(UsbUart* usb_uart, UsbUartState* st) {
    furi_assert(usb_uart);
    furi_assert(st);
    furi_check(furi_mutex_acquire(usb_uart->usb_mutex, FuriWaitForever) == FuriStatusOk);
    memcpy(st, &(usb_uart->st), sizeof(UsbUartState));
    furi_check(furi_mutex_release(usb_uart->usb_mutex) == FuriStatusOk);
}
