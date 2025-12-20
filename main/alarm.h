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


/**
 * @brief calculates amount of time in microseconds between the current time and the desired wake-up time.
 * @param int wakeup_time (between 0 and 23), int wakeup_min (between 0 and 59)
 * output: true if diff calcualted correctly, false otherwise (probably if inputs out of range)
 */
bool calculate_sleep_time(int wakeup_hour, int wakeup_min, uint64_t *sleep_us_out);

esp_err_t wifi_initialize(void);

esp_err_t wifi_connect(const char* wifi_ssid, const char* wifi_password);

esp_err_t wifi_disconnect(void);

esp_err_t wifi_deinitialize(void);

