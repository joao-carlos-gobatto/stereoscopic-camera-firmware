#include "camera_utils.h"

camera_config_t photo_config = {
    .pin_pwdn = CONFIG_PWDN,
    .pin_reset = CONFIG_RESET,
    .pin_xclk = CONFIG_XCLK,
    .pin_sccb_sda = CONFIG_SDA,
    .pin_sccb_scl = CONFIG_SCL,
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
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12,
    .fb_count = 1,
    .grab_mode = CAMERA_GRAB_LATEST,
    .fb_location = CAMERA_FB_IN_PSRAM,
};

static const char *TAG = "camera_driver";

esp_err_t camera_startup(void) {
    if (esp_psram_is_initialized()) {
        photo_config.fb_count = 2; //Improve frame rate with PSRAM
    } else {
        photo_config.fb_count = 1;
    }
    photo_config.frame_size = FRAMESIZE_QVGA;
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
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t set_camera_framesize(framesize_t size) {
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_ERR_NO_MEM;
    } else {
        if(size == s->status.framesize) {
            ESP_LOGI(TAG, "Camera resolution already set to %d", size);
            return ESP_OK;
        } else {
            int err = s->set_framesize(s, size);
            if (err == 0) {
                ESP_LOGI(TAG, "Camera resolution set to %d", size);
                return ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to set camera resolution: error 0x%x", err);
                return ESP_ERR_CAMERA_BASE+err;
            }
        }
    }
}