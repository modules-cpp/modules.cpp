// TinyUSB configuration for a board with a PIO USB host port
// (MM_BOARD_HAS_USB_HOST): the native USB port stays the device pico_stdio_usb
// makes it, a CDC console, and a second port on two GPIOs, driven by
// Pico-PIO-USB, is a host for one USB mass-storage device.
//
// pico_stdio_usb supplies its own configuration only when the application
// links neither TinyUSB's device nor its host; linking the host makes this
// file the configuration, so the device half repeats pico_stdio_usb's.
#ifndef MM_PICO_TUSB_CONFIG_H
#define MM_PICO_TUSB_CONFIG_H

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

// Port 0, the native controller: the console device.
#define BOARD_TUD_RHPORT 0
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64
#define CFG_TUD_CDC_EP_BUFSIZE 64
// pico_stdio_usb's reset interface is a vendor interface with its own driver.
#define CFG_TUD_VENDOR 0

// Port 1, PIO: the mass-storage host.
#define BOARD_TUH_RHPORT 1
#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1
#define CFG_TUH_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB 0
#define CFG_TUH_DEVICE_MAX 1
#define CFG_TUH_MSC 1
#define CFG_TUH_MSC_MAXLUN 1

#endif
