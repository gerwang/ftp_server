//
// Created by gerw on 10/29/18.
//

#include "async_util.h"
#include <sys/epoll.h>
#include <stdio.h>


util_config_t util_config;

int start_up() {
    util_config.wait_size = 0;
    util_config.ep_fd = epoll_create1(0);
    if (util_config.ep_fd == -1) {
        perror("epoll_create1");
        return -1;
    }
    service_start();
    if (net_listener_start() == -1) {
        return -1;
    }
    return 0;
}

int main_loop() {
    static struct epoll_event buffer[MAX_EPOLL_PER];
    while (util_config.wait_size > 0) {
        int fd_num = epoll_wait(util_config.ep_fd, buffer, MAX_EPOLL_PER, -1); // wait forever
        if (fd_num == -1) {
            if (util_config.log_level >= LOG_ERR) {
                perror("epoll_wait");
                return -1;
            }
        }
        for (int i = 0; i < fd_num; i++) {
            epoll_payload_t *payload = buffer[i].data.ptr;
            if (payload->callback != NULL) {
                payload->callback(payload->receiver, buffer[i].events);
            }
        }
    }
    return 0;
}

void tear_down() {

}