// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Pico SDK USB host adapter implementation of platform.pico.usb.host.

#include "../usb/host/usb-host-c.h"

#if MM_BOARD_HAS_USB_HOST

#define MM_PICO_USB_HOST_EVENT_QUEUE_SIZE 8

static struct mm_pico_usb_host_event mm_pico_host_events[MM_PICO_USB_HOST_EVENT_QUEUE_SIZE];
static uint8_t mm_pico_host_event_head = 0;
static uint8_t mm_pico_host_event_tail = 0;

void tuh_mount_cb(uint8_t daddr) {
    uint8_t next = (mm_pico_host_event_head + 1) % MM_PICO_USB_HOST_EVENT_QUEUE_SIZE;
    if (next != mm_pico_host_event_tail) {
        struct mm_pico_usb_host_event* ev = &mm_pico_host_events[mm_pico_host_event_head];
        ev->kind = MM_PICO_USB_HOST_EVENT_ATTACHED;
        ev->device.bus = 1;
        ev->device.address = daddr;
        uint16_t vid = 0, pid = 0;
        tuh_vid_pid_get(daddr, &vid, &pid);
        ev->device.vendor = vid;
        ev->device.product = pid;
        tusb_speed_t sp = tuh_speed_get(daddr);
        int msp = MM_PICO_USB_HOST_SPEED_UNKNOWN;
        if (sp == TUSB_SPEED_LOW) msp = MM_PICO_USB_HOST_SPEED_LOW;
        else if (sp == TUSB_SPEED_FULL) msp = MM_PICO_USB_HOST_SPEED_FULL;
        else if (sp == TUSB_SPEED_HIGH) msp = MM_PICO_USB_HOST_SPEED_HIGH;
        ev->device.speed = msp;
        mm_pico_host_event_head = next;
    }
}

void tuh_umount_cb(uint8_t daddr) {
    uint8_t next = (mm_pico_host_event_head + 1) % MM_PICO_USB_HOST_EVENT_QUEUE_SIZE;
    if (next != mm_pico_host_event_tail) {
        struct mm_pico_usb_host_event* ev = &mm_pico_host_events[mm_pico_host_event_head];
        ev->kind = MM_PICO_USB_HOST_EVENT_DETACHED;
        ev->device.bus = 1;
        ev->device.address = daddr;
        ev->device.vendor = 0;
        ev->device.product = 0;
        ev->device.speed = MM_PICO_USB_HOST_SPEED_UNKNOWN;
        mm_pico_host_event_head = next;
    }
}

struct mm_pico_host_sync_ctx {
    volatile int done;
    volatile xfer_result_t result;
    volatile uint32_t transferred;
};

static void mm_pico_host_xfer_cb(tuh_xfer_t* xfer) {
    struct mm_pico_host_sync_ctx* ctx = (struct mm_pico_host_sync_ctx*)xfer->user_data;
    if (ctx) {
        ctx->result = xfer->result;
        ctx->transferred = xfer->actual_len;
        ctx->done = 1;
    }
}

#endif  // MM_BOARD_HAS_USB_HOST

