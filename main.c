#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <memory.h>
#include "async_util.h"

void parse_arguments(int argc, char *argv[]) {
    struct option options[] = {
            {"root",  required_argument, NULL, 'r'},
            {"port",  required_argument, NULL, 'p'},
            {"info",  no_argument,       NULL, 'i'},
            {"debug", no_argument,       NULL, 'd'}
    };
    int c;
    char *root = "/tmp", *port = "21";
    util_config.log_level = LOG_WARN;
    while ((c = getopt_long_only(argc, argv, "idr:p:", options, NULL)) != -1) {
        switch (c) {
            case 'i':
                util_config.log_level = LOG_INFO;
                break;
            case 'd':
                util_config.log_level = LOG_DEBUG;
                break;
            case 'r':
                root = optarg;
                break; // prev bug: ftl
            case 'p':
                port = optarg;
                break;
            default:
                printf("usage: server [-root <root>] [-port <port>]\n");
                exit(-1);
        }
    }

    util_config.root_len = strlen(root);
    if (util_config.root_len >= PATH_MAX_LEN) {
        if (util_config.log_level >= LOG_ERR) {
            fprintf(stderr, "root path too long\n");
        }
        exit(-1);
    }
    if (util_config.root_len > 0 && root[util_config.root_len - 1] == '/') {
        root[--util_config.root_len] = '\0'; // ensure that root do not end with /
    }
    util_config.root = root;
    util_config.port = port;
}

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);
    if (start_up() == -1) {
        return -1;
    }
    main_loop();
    tear_down();
    return 0;
}