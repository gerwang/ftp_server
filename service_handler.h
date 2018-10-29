//
// Created by gerw on 10/29/18.
//

#ifndef FTP_SERVER_SERVICE_HANDLER_H
#define FTP_SERVER_SERVICE_HANDLER_H

typedef struct service_handler_t {
    struct service_handler_t *prev, *next;
} service_handler_t;

typedef int(*service_handler_callback_t)(service_handler_t *handler, char *parameter);

#endif //FTP_SERVER_SERVICE_HANDLER_H