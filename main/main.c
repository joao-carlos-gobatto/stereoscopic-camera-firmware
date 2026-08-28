#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "camera_utils.h"
#include "udp_socket_utils.h"
#include "wifi_utils.h"

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Starts up wifi
  wifi_init_sta();

  // Starts up camera
  ESP_ERROR_CHECK(camera_startup());

  // Starts udp socket task
  xTaskCreate(udp_discovery_listener_task, "udp_discovery", 4096, NULL, 4,
              NULL);
}
