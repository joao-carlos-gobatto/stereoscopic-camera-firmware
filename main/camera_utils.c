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
    .frame_size = FRAMESIZE_VGA,
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
    photo_config.frame_size = FRAMESIZE_VGA;
    ESP_ERROR_CHECK(esp_camera_init(&photo_config));
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        // --- All auto features disabled for full manual control ---
        s->set_exposure_ctrl(s, 0);   // [0=manual, 1=auto] Exposure control
        s->set_aec2(s, 0);            // [0=manual, 1=auto] DSP auto-exposure
        s->set_gain_ctrl(s, 0);       // [0=manual, 1=auto] Gain control
        s->set_whitebal(s, 0);        // [0=manual, 1=auto] White balance
        s->set_awb_gain(s, 0);        // [0=manual, 1=auto] AWB gain

        // Exposure (brightness/light sensitivity):
        //   Range: 0–1200 (higher = brighter, try 300–1200)
        s->set_aec_value(s, 300); // Exposure time

        // Gain (sensor sensitivity):
        //   Range: 0–30 (higher = more sensitive, more noise)
        s->set_agc_gain(s, 5); // Manual gain

        // White balance mode:
        //   0: Auto, 1: Sunny, 2: Cloudy, 3: Office, 4: Home
        s->set_wb_mode(s, 1); // Fixed white balance (1 = sunny)

        // Brightness: -2 to 2
        s->set_brightness(s, 0);
        // Contrast: -2 to 2
        s->set_contrast(s, 0);
        // Saturation: -2 to 2
        s->set_saturation(s, 0);
        // Sharpness: -2 to 2 (if supported)
        if (s->set_sharpness) s->set_sharpness(s, 0);

        // Lens correction: 1=enable, 0=disable
        s->set_lenc(s, 1);

        s->set_special_effect(s, 2); // Grayscale
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