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
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
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

    handler->local_addr = *(struct sockaddr_in *) addr;
    handler->local_addr_len = addr_len;

    handler->control_payload.callback = control_callback;
    handler->control_payload.receiver = handler;
    handler->data_in_payload.callback = data_in_callback;
    handler->data_in_payload.receiver = handler;
    handler->data_out_payload.callback = data_out_callback;
    handler->data_out_payload.receiver = handler;
    handler->pasv_payload.callback = pasv_listen_callback;
    handler->pasv_payload.receiver = handler;

    handler->control_fd = fd;

    handler->write_buffer_len = 0;
    handler->read_buffer_len = 0;
    handler->read_buffer_head = 0;

    handler->data_buffer_len = 0;
    handler->port_len = 0;

    handler->should_exit = 0;
    handler->entered = 0;
    handler->transfer_flag = 0;
    handler->user_used = 0;
    handler->logged_in = 0;
    handler->port_len = 0;

    util_config.wait_size++;

    int path_len = join_path(util_config.root, (int) util_config.root_len,
                             "", 0, "/", 1, handler->path);
    handler->root_len = util_config.root_len;
    handler->wd_len = path_len - handler->root_len;

    handler->next = util_config.service_head.next;
    util_config.service_head.next = handler;
    handler->prev = &util_config.service_head;

    service_write_line(handler, "220 Anonymous FTP server ready.");
}

