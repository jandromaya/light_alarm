#pragma once

#include <sys/param.h>
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.H"
#include "lwip/netdb.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"

#define MAX_HTTP_OUTPUT_BUFFER 2048

esp_err_t http_send_request();
