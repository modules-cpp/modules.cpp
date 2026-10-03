// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// Private ABI between platform.pico.usb.host and the Pico SDK adapter. C includes
// this file directly; C++ includes usb-host-cxx.h to give these names C linkage.

#ifndef MM_PICO_USB_HOST_C_H
#define MM_PICO_USB_HOST_C_H

#include <stddef.h>
#include <stdint.h>

enum {
    MM_PICO_USB_HOST_OK = 0,
    MM_PICO_USB_HOST_BAD_ARGUMENT = 1,
    MM_PICO_USB_HOST_UNSUPPORTED = 2,
    MM_PICO_USB_HOST_NOT_INITIALIZED = 3,
    MM_PICO_USB_HOST_BUSY = 4,
    MM_PICO_USB_HOST_TIMEOUT = 5,
    MM_PICO_USB_HOST_TRANSPORT_ERROR = 6
};

enum {
    MM_PICO_USB_HOST_EVENT_NONE = 0,
    MM_PICO_USB_HOST_EVENT_ATTACHED = 1,
    MM_PICO_USB_HOST_EVENT_DETACHED = 2
};

enum {
    MM_PICO_USB_HOST_SPEED_UNKNOWN = 0,
    MM_PICO_USB_HOST_SPEED_LOW = 1,
    MM_PICO_USB_HOST_SPEED_FULL = 2,
    MM_PICO_USB_HOST_SPEED_HIGH = 3
};

struct mm_pico_usb_host_device_info {
    uint8_t bus;
    uint8_t address;
    uint16_t vendor;
    uint16_t product;
    int speed;
};

struct mm_pico_usb_host_event {
    int kind;
    struct mm_pico_usb_host_device_info device;
};

struct mm_pico_usb_host_setup {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
};

int mm_pico_usb_host_initialize(void);
int mm_pico_usb_host_list(struct mm_pico_usb_host_device_info* out_devices,
                          size_t capacity, size_t* out_count);
int mm_pico_usb_host_take_event(struct mm_pico_usb_host_event* out_event);
int mm_pico_usb_host_open(const struct mm_pico_usb_host_device_info* info,
                          unsigned int* out_handle);
int mm_pico_usb_host_close(unsigned int handle);
int mm_pico_usb_host_descriptors(unsigned int handle, uint8_t* out_buf,
                                 size_t capacity, size_t* out_len);
int mm_pico_usb_host_claim(unsigned int handle, unsigned int iface);
int mm_pico_usb_host_release(unsigned int handle, unsigned int iface);
int mm_pico_usb_host_control(unsigned int handle,
                             const struct mm_pico_usb_host_setup* setup,
                             uint8_t* data, size_t length,
                             size_t* out_transferred,
                             unsigned long timeout_ms);
int mm_pico_usb_host_transfer(unsigned int handle, uint8_t ep,
                              uint8_t* data, size_t length,
                              size_t* out_transferred,
                              unsigned long timeout_ms);

#endif
