//
// Created by gerw on 10/29/18.
//

#ifndef FTP_SERVER_NET_LISTENER_H
#define FTP_SERVER_NET_LISTENER_H

#include <netdb.h>
#include "constants.h"

typedef struct net_listener_t {
    epoll_payload_t accept_event;

    char hostname[HOST_MAX_LEN];
    in_port_t port;

    int server_fd;

    struct net_listener_t *prev, *next;
} net_listener_t;

int net_listener_start();

void listener_remove_all();

void accept_callback(void *receiver, int events);

#endif //FTP_SERVER_NET_LISTENER_H
