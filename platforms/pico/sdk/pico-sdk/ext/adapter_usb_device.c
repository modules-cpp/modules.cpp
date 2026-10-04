// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026

#if MM_BOARD_USB_PORT_APPLICATION

#include "tusb.h"
#include "device/usbd_pvt.h"
#include <string.h>

#define MM_PICO_USB_DEV_DESC_MAX 64
#define MM_PICO_USB_CFG_DESC_MAX 512
#define MM_PICO_USB_STR_COUNT_MAX 8
#define MM_PICO_USB_STR_LEN_MAX 64
#define MM_PICO_USB_EVENT_QUEUE_SIZE 16

static uint8_t mm_pico_dev_desc[MM_PICO_USB_DEV_DESC_MAX];
static size_t mm_pico_dev_desc_len = 0;

static uint8_t mm_pico_cfg_desc[MM_PICO_USB_CFG_DESC_MAX];
static size_t mm_pico_cfg_desc_len = 0;

static uint8_t mm_pico_str_descs[MM_PICO_USB_STR_COUNT_MAX][MM_PICO_USB_STR_LEN_MAX];
static size_t mm_pico_str_lens[MM_PICO_USB_STR_COUNT_MAX];
static size_t mm_pico_str_count = 0;

static int mm_pico_usb_state_val = MM_PICO_USB_STATE_DETACHED;
static int mm_pico_usb_initialized = 0;

static struct mm_pico_usb_event mm_pico_events[MM_PICO_USB_EVENT_QUEUE_SIZE];
static size_t mm_pico_event_head = 0;
static size_t mm_pico_event_tail = 0;

static tusb_control_request_t mm_pico_current_setup;

struct ep_out_state {
    uint8_t ep_addr;
    uint16_t max_packet_size;
    uint8_t rx_buf[64];
    size_t rx_len;
    size_t rx_offset;
    int rx_busy;
    int open;
};

struct ep_in_state {
    uint8_t ep_addr;
    uint16_t max_packet_size;
    uint8_t tx_buf[64];
    int tx_busy;
    int open;
};

static struct ep_out_state mm_pico_ep_out[16];
static struct ep_in_state mm_pico_ep_in[16];

static void mm_pico_queue_event(int kind, const struct mm_pico_usb_setup* setup) {
    size_t next = (mm_pico_event_head + 1) % MM_PICO_USB_EVENT_QUEUE_SIZE;
    if (next != mm_pico_event_tail) {
        mm_pico_events[mm_pico_event_head].kind = kind;
        if (setup) {
            mm_pico_events[mm_pico_event_head].setup = *setup;
        } else {
            memset(&mm_pico_events[mm_pico_event_head].setup, 0, sizeof(struct mm_pico_usb_setup));
        }
        mm_pico_event_head = next;
    }
}

// TinyUSB device callbacks
uint8_t const* tud_descriptor_device_cb(void) {
    return mm_pico_dev_desc;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return mm_pico_cfg_desc;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    if (index >= mm_pico_str_count) return NULL;
    return (uint16_t const*)mm_pico_str_descs[index];
}

void tud_mount_cb(void) {
    mm_pico_usb_state_val = MM_PICO_USB_STATE_CONFIGURED;
    mm_pico_queue_event(MM_PICO_USB_EVENT_CONFIGURED, NULL);
}

void tud_umount_cb(void) {
    mm_pico_usb_state_val = MM_PICO_USB_STATE_DEFAULT;
    mm_pico_queue_event(MM_PICO_USB_EVENT_DECONFIGURED, NULL);
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    mm_pico_usb_state_val = MM_PICO_USB_STATE_SUSPENDED;
    mm_pico_queue_event(MM_PICO_USB_EVENT_SUSPEND, NULL);
}

void tud_resume_cb(void) {
    mm_pico_usb_state_val = MM_PICO_USB_STATE_CONFIGURED;
    mm_pico_queue_event(MM_PICO_USB_EVENT_RESUME, NULL);
}

// Custom class driver
static void mm_pico_driver_init(void) {
    memset(mm_pico_ep_out, 0, sizeof(mm_pico_ep_out));
    memset(mm_pico_ep_in, 0, sizeof(mm_pico_ep_in));
}

static bool mm_pico_driver_deinit(void) {
    return true;
}

static void mm_pico_driver_reset(uint8_t rhport) {
    (void)rhport;
    mm_pico_driver_init();
    mm_pico_usb_state_val = MM_PICO_USB_STATE_DEFAULT;
    mm_pico_queue_event(MM_PICO_USB_EVENT_RESET, NULL);
}

