//
// Created by gerw on 10/29/18.
//

#include <memory.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>
#include "async_util.h"

int net_listener_start() {
    bzero(&util_config.listener_head, sizeof(util_config.listener_head));
    util_config.listener_head.next = util_config.listener_head.prev = &util_config.listener_head;

    struct addrinfo hints, *head;
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_INET; // force ipv4
    hints.ai_socktype = SOCK_STREAM; // tcp
    hints.ai_flags = AI_PASSIVE; // server socket

    int ret = getaddrinfo(NULL, util_config.port, &hints, &head);
    if (ret != 0) {
        if (util_config.log_level >= LOG_ERR) {
            fprintf(stderr, "getaddrinfo %s\n", gai_strerror(ret));
        }
        freeaddrinfo(head);
        return -1;
    }

    int any = -1;

    for (struct addrinfo *p = head; p != NULL; p = p->ai_next) {
        net_listener_t *listener = malloc(sizeof(net_listener_t));
        if (listener == NULL) {
            if (util_config.log_level >= LOG_ERR) {
                perror("malloc(net_listener)");
            }
            continue;
        }

        // now have listener

        const struct sockaddr_in *v4_addr = (const struct sockaddr_in *) p->ai_addr;
        if (inet_ntop(AF_INET, &v4_addr->sin_addr, listener->hostname, HOST_MAX_LEN) == NULL) {
            if (util_config.log_level >= LOG_ERR) {
                perror("inet_ntop");
            }
            free(listener);
            continue;
        }
        listener->port = htons(v4_addr->sin_port);

        listener->server_fd = socket(p->ai_family, p->ai_socktype | SOCK_NONBLOCK, p->ai_protocol);
        if (listener->server_fd == -1) {
            if (util_config.log_level >= LOG_ERR) {
                perror("socket(listener)");
            }
            free(listener);
            continue;
        }

        // now have listener->server_fd

        int truth = 1;
        if (setsockopt(listener->server_fd, SOL_SOCKET, SO_REUSEADDR, &truth, sizeof(truth)) == -1) {
            if (util_config.log_level >= LOG_WARN) {
                perror("setsockopt(SO_REUSEADDR)");
            }
        }

        if (bind(listener->server_fd, p->ai_addr, p->ai_addrlen) == -1) {
            if (util_config.log_level >= LOG_ERR) {
                perror("bind");
            }
            close(listener->server_fd);
            free(listener);
            continue;
        }

        if (listen(listener->server_fd, SO_MAX_QUEUE) == -1) {
            if (util_config.log_level >= LOG_ERR) {
                perror("listen");
            }
            close(listener->server_fd);
            free(listener);
            continue;
        }

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET | EPOLLET;
        event.data.ptr = &listener->accept_event;
        if (epoll_ctl(util_config.ep_fd, EPOLL_CTL_ADD, listener->server_fd, &event) == -1) {
            perror("epoll_ctl(add listener)");
            close(listener->server_fd);
            free(listener);
            continue;
        }

        // now have epoll_event

        util_config.wait_size++;

        if (util_config.log_level >= LOG_INFO) {
            printf("started %s:%d\n", listener->hostname, listener->port);
        }

        listener->accept_event.receiver = listener;
        listener->accept_event.callback = accept_callback;

        listener->next = util_config.listener_head.next;
        util_config.listener_head.next = listener;
        listener->prev = &util_config.listener_head;

        any = 0;
    }
    freeaddrinfo(head);
    return any;
}

void listener_remove(net_listener_t *listener) {
    if (listener->server_fd != -1) {
        close(listener->server_fd);
        listener->server_fd = -1;
    }
    listener->prev->next = listener->next;
    listener->next->prev = listener->prev;
    free(listener);
    util_config.wait_size--;
    if (util_config.log_level >= LOG_DEBUG) {
        printf("close listener\n");
    }
}

void accept_callback(void *receiver, int events) {
    net_listener_t *listener = receiver;
    if (events & EPOLLIN) {
        struct sockaddr addr;
        socklen_t addr_len;
        while (1) {
            int handler_fd = accept(listener->server_fd, &addr, &addr_len);
            if (handler_fd == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    if (util_config.log_level >= LOG_WARN) {
                        perror("accept");
                    }
                }
                break;
            }
            int flag = fcntl(handler_fd, F_GETFL, 0);
            if (flag == -1 || fcntl(handler_fd, F_SETFL, flag | O_NONBLOCK) == -1) {
                perror("fcntl set");
                close(handler_fd);
                continue;
            }
            if (util_config.log_level >= LOG_DEBUG) {
                printf("new incoming!\n");
            }
            service_add(handler_fd, &addr, addr_len);
        }
    }
    if (events & (EPOLLERR | EPOLLHUP)) {
        listener_remove(listener);
    }
}

void listener_remove_all() {
    while (util_config.listener_head.prev != &util_config.listener_head) {
        listener_remove(util_config.listener_head.prev);
    }
}
