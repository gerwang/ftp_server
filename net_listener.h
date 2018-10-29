//
// Created by gerw on 10/29/18.
//

#ifndef FTP_SERVER_NET_LISTENER_H
#define FTP_SERVER_NET_LISTENER_H

#include "constants.h"

typedef struct net_listener_t {
    int server_socket_fd;

    char path[PATH_MAX_LEN];
    char *wd;
    int wd_len;

    struct net_listener_t *prev, *next;
} net_listener_t;

#endif //FTP_SERVER_NET_LISTENER_H