int mm_pico_usb_host_initialize(void) {
#if MM_BOARD_HAS_USB_HOST
    mm_pico_usb_start();
    return MM_PICO_USB_HOST_OK;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_list(struct mm_pico_usb_host_device_info* out_devices,
                          size_t capacity, size_t* out_count) {
#if MM_BOARD_HAS_USB_HOST
    if (out_count == NULL) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    *out_count = 0;
    mm_pico_usb_start();
    const absolute_time_t until = make_timeout_time_ms(2);
    do {
        tuh_task();
    } while (!time_reached(until));

    for (uint8_t daddr = 1; daddr <= CFG_TUH_DEVICE_MAX; ++daddr) {
        if (tuh_mounted(daddr)) {
            if (*out_count < capacity && out_devices != NULL) {
                out_devices[*out_count].bus = 1;
                out_devices[*out_count].address = daddr;
                uint16_t vid = 0, pid = 0;
                tuh_vid_pid_get(daddr, &vid, &pid);
                out_devices[*out_count].vendor = vid;
                out_devices[*out_count].product = pid;
                tusb_speed_t sp = tuh_speed_get(daddr);
                int msp = MM_PICO_USB_HOST_SPEED_UNKNOWN;
                if (sp == TUSB_SPEED_LOW) msp = MM_PICO_USB_HOST_SPEED_LOW;
                else if (sp == TUSB_SPEED_FULL) msp = MM_PICO_USB_HOST_SPEED_FULL;
                else if (sp == TUSB_SPEED_HIGH) msp = MM_PICO_USB_HOST_SPEED_HIGH;
                out_devices[*out_count].speed = msp;
            }
            (*out_count)++;
        }
    }
    return MM_PICO_USB_HOST_OK;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_take_event(struct mm_pico_usb_host_event* out_event) {
#if MM_BOARD_HAS_USB_HOST
    if (out_event == NULL) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    mm_pico_usb_start();
    tuh_task();
    if (mm_pico_host_event_tail == mm_pico_host_event_head) {
        out_event->kind = MM_PICO_USB_HOST_EVENT_NONE;
        return MM_PICO_USB_HOST_OK;
    }
    *out_event = mm_pico_host_events[mm_pico_host_event_tail];
    mm_pico_host_event_tail = (mm_pico_host_event_tail + 1) % MM_PICO_USB_HOST_EVENT_QUEUE_SIZE;
    return MM_PICO_USB_HOST_OK;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_open(const struct mm_pico_usb_host_device_info* info,
                          unsigned int* out_handle) {
#if MM_BOARD_HAS_USB_HOST
    if (info == NULL || out_handle == NULL) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    if (info->address == 0 || info->address > CFG_TUH_DEVICE_MAX) {
        return MM_PICO_USB_HOST_BAD_ARGUMENT;
    }
    if (!tuh_mounted(info->address)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;
    *out_handle = info->address;
    return MM_PICO_USB_HOST_OK;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_close(unsigned int handle) {
#if MM_BOARD_HAS_USB_HOST
    if (handle == 0 || handle > CFG_TUH_DEVICE_MAX) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    return MM_PICO_USB_HOST_OK;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_descriptors(unsigned int handle, uint8_t* out_buf,
                                 size_t capacity, size_t* out_len) {
#if MM_BOARD_HAS_USB_HOST
    if (out_buf == NULL || out_len == NULL) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    *out_len = 0;
    if (handle == 0 || handle > CFG_TUH_DEVICE_MAX) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;

    size_t copied = 0;

    if (capacity >= sizeof(tusb_desc_device_t)) {
        struct mm_pico_host_sync_ctx ctx = {0, XFER_RESULT_FAILED, 0};
        if (!tuh_descriptor_get_device((uint8_t)handle, out_buf, sizeof(tusb_desc_device_t),
                                       mm_pico_host_xfer_cb, (uintptr_t)&ctx)) {
            return MM_PICO_USB_HOST_TRANSPORT_ERROR;
        }
        const absolute_time_t deadline = make_timeout_time_ms(2000);
        while (!ctx.done) {
            tuh_task();
            if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;
            if (time_reached(deadline)) return MM_PICO_USB_HOST_TIMEOUT;
        }
        if (ctx.result != XFER_RESULT_SUCCESS) return MM_PICO_USB_HOST_TRANSPORT_ERROR;
        copied = ctx.transferred;
    }

    if (capacity >= copied + sizeof(tusb_desc_configuration_t)) {
        uint8_t cfg_hdr[sizeof(tusb_desc_configuration_t)];
        struct mm_pico_host_sync_ctx ctx = {0, XFER_RESULT_FAILED, 0};
        if (tuh_descriptor_get_configuration((uint8_t)handle, 0, cfg_hdr, sizeof(cfg_hdr),
                                              mm_pico_host_xfer_cb, (uintptr_t)&ctx)) {
            const absolute_time_t deadline = make_timeout_time_ms(2000);
            while (!ctx.done) {
                tuh_task();
                if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;
                if (time_reached(deadline)) return MM_PICO_USB_HOST_TIMEOUT;
            }
            if (ctx.result == XFER_RESULT_SUCCESS && ctx.transferred >= sizeof(tusb_desc_configuration_t)) {
                tusb_desc_configuration_t const* desc_cfg = (tusb_desc_configuration_t const*)cfg_hdr;
                uint16_t total_len = desc_cfg->wTotalLength;
                size_t available = capacity - copied;
                uint16_t to_fetch = (uint16_t)(available < total_len ? available : total_len);

                ctx.done = 0;
                ctx.result = XFER_RESULT_FAILED;
                ctx.transferred = 0;
                if (tuh_descriptor_get_configuration((uint8_t)handle, 0, out_buf + copied, to_fetch,
                                                     mm_pico_host_xfer_cb, (uintptr_t)&ctx)) {
                    const absolute_time_t deadline2 = make_timeout_time_ms(2000);
                    while (!ctx.done) {
                        tuh_task();
                        if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;
                        if (time_reached(deadline2)) return MM_PICO_USB_HOST_TIMEOUT;
                    }
                    if (ctx.result == XFER_RESULT_SUCCESS) {
                        copied += ctx.transferred;
                    }
                }
            }
        }
    }

    *out_len = copied;
    return MM_PICO_USB_HOST_OK;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_claim(unsigned int handle, unsigned int iface) {
#if MM_BOARD_HAS_USB_HOST
    if (handle == 0 || handle > CFG_TUH_DEVICE_MAX) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;
    if (mm_pico_storage_address != 0 && (uint8_t)handle == mm_pico_storage_address) {
        return MM_PICO_USB_HOST_BUSY;
    }

    uint8_t cfg_buf[256];
    struct mm_pico_host_sync_ctx ctx = {0, XFER_RESULT_FAILED, 0};
    if (tuh_descriptor_get_configuration((uint8_t)handle, 0, cfg_buf, sizeof(cfg_buf),
                                         mm_pico_host_xfer_cb, (uintptr_t)&ctx)) {
        const absolute_time_t deadline = make_timeout_time_ms(1000);
        while (!ctx.done) {
            tuh_task();
            if (time_reached(deadline)) break;
        }
        if (ctx.result == XFER_RESULT_SUCCESS && ctx.transferred >= sizeof(tusb_desc_configuration_t)) {
            tusb_desc_configuration_t const* cfg = (tusb_desc_configuration_t const*)cfg_buf;
            uint16_t total = cfg->wTotalLength;
            if (total > sizeof(cfg_buf)) total = sizeof(cfg_buf);
            uint8_t const* p = cfg_buf + sizeof(tusb_desc_configuration_t);
            uint8_t const* end = cfg_buf + total;
            int in_target_iface = 0;
            while (p + 2 <= end) {
                uint8_t len = p[0];
                uint8_t type = p[1];
                if (len < 2 || p + len > end) break;
                if (type == TUSB_DESC_INTERFACE) {
                    tusb_desc_interface_t const* itf = (tusb_desc_interface_t const*)p;
                    in_target_iface = (itf->bInterfaceNumber == iface);
                } else if (in_target_iface && type == TUSB_DESC_ENDPOINT) {
                    tusb_desc_endpoint_t const* ep_desc = (tusb_desc_endpoint_t const*)p;
                    tuh_edpt_open((uint8_t)handle, ep_desc);
                }
                p += len;
            }
        }
    }
    return MM_PICO_USB_HOST_OK;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_release(unsigned int handle, unsigned int iface) {
#if MM_BOARD_HAS_USB_HOST
    (void)iface;
    if (handle == 0 || handle > CFG_TUH_DEVICE_MAX) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    return MM_PICO_USB_HOST_OK;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_control(unsigned int handle,
                             const struct mm_pico_usb_host_setup* setup,
                             uint8_t* data, size_t length,
                             size_t* out_transferred,
                             unsigned long timeout_ms) {
#if MM_BOARD_HAS_USB_HOST
    if (setup == NULL || out_transferred == NULL) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    *out_transferred = 0;
    if (handle == 0 || handle > CFG_TUH_DEVICE_MAX) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;

    struct mm_pico_host_sync_ctx ctx = {0, XFER_RESULT_FAILED, 0};
    tusb_control_request_t req;
    req.bmRequestType = setup->request_type;
    req.bRequest = setup->request;
    req.wValue = setup->value;
    req.wIndex = setup->index;
    req.wLength = (uint16_t)length;

    tuh_xfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.daddr = (uint8_t)handle;
    xfer.ep_addr = 0;
    xfer.setup = &req;
    xfer.buffer = data;
    xfer.complete_cb = mm_pico_host_xfer_cb;
    xfer.user_data = (uintptr_t)&ctx;

    if (!tuh_control_xfer(&xfer)) {
        return MM_PICO_USB_HOST_TRANSPORT_ERROR;
    }

    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms ? timeout_ms : 5000);
    while (!ctx.done) {
        tuh_task();
        if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;
        if (time_reached(deadline)) {
            return MM_PICO_USB_HOST_TIMEOUT;
        }
    }

    *out_transferred = ctx.transferred;
    if (ctx.result == XFER_RESULT_SUCCESS) return MM_PICO_USB_HOST_OK;
    if (ctx.result == XFER_RESULT_TIMEOUT) return MM_PICO_USB_HOST_TIMEOUT;
    return MM_PICO_USB_HOST_TRANSPORT_ERROR;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}

int mm_pico_usb_host_transfer(unsigned int handle, uint8_t ep,
                              uint8_t* data, size_t length,
                              size_t* out_transferred,
                              unsigned long timeout_ms) {
#if MM_BOARD_HAS_USB_HOST
    if (out_transferred == NULL) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    *out_transferred = 0;
    if (handle == 0 || handle > CFG_TUH_DEVICE_MAX) return MM_PICO_USB_HOST_BAD_ARGUMENT;
    if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;

    struct mm_pico_host_sync_ctx ctx = {0, XFER_RESULT_FAILED, 0};
    tuh_xfer_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.daddr = (uint8_t)handle;
    xfer.ep_addr = ep;
    xfer.buflen = length;
    xfer.buffer = data;
    xfer.complete_cb = mm_pico_host_xfer_cb;
    xfer.user_data = (uintptr_t)&ctx;

    if (!tuh_edpt_xfer(&xfer)) {
        return MM_PICO_USB_HOST_TRANSPORT_ERROR;
    }

    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms ? timeout_ms : 5000);
    while (!ctx.done) {
        tuh_task();
        if (!tuh_mounted((uint8_t)handle)) return MM_PICO_USB_HOST_TRANSPORT_ERROR;
        if (time_reached(deadline)) {
            tuh_edpt_abort_xfer((uint8_t)handle, ep);
            return MM_PICO_USB_HOST_TIMEOUT;
        }
    }

    *out_transferred = ctx.transferred;
    if (ctx.result == XFER_RESULT_SUCCESS) return MM_PICO_USB_HOST_OK;
    if (ctx.result == XFER_RESULT_TIMEOUT) return MM_PICO_USB_HOST_TIMEOUT;
    return MM_PICO_USB_HOST_TRANSPORT_ERROR;
#else
    return MM_PICO_USB_HOST_UNSUPPORTED;
#endif
}