void service_remove(service_handler_t *handler) {
    if (handler->control_fd != -1) {

        data_clear_connection(handler);
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
        service_remove(handler); // cannot sync, terminate
        return;
    }
    if (util_config.log_level >= LOG_DEBUG) {
        printf("--> %s\n", line);
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

void quit_handle(service_handler_t *handler, char *parameter) {
    service_write_line(handler, "221 Goodbye.");
    handler->should_exit = 1;
}

void syst_handle(service_handler_t *handler, char *parameter) {
    service_write_line(handler, "215 UNIX Type: L8");
}

void type_handle(service_handler_t *handler, char *parameter) {
    if (strcmp(parameter, "I") == 0) {
        service_write_line(handler, "200 Type set to I.");
    } else {
        service_write_line(handler, "500 Only support binary mode.");
    }
}

void port_handle(service_handler_t *handler, char *parameter) {
    data_clear_connection(handler);
    struct sockaddr_in *addr_in = &handler->port_addr;
    const char *host = (const char *) &addr_in->sin_addr.s_addr, *port = (const char *) &addr_in->sin_port;
    if (sscanf("%hhu,%hhu,%hhu,%hhu,%hhu,%hhu",
               host, host + 1, host + 2, host + 3, port, port + 1) != 6) { // big endian order
        service_write_line(handler, "500 Not a valid socket address");
    } else {
        addr_in->sin_family = AF_INET;
        handler->port_len = sizeof(struct sockaddr_in);
        service_write_line(handler, "200 Port command succeed.");
    }
}

void pwd_handle(service_handler_t *handler, char *parameter) {
    static char pwd_buffer[PWD_MAX_LEN];
    sprintf(pwd_buffer, "257 \"%s\" is the current directory.", handler->path + handler->root_len);
    service_write_line(handler, pwd_buffer);
}

void cwd_handle(service_handler_t *handler, char *parameter) {
    static char cwd_buffer[PWD_MAX_LEN];
    int ok = 1;
    int len = (int) strlen(parameter);
    int cwd_len = join_path(handler->path, (int) handler->root_len,
                            handler->path + handler->root_len,
                            (int) handler->wd_len, parameter, len, cwd_buffer);
    if (cwd_len == -1) {
        ok = 0;
    }
    struct stat res;
    if (ok && stat(cwd_buffer, &res) == -1) {
        ok = 0;
    }
    if (ok && !S_ISDIR(res.st_mode)) {
        ok = 0;
    }
    if (ok && !access(cwd_buffer, R_OK) == -1) {
        ok = 0;
    }
    if (ok) {
        strcpy(handler->path, cwd_buffer);
        handler->wd_len = cwd_len - handler->root_len;
        service_write_line(handler, "250 CWD command successful.");
    } else {
        service_write_line(handler, "550 CWD failed.");
    }
}

int data_can_start(service_handler_t *handler) {
    return handler->port_len != 0 || handler->pasv_listen_fd != -1 || handler->remote_fd != -1;
}

void data_start_transfer(service_handler_t *handler) {
    if (handler->local_fd != -1 && handler->remote_fd == -1 && handler->port_len != 0) {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
        if (fd == -1) {
            if (util_config.log_level >= LOG_ERR) {
                perror("PORT socket");
            }
            data_abort_connection(handler, "426 Temporarily unavailable.");
        }
        int ret = connect(fd, (const struct sockaddr *) &handler->port_addr, (socklen_t) handler->port_len);
        if (ret == -1 && errno != EINPROGRESS) { // really error
            if (util_config.log_level >= LOG_ERR) {
                perror("PORT connect");
            }
            data_abort_connection(handler, "426 PORT connection failed.");
        } else {
            handler->remote_fd = fd;
        }
    }

    if (handler->local_fd != -1 && handler->remote_fd != -1) {// PASV
        if (handler->command_type == DT_STOR) {
            handler->data_in_fd = handler->remote_fd;
            handler->data_out_fd = handler->local_fd;
        } else {
            handler->data_in_fd = handler->local_fd;
            handler->data_out_fd = handler->remote_fd;
        }
        handler->local_fd = handler->remote_fd = -1;
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.ptr = &handler->data_in_payload;

        int ok = 1;
        if (epoll_ctl(util_config.ep_fd, EPOLL_CTL_ADD, handler->data_in_fd, &event) == -1) {
            if (util_config.log_level >= LOG_ERR) {
                perror("epoll add in");
            }
            ok = 0;
        }
        if (ok && epoll_ctl(util_config.ep_fd, EPOLL_CTL_ADD, handler->data_out_fd, &event) == -1) {
            if (util_config.log_level >= LOG_ERR) {
                perror("epoll add out");
            }
            ok = 0;
        }
        if (ok) {
            if (handler->command_type == DT_LIST) {
                service_write_line(handler, "150 Opening BINARY mode data connection for '/bin/ls'.");
            } else {
                service_write_line(handler, "150 Transfer started.");
            }
            data_update(handler);
        } else {
            data_abort_connection(handler, "426 Temporarily unavailable.");
        }
    }
}

void list_handle(service_handler_t *handler, char *parameter) {
    if (!data_can_start(handler)) {
        service_write_line(handler, "425 Cannot establish data connection.");
        return;
    }

    char args[ARG_MAX_LEN] = "-nN";
    int index = 3;
    if (*parameter == '-') {
        parameter++;
        while (isalpha(*parameter)) {
            args[index++] = *parameter;
            if (index + 1 == ARG_MAX_LEN) {
                break;
            }
            parameter++;
        }
        args[index] = '\0';
    }

    // prevent attack like "ls: cannot access '/my/secret/directory/name/ftp/root': No such file or directory"
    struct stat s;
    int ok = 1;
    if (access(handler->path, F_OK | R_OK) == -1) {
        ok = 0;
    }

    int pipes[2];
    if (ok && pipe(pipes) == -1) {
        ok = 0;
    }

    // have pipes
    if (ok) {
        int ret = fork();
        if (ret == -1) {
            close(pipes[0]);
            close(pipes[1]);
            ok = 0;
        } else if (ret == 0) { // new process
            close(pipes[0]);
            dup2(pipes[1], STDOUT_FILENO);
            dup2(pipes[1], STDERR_FILENO);
            close(pipes[1]);
            execlp("ls", "ls", args, handler->path, NULL);
        } else {
            close(pipes[1]);
            int flags = fcntl(pipes[0], F_GETFL, 0);
            if (flags == -1 || fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK) == -1) {
                if (util_config.log_level >= LOG_ERR) {
                    perror("fcntl(pipe)");
                }
                close(pipes[0]);
                ok = 0;
            }
            if (ok) {
                handler->local_fd = pipes[0];
                data_start_transfer(handler);
            }
        }
    }
    if (!ok) {
        service_write_line(handler, "426 Temporarily unavailable.");
    }
}

void pasv_handle(service_handler_t *handler, char *parameter) {
    data_clear_connection(handler);
    int ok = 1;
    int fd = socket(AF_INET, SOCK_NONBLOCK | SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1) {
        if (util_config.log_level >= LOG_ERR) {
            perror("PASV socket");
        }
        ok = 0;
    }
    struct sockaddr_in temp = handler->local_addr;
    socklen_t temp_len = (socklen_t) handler->local_addr_len;
    temp.sin_port = 0; // any port
    if (ok && bind(fd, (const struct sockaddr *) &handler->local_addr, temp_len) == -1) {
        if (util_config.log_level >= LOG_ERR) {
            perror("PASV bind");
        }
        close(fd);
        ok = 0;
    }
    if (ok && getsockname(fd, (struct sockaddr *) &temp, &temp_len) == -1) {
        if (util_config.log_level >= LOG_ERR) {
            perror("PASV getsockname");
        }
        close(fd);
        ok = 0;
    }
    if (ok && listen(fd, SO_MAX_QUEUE) == -1) {
        if (util_config.log_level >= LOG_ERR) {
            perror("PASV listen");
        }
        close(fd);
        ok = 0;
    }
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = &handler->pasv_payload;
    if (epoll_ctl(util_config.ep_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        if (util_config.log_level >= LOG_ERR) {
            perror("epoll add PASV");
        }
        close(fd);
        ok = 0;
    }
    static char pasv_buffer[PASV_MAX_LEN];
    if (ok) {
        handler->pasv_listen_fd = fd;
        const char *host = (const char *) &temp.sin_addr, *port = (const char *) &temp.sin_port;
        if (sprintf(pasv_buffer, "227 Entering PASSIVE mode (%hhu,%hhu,%hhu,%hhu,%hhu,%hhu)",
                    host[0], host[1], host[2], host[3], port[0], port[1]) != 6) {
            ok = 0;
        }
    }
    if (ok) {
        service_write_line(handler, pasv_buffer);
    } else {
        service_write_line(handler, "451 PASV command failed.");
    }
}

void mkd_handle(service_handler_t *handler, char *parameter) {
    int ok = 1;
    static char mkd_buffer[PWD_MAX_LEN];
    int len = (int) strlen(parameter);
    int path_len = join_path(handler->path, (int) handler->root_len,
                             handler->path + handler->root_len, (int) handler->wd_len,
                             parameter, len, mkd_buffer);
    if (path_len == -1) {
        ok = 0;
    }
    if (ok && mkdir(mkd_buffer, 0755) == -1) {
        ok = 0;
    }
    if (ok) {
        service_write_line(handler, "250 MKD successful.");
    } else {
        service_write_line(handler, "550 MKD failed.");
    }
}

void rmd_handle(service_handler_t *handler, char *parameter) {
    int ok = 1;
    static char rmd_buffer[PWD_MAX_LEN];
    int len = (int) strlen(parameter);
    int path_len = join_path(handler->path, (int) handler->root_len,
                             handler->path + handler->root_len, (int) handler->wd_len,
                             parameter, len, rmd_buffer);
    if (path_len == -1) {
        ok = 0;
    }
    if (ok && rmdir(rmd_buffer) == -1) {
        ok = 0;
    }
    if (ok) {
        service_write_line(handler, "250 RMD successful.");
    } else {
        service_write_line(handler, "550 RMD failed.");
    }
}

void retr_handle(service_handler_t *handler, char *parameter) {
    if (!data_can_start(handler)) {
        service_write_line(handler, "425 Cannot establish data connection.");
        return;
    }
    int ok = 1;
    static char retr_buffer[PWD_MAX_LEN];
    int len = (int) strlen(parameter);
    int path_len = join_path(handler->path, (int) handler->root_len,
                             handler->path + handler->root_len, (int) handler->wd_len,
                             parameter, len, retr_buffer);
    if (path_len == -1) {
        ok = 0;
    }
    struct stat s;
    if (ok && stat(retr_buffer, &s) == -1) {
        ok = 0;
    }
    if (ok && !S_ISREG(s.st_mode)) {
        ok = 0;
    }
    int fd;
    if (ok && (fd = open(retr_buffer, O_RDONLY | O_NONBLOCK)) == -1) {
        ok = 0;
    }
    if (ok) {
        handler->local_fd = fd;
        handler->data_flag |= EPOLLIN;
        handler->command_type = DT_RETR;
        data_start_transfer(handler);
    } else {
        service_write_line(handler, "451 Retrieve file failed.");
    }
}

void stor_handle(service_handler_t *handler, char *parameter) {
    if (!data_can_start(handler)) {
        service_write_line(handler, "425 Cannot establish data connection.");
        return;
    }
    int ok = 1;
    static char stor_buffer[PWD_MAX_LEN];
    int len = (int) strlen(parameter);
    int path_len = join_path(handler->path, (int) handler->root_len,
                             handler->path + handler->root_len, (int) handler->wd_len,
                             parameter, len, stor_buffer);
    if (path_len == -1) {
        ok = 0;
    }
    int fd;
    if (ok && (fd = open(stor_buffer, O_WRONLY | O_NONBLOCK | O_TRUNC | O_CREAT, 0644)) == -1) {
        ok = 0;
    }
    if (ok) {
        handler->local_fd = fd;
        handler->data_flag |= EPOLLOUT;
        handler->command_type = DT_STOR;
        data_start_transfer(handler);
    } else {
        service_write_line(handler, "451 Store file failed.");
    }
}

service_command_t supported_commands[] = {
        {"USER", user_handle, 0},
        {"PASS", pass_handle, 0},
        {"RETR", retr_handle, 1},
        {"STOR", stor_handle, 1},
        {"QUIT", quit_handle, 0},
        {"SYST", syst_handle, 0},
        {"TYPE", type_handle, 0},
        {"PORT", port_handle, 1},
        {"PASV", pasv_handle, 1},
        {"MKD",  mkd_handle,  1},
        {"CWD",  cwd_handle,  1},
        {"PWD",  pwd_handle,  1},
        {"LIST", list_handle, 1},
        {"RMD",  rmd_handle,  1},
        {"RNFR", NULL,        1},
        {"RNTO", NULL,        1},
        {"REST", NULL,        1}
};

void handle_command(service_handler_t *handler, const char *command) {
    int command_count = sizeof(supported_commands) / sizeof(service_command_t);
    if (util_config.log_level >= LOG_DEBUG) {
        printf("<-- %s\n", command);
    }
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
                service_remove(handler);
            } else {
                handler->read_buffer_len += ret;
                if (handler->read_buffer_len >= CONTROL_BUFFER_LEN) {
                    service_write_line(handler, "500 Command too long."); // cannot sync, terminate
                    handler->should_exit = 1;
                }
            }
        } else if (handler->read_buffer_head + 1 < handler->read_buffer_len
                   && handler->write_buffer_len == 0
                   && !handler->transfer_flag) { // execute command
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
                handler->read_buffer_len -= index + 2; // prev bug: forget this line
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
                        service_remove(handler);
                    }
                }
            }
        } else {
            break;
        }
    }
    handler->entered = 0;
}

