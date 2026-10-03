// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// TinyUSB configuration for a board where the native USB port is given to
// the application (MM_BOARD_USB_PORT == "application").

#ifndef MM_PICO_USB_DEVICE_TUSB_CONFIG_H
#define MM_PICO_USB_DEVICE_TUSB_CONFIG_H

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

// Port 0, native USB controller: application device
#define BOARD_TUD_RHPORT 0
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64

// No built-in class drivers; the provider registers its own class driver
// via usbd_app_driver_get_cb
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#endif
