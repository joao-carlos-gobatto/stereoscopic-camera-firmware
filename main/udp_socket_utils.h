#ifndef UDP_SOCKET_UTILS_H
#define UDP_SOCKET_UTILS_H

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>
#include <sys/param.h>

#include "esp_timer.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <errno.h>
#include <lwip/netdb.h>

#include "camera_utils.h"

#define PACKET_SIZE 1024         // Tamanho do payload UDP
#define RESPONSE_TIMEOUT_MS 1000 // Tempo máximo de espera por resposta
#define FPS_TARGET 24
#define FRAME_INTERVAL_US (1000000 / FPS_TARGET)

typedef struct {
  uint32_t frame_number;
  uint16_t packet_id;
  uint16_t total_packets;
} __attribute__((packed)) frame_header_t;

void udp_discovery_listener_task(void *pvParameters);

#endif // UDP_SOCKET_UTILS_H
