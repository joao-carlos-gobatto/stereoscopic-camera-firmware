#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#include <esp_log.h>
#include <esp_system.h>

#include "freertos/FreeRTOS.h"
#include <string.h>

#include "esp_camera.h"
#include "esp_psram.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

typedef struct {
    uint8_t *buf;
    size_t len;
} picture_t;

esp_err_t camera_startup(void);
esp_err_t camera_sensors_warmup(void);
esp_err_t set_camera_framesize(framesize_t size);
picture_t *capture_image(void);

#endif // CAMERA_UTILS_H