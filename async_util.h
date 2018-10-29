//
// Created by gerw on 10/29/18.
//

#ifndef FTP_SERVER_ASYNC_UTIL_H
#define FTP_SERVER_ASYNC_UTIL_H


#include <lzma.h>
#include "constants.h"

typedef struct util_config_t {
    int wait_size;

    char *root;
    size_t root_len;

    char *port;

    enum log_level_t log_level;
} util_config_t;

extern util_config_t util_config;

#endif //FTP_SERVER_ASYNC_UTIL_H
