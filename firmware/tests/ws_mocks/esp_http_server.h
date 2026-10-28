#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
using httpd_handle_t = void *;
struct httpd_req_t {
    int fd;
};
struct httpd_ws_frame_t {
    size_t len = 0;
    int type = 0;
};
constexpr int HTTPD_WS_TYPE_CLOSE = 8;
constexpr int HTTPD_WS_CLIENT_WEBSOCKET = 2;
constexpr int HTTPD_WS_CLIENT_INVALID = 0;
int httpd_req_to_sockfd(httpd_req_t *request);
esp_err_t httpd_ws_recv_frame(httpd_req_t *request, httpd_ws_frame_t *frame, size_t max_len);
int httpd_ws_get_fd_info(httpd_handle_t server, int fd);
esp_err_t httpd_get_client_list(httpd_handle_t server, size_t *count, int *fds);
esp_err_t httpd_queue_work(httpd_handle_t server, void (*fn)(void *), void *arg);
esp_err_t httpd_sess_trigger_close(httpd_handle_t server, int fd);
