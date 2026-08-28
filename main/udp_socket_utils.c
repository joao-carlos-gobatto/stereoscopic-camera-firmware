#include "udp_socket_utils.h"
#include <string.h>

const char *TAG = "udp_socket";

// Configurações importantes - devem bater com o Python
#define DISCOVERY_PORT 12345
#define BROADCAST_MSG "ESP_DISCOVERY"

struct sockaddr_in server_addr;
bool have_server_ip = false;
bool sending_enabled = false;

void udp_stream_task(void *pvParameters) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Falha ao criar socket de streaming: %d", errno);
    vTaskDelete(NULL);
    return;
  }

  // Aumentar buffer de envio ajuda com frames maiores
  int sndbuf = PACKET_SIZE; // ou até 8192 se necessário
  setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

  ESP_LOGI(TAG, "Streaming iniciado para %s:%d",
           inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

  while (1) {
    if (!sending_enabled) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      ESP_LOGW(TAG, "Falha ao capturar frame");
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    // Envia o JPEG cru
    sendto(sock, fb->buf, fb->len, 0, (struct sockaddr *)&server_addr,
           sizeof(server_addr));

    esp_camera_fb_return(fb);

    vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_US / 1000));
  }

  close(sock);
  vTaskDelete(NULL);
}

void udp_discovery_listener_task(void *pvParameters) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Falha ao criar socket discovery: %d", errno);
    vTaskDelete(NULL);
    return;
  }

  // Permitir broadcast
  int yes = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in listen_addr = {.sin_family = AF_INET,
                                    .sin_port = htons(DISCOVERY_PORT),
                                    .sin_addr.s_addr = htonl(INADDR_ANY)};

  if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
    ESP_LOGE(TAG, "Falha no bind porta %d: %d", DISCOVERY_PORT, errno);
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Aguardando broadcasts de discovery na porta %d...",
           DISCOVERY_PORT);

  char rx_buf[64];
  struct sockaddr_in sender_addr;
  socklen_t addr_len = sizeof(sender_addr);

  while (1) {
    int len = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
                       (struct sockaddr *)&sender_addr, &addr_len);

    if (len > 0) {
      rx_buf[len] = '\0';

      if (strcmp(rx_buf, BROADCAST_MSG) == 0) {
        ESP_LOGI(TAG, "Discovery recebido de %s",
                 inet_ntoa(sender_addr.sin_addr));

        // Configura endereço do servidor (IP + porta fixa de streaming)
        server_addr = sender_addr;
        server_addr.sin_port = htons(CONFIG_UDP_STREAM_PORT);

        have_server_ip = true;

        // Se ainda não iniciamos o streaming, iniciamos agora
        if (!sending_enabled) {
          sending_enabled = true;
          ESP_LOGI(TAG, "Iniciando streaming para %s:%d",
                   inet_ntoa(server_addr.sin_addr), CONFIG_UDP_STREAM_PORT);

          xTaskCreate(udp_stream_task, "udp_stream", 8192, NULL, 5, NULL);
          close(sock);
          vTaskDelete(NULL);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }

  close(sock);
  vTaskDelete(NULL);
}
