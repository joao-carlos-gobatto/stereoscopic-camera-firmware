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

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    uint32_t frame_counter = 0;

    ESP_LOGI(TAG, "Iniciando streaming para %s:%d",
             inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

    while (1) {
        int64_t frame_start = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Falha ao capturar frame");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Calcula número total de pacotes
        uint16_t total_packets = fb->len / PACKET_SIZE;
        if (fb->len % PACKET_SIZE) total_packets++;

        // Envia em fragmentos com cabeçalho
        for (uint16_t i = 0; i < total_packets; i++) {
            size_t chunk_size = (i < total_packets - 1) ? PACKET_SIZE : (fb->len - (i * PACKET_SIZE));

            uint8_t buffer[sizeof(frame_header_t) + PACKET_SIZE];
            frame_header_t hdr = {
                .frame_number = frame_counter,
                .packet_id = i,
                .total_packets = total_packets
            };
            memcpy(buffer, &hdr, sizeof(hdr));
            memcpy(buffer + sizeof(hdr), fb->buf + (i * PACKET_SIZE), chunk_size);

            int err = sendto(sock, buffer, sizeof(hdr) + chunk_size, 0,
                             (struct sockaddr *)&server_addr, sizeof(server_addr));
            if (err < 0 && errno != EAGAIN) {
                ESP_LOGW(TAG, "Erro no envio (frame %lu pkt %u): %d", frame_counter, i, errno);
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(1)); // aliviar buffer
        }

        esp_camera_fb_return(fb);
        frame_counter++;

        // Mantém taxa de 15 fps
        int64_t frame_time = esp_timer_get_time() - frame_start;
        if (frame_time < FRAME_INTERVAL_US) {
            vTaskDelay(pdMS_TO_TICKS((FRAME_INTERVAL_US - frame_time) / 1000));
        }

        ESP_LOGD(TAG, "Frame %lu enviado (%u pacotes, %lld ms)",
                 frame_counter, total_packets, frame_time / 1000);
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