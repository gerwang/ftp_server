//
// Created by gerw on 10/29/18.
//

#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <memory.h>
#include "service_handler.h"
#include "async_util.h"


void service_start() {
    bzero(&util_config.service_head, sizeof(util_config.service_head));
    util_config.service_head.prev = util_config.service_head.next = &util_config.service_head;
    util_config.service_head.control_fd = -1;
}

void service_add(int fd, struct sockaddr *addr, int addr_len) {
    // have fd
    service_handler_t *handler = malloc(sizeof(service_handler_t));
    if (handler == NULL) {
        if (util_config.log_level >= LOG_ERR) {
            perror("malloc(service_handler)");
        }
        close(fd);
        return;
    }
    // have handler

    // todo get local addr
    // todo get local addr info
    // todo get peer info

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    event.data.ptr = &handler->control_payload;
    int ret = epoll_ctl(util_config.ep_fd, EPOLL_CTL_ADD, fd, &event);
    if (ret == -1) {
        if (util_config.log_level >= LOG_DEBUG) {
            perror("epoll add client");
        }
        free(handler);
        close(fd);
        return;
    }

    handler->control_fd = fd;
    handler->control_payload.callback = control_callback;
    handler->control_payload.receiver = handler;
    handler->write_buffer_len = 0;
    handler->read_buffer_len = 0;
    handler->read_buffer_head = 0;
    handler->should_exit = 0;
    handler->entered = 0;
    handler->transfering = 0;
    handler->user_used = 0;
    handler->logged_in = 0;

    util_config.wait_size++;

    handler->next = util_config.service_head.next;
    util_config.service_head.next = handler;
    handler->prev = &util_config.service_head;

    service_write_line(handler, "220 Anonymous FTP server ready.");
}

void service_close(service_handler_t *handler) {
    if (handler->control_fd != -1) {

        close(handler->control_fd);
        util_config.wait_size--;

        handler->prev->next = handler->next;
        handler->next->prev = handler->prev;

        handler->control_fd = -1;
    }
}

void service_write_line(service_handler_t *handler, const char *line) {
    size_t len = strlen(line);
    if (handler->write_buffer_len + len + 2 > CONTROL_BUFFER_LEN) {
        if (util_config.log_level >= LOG_ERR) {
            fprintf(stderr, "write buffer overflow\n");
        }
        service_close(handler); // cannot sync, terminate
        return;
    }
    memcpy(handler->control_write_buffer + handler->write_buffer_len, line, len);
    handler->write_buffer_len += len;
    memcpy(handler->control_write_buffer + handler->write_buffer_len, "\r\n", 2);
    handler->write_buffer_len += 2;
    control_update(handler);
}

void user_handle(service_handler_t *handler, char *parameter) {
    if (!handler->logged_in) {
        if (strcmp(parameter, "anonymous") == 0) {
            service_write_line(handler, "331 Guest login ok, type your email as password.");
            handler->user_used = 1;
        } else {
            service_write_line(handler, "530 Only anonymous is acceptable.");
        }
    } else {
        service_write_line(handler, "530 Can't change user from guest login.");
    }
}

void pass_handle(service_handler_t *handler, char *parameter) {
    if (handler->user_used) {
        service_write_line(handler, "230 Guest login ok.");
        handler->logged_in = 1;
        handler->user_used = 0;
    } else {
        service_write_line(handler, "503 Login with USER first.");
    }
}

void pass_quit(service_handler_t *handler, char *parameter) {
    service_write_line(handler, "221 Goodbye.");
    handler->should_exit = 1;
}

void pass_syst(service_handler_t *handler, char *parameter) {
    service_write_line(handler, "215 UNIX Type: L8");
}

void pass_type(service_handler_t *handler, char *parameter) {
    if (strcmp(parameter, "I") == 0) {
        service_write_line(handler, "200 Type set to I.");
    } else {
        service_write_line(handler, "500 Only support binary mode.");
    }
}