void data_update(service_handler_t *handler) {
    int error_flag = 0;
    while (handler->data_out_fd != -1) {
        if (handler->data_in_fd != -1 && handler->data_flag & EPOLLIN && handler->data_buffer_len < DATA_BUFFER_LEN) {
            ssize_t ret = read(handler->data_in_fd, handler->data_transfer_buffer + handler->data_buffer_len,
                               (size_t) (DATA_BUFFER_LEN - handler->data_buffer_len));
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    handler->data_flag &= ~EPOLLIN;
                } else {
                    if (util_config.log_level >= LOG_ERR) {
                        perror("data read");
                    }
                    data_abort_connection(handler, "426 Input stream error.");
                    error_flag = 1;
                    break;
                }
            } else if (ret == 0) {
                close(handler->data_in_fd);
                handler->data_in_fd = -1;
            } else {
                handler->data_buffer_len += ret;
            }
        } else if (handler->data_flag & EPOLLOUT && handler->data_buffer_len > 0) {
            ssize_t ret = write(handler->data_out_fd, handler->data_transfer_buffer, (size_t) handler->data_buffer_len);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    handler->data_flag &= ~EPOLLOUT;
                } else {
                    if (util_config.log_level >= LOG_ERR) {
                        perror("data write");
                    }
                    data_abort_connection(handler, "426 Output stream error.");
                    error_flag = 1;
                    break;
                }
            } else {
                memmove(handler->data_transfer_buffer, handler->data_transfer_buffer + ret,
                        (size_t) (handler->data_buffer_len - ret));
                handler->data_buffer_len -= ret;
                if (handler->data_in_fd == -1 && handler->data_buffer_len == 0) {
                    close(handler->data_out_fd);
                    handler->data_out_fd = -1;
                }
            }
        }
    }
    if (!error_flag && handler->data_in_fd == -1 && handler->data_out_fd == -1) { // transfer end
        service_write_line(handler, "226 Transfer complete.");
        data_clear_connection(handler);
    }
}

