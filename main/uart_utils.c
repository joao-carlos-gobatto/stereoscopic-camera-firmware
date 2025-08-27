#include <esp_log.h>
#include <esp_system.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"

#include "esp_camera.h"

#include "uart_utils.h"
#include "camera_utils.h"

QueueHandle_t uart_queue;
uint8_t rx_buffer[128];

static const char *TAG = "uart_test";

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

void send_ready_message(void) {
    const uint8_t ready_msg = 0x00; // Binary READY indicator
    uart_write_bytes(UART_PORT_NUM, (const char*)&ready_msg, 1);
    ESP_LOGI(TAG, "Sent READY message");
}

void send_ok_message(void) {
    const uint8_t ok_msg = 0x01; // Binary OK indicator
    uart_write_bytes(UART_PORT_NUM, (const char*)&ok_msg, 1);
}

void send_error_message(void) {
    const uint8_t error_msg = 0x02; // Binary ERROR indicator
    uart_write_bytes(UART_PORT_NUM, (const char*)&error_msg, 1);
}

void uart_rx_task(void *pvParameters) {
    uart_event_t event;
    uint8_t expected_len = 0;

    while (1) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            if (event.type == UART_DATA) {
                int len = uart_read_bytes(UART_PORT_NUM, rx_buffer, sizeof(rx_buffer) - 1, 0);
                if (len > 0) {
                    ESP_LOGI(TAG, "Received %d bytes", len);
                    if (len >= 2) { // Ensure we have command byte and length
                        uint8_t cmd = rx_buffer[0];
                        expected_len = rx_buffer[1];
                        if (len >= 2 + expected_len) { // Full packet received
                            if (cmd == CMD_TAKE_PICTURE && expected_len == 0) {
                                command_received = true;
                            } else if (cmd == CMD_SET_FRAMESIZE && expected_len == 1) {
                                uint8_t framesize_val = rx_buffer[2];
                                if (framesize_val <= 15) { // Valid range for framesizes
                                    sensor_t *s = esp_camera_sensor_get();
                                    if (s != NULL) {
                                        int res = s->set_framesize(s, (framesize_t)framesize_val);
                                        if (res == 0) {
                                            ESP_LOGI(TAG, "Stabilizing after framesize change...");
                                            for (int i = 0; i < STABILIZE_FRAMES; i++) {
                                                camera_fb_t *fb = esp_camera_fb_get();
                                                if (fb) {
                                                    esp_camera_fb_return(fb);
                                                }
                                                vTaskDelay(100 / portTICK_PERIOD_MS);
                                            }
                                            ESP_LOGI(TAG, "Stabilization complete");
                                            send_ok_message();
                                        } else {
                                            ESP_LOGE(TAG, "Failed to set framesize: %d", res);
                                            send_error_message();
                                        }
                                    } else {
                                        send_error_message();
                                    }
                                } else {
                                    ESP_LOGE(TAG, "Invalid framesize value: %d", framesize_val);
                                    send_error_message();
                                }
                            } else {
                                ESP_LOGE(TAG, "Invalid command or length: cmd=%d, len=%d", cmd, expected_len);
                                send_error_message();
                            }
                        }
                    }
                }
            } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
                ESP_LOGW(TAG, "UART buffer overflow, flushing");
                uart_flush_input(UART_PORT_NUM);
                xQueueReset(uart_queue);
            }
        }
    }
}