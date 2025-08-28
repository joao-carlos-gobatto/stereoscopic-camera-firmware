#include "uart_utils.h"

QueueHandle_t uart_queue;


static const char *TAG = "uart_driver";

void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 460800,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_driver_install(UART_PORT_NUM, UART_BUFFER_SIZE * 2, UART_BUFFER_SIZE * 2, 10, &uart_queue, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
        return;
    }
    err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(err));
        return;
    }
    err = uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
        return;
    }

    err = uart_enable_rx_intr(UART_PORT_NUM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART RX interrupt enable failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "UART initialized");
}

static void send_ready_message(void) {
    const char *ready_msg = "READY\r\n";
    int sent = uart_write_bytes(UART_PORT_NUM, ready_msg, strlen(ready_msg));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send READY message");
    } else {
        ESP_LOGI(TAG, "Sent READY message");
    }
}

static void send_ok_message(void) {
    const char *ok_msg = "OK\r\n";
    uart_write_bytes(UART_PORT_NUM, ok_msg, strlen(ok_msg));
}

static void send_error_message(void) {
    const char *error_msg = "ERROR\r\n";
    uart_write_bytes(UART_PORT_NUM, error_msg, strlen(error_msg));
}

void send_image(const picture_t *picture) {
    ESP_LOGI(TAG, "Transmitting image: %u bytes", picture->len);
    uint32_t image_size = picture->len;
    int sent = uart_write_bytes(UART_PORT_NUM, (const char*)&image_size, sizeof(image_size));
    if (sent != sizeof(image_size)) {
        ESP_LOGE(TAG, "Failed to send image size: %d bytes sent", sent);
        return;
    }

    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < picture->len; i++) {
        crc = esp_crc32_le(crc, &picture->buf[i], 1);
    }
    crc ^= 0xFFFFFFFF;
    sent = uart_write_bytes(UART_PORT_NUM, (const char*)&crc, sizeof(crc));
    if (sent != sizeof(crc)) {
        ESP_LOGE(TAG, "Failed to send CRC: %d bytes sent", sent);
        return;
    }

    size_t bytes_sent = 0;
    while (bytes_sent < picture->len) {
        size_t chunk_size = MIN(CHUNK_SIZE, picture->len - bytes_sent);
        sent = uart_write_bytes(UART_PORT_NUM, (const char*)(picture->buf + bytes_sent), chunk_size);
        if (sent < 0) {
            ESP_LOGE(TAG, "UART transmission error at %u bytes", bytes_sent);
            break;
        }
        bytes_sent += sent;
        vTaskDelay(50 / portTICK_PERIOD_MS); // Increased delay for stability
    }
    ESP_LOGI(TAG, "Image transmission complete");
}

void uart_rx_task(void *pvParameters) {
    uart_event_t event;
    char rx_buffer[128];

    camera_startup();

    camera_sensors_warmup();

    send_ready_message();

    while (1) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    {
                        uint8_t cmd_byte;
                        int len = uart_read_bytes(UART_PORT_NUM, &cmd_byte, 1, 0);  // Read command byte
                        if (len == 1) {
                            len = uart_read_bytes(UART_PORT_NUM, rx_buffer, sizeof(rx_buffer) - 1, 0);  // Read command string
                            if (len > 0) {
                                rx_buffer[len] = 0;
                                ESP_LOGI(TAG, "Received command byte: %d, data: %s", cmd_byte, rx_buffer);

                                // Determine command based on byte
                                command_t cmd = (command_t)cmd_byte;
                                switch (cmd) {
                                    case CMD_TAKE_PICTURE:
                                        if (strstr(rx_buffer, "TAKE_PICTURE") != NULL) {
                                            picture_t *picture = capture_image();
                                            if (picture) {
                                                send_image(picture);
                                                free(picture->buf);
                                                free(picture);
                                                ESP_LOGI(TAG, "Resources freed, preparing to send READY");
                                                vTaskDelay(100 / portTICK_PERIOD_MS);
                                            }
                                            uart_flush_input(UART_PORT_NUM);
                                            send_ready_message();
                                        } else {
                                            ESP_LOGE(TAG, "Invalid TAKE_PICTURE format");
                                            send_error_message();
                                        }
                                        break;

                                    case CMD_SET_FRAMESIZE:
                                        if (strstr(rx_buffer, "SET_FRAMESIZE") != NULL) {
                                            char *token = strtok(rx_buffer, " ");
                                            token = strtok(NULL, " ");
                                            if (token) {
                                                int size = atoi(token);
                                                if (size >= FRAMESIZE_96X96 && size <= FRAMESIZE_UXGA) {
                                                    set_camera_framesize((framesize_t)size);
                                                    send_ok_message();
                                                } else {
                                                    ESP_LOGE(TAG, "Invalid framesize value: %d", size);
                                                    send_error_message();
                                                }
                                            } else {
                                                ESP_LOGE(TAG, "No framesize value provided");
                                                send_error_message();
                                            }
                                        } else {
                                            ESP_LOGE(TAG, "Invalid SET_FRAMESIZE format");
                                            send_error_message();
                                        }
                                        break;

                                    default:
                                        ESP_LOGW(TAG, "Unknown command byte: %d", cmd_byte);
                                        send_error_message();
                                        break;
                                }
                            }
                        }
                    }
                    break;

                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART buffer overflow, flushing");
                    uart_flush_input(UART_PORT_NUM);
                    xQueueReset(uart_queue);
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown UART event");
                    break;
            }
        }
    }
}