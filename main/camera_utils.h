#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#include <esp_log.h>
#include <esp_system.h>

#include "freertos/FreeRTOS.h"
#include <string.h>

#include "esp_camera.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

extern camera_config_t photo_config;

typedef struct {
    uint8_t *buf;
    size_t len;
} picture_t;

void camera_startup(void);
void camera_sensors_warmup(void);
void send_image(const picture_t *picture);
void set_camera_framesize(framesize_t size);
picture_t *capture_image(void);

#endif // CAMERA_UTILS_H