// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Private ABI between platform.pico.usb.device and the Pico SDK adapter. C includes
// this file directly; C++ includes usb-device-cxx.h to give these names C linkage.

#ifndef MM_PICO_USB_DEVICE_C_H
#define MM_PICO_USB_DEVICE_C_H

#include <stddef.h>
#include <stdint.h>

enum {
    MM_PICO_USB_OK = 0,
    MM_PICO_USB_BAD_ARGUMENT = 1,
    MM_PICO_USB_UNSUPPORTED = 2,
    MM_PICO_USB_NOT_INITIALIZED = 3,
    MM_PICO_USB_BUSY = 4,
    MM_PICO_USB_TIMEOUT = 5,
    MM_PICO_USB_TRANSPORT_ERROR = 6
};

enum {
    MM_PICO_USB_EVENT_NONE = 0,
    MM_PICO_USB_EVENT_RESET = 1,
    MM_PICO_USB_EVENT_CONFIGURED = 2,
    MM_PICO_USB_EVENT_DECONFIGURED = 3,
    MM_PICO_USB_EVENT_SUSPEND = 4,
    MM_PICO_USB_EVENT_RESUME = 5,
    MM_PICO_USB_EVENT_SETUP = 6
};

enum {
    MM_PICO_USB_STATE_DETACHED = 0,
    MM_PICO_USB_STATE_ATTACHED = 1,
    MM_PICO_USB_STATE_POWERED = 2,
    MM_PICO_USB_STATE_DEFAULT = 3,
    MM_PICO_USB_STATE_ADDRESS = 4,
    MM_PICO_USB_STATE_CONFIGURED = 5,
    MM_PICO_USB_STATE_SUSPENDED = 6
};

enum {
    MM_PICO_USB_SPEED_UNKNOWN = 0,
    MM_PICO_USB_SPEED_LOW = 1,
    MM_PICO_USB_SPEED_FULL = 2,
    MM_PICO_USB_SPEED_HIGH = 3
};

struct mm_pico_usb_setup {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
};

struct mm_pico_usb_event {
    int kind;
    struct mm_pico_usb_setup setup;
};

int mm_pico_usb_device_initialize(const uint8_t* dev_desc, size_t dev_desc_len,
                                  const uint8_t* cfg_desc, size_t cfg_desc_len,
                                  const uint8_t* const* str_descs,
                                  const size_t* str_lens, size_t str_count);
int mm_pico_usb_device_attach(void);
int mm_pico_usb_device_detach(void);
int mm_pico_usb_device_state(int* out_state);
int mm_pico_usb_device_speed(int* out_speed);
int mm_pico_usb_device_take_event(struct mm_pico_usb_event* out_event);
int mm_pico_usb_device_control_receive(uint8_t* buf, size_t size, size_t* out_received);
int mm_pico_usb_device_control_reply(const uint8_t* buf, size_t size);
int mm_pico_usb_device_control_stall(void);
int mm_pico_usb_device_write(uint8_t ep, const uint8_t* buf, size_t size, size_t* out_written);
int mm_pico_usb_device_read(uint8_t ep, uint8_t* buf, size_t size, size_t* out_read);
int mm_pico_usb_device_stall(uint8_t ep, int enable);

#endif
