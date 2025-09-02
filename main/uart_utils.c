#include "uart_utils.h"

QueueHandle_t uart_queue;

static const char *TAG = "uart_driver";

esp_err_t uart_init(void) {
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
        return err;
    }
    err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_enable_rx_intr(UART_PORT_NUM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART RX interrupt enable failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "UART initialized");
    return ESP_OK;
}

static esp_err_t send_uart_message(const char *msg) {
    int sent = uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
    if (sent < 0) {
        return ESP_FAIL;
    } else {
        return ESP_OK;
    }
}

void send_image(const picture_t *picture) {
    uint32_t image_size = picture->len;
    send_uart_message(IMAGE_START_STRING);
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
            ESP_LOGE(TAG, "UART transmission ERROR_STRING at %u bytes", bytes_sent);
            break;
        }
        bytes_sent += sent;
        vTaskDelay(50 / portTICK_PERIOD_MS); // Delay for stability
    }
    ESP_ERROR_CHECK(send_uart_message(READY_STRING));
}

void uart_rx_task(void *pvParameters) {
    uart_event_t event;

    ESP_ERROR_CHECK(send_uart_message(READY_STRING));

    while (1) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    {
                        char cmd_byte = 0, payload_byte = 0;
                        int len = uart_read_bytes(UART_PORT_NUM, &cmd_byte, 1, 0);  // Read command byte
                        if (len == 1) {
                            command_t cmd = (command_t)cmd_byte;
                            switch (cmd) {
                                case CMD_TAKE_PICTURE:
                                    picture_t *picture = capture_image();
                                    if (picture) {
                                        send_image(picture);
                                        free(picture->buf);
                                        free(picture);
                                        ESP_LOGI(TAG, "Resources freed, preparing to send READY_STRING");
                                        vTaskDelay(100 / portTICK_PERIOD_MS);
                                    }
                                    ESP_ERROR_CHECK(uart_flush_input(UART_PORT_NUM));
                                    ESP_ERROR_CHECK(send_uart_message(OK_STRING));
                                    break;

                                case CMD_SET_FRAMESIZE:
                                    int len = uart_read_bytes(UART_PORT_NUM, &payload_byte, 1, 0);  // Read payload byte
                                    if (len == 1) {
                                        if (payload_byte >= FRAMESIZE_96X96 && payload_byte <= FRAMESIZE_UXGA) {
                                            ESP_ERROR_CHECK(set_camera_framesize((framesize_t)payload_byte));
                                            ESP_ERROR_CHECK(camera_sensors_warmup());
                                            ESP_ERROR_CHECK(send_uart_message(OK_STRING));
                                        } else {
                                            ESP_LOGE(TAG, "Invalid framesize value: %d", payload_byte);
                                            ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                        }
                                    } else {
                                        ESP_LOGE(TAG, "Failed to read framesize payload");
                                        ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                        break;
                                    }
                                    break;

                                case CMD_CAMERA_SIDE:
                                    if (CONFIG_CAMERA_SIDE == RIGHT)
                                    {
                                        ESP_ERROR_CHECK(send_uart_message(RIGHT_STRING));
                                    } else if (CONFIG_CAMERA_SIDE == LEFT)
                                    {
                                        ESP_ERROR_CHECK(send_uart_message(LEFT_STRING));
                                    }
                                    break;

                                default:
                                    ESP_LOGW(TAG, "Unknown command byte: %d", cmd_byte);
                                    ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                    break;
                            }
                        } else {
                            ESP_LOGE(TAG, "Failed to read command byte");
                            ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                        }
                    }
                    break;

                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART buffer overflow, flushing");
                    ESP_ERROR_CHECK(uart_flush_input(UART_PORT_NUM));
                    xQueueReset(uart_queue);
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown UART event");
                    break;
            }
        }
    }
}