void control_callback(void *receiver, int events) {
    service_handler_t *handler = receiver;
    int mask = events & (EPOLLIN | EPOLLOUT);
    handler->control_flag |= mask;
    if (mask) {
        control_update(handler);
    }
    if (events & (EPOLLERR | EPOLLHUP)) {
        service_remove(handler);
    }
}

void data_in_callback(void *receiver, int events) {
    service_handler_t *handler = receiver;
    if (events & EPOLLIN) {
        handler->data_flag |= EPOLLIN;
        data_update(handler);
    }
    if (handler->transfer_flag && handler->data_in_fd != -1 && events & EPOLLERR) { // todo'
        data_abort_connection(handler, "426 Input stream error.");
    }
}

void data_out_callback(void *receiver, int events) {
    service_handler_t *handler = receiver;
    if (events & EPOLLOUT) {
        handler->data_flag |= EPOLLOUT;
        data_update(handler);
    }
    if (handler->transfer_flag && handler->data_out_fd != -1 && events & (EPOLLERR | EPOLLHUP)) {
        data_abort_connection(handler, "426 Output stream error.");
    }
}

void pasv_listen_callback(void *receiver, int events) {
    service_handler_t *handler = receiver;
    if (handler->pasv_listen_fd != -1 && events & EPOLLIN) {
        int fd = accept(handler->pasv_listen_fd, NULL, NULL);
        if (fd == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                if (util_config.log_level >= LOG_ERR) {
                    perror("pasv accept");
                }
                close(handler->pasv_listen_fd);
                handler->pasv_listen_fd = -1;
            }
        } else {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                if (util_config.log_level >= LOG_ERR) {
                    perror("fcntl pasv trans fd");
                }
            } else {
                close(handler->pasv_listen_fd);
                handler->pasv_listen_fd = -1;

                handler->remote_fd = fd;
                data_start_transfer(handler);
            }
        }
    }
    if (events & (EPOLLERR | EPOLLHUP)) {
        close(handler->pasv_listen_fd);
        handler->pasv_listen_fd = -1;
    }
}

void data_abort_connection(service_handler_t *handler, char *prompt) {
    data_clear_connection(handler);
    service_write_line(handler, prompt);
}

void data_clear_connection(service_handler_t *handler) {
    if (handler->data_in_fd != -1) {
        close(handler->data_in_fd);
        handler->data_in_fd = -1;
    }
    if (handler->data_out_fd != -1) {
        close(handler->data_out_fd);
        handler->data_out_fd = -1;
    }
    handler->data_flag = 0;
    if (handler->pasv_listen_fd != -1) {
        close(handler->pasv_listen_fd);
        handler->pasv_listen_fd = -1;
    }
    if (handler->remote_fd != -1) {
        close(handler->remote_fd);
        handler->remote_fd = -1;
    }
    if (handler->local_fd != -1) {
        close(handler->local_fd);
        handler->local_fd = -1;
    }
    handler->data_buffer_len = 0;
    handler->transfer_flag = 0;
    handler->port_len = 0;
    handler->command_type = DT_NONE;
}

void service_remove_all() {
    while (util_config.service_head.next != &util_config.service_head) {
        service_remove(util_config.service_head.next);
    }
}

