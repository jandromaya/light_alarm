/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_rx.h"
#include "driver/rtc_io.h"
#include "ir_nec_decoder.h"
#include "servo.h"
#include "wifi.h"
#include "esp_wifi.h"
#include "esp_netif_sntp.h"
#include "http.h"
#include "cJSON.h"

#define WIFI_SSID   CONFIG_WIFI_SSID
#define WIFI_PASSWORD   CONFIG_WIFI_PASSWORD

static const char *TAG = "main";
RTC_SLOW_ATTR static struct timeval last_sleep;

static int ALARM_HOUR;
static int ALARM_MIN;
static bool ALARM_ENABLED;

/**
 * @brief calculates amount of time in microseconds between the current time and the desired wake-up time.
 * @param int wakeup_time (between 0 and 23), int wakeup_min (between 0 and 59)
 * output: true if diff calcualted correctly, false otherwise (probably if inputs out of range)
 */
bool calculate_sleep_time(int wakeup_hour, int wakeup_min, uint64_t *sleep_us_out) {
    if (wakeup_hour < 0 || wakeup_hour > 23 ||
         wakeup_min < 0 || wakeup_min > 59) {
        return false;
    }


    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update system time within 10s timeout");
    }


    time_t now;
    struct tm target_time;
    time(&now);
    setenv("TZ", "CST6CDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    localtime_r(&now, &target_time);
    // set time to desired wake up time (may be on same or next day)
    target_time.tm_sec = 0;
    if (target_time.tm_hour < wakeup_hour ||
        (target_time.tm_hour == wakeup_hour &&
         target_time.tm_min < wakeup_min)) {
        // today
        target_time.tm_hour = wakeup_hour;
        target_time.tm_min = wakeup_min;
    }
    else {
        // next day
        target_time.tm_hour = wakeup_hour;
        target_time.tm_min = wakeup_min;
        target_time.tm_mday += 1;
    }

    // return difference in microseconds
    time_t wakeup_time = mktime(&target_time);
    
    if (wakeup_time <= now) // safety check
        return false;

    *sleep_us_out = (uint64_t)(wakeup_time - now) * 1000000ULL;
    
    return true;
}

