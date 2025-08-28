#include "camera_utils.h"

SemaphoreHandle_t take_picture_mutex;
volatile bool command_received = false;
camera_config_t photo_config = {
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
    .xclk_freq_hz = CONFIG_XCLK_FREQ,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_UXGA,
    .jpeg_quality = 12,
    .fb_count = 1
};

static const char *TAG = "camera_driver";

picture_t *capture_image(void) {
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
    ESP_LOGI(TAG, "Captured image: %u bytes", pic->len);
    return picture;
}

void camera_sensors_warmup(void) {
    ESP_LOGI(TAG, "Warming up camera...");
    for (int i = 0; i < CONFIG_CAMERA_SENSOR_WARMUP_FRAMES; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            esp_camera_fb_return(fb);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void camera_startup(void) {
    ESP_ERROR_CHECK(esp_camera_init(&photo_config));
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_exposure_ctrl(s, 1);  // Enable auto-exposure
        s->set_aec2(s, 1);           // Enable DSP auto-exposure
        s->set_ae_level(s, 0);       // Neutral auto-exposure level
        s->set_gain_ctrl(s, 1);      // Enable auto-gain
        s->set_whitebal(s, 1);       // Enable auto-white balance
        s->set_awb_gain(s, 1);       // Enable AWB gain
        s->set_wb_mode(s, 0);        // Auto WB mode
    }
    ESP_LOGI(TAG, "Camera warmed up");
}

void set_camera_framesize(framesize_t size) {
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        if (s->set_framesize(s, size) == 0) {
            ESP_LOGI(TAG, "Camera resolution set to %d", size);
        } else {
            ESP_LOGE(TAG, "Failed to set camera resolution");
        }
    } else {
        ESP_LOGE(TAG, "Failed to get camera sensor");
    }
}