static uint16_t mm_pico_driver_open(uint8_t rhport, tusb_desc_interface_t const * desc_itf, uint16_t max_len) {
    if (desc_itf->bDescriptorType != TUSB_DESC_INTERFACE) return 0;

    uint8_t const * p_desc = (uint8_t const *) desc_itf;
    uint8_t const * p_desc_end = p_desc + max_len;

    p_desc = tu_desc_next(p_desc);
    while (p_desc < p_desc_end && p_desc[1] != TUSB_DESC_INTERFACE) {
        if (p_desc[1] == TUSB_DESC_ENDPOINT) {
            tusb_desc_endpoint_t const * desc_ep = (tusb_desc_endpoint_t const *) p_desc;
            usbd_edpt_open(rhport, desc_ep);
            uint8_t ep_addr = desc_ep->bEndpointAddress;
            uint8_t ep_num = ep_addr & 0x0f;
            uint16_t ep_mps = tu_edpt_packet_size(desc_ep);
            if (ep_addr & 0x80) { // IN
                mm_pico_ep_in[ep_num].ep_addr = ep_addr;
                mm_pico_ep_in[ep_num].max_packet_size = ep_mps;
                mm_pico_ep_in[ep_num].tx_busy = 0;
                mm_pico_ep_in[ep_num].open = 1;
            } else { // OUT
                mm_pico_ep_out[ep_num].ep_addr = ep_addr;
                mm_pico_ep_out[ep_num].max_packet_size = ep_mps;
                mm_pico_ep_out[ep_num].rx_len = 0;
                mm_pico_ep_out[ep_num].rx_offset = 0;
                mm_pico_ep_out[ep_num].open = 1;
                mm_pico_ep_out[ep_num].rx_busy = 1;
                usbd_edpt_xfer(rhport, ep_addr, mm_pico_ep_out[ep_num].rx_buf, ep_mps);
            }
        }
        p_desc = tu_desc_next(p_desc);
    }
    return (uint16_t)(p_desc - (uint8_t const *)desc_itf);
}

static bool mm_pico_driver_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request) {
    (void)rhport;
    if (stage == CONTROL_STAGE_SETUP) {
        uint8_t type = (request->bmRequestType >> 5) & 0x03;
        uint8_t recip = request->bmRequestType & 0x1f;
        if (type == 1 || type == 2 || recip == 1) { // Class, Vendor, or Interface
            mm_pico_current_setup = *request;
            struct mm_pico_usb_setup s = {
                .request_type = request->bmRequestType,
                .request = request->bRequest,
                .value = request->wValue,
                .index = request->wIndex,
                .length = request->wLength
            };
            mm_pico_queue_event(MM_PICO_USB_EVENT_SETUP, &s);
            return true;
        }
    }
    return false;
}

static bool mm_pico_driver_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)rhport;
    if (result != XFER_RESULT_SUCCESS) return false;
    uint8_t ep_num = ep_addr & 0x0f;
    if (ep_addr & 0x80) { // IN
        mm_pico_ep_in[ep_num].tx_busy = 0;
    } else { // OUT
        mm_pico_ep_out[ep_num].rx_len = xferred_bytes;
        mm_pico_ep_out[ep_num].rx_offset = 0;
        mm_pico_ep_out[ep_num].rx_busy = 0;
    }
    return true;
}

static const usbd_class_driver_t mm_pico_app_driver = {
    .name = "mm_usb_device",
    .init = mm_pico_driver_init,
    .deinit = mm_pico_driver_deinit,
    .reset = mm_pico_driver_reset,
    .open = mm_pico_driver_open,
    .control_xfer_cb = mm_pico_driver_control_xfer_cb,
    .xfer_cb = mm_pico_driver_xfer_cb,
    .sof = NULL
};

usbd_class_driver_t const* usbd_app_driver_get_cb(uint8_t* driver_count) {
    *driver_count = 1;
    return &mm_pico_app_driver;
}

