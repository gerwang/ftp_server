//
// Created by gerw on 10/29/18.
//

#include "async_util.h"
#include <sys/epoll.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>


util_config_t util_config;

int util_start() {
    util_config.wait_size = 0;
    util_config.ep_fd = epoll_create1(0);
    if (util_config.ep_fd == -1) {
        perror("epoll_create1");
        return -1;
    }
    return 0;
}

void util_remove() {
    if (util_config.ep_fd != -1) {
        close(util_config.ep_fd);
        util_config.ep_fd = -1;
    }
}

int start_up() {
    if (util_start() == -1) {
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
    service_remove_all();
    listener_remove_all();
    util_remove();
}

int push_dir(char *tmp, int tmp_len, const char *dir, int dir_len) {
    if (tmp_len == 0 || tmp[tmp_len - 1] != '/') {
        tmp[tmp_len++] = '/';
    }
    if (tmp_len + dir_len >= PATH_MAX_LEN) {
        return -1;
    }
    memcpy(tmp + tmp_len, dir, (size_t) dir_len);
    tmp_len += dir_len;
    return tmp_len;
}

int pop_dir(char *tmp, int tmp_len) {
    if (tmp_len > 0) {
        tmp_len--;
        while (tmp_len > 0 && tmp[tmp_len - 1] != '/') {
            tmp_len--;
        }
    }
    if (tmp_len == 0) {
        tmp[tmp_len++] = '/';
    }
    tmp[tmp_len] = '\0';
    return tmp_len;
}

// root must not end with /
// wd must start with /
// path's .. and . will be normalized
// if wd is root. then /, else wd will not end with /
int join_path(const char *root, int root_len, const char *wd, int wd_len, const char *path, int path_len, char *res) {
    static char tmp[PATH_MAX_LEN];
    memcpy(tmp, root, (size_t) root_len);
    int tmp_len = root_len;
    if (*path != '/') {
        memcpy(tmp + tmp_len, wd, (size_t) wd_len);
        tmp_len += wd_len;
    }
    const char *dir = path;
    while (path_len > 0) {
        int index = 0;
        while (index < path_len && dir[index] != '/') {
            index++;
        }
        if (strncmp(dir, ".", (size_t) index) == 0) {
            // do nothing
        } else if (strncmp(dir, "..", (size_t) index) == 0) {
            tmp_len = root_len + pop_dir(tmp + root_len, tmp_len - root_len);
        } else {
            tmp_len = push_dir(tmp + root_len, tmp_len - root_len, dir, index);
            if (tmp_len == -1) {
                return -1;
            }
            tmp_len += root_len;
        }
        index++;
        dir += index;
        path_len -= index;
    }
    if (tmp_len == root_len) {
        tmp[tmp_len++] = '/';
    }
    tmp[tmp_len] = '\0';
    strcpy(res, tmp);
    return tmp_len;
}