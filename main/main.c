#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "uart_utils.h"
#include "camera_utils.h"

static const char *TAG = "app_main";

void app_main(void) {
    ESP_LOGI(TAG, "Starting application at 11:19 AM -03 on Wednesday, August 27, 2025");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    uart_init();

    // Initialize mutex
    take_picture_mutex = xSemaphoreCreateMutex();
    if (take_picture_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create command mutex");
        return;
    }

    camera_warm_up();
    send_ready_message();

    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx_task", 8192, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(picture_task, "picture_task", 16384, NULL, 1, NULL, 1); // Increased stack for camera
}