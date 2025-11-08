#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#include <esp_log.h>
#include <esp_system.h>

#include "freertos/FreeRTOS.h"
#include <string.h>

#include "esp_camera.h"
#include "esp_psram.h"

esp_err_t camera_startup(void);
esp_err_t set_camera_framesize(framesize_t size);

#endif // CAMERA_UTILS_H