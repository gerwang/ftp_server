//
// Created by gerw on 10/29/18.
//

#ifndef FTP_SERVER_SERVICE_HANDLER_H
#define FTP_SERVER_SERVICE_HANDLER_H

#include <netdb.h>
#include "constants.h"

enum data_type {
    DT_LIST, DT_RETR, DT_STOR, DT_NONE
};

typedef struct service_handler_t {
    char path[PATH_MAX_LEN];
    size_t root_len, wd_len;

    int control_fd;
    int control_flag;

    int data_in_fd, data_out_fd;
    int data_flag;

    int pasv_listen_fd, remote_fd;
    int local_fd;

    epoll_payload_t control_payload, data_in_payload, data_out_payload, pasv_payload;

    char control_read_buffer[CONTROL_BUFFER_LEN];
    size_t read_buffer_len, read_buffer_head;
    char control_write_buffer[CONTROL_BUFFER_LEN];
    size_t write_buffer_len;

    char data_transfer_buffer[DATA_BUFFER_LEN];
    int data_buffer_len;

    int should_exit, entered, transfer_flag;
    int user_used, logged_in;

    struct sockaddr_in port_addr;
    int port_len;
    struct sockaddr_in local_addr;
    int local_addr_len;

    enum data_type command_type;

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

void service_remove(service_handler_t *handler);

void service_remove_all();

void control_update(service_handler_t *handler);

void service_start();

int data_can_start(service_handler_t *handler);

void data_start_transfer(service_handler_t *handler);

void data_update(service_handler_t *handler);

void data_abort_connection(service_handler_t *handler, char *line);

void control_callback(void *receiver, int events);

void data_in_callback(void *receiver, int events);

void data_out_callback(void *receiver, int events);

void pasv_listen_callback(void *receiver, int events);

void data_clear_connection(service_handler_t *handler);

#endif //FTP_SERVER_SERVICE_HANDLER_H