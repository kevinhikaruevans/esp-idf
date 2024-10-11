#pragma once

#include "esp_http_server.h"
#include "esp_err.h"

#define WS_MAX_CLIENT_COUNT 10

size_t ws_broadcast_str(httpd_req_t* req, const char* str);
esp_err_t ws_helper_init(httpd_handle_t *handle);