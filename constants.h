//
// Created by gerw on 10/29/18.
//

#ifndef FTP_SERVER_CONSTANTS_H
#define FTP_SERVER_CONSTANTS_H

#define PATH_MAX_LEN 4096
#define MAX_EPOLL_PER 64
#define HOST_MAX_LEN 4096
#define SO_MAX_QUEUE 64
#define CONTROL_BUFFER_LEN 4096
#define DATA_BUFFER_LEN 8192
#define PWD_MAX_LEN 8192
#define ARG_MAX_LEN 20
#define PASV_MAX_LEN 200

enum log_level_t {
    LOG_NONE,
    LOG_ERR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
};

typedef void(*epoll_callback_t)(void *receiver, int events);

typedef struct epoll_payload_t {
    epoll_callback_t callback;
    void *receiver;
} epoll_payload_t;


#endif //FTP_SERVER_CONSTANTS_H
