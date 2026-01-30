#include <Arduino.h>
#include "hall_sensor.h"

// ======== PRIVATE STATE ========
static int hallPin = -1;
static int hallThreshold = 1000;
static int baseline = 0;

static bool currentState = false;
static bool prevState = false;

// ======== INIT ========
void initHallSensor(int pin, int threshold) {
    hallPin = pin;
    hallThreshold = threshold;

    pinMode(hallPin, INPUT);
    analogReadResolution(12);
    delay(50);

    baseline = analogRead(hallPin);
}

// ======== UPDATE ========
void hallUpdate() {
    int value = analogRead(hallPin);
    int diff = abs(value - baseline);

    currentState = (diff >= hallThreshold);
}

// ======== EVENT API ========
bool hallEvent() {
    bool event = currentState && !prevState;
    prevState = currentState;
    return event;
}

bool hallActive() {
    return currentState;
}