int mm_pico_usb_device_initialize(const uint8_t* dev_desc, size_t dev_desc_len,
                                  const uint8_t* cfg_desc, size_t cfg_desc_len,
                                  const uint8_t* const* str_descs,
                                  const size_t* str_lens, size_t str_count) {
    if (dev_desc == NULL || cfg_desc == NULL || dev_desc_len > MM_PICO_USB_DEV_DESC_MAX ||
        cfg_desc_len > MM_PICO_USB_CFG_DESC_MAX || str_count > MM_PICO_USB_STR_COUNT_MAX) {
        return MM_PICO_USB_BAD_ARGUMENT;
    }

    memcpy(mm_pico_dev_desc, dev_desc, dev_desc_len);
    mm_pico_dev_desc_len = dev_desc_len;

    memcpy(mm_pico_cfg_desc, cfg_desc, cfg_desc_len);
    mm_pico_cfg_desc_len = cfg_desc_len;

    mm_pico_str_count = str_count;
    for (size_t i = 0; i < str_count; ++i) {
        if (str_descs[i] == NULL || str_lens[i] > MM_PICO_USB_STR_LEN_MAX) {
            return MM_PICO_USB_BAD_ARGUMENT;
        }
        memcpy(mm_pico_str_descs[i], str_descs[i], str_lens[i]);
        mm_pico_str_lens[i] = str_lens[i];
    }

    mm_pico_event_head = 0;
    mm_pico_event_tail = 0;
    mm_pico_driver_init();

    if (!mm_pico_usb_initialized) {
        if (!tud_init(BOARD_TUD_RHPORT)) {
            return MM_PICO_USB_TRANSPORT_ERROR;
        }
        mm_pico_usb_initialized = 1;
    }
    mm_pico_usb_state_val = MM_PICO_USB_STATE_DEFAULT;
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_attach(void) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    if (!tud_connect()) return MM_PICO_USB_TRANSPORT_ERROR;
    mm_pico_usb_state_val = MM_PICO_USB_STATE_ATTACHED;
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_detach(void) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    if (!tud_disconnect()) return MM_PICO_USB_TRANSPORT_ERROR;
    mm_pico_usb_state_val = MM_PICO_USB_STATE_DETACHED;
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_state(int* out_state) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    if (out_state == NULL) return MM_PICO_USB_BAD_ARGUMENT;
    tud_task();
    *out_state = mm_pico_usb_state_val;
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_speed(int* out_speed) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    if (out_speed == NULL) return MM_PICO_USB_BAD_ARGUMENT;
    tud_task();
    *out_speed = (mm_pico_usb_state_val != MM_PICO_USB_STATE_DETACHED)
                     ? MM_PICO_USB_SPEED_FULL
                     : MM_PICO_USB_SPEED_UNKNOWN;
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_take_event(struct mm_pico_usb_event* out_event) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    if (out_event == NULL) return MM_PICO_USB_BAD_ARGUMENT;
    tud_task();
    if (mm_pico_event_head == mm_pico_event_tail) {
        out_event->kind = MM_PICO_USB_EVENT_NONE;
        return MM_PICO_USB_OK;
    }
    *out_event = mm_pico_events[mm_pico_event_tail];
    mm_pico_event_tail = (mm_pico_event_tail + 1) % MM_PICO_USB_EVENT_QUEUE_SIZE;
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_control_receive(uint8_t* buf, size_t size, size_t* out_received) {
    (void)buf;
    (void)size;
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    if (out_received == NULL) return MM_PICO_USB_BAD_ARGUMENT;
    *out_received = 0;
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_control_reply(const uint8_t* buf, size_t size) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    tud_task();
    if (size == 0) {
        return tud_control_status(0, &mm_pico_current_setup) ? MM_PICO_USB_OK : MM_PICO_USB_TRANSPORT_ERROR;
    } else {
        return tud_control_xfer(0, &mm_pico_current_setup, (void*)buf, (uint16_t)size) ? MM_PICO_USB_OK : MM_PICO_USB_TRANSPORT_ERROR;
    }
}

int mm_pico_usb_device_control_stall(void) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    tud_task();
    usbd_edpt_stall(0, 0);
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_write(uint8_t ep, const uint8_t* buf, size_t size, size_t* out_written) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    if (out_written == NULL) return MM_PICO_USB_BAD_ARGUMENT;
    tud_task();

    uint8_t ep_num = ep & 0x0f;
    if ((ep & 0x80) == 0 || ep_num >= 16 || !mm_pico_ep_in[ep_num].open) {
        return MM_PICO_USB_BAD_ARGUMENT;
    }
    struct ep_in_state* eps = &mm_pico_ep_in[ep_num];
    if (eps->tx_busy) {
        *out_written = 0;
        return MM_PICO_USB_OK;
    }

    size_t to_copy = size < eps->max_packet_size ? size : eps->max_packet_size;
    if (to_copy > sizeof(eps->tx_buf)) to_copy = sizeof(eps->tx_buf);
    memcpy(eps->tx_buf, buf, to_copy);
    eps->tx_busy = 1;

    if (!usbd_edpt_xfer(0, eps->ep_addr, eps->tx_buf, (uint16_t)to_copy)) {
        eps->tx_busy = 0;
        return MM_PICO_USB_TRANSPORT_ERROR;
    }

    *out_written = to_copy;
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_read(uint8_t ep, uint8_t* buf, size_t size, size_t* out_read) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    if (out_read == NULL) return MM_PICO_USB_BAD_ARGUMENT;
    tud_task();

    uint8_t ep_num = ep & 0x0f;
    if ((ep & 0x80) != 0 || ep_num >= 16 || !mm_pico_ep_out[ep_num].open) {
        return MM_PICO_USB_BAD_ARGUMENT;
    }
    struct ep_out_state* eps = &mm_pico_ep_out[ep_num];
    size_t available = eps->rx_len - eps->rx_offset;
    if (available == 0) {
        *out_read = 0;
        if (!eps->rx_busy) {
            eps->rx_busy = 1;
            usbd_edpt_xfer(0, eps->ep_addr, eps->rx_buf, eps->max_packet_size);
        }
        return MM_PICO_USB_OK;
    }

    size_t to_copy = size < available ? size : available;
    memcpy(buf, eps->rx_buf + eps->rx_offset, to_copy);
    eps->rx_offset += to_copy;
    *out_read = to_copy;

    if (eps->rx_offset >= eps->rx_len && !eps->rx_busy) {
        eps->rx_len = 0;
        eps->rx_offset = 0;
        eps->rx_busy = 1;
        usbd_edpt_xfer(0, eps->ep_addr, eps->rx_buf, eps->max_packet_size);
    }
    return MM_PICO_USB_OK;
}

