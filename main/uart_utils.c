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
    }
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    return ESP_OK;
}

static esp_err_t send_image_size(uint32_t image_size) {
    int sent = uart_write_bytes(UART_PORT_NUM, &image_size, sizeof(uint32_t));
    if (sent != sizeof(image_size)) {
        return ESP_FAIL;
    }
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    return ESP_OK;
}

static esp_err_t send_image_crc(uint32_t crc) {
    int sent = uart_write_bytes(UART_PORT_NUM, &crc, sizeof(uint32_t));
    if (sent != sizeof(crc)) {
        return ESP_FAIL;
    }
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    return ESP_OK;
}

static esp_err_t send_image_data(const picture_t *picture) {
    size_t bytes_sent = 0;
    while (bytes_sent < picture->len) {
        size_t chunk_size = MIN(CHUNK_SIZE, picture->len - bytes_sent);
        int sent = uart_write_bytes(UART_PORT_NUM, (picture->buf + bytes_sent), chunk_size);
        if (sent < 0) {
            return ESP_FAIL;
        }
        bytes_sent += sent;
        uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

void uart_rx_task(void *pvParameters) {
    uart_event_t event;
    uint8_t cmd_value = CMD_INVALID;
    framesize_t framesize_value = FRAMESIZE_QVGA; // Default to max resolution
    picture_t *picture = NULL;

    ESP_ERROR_CHECK(send_uart_message(READY_STRING));

    while (1) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    {
                        cmd_value = CMD_INVALID;
                        framesize_value = FRAMESIZE_QVGA;
                        int len = uart_read_bytes(UART_PORT_NUM, &cmd_value, 1, 0);  // Read command value
                        if (len == 1) {
                            command_t cmd = cmd_value;
                            switch (cmd) {
                                case CMD_SET_FRAMESIZE:
                                    int len = uart_read_bytes(UART_PORT_NUM, &framesize_value, 1, 0);  // Read framesize value
                                    if (len == 1) {
                                        if (framesize_value >= FRAMESIZE_96X96 && framesize_value <= FRAMESIZE_UXGA) {
                                            ESP_ERROR_CHECK(set_camera_framesize(framesize_value));
                                            ESP_ERROR_CHECK(camera_sensors_warmup());
                                            ESP_ERROR_CHECK(send_uart_message(OK_STRING));
                                        } else {
                                            ESP_LOGE(TAG, "Invalid framesize value: %d", framesize_value);
                                            ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                        }
                                    } else {
                                        ESP_LOGE(TAG, "Failed to read framesize value");
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
                                    ESP_ERROR_CHECK(uart_flush_input(UART_PORT_NUM));
                                    break;

                                case CMD_TAKE_PICTURE:
                                picture = capture_image();
                                if (picture) {
                                    ESP_ERROR_CHECK(send_uart_message(OK_STRING));
                                    ESP_ERROR_CHECK(uart_flush_input(UART_PORT_NUM));
                                } else {
                                    ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                }
                                break;

                                case CMD_PICTURE_SIZE:
                                    if (picture) {
                                        if (send_image_size(picture->len) == ESP_OK) {
                                            ESP_ERROR_CHECK(uart_flush_input(UART_PORT_NUM));
                                        } else {
                                            ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                        }
                                    } else {
                                        ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                    }
                                    break;
                                case CMD_PICTURE_CRC:
                                    if (picture) {
                                        uint32_t crc = esp_crc32_le(0, picture->buf, picture->len); // Init=0
                                        if (send_image_crc(crc) == ESP_OK) {
                                            ESP_ERROR_CHECK(uart_flush_input(UART_PORT_NUM));
                                        } else {
                                            ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                        }
                                    } else {
                                        ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                    }
                                    break;
                                case CMD_PICTURE_DATA:
                                    if (picture) {
                                        if (send_image_data(picture) == ESP_OK) {
                                            ESP_ERROR_CHECK(uart_flush_input(UART_PORT_NUM));
                                        } else {
                                            ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                        }
                                    } else {
                                        ESP_ERROR_CHECK(send_uart_message(ERROR_STRING));
                                    }
                                    break;

                                default:
                                    ESP_LOGW(TAG, "Unknown command byte: %d", cmd_value);
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