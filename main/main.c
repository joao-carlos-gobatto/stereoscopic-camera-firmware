#include <esp_event.h>
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

static const char *TAG = "example_take_picture";
QueueHandle_t queue;

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

static void uart_transmit_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting UART transmit task");
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_driver_install(UART_PORT_NUM, UART_BUFFER_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    err = uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "UART initialized");
    while (1) {
        picture_t *picture;
        if (xQueueReceive(queue, &picture, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Transmitting image: %u bytes", picture->len);
            uint32_t image_size = picture->len;
            int sent = uart_write_bytes(UART_PORT_NUM, (const char*)&image_size, sizeof(image_size));
            if (sent != sizeof(image_size)) {
                ESP_LOGE(TAG, "Failed to send image size: %d bytes sent", sent);
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

            free(picture->buf);
            free(picture);
            ESP_LOGI(TAG, "Image transmission complete");
        }
    }
}

static void take_picture_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting take picture task");
    ESP_ERROR_CHECK(esp_camera_init(&photo_config));
    while (1) {
        // int64_t timestamp = esp_timer_get_time();
        camera_fb_t *pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGE(TAG, "Failed to capture image");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        picture_t *picture = malloc(sizeof(picture_t));
        if (picture == NULL) {
            ESP_LOGE(TAG, "Error allocating picture struct");
            esp_camera_fb_return(pic);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        uint8_t *buf = malloc(pic->len);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Error allocating picture buffer");
            free(picture);
            esp_camera_fb_return(pic);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        for (int i = 0; i < pic->len; i++) {
        buf[i] = pic->buf[i];
        }
        picture->buf = buf;
        picture->len = pic->len;
        ESP_LOGI(TAG, "Captured image: %u bytes", picture->len);

        if (xQueueSend(queue, &picture, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send image to queue");
            free(picture->buf);
            free(picture);
        }
        esp_camera_fb_return(pic);
        // ESP_LOGI(TAG, "Time to take picture: %lld us", esp_timer_get_time() - timestamp);
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Capture every 5 seconds
    }
}

void app_main() {
    ESP_LOGI(TAG, "Starting application");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    queue = xQueueCreate(CONFIG_PICTURE_QUEUE_SIZE, sizeof(picture_t *));
    if (queue == NULL) {
        ESP_LOGE(TAG, "Error creating the queue");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    xTaskCreatePinnedToCore(take_picture_task, "take_picture", 12288, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(uart_transmit_task, "uart_transmit", 4096, NULL, 2, NULL, 1);
}