int mm_pico_usb_device_stall(uint8_t ep, int enable) {
    if (!mm_pico_usb_initialized) return MM_PICO_USB_NOT_INITIALIZED;
    tud_task();
    if (enable) {
        usbd_edpt_stall(0, ep);
    } else {
        usbd_edpt_clear_stall(0, ep);
    }
    return MM_PICO_USB_OK;
}

#else

int mm_pico_usb_device_initialize(const uint8_t* dev_desc, size_t dev_desc_len,
                                  const uint8_t* cfg_desc, size_t cfg_desc_len,
                                  const uint8_t* const* str_descs,
                                  const size_t* str_lens, size_t str_count) {
    (void)dev_desc; (void)dev_desc_len; (void)cfg_desc; (void)cfg_desc_len;
    (void)str_descs; (void)str_lens; (void)str_count;
    return MM_PICO_USB_UNSUPPORTED;
}
int mm_pico_usb_device_attach(void) { return MM_PICO_USB_UNSUPPORTED; }
int mm_pico_usb_device_detach(void) { return MM_PICO_USB_UNSUPPORTED; }
int mm_pico_usb_device_state(int* out_state) { (void)out_state; return MM_PICO_USB_UNSUPPORTED; }
int mm_pico_usb_device_speed(int* out_speed) { (void)out_speed; return MM_PICO_USB_UNSUPPORTED; }
int mm_pico_usb_device_take_event(struct mm_pico_usb_event* out_event) { (void)out_event; return MM_PICO_USB_UNSUPPORTED; }
int mm_pico_usb_device_control_receive(uint8_t* buf, size_t size, size_t* out_received) {
    (void)buf; (void)size; (void)out_received; return MM_PICO_USB_UNSUPPORTED;
}
int mm_pico_usb_device_control_reply(const uint8_t* buf, size_t size) { (void)buf; (void)size; return MM_PICO_USB_UNSUPPORTED; }
int mm_pico_usb_device_control_stall(void) { return MM_PICO_USB_UNSUPPORTED; }
int mm_pico_usb_device_write(uint8_t ep, const uint8_t* buf, size_t size, size_t* out_written) {
    (void)ep; (void)buf; (void)size; (void)out_written; return MM_PICO_USB_UNSUPPORTED;
}
int mm_pico_usb_device_read(uint8_t ep, uint8_t* buf, size_t size, size_t* out_read) {
    (void)ep; (void)buf; (void)size; (void)out_read; return MM_PICO_USB_UNSUPPORTED;
}
int mm_pico_usb_device_stall(uint8_t ep, int enable) { (void)ep; (void)enable; return MM_PICO_USB_UNSUPPORTED; }

#endif
