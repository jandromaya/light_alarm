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

#define WIFI_SSID   CONFIG_WIFI_SSID
#define WIFI_PASSWORD   CONFIG_WIFI_PASSWORD

static const char *TAG = "main";
RTC_SLOW_ATTR static struct timeval last_sleep;
static const int ALARM_HOUR = 21;
static const int ALARM_MIN = 56;

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
    gettimeofday(&last_sleep, NULL);
    struct tm *time = localtime(&last_sleep.tv_sec);
    ESP_LOGI(TAG, "It is %d:%d:%d", time->tm_hour, time->tm_min, time->tm_sec);
    ESP_LOGI(TAG, "Entering deep sleep...");
    
    uint64_t wake_time_us;
    if (calculate_sleep_time(ALARM_HOUR, ALARM_MIN, &wake_time_us) == false) {
        ESP_LOGE(TAG, "Failed to compute sleep time");
        return;
    }
    
    ESP_ERROR_CHECK(wifi_disconnect());
    ESP_ERROR_CHECK(wifi_deinitialize());

    esp_sleep_enable_timer_wakeup(wake_time_us);
    ESP_LOGI(TAG, "Waking up in %llu microseconds", wake_time_us);
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

    ESP_ERROR_CHECK(http_dns_lookup());
    ESP_ERROR_CHECK(http_create_socket_and_set_timeouts());
    ESP_ERROR_CHECK(http_connect_to_server());
    ESP_ERROR_CHECK(http_send_request());
    ESP_ERROR_CHECK(http_receive_data());

    xTaskCreate(deep_sleep_task, "deep_sleep_task", 4096, NULL, 6, NULL);
}
