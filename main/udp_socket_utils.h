#ifndef UDP_SOCKET_UTILS_H
#define UDP_SOCKET_UTILS_H

#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "esp_timer.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <errno.h>

#include "camera_utils.h"

#define BROADCAST_IP_ADDR "255.255.255.255"
#define BROADCAST_MSG  "ESP_DISCOVERY"       // Mensagem de identificação da câmera
#define PACKET_SIZE     1024         // Tamanho do payload UDP
#define RESPONSE_TIMEOUT_MS 1000     // Tempo máximo de espera por resposta
#define FPS_TARGET      24
#define FRAME_INTERVAL_US (1000000 / FPS_TARGET)

typedef struct {
    uint32_t frame_number;
    uint16_t packet_id;
    uint16_t total_packets;
} __attribute__((packed)) frame_header_t;

void udp_discovery_task(void *pvParameters);

#endif // UDP_SOCKET_UTILS_H