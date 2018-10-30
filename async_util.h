//
// Created by gerw on 10/29/18.
//

#ifndef FTP_SERVER_ASYNC_UTIL_H
#define FTP_SERVER_ASYNC_UTIL_H


#include <lzma.h>
#include "constants.h"
#include "net_listener.h"
#include "service_handler.h"

typedef struct util_config_t {
    int wait_size;
    int ep_fd;

    char *root;
    size_t root_len;
    char *port;

    net_listener_t listener_head;
    service_handler_t service_head;

    enum log_level_t log_level;
} util_config_t;


extern util_config_t util_config;

int start_up();

int main_loop();

void tear_down();

int join_path(const char *root, int root_len, const char *wd, int wd_len, const char *path, int path_len, char *res);

#endif //FTP_SERVER_ASYNC_UTIL_H
