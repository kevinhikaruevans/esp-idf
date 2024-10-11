#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

#include "esp_log.h"
#include "ws_helper.h"

#define USE_MUTEX

static const char* TAG = "ws";

static httpd_handle_t* s_current_httpd_handle = NULL;

#ifdef USE_MUTEX
static SemaphoreHandle_t s_write_mutex = NULL;
#endif

/**
 * @brief Checks if an fd is valid
 *
 * @param fd The file descriptor
 * @return true fd is valid
 * @return false fd is invalid
 */
static inline bool fd_is_valid(const int fd) {
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

/**
 * @brief Gets a list of ws clients and stores it in out_fds
 *
 * @param out_fds an array to store the fds in, or NULL for just getting the number of connected clients.
 * @return size_t the number of fds clients, a max of WS_MAX_CLIENT_COUNT
 */
static size_t ws_get_clients(int* out_fds) {
    size_t httpClientCount = WS_MAX_CLIENT_COUNT;
    size_t websocketCount = 0;
    int httpClientFds[WS_MAX_CLIENT_COUNT];

    if (s_current_httpd_handle == NULL) {
        ESP_LOGW(TAG, "current httpd ptr is null");

        return websocketCount;
    }

    if (httpd_get_client_list(*s_current_httpd_handle, &httpClientCount, httpClientFds) == ESP_OK) {
        for (size_t i = 0; i < httpClientCount; i++) {
            if (httpd_ws_get_fd_info(*s_current_httpd_handle, httpClientFds[i]) == HTTPD_WS_CLIENT_WEBSOCKET && fd_is_valid(httpClientFds[i])) {
                if (out_fds == NULL) {
                    websocketCount++;
                } else {
                    out_fds[websocketCount++] = httpClientFds[i];
                }
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to get client list");
    }

    return websocketCount;
}

/**
 * @brief Broadcasts a frame to all connected ws clients
 *
 * @param req The http req initiator
 * @param frame The frame to broadcast
 * @return size_t returns the number of ws clients that received the frame
 */
static size_t ws_broadcast_frame(httpd_req_t* req, httpd_ws_frame_t frame) {
    int fds[WS_MAX_CLIENT_COUNT];
    size_t ws_count = ws_get_clients(fds);
    size_t successes = 0;
    int req_fd = -1;
    esp_err_t err = ESP_FAIL;

    if (req != NULL) {
        req_fd = httpd_req_to_sockfd(req);
    }

#ifdef USE_MUTEX
    if (s_write_mutex != NULL && xSemaphoreTake(s_write_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
#endif

        for (size_t i = 0; i < ws_count; i++) {
            if (fds[i] == req_fd) {
                // ignore the initiator
                continue;
            }
            err = httpd_ws_send_frame_async(*s_current_httpd_handle, fds[i], &frame);

            vTaskDelay(1);

            if (err == ESP_OK) {
                successes++;
            } else {
                ESP_LOGW(TAG, "failed to send ws frame to fd=%d", fds[i]);
            }
        }


#ifdef USE_MUTEX
        xSemaphoreGive(s_write_mutex);
    } else {
        ESP_LOGW(TAG, "failed to get write mutex within timeout");
    }
#endif

    return successes;
}

/**
 * @brief Broadcasts a string over all open websockets
 *
 * @param req The http req initiator (or NULL for all clients)
 * @param str the string to broadcast
 * @return size_t the number of successful broadcasts
 */
size_t ws_broadcast_str(httpd_req_t* req, const char* str) {
    if (str == NULL || strlen(str) == 0) {
        ESP_LOGE(TAG, "ws_brodacast_str: str is NULL!");
        return 0;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .len = strlen(str),
        .payload = (uint8_t*)str,
    };

    return ws_broadcast_frame(req, frame);
}

esp_err_t ws_helper_init(httpd_handle_t *handle) {
    s_current_httpd_handle = handle;

#ifdef USE_MUTEX
    s_write_mutex = xSemaphoreCreateMutex();
#endif

    return ESP_OK;
}