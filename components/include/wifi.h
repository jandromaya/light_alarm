#pragma once

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_sntp.h"

esp_err_t wifi_initialize(void);

esp_err_t wifi_connect(const char* wifi_ssid, const char* wifi_password);

esp_err_t wifi_disconnect(void);

esp_err_t wifi_deinitialize(void);

