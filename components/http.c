#include <string.h>
#include "http.h"


// Define the HTTP request
static const char *REQUEST = "GET " WEB_PATH " HTTP/1.0\r\n"
                             "Host: " WEB_HOST "\r\n"
                             "User-Agent: esp-idf/1.0 esp32\r\n"
                             "\r\n";

static const char *TAG = "http";

static struct addrinfo *dns_res;    // DNS result from DNS lookup
static int sock;   // socket handle
static char recv_buf[RX_BUF_SIZE]; // receive buffer
static ssize_t recv_len; // amt of bytes received at at time per chunk


/**
 * @brief Performs a DNS lookup. On success, prints IP addresses, returns ESP_OK.
 * On failure, prints error message and returns ESP_FAIL
 */
esp_err_t http_dns_lookup() {
    int ret;    // var to store return code

    // hints for the DNS lookup
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };

    // Does DNS lookup, returns ESP_FAIL if any errors or no dns result
    ret = getaddrinfo(WEB_HOST, WEB_PORT, &hints, &dns_res);
    if (ret != 0 || dns_res == NULL) {
        ESP_LOGE(TAG, "Error (%d): Failed DNS lookup", ret);
        return ESP_FAIL;
    }

    // Prints IP addresses and returns ESP_OK if everything went right
    for (struct addrinfo *addr = dns_res; addr != NULL; addr = addr->ai_next) {
        if (addr->ai_family == AF_INET) //  We received an IPv4 Address
        {
            struct in_addr *ip = &((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            inet_ntop(AF_INET, ip, recv_buf, INET_ADDRSTRLEN);
            ESP_LOGI(TAG, "IPv4 Address: %s", recv_buf);
        } else if (addr->ai_family == AF_INET6) // We received an IPv6 address
        {
            struct in_addr *ip = &((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            inet_ntop(AF_INET6, ip, recv_buf, INET6_ADDRSTRLEN);
            ESP_LOGI(TAG, "IPv6 Address: %s", recv_buf);
        }
    }

    return ESP_OK;
}

/**
 * @brief This will create the sockets and set the timeouts.
 * Must be done AFTER the DNS lookup. Will free DNS result.
 * Returns ESP_OK on success, ESP_FAIL otherwise (and prints error code)
 */
esp_err_t http_create_socket_and_set_timeouts() {
    int ret;    // int for error codes for socket operations

    // Creating socket
    sock = socket(dns_res->ai_family, SOCK_STREAM, dns_res->ai_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "Error (%d): Couldn't create socket %s", errno, strerror(errno));
        vTaskDelay(pdMS_TO_TICKS(1000));
        return ESP_FAIL;
    }

    // Creating the socket timeout
    struct timeval sock_timeout = {
        .tv_sec = SOCKET_TIMEOUT_SEC,
        .tv_usec = 0,
    };

    // Setting socket send timeouts
    ret = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &sock_timeout, sizeof(sock_timeout));
    if (ret < 0) {
        ESP_LOGE(TAG, "Error (%d): Couldn't set socket send timeout %s", errno, strerror(errno));
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return ESP_FAIL;
    }

    // Setting socket receive timeout
    ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &sock_timeout, sizeof(sock_timeout));
    if (ret < 0) {
        ESP_LOGE(TAG, "Error (%d): Couldn't set socket receive timeout %s", errno, strerror(errno));
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Connects to server specified by WEB_HOST in http file and frees DNS result
 * On success, returns ESP_OK, on failure returns ESP_FAIL and prints error
 */
esp_err_t http_connect_to_server() {
    int ret;    // int for socket error codes

    ret = connect(sock, dns_res->ai_addr, dns_res->ai_addrlen);
    if (ret < 0) {
        ESP_LOGE(TAG, "Error (%d): Couldn't connect to server %s", errno, strerror(errno));
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return ESP_FAIL;
    }

    freeaddrinfo(dns_res);

    return ESP_OK;
}

/**
 * @brief sends HTTP request. On success, returns ESP_OK. On failure,
 * returns ESP_FAIL, prints error, and closes socket
 */
esp_err_t http_send_request() {
    ssize_t ret;    // Return value for error code

    ret = send(sock, REQUEST, strlen(REQUEST), 0);
    if (ret < 0) {
        ESP_LOGE(TAG, "Error (%d): Couldn't send http request %s", errno, strerror(errno));
        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief receives data from socket. On success, returns ESP_OK prints received data and closes socket
 * On failure, returns ESP_FAIL, prints error message, and closes socket
 */
esp_err_t http_receive_data() {

    ESP_LOGI(TAG, "Printing HTTP response...");
    while (1) {
        recv_len = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);

        if (recv_len == 0) {    // If there is nothing to receive...
            break;
        }

        if (recv_len < 0) {     // If there is an error...
            ESP_LOGE(TAG, "Error (%d): Couldn't receive data from socket %s", errno, strerror(errno));
            close(sock);
            return ESP_FAIL;
        }

        recv_buf[recv_len] = '\0';
        printf("%s", recv_buf);
    }

    close(sock);
    return ESP_OK;
}