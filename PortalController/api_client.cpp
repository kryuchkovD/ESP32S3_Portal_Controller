#include "api_client.h"

#include <WiFi.h>
#include <HTTPClient.h>

// ======== CONFIG ========
static String apiBaseUrl;

// ======== API ========
void setApiEndpoint(const char* baseUrl) {
    apiBaseUrl = String(baseUrl);
}

// =======================================================
// 1. POST текст → сервер обработки
// Возвращает:
// true  → POST выполнен успешно (200)
// false → ошибка сети / сервера
// =======================================================
bool sendTextToApi(const char* text) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API] WiFi not connected");
        return false;
    }

    HTTPClient http;
    http.begin(apiBaseUrl);  // apiBaseUrl должен быть http://IP:5000/check
    http.addHeader("Content-Type", "text/plain; charset=utf-8");

    Serial.println("[API] Sending text: " + String(text));

    // Отправка текста как байты, чтобы Flask получил request.data
    int httpCode = http.POST((uint8_t*)text, strlen(text));

    Serial.printf("[API] POST returned code: %d\n", httpCode);

    http.end();

    return httpCode == HTTP_CODE_OK;
}


// =======================================================
// 2. POST JPEG → сервер обработки
// Возвращает:
// true  → POST выполнен успешно (200)
// false → ошибка сети / сервера
// =======================================================
bool sendPhotoToApi(camera_fb_t* fb) {
    if (!fb || fb->len == 0) {
        Serial.println("[API] No photo to send");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API] WiFi not connected");
        return false;
    }

    HTTPClient http;
    // используем apiBaseUrl напрямую, без добавления "/check"
    String url = apiBaseUrl;

    Serial.println("[API] POST photo -> " + url);
    Serial.printf("[API] Photo size: %u bytes\n", fb->len);

    http.begin(url);
    http.addHeader("Content-Type", "image/jpeg");

    int httpCode = http.sendRequest("POST", fb->buf, fb->len);
    http.end();

    if (httpCode == HTTP_CODE_OK) {
        Serial.println("[API] Photo uploaded successfully");
        return true;
    }

    Serial.printf("[API] POST failed, code: %d\n", httpCode);
    return false;
}

// =======================================================
// 3. GET результат допуска
// Ожидаемый ответ:
// { "access": true } или { "access": false }
// =======================================================
bool getResultFromApi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API] WiFi not connected");
        return false;
    }

    HTTPClient http;
    String url = apiBaseUrl + "/result";

    Serial.println("[API] GET result -> " + url);

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[API] GET failed, code: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    payload.trim();
    Serial.println("[API] Result payload: " + payload);

    // Минимально жёстко: true / false
    if (payload == "true" || payload == "1") {
        return true;
    }

    return false;
}
