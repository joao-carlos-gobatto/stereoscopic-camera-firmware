#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#include <esp_log.h>
#include <esp_system.h>
#include <esp_crc.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"  // For mutex

#include "esp_camera.h"

#define STABILIZE_FRAMES    30
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

extern SemaphoreHandle_t take_picture_mutex;
extern volatile bool command_received;
extern camera_config_t photo_config;

typedef struct {
    uint8_t *buf;
    size_t len;
} picture_t;

void camera_warm_up(void);
void picture_task(void *pvParameters);

#endif // CAMERA_UTILS_H