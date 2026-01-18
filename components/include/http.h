#pragma once

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.H"
#include "lwip/netdb.h"

#define RX_BUF_SIZE 64

//  Server settings and URL to fetch
#define WEB_HOST    "docs-examples.firebaseio.com"
#define WEB_PORT    "80"
#define WEB_PATH    "/fireblog/posts.json?print=pretty"

// Define timeouts
#define CONNECTION_TIMEOUT_SEC  10
#define SOCKET_TIMEOUT_SEC  5

esp_err_t http_dns_lookup();

esp_err_t http_create_socket_and_set_timeouts();

esp_err_t http_connect_to_server();

esp_err_t http_send_request();

esp_err_t http_receive_data();