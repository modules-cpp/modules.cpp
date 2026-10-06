#ifndef MM_PICO_CAM_CAMERA_C_H
#define MM_PICO_CAM_CAMERA_C_H
#include <stddef.h>
/* Values match mm.camera::Status; this is a private C ABI. */
enum { MM_CAM_OK, MM_CAM_BAD_ARGUMENT, MM_CAM_UNSUPPORTED, MM_CAM_NOT_INITIALIZED,
       MM_CAM_BUSY, MM_CAM_TIMEOUT, MM_CAM_TRANSPORT_ERROR };
int mm_pico_cam_initialize(void);
int mm_pico_cam_capture(unsigned char* data, size_t size, unsigned long timeout_ms);
int mm_pico_cam_sleep(void);
#endif
