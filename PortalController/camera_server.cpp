#include <WiFi.h>
#include <Arduino.h> // для delay()
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "camera_server.h"
//#include "camera_pins.h"
#include "camera_index.h"
#include "board_config.h"
#include <string.h>
#include <stdlib.h>

// ================= LED =================
#if defined(LED_GPIO_NUM)
#define CONFIG_LED_MAX_INTENSITY 255
static int led_duty = 0;
static bool isStreaming = false;

void enable_led(bool en) {
    int duty = en ? led_duty : 0;
    if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY))
        duty = CONFIG_LED_MAX_INTENSITY;
    ledcWrite(LED_GPIO_NUM, duty);
}
#endif

// ================= RA Filter (FPS smoothing) =================
typedef struct {
    size_t size;
    size_t index;
    size_t count;
    int sum;
    int *values;
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t* ra_filter_init(ra_filter_t *filter, size_t sample_size) {
    memset(filter, 0, sizeof(ra_filter_t));
    filter->values = (int*)malloc(sample_size * sizeof(int));
    if (!filter->values) return NULL;
    memset(filter->values, 0, sample_size * sizeof(int));
    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t *filter, int value) {
    if (!filter->values) return value;
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index = (filter->index + 1) % filter->size;
    if (filter->count < filter->size) filter->count++;
    return filter->sum / filter->count;
}

// ================= CAMERA =================
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;

    if (esp_camera_init(&config) != ESP_OK) {
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);  // пример настройки сенсора
    return true;
}

// ================= WIFI =================
bool initWiFi(const char* ssid, const char* password) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    WiFi.setSleep(false);  // отключаем энергосбережение
    return true;
}

// ================= CAPTURE =================
camera_fb_t* capturePhoto() {
#if defined(LED_GPIO_NUM)
    enable_led(true);
    vTaskDelay(150 / portTICK_PERIOD_MS);
#endif
    camera_fb_t* fb = esp_camera_fb_get();
#if defined(LED_GPIO_NUM)
    enable_led(false);
#endif
    return fb;
}

// ================= HTTP SERVER =================
httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t* fb = capturePhoto();
    if (!fb) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t* fb = NULL;
    char part_buf[128];
    static int64_t last_frame = 0;
    if (!last_frame) last_frame = esp_timer_get_time();

    httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
#if defined(LED_GPIO_NUM)
    isStreaming = true;
    enable_led(true);
#endif

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) break;

        size_t _jpg_buf_len;
        uint8_t* _jpg_buf;
        if (fb->format != PIXFORMAT_JPEG) {
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            esp_camera_fb_return(fb);
            fb = NULL;
            if (!jpeg_converted) break;
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        size_t hlen = snprintf(part_buf, 128, _STREAM_PART, _jpg_buf_len, 0, 0);
        httpd_resp_send_chunk(req, part_buf, hlen);
        httpd_resp_send_chunk(req, (const char*)_jpg_buf, _jpg_buf_len);

        if (fb) esp_camera_fb_return(fb);
        else if (_jpg_buf) free(_jpg_buf);

        int64_t fr_end = esp_timer_get_time();
        ra_filter_run(&ra_filter, (fr_end - last_frame) / 1000);
        last_frame = fr_end;
    }

#if defined(LED_GPIO_NUM)
    isStreaming = false;
    enable_led(false);
#endif
    return ESP_OK;
}

// ================= START SERVER =================
void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    httpd_uri_t capture_uri = { .uri="/capture", .method=HTTP_GET, .handler=capture_handler, .user_ctx=NULL };
    httpd_uri_t stream_uri  = { .uri="/stream",  .method=HTTP_GET, .handler=stream_handler,  .user_ctx=NULL };

    if (httpd_start(&camera_httpd, &config) == ESP_OK)
        httpd_register_uri_handler(camera_httpd, &capture_uri);

    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
        httpd_register_uri_handler(stream_httpd, &stream_uri);

    ra_filter_init(&ra_filter, 20);
}
