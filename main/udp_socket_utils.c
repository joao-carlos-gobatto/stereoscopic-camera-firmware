#include "udp_socket_utils.h"

const char *TAG = "udp_socket";

// Server IP
static struct sockaddr_in server_addr;
static bool server_found = false;


static void udp_stream_task(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Falha ao criar socket stream: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int send_buf_size = 1024;  // Aumenta buffer de envio
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));

    ESP_LOGI(TAG, "Iniciando streaming para %s:%d",
             inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
    
    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Falha ao capturar frame");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int err = sendto(sock, fb->buf, fb->len, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
   
        if (err < 0) {
            if (errno == ENOMEM) {
                ESP_LOGW(TAG, "Erro no envio de pacotes: %d", errno);
                break;
            }
        }

        esp_camera_fb_return(fb);

        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_US / 1000));
    }

    close(sock);
    vTaskDelete(NULL);
}

void udp_discovery_task(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Falha ao criar socket discovery: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int broadcastEnable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    struct sockaddr_in broadcast_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_UDP_STREAM_PORT),
        .sin_addr.s_addr = inet_addr(BROADCAST_IP_ADDR)
    };

    char rx_buffer[64];

    ESP_LOGI(TAG, "Iniciando discovery UDP...");

    while (!server_found) {
        // Envia broadcast
        int err = sendto(sock, BROADCAST_MSG, strlen(BROADCAST_MSG), 0,
                         (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        if (err < 0) {
            ESP_LOGW(TAG, "Falha no broadcast: errno %d", errno);
        } else {
            ESP_LOGI(TAG, "Broadcast enviado: %s", BROADCAST_MSG);
        }

        // Aguarda resposta
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        struct timeval timeout = { .tv_sec = RESPONSE_TIMEOUT_MS / 1000, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                           (struct sockaddr *)&source_addr, &socklen);

        if (len > 0) {
            rx_buffer[len] = 0;
            ESP_LOGI(TAG, "Servidor encontrado: %s:%d - msg: %s",
                     inet_ntoa(source_addr.sin_addr), ntohs(source_addr.sin_port), rx_buffer);

            server_addr = source_addr;
            server_addr.sin_port = htons(CONFIG_UDP_STREAM_PORT);
            server_found = true;
        } else {
            ESP_LOGI(TAG, "Sem resposta, tentando novamente...");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    close(sock);
    ESP_LOGI(TAG, "Discovery concluído, iniciando streaming...");
    xTaskCreatePinnedToCore(udp_stream_task, "udp_stream_task", 8192, NULL, 5, NULL, 1);
    vTaskDelete(NULL);
}