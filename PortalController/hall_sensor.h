#pragma once

void initHallSensor(int pin, int threshold);
void hallUpdate();

bool hallEvent();    
bool hallActive();   // текущее состояние
