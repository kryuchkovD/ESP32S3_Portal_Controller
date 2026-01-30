#pragma once

#include "esp_camera.h"

void setApiEndpoint(const char* baseUrl);

bool sendPhotoToApi(camera_fb_t* fb);
bool getResultFromApi();
bool sendTextToApi(const char* text);