service_command_t supported_commands[] = {
        {"USER", user_handle, 0},
        {"PASS", pass_handle, 0},
        {"RETR", NULL,        1},
        {"STOR", NULL,        1},
        {"QUIT", pass_quit,   0},
        {"SYST", pass_syst,   0},
        {"TYPE", pass_type,   0},
        {"PORT", NULL,        1},
        {"PASV", NULL,        1},
        {"MKD",  NULL,        1},
        {"CWD",  NULL,        1},
        {"PWD",  NULL,        1},
        {"LIST", NULL,        1},
        {"RMD",  NULL,        1},
        {"RNFR", NULL,        1},
        {"RNTO", NULL,        1},
        {"REST", NULL,        1}
};

void handle_command(service_handler_t *handler, const char *command) {
    int command_count = sizeof(supported_commands) / sizeof(service_command_t);
    for (int i = 0; i < command_count; i++) {
        size_t len = strlen(supported_commands[i].command_name);
        if (strncasecmp(supported_commands[i].command_name, command, len) == 0) {
            if (supported_commands[i].require_login && !handler->logged_in) {
                service_write_line(handler, "530 Please login with USER and PASS.");
            } else if (supported_commands[i].callback != NULL) {
                char *parameter = (char *) (command + len);
                while (*parameter == ' ') {
                    parameter++;
                }
                supported_commands[i].callback(handler, parameter);
            } else {
                service_write_line(handler, "502 Command not implemented.");
            }
            return;
        }
    }
    service_write_line(handler, "500 Command not recognized.");
}

void control_update(service_handler_t *handler) {
    if (handler->entered) {
        return;
    }
    handler->entered = 1;
    while (handler->control_fd != -1) {
        if (handler->control_flag & EPOLLIN && handler->write_buffer_len == 0) { // can read
            ssize_t ret = read(handler->control_fd, handler->control_read_buffer,
                               CONTROL_BUFFER_LEN - handler->read_buffer_len);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    handler->control_flag &= ~EPOLLIN;
                } else {
                    if (util_config.log_level >= LOG_ERR) {
                        perror("control read");
                    }
                    break;
                }
            } else if (ret == 0) {
                service_close(handler);
            } else {
                handler->read_buffer_len += ret;
                if (handler->read_buffer_len >= CONTROL_BUFFER_LEN) {
                    service_write_line(handler, "500 Command too long."); // cannot sync, terminate
                    handler->should_exit = 1;
                }
            }
        } else if (handler->read_buffer_head + 1 < handler->read_buffer_len
                   && handler->write_buffer_len == 0
                   && !handler->transfering) { // execute command
            size_t index = handler->read_buffer_head;
            char *const buffer = handler->control_read_buffer;
            while (index + 1 < handler->read_buffer_len) {
                if (buffer[index] == '\r'
                    && buffer[index + 1] == '\n') {
                    break;
                }
                index++;
            }
            if (index + 1 < handler->read_buffer_len) { // find \r\n
                buffer[index] = buffer[index + 1] = '\0';
                handle_command(handler, handler->control_read_buffer);
                memmove(buffer, buffer + index + 2,
                        handler->read_buffer_len - index - 2);
                handler->read_buffer_head = 0;
            } else {
                handler->read_buffer_head = index;
            }
        } else if (handler->control_flag & EPOLLOUT && handler->write_buffer_len > 0) {// can write
            ssize_t ret = write(handler->control_fd, handler->control_write_buffer,
                                handler->write_buffer_len);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    handler->control_flag &= ~EPOLLOUT;
                } else {
                    if (util_config.log_level >= LOG_ERR) {
                        perror("control write");
                    }
                    break;
                }
            } else {
                handler->write_buffer_len -= ret;
                char *const buffer = handler->control_write_buffer;
                memmove(buffer, buffer + ret, handler->write_buffer_len);
                if (handler->write_buffer_len == 0) {
                    if (handler->should_exit) {
                        service_close(handler);
                    }
                }
            }
        } else {
            break;
        }
    }
    handler->entered = 0;
}

void control_callback(void *receiver, int events) {
    service_handler_t *handler = receiver;
    int mask = events & (EPOLLIN | EPOLLOUT);
    handler->control_flag |= mask;
    if (mask) {
        control_update(handler);
    }
    if (events & (EPOLLERR | EPOLLHUP)) {
        service_close(handler);
    }
}