static bool example_rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t receive_queue = (QueueHandle_t)user_data;
    // send the received RMT symbols to the parser task
    xQueueSendFromISR(receive_queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void deep_sleep_task() {
    // first we just print what time it is now 
    gettimeofday(&last_sleep, NULL);
    struct tm *time = localtime(&last_sleep.tv_sec);
    ESP_LOGI(TAG, "It is %d:%d:%d", time->tm_hour, time->tm_min, time->tm_sec);
    ESP_LOGI(TAG, "Entering deep sleep...");
    
    // now we find out how many us we need to sleep for using calculate_sleep_time
    uint64_t wake_time_us;
    if (calculate_sleep_time(ALARM_HOUR, ALARM_MIN, &wake_time_us) == false) {
        ESP_LOGE(TAG, "Failed to compute sleep time");
        return;
    }
    
    // now that we have gotten all the HTTP and SNTP data we need, wifi is
    // no longer needed
    ESP_ERROR_CHECK(wifi_disconnect());
    ESP_ERROR_CHECK(wifi_deinitialize());

    // if the alarm is enabled, we should set a wakeup time
    if (ALARM_ENABLED) {
        esp_sleep_enable_timer_wakeup(wake_time_us);
        ESP_LOGI(TAG, "Waking up in %llu microseconds", wake_time_us);
    }

    // otherwise, we can just sleep and wait for an interrupt wakeup
    esp_deep_sleep_start();
}

static void ext_wakeup_setup() {
    esp_sleep_enable_ext0_wakeup(EXAMPLE_IR_RX_GPIO_NUM, 0);

    rtc_gpio_pulldown_dis(EXAMPLE_IR_RX_GPIO_NUM);
    rtc_gpio_pullup_en(EXAMPLE_IR_RX_GPIO_NUM);
}

void print_wakeup_cause() {
    struct timeval now;
    gettimeofday(&now, NULL);

    int sleep_time_ms = (now.tv_sec - last_sleep.tv_sec) * 1000 + (now.tv_usec - last_sleep.tv_usec) / 1000;
    
    switch(esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(TAG, "Woke up from ext0");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Woke up from timer");
            break;
        default:
            ESP_LOGE(TAG, "ERROR: Something else woke me up. Time: %d", sleep_time_ms);
            break;
    }
}

esp_err_t process_web_data(char *buffer) {
    cJSON *json;    // cJSON variables for each cJSON object we access
    cJSON *alarm;
    cJSON *enabled;
    cJSON *hour;
    cJSON *minute;
    
    // Parsing the JSON
    json = cJSON_Parse(buffer);

    if (json == NULL) { // checking for errors, per cJSON docs, only need to do after parse
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        return ESP_FAIL;
    }

    // After parsing the JSON, we get each key-value pair with GetObjectItem
    alarm = cJSON_GetObjectItem(json, "alarm");
    enabled = cJSON_GetObjectItem(alarm, "enabled");
    hour = cJSON_GetObjectItem(alarm, "hour");
    minute = cJSON_GetObjectItem(alarm, "minute");

    // we set the ALARM variables to their correct values
    ALARM_ENABLED = (enabled->type == cJSON_True) ? true : false;
    ESP_LOGI(TAG, "ALARM_ENABLED = %d", ALARM_ENABLED);
    ALARM_HOUR = hour->valueint;
    ESP_LOGI(TAG, "ALARM_HOUR = %d", ALARM_HOUR);
    ALARM_MIN = minute->valueint;
    ESP_LOGI(TAG, "ALARM_MIN = %d", ALARM_MIN);

    // we no longer need the JSON items, so we can free all the memory associated with it
    cJSON_Delete(json);
    return ESP_OK;
}

void app_main(void)
{
    print_wakeup_cause();

    // ----------- IR RECEIVER SET UP ---------------------
    ESP_LOGI(TAG, "create RMT RX channel");
    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64, // amount of RMT symbols that the channel can store at a time
        .gpio_num = EXAMPLE_IR_RX_GPIO_NUM,
    };
    rmt_channel_handle_t rx_channel = NULL;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &rx_channel));

    ESP_LOGI(TAG, "register RX done callback");
    QueueHandle_t receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    assert(receive_queue);
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = example_rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, receive_queue));

    // the following timing requirement is based on NEC protocol
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1250,     // the shortest duration for NEC signal is 560us, 1250ns < 560us, valid signal won't be treated as noise
        .signal_range_max_ns = 12000000, // the longest duration for NEC signal is 9000us, 12000000ns > 9000us, the receive won't stop early
    };

    ESP_LOGI(TAG, "enable RMT RX channel");
    // ESP_ERROR_CHECK(rmt_enable(tx_channel));
    ESP_ERROR_CHECK(rmt_enable(rx_channel));

    // save the received RMT symbols
    rmt_symbol_word_t raw_symbols[64]; // 64 symbols should be sufficient for a standard NEC frame
    rmt_rx_done_event_data_t rx_data;
    // ready to receive
    ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config));
    // ----------------------------- END OF IR REC SET UP -----------------------
    
    setup_servo();

    ext_wakeup_setup();
    
    int angle = 0;

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER)
    {
        angle = 15;
        servo_set_angle(angle);
        vTaskDelay(pdMS_TO_TICKS(1000));
        angle = 0;
        servo_set_angle(angle);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    while (xQueueReceive(receive_queue, &rx_data, pdMS_TO_TICKS(5000)) == pdPASS) {
        // parse the receive symbols and print the result
        // example_parse_nec_frame(rx_data.received_symbols, rx_data.num_symbols);
        int parsed_frame = example_parse_nec_frame(rx_data.received_symbols, rx_data.num_symbols);
        if (parsed_frame == 0xE916) {
            ESP_LOGI(TAG, "0 was pressed!");
            angle = -15;
            servo_set_angle(angle);
        }
        else if (parsed_frame == 0xF30C) {
            ESP_LOGI(TAG, "1 was pressed!");
            angle = 15;
            servo_set_angle(angle);
        } 
        else {
            ESP_LOGE(TAG, "Bad input (neither 1 or 0)");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        angle = 0;
        servo_set_angle(angle);
        // start receive again
        ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config));
    }
    
    ESP_ERROR_CHECK(wifi_initialize());
    ESP_ERROR_CHECK(wifi_connect(WIFI_SSID, WIFI_PASSWORD));

    char *buf;
    ESP_ERROR_CHECK(http_send_request(&buf));
    ESP_LOGI(TAG, "PRINTING FROM MAIN:");
    printf("%s\r\n", buf);

    ESP_ERROR_CHECK(process_web_data(buf));
    
    

    xTaskCreate(deep_sleep_task, "deep_sleep_task", 4096, NULL, 6, NULL);
}
