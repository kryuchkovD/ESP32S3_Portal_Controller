#include <Arduino.h>
#include <ESP32Servo.h>
#include "portal_servo.h"

static Servo portalServo;
static int servoPin = -1;

// ⚠️ Подстрой под механику!
static const int SERVO_CLOSED = 10;
static const int SERVO_OPEN   = 80;

void initServo(int pin) {
    servoPin = pin;

    portalServo.setPeriodHertz(50);          // 50 Гц — стандарт для сервы
    portalServo.attach(servoPin, 500, 2500); 

    portalServo.write(SERVO_CLOSED);
    delay(500);
    portalServo.detach();
}

void openPortal() {
    if (servoPin < 0) return;

    portalServo.attach(servoPin);
    portalServo.write(SERVO_OPEN);
    delay(500);
    portalServo.detach();
}

void closePortal() {
    if (servoPin < 0) return;

    portalServo.attach(servoPin);
    portalServo.write(SERVO_CLOSED);
    delay(500);
    portalServo.detach();
}
