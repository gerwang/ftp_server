//
// Created by gerw on 10/29/18.
//

#ifndef FTP_SERVER_SERVICE_HANDLER_H
#define FTP_SERVER_SERVICE_HANDLER_H

#include "constants.h"

typedef struct service_handler_t {
    char path[PATH_MAX_LEN];
    char *wd;
    int wd_len;

    int control_fd;
    int control_flag;

    int data_in_fd, data_out_fd;
    int pasv_listen_fd;

    epoll_payload_t control_payload;

    char control_read_buffer[CONTROL_BUFFER_LEN];
    size_t read_buffer_len, read_buffer_head;
    char control_write_buffer[CONTROL_BUFFER_LEN];
    size_t write_buffer_len;

    char data_transfer_buffer[DATA_BUFFER_LEN];
    int data_buffer_head, data_buffer_tail;

    int should_exit, entered, transfering;
    int user_used, logged_in;

    struct service_handler_t *prev, *next;
} service_handler_t;

typedef void(*service_handler_callback_t)(service_handler_t *handler, char *parameter);

typedef struct service_command_t {
    char *command_name;
    service_handler_callback_t callback;
    int require_login;
} service_command_t;

void service_write_line(service_handler_t *handler, const char *line);

void service_add(int fd, struct sockaddr *addr, int addr_len);

void control_callback(void *receiver, int events);

void control_update(service_handler_t *handler);

void service_start();

#endif //FTP_SERVER_SERVICE_HANDLER_H