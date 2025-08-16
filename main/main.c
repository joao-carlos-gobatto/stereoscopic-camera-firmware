#include <esp_event.h>
#include <string.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"

#include "esp_camera.h"

#define UART_PORT_NUM      UART_NUM_0  // Use UART0 for USB-serial
#define TXD_PIN            1           // GPIO1 (TX for USB-serial)
#define RXD_PIN            3           // GPIO3 (RX for USB-serial)
#define UART_BUFFER_SIZE   (2048)
#define CHUNK_SIZE         (512)

static const char *TAG = "uart_test";
static QueueHandle_t uart_queue;
static volatile bool command_received = false;
static char rx_buffer[128];

static camera_config_t photo_config = {
    .pin_pwdn = CONFIG_PWDN,
    .pin_reset = CONFIG_RESET,
    .pin_xclk = CONFIG_XCLK,
    .pin_sscb_sda = CONFIG_SDA,
    .pin_sscb_scl = CONFIG_SCL,

    .pin_d7 = CONFIG_D7,
    .pin_d6 = CONFIG_D6,
    .pin_d5 = CONFIG_D5,
    .pin_d4 = CONFIG_D4,
    .pin_d3 = CONFIG_D3,
    .pin_d2 = CONFIG_D2,
    .pin_d1 = CONFIG_D1,
    .pin_d0 = CONFIG_D0,
    .pin_vsync = CONFIG_VSYNC,
    .pin_href = CONFIG_HREF,
    .pin_pclk = CONFIG_PCLK,

    //XCLK 20MHz or 10MHz
    .xclk_freq_hz = CONFIG_XCLK_FREQ,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_UXGA,   //QQVGA-UXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 12, //0-63 lower number means higher quality
    .fb_count = 1       //if more than one, i2s runs in continuous mode. Use only with JPEG
};

typedef struct {
    uint8_t *buf;
    size_t len;
} picture_t;

static void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
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

static void send_image(const picture_t *picture) {
    ESP_LOGI(TAG, "Transmitting image: %u bytes", picture->len);
    uint32_t image_size = picture->len;
    int sent = uart_write_bytes(UART_PORT_NUM, (const char*)&image_size, sizeof(image_size));
    if (sent != sizeof(image_size)) {
        ESP_LOGE(TAG, "Failed to send image size: %d bytes sent", sent);
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
        ESP_LOGD(TAG, "Sent %d bytes, total %u/%u", sent, bytes_sent, picture->len);
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Image transmission complete");
}

static picture_t* capture_image(void) {
    camera_fb_t *pic = esp_camera_fb_get();
    if (!pic) {
        ESP_LOGE(TAG, "Failed to capture image");
        return NULL;
    }

    picture_t *picture = malloc(sizeof(picture_t));
    if (picture == NULL) {
        ESP_LOGE(TAG, "Error allocating picture struct");
        esp_camera_fb_return(pic);
        return NULL;
    }

    uint8_t *buf = malloc(pic->len);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Error allocating picture buffer");
        free(picture);
        esp_camera_fb_return(pic);
        return NULL;
    }

    memcpy(buf, pic->buf, pic->len);
    picture->buf = buf;
    picture->len = pic->len;
    esp_camera_fb_return(pic);
    ESP_LOGI(TAG, "Captured image: %u bytes", picture->len);
    return picture;
}

static void uart_rx_task(void *pvParameters) {
    uart_event_t event;
    size_t buffered_size;

    while (1) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            if (event.type == UART_DATA) {
                int len = uart_read_bytes(UART_PORT_NUM, rx_buffer, sizeof(rx_buffer) - 1, 0);
                if (len > 0) {
                    rx_buffer[len] = 0;
                    ESP_LOGI(TAG, "Received: %s", rx_buffer);
                    if (strstr(rx_buffer, "TAKE_PICTURE") != NULL) {
                        command_received = true;
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

static void command_task(void *pvParameters) {
    while (1) {
        if (command_received) {
            picture_t *picture = capture_image();
            if (picture) {
                send_image(picture);
                free(picture->buf);
                free(picture);
            }
            command_received = false;
            send_ready_message();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting application");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    uart_init();
    ESP_ERROR_CHECK(esp_camera_init(&photo_config));
    send_ready_message();

    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx_task", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(command_task, "command_task", 8192, NULL, 1, NULL, 1); // Increased stack for camera
}