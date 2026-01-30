#include <WiFi.h>
#include <HTTPClient.h>  // для проверки сервера
#include "camera_server.h"
#include "hall_sensor.h"
#include "portal_servo.h"
#include "api_client.h"

// ================== Конфигурация ==================
const char* ssid     = "ssid";
const char* password = "password";

// Hall sensor (SS49E)
const int hallPin   = 14;
const int threshold = 1000;

// Servo (SG90)
const int servoPin  = 21;

// API server (PC)
const char* apiUrl = "http://192.168.0.163:5000/check";
const char* pingUrl = "http://192.168.0.163:5000/ping"; // для проверки подключения

unsigned long lastPingTime = 0; // для таймера ping
const unsigned long pingInterval = 10000; // 10 секунд

// ================== Setup ==================
void setup() {
    // 1. Базовая среда
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== Portal Controller ===");
    Serial.println("ESP32-S3 + Camera + Hall + Servo");

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    // 2. Камера
    initCamera();

    // 3. Wi-Fi
    initWiFi(ssid, password);

    // 4. HTTP сервер камеры (стрим живёт всегда)
    startCameraServer();

    // 5. Hall sensor
    initHallSensor(hallPin, threshold);

    // 6. Servo
    initServo(servoPin);   // начальное положение: ЗАКРЫТО

    // 7. API client
    setApiEndpoint(apiUrl);

    Serial.println("Stream available at:");
    Serial.println("http://" + WiFi.localIP().toString() + "/stream");
    Serial.println("System ready. Waiting for trigger...");
}

// ================== Проверка подключения к серверу ==================
void pingServer() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[PING] Wi-Fi not connected");
        return;
    }

    HTTPClient http;
    http.begin(pingUrl);

    int code = http.GET();
    if (code == 200) {
        String payload = http.getString();
        Serial.print("[PING] Server reachable: ");
        Serial.println(payload); // должно быть {"ok":true,"message":"pong"}
    } else {
        Serial.print("[PING] Server not reachable, code: ");
        Serial.println(code);
    }

    http.end();
}


void loop() {
    unsigned long now = millis();

    if (now - lastPingTime >= pingInterval) {
        pingServer();
        lastPingTime = now;
    }

     hallUpdate();
    
    if (hallEvent()) {
    Serial.println("[EVENT] Hall triggered");

    // 1. Отправка текста на сервер
    sendTextToApi("Прием. Холл сработал!");
    delay(3000);

    // 2. Захват фото с камеры
    camera_fb_t* fb = capturePhoto();
    if (fb) {
        Serial.println("[PHOTO] Captured, sending to server...");
        sendPhotoToApi(fb);
        esp_camera_fb_return(fb); // освобождаем буфер
    } else {
        Serial.println("[ERROR] Camera capture failed");
    }
const char* apiUrl = "http://192.168.0.163:5000/check";
const char* pingUrl = "http://192.168.0.163:5000/ping"; // для проверки подключения

    // 3. Получение результата доступа
    bool open = getResultFromApi();

    // ===== Управление воротами =====
    if (open) {
        Serial.println("[PORTAL] OPEN");
        openPortal();
        delay(3000);
        closePortal();
        Serial.println("[PORTAL] CLOSED");
    } else {
        Serial.println("[PORTAL] ACCESS DENIED");
    }

    delay(3000); // антидребезг}
    }
}
