
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include "systemd-compat.h"
#include "environment.h"
#include "dbus-http.h"
#include "log.h"


#define _cleanup_(fn) __attribute__((__cleanup__(fn)))

const char default_www_dir[] = "/usr/share/dbus-http/www";

typedef struct {
        bool session_bus;
        uint16_t http_port;
        char *www_dir;
} CmdArgs;


static void cmd_args_free(CmdArgs **cmd_args) {
        if(*cmd_args){
                if((*cmd_args)->www_dir) {
                        free((*cmd_args)->www_dir);
                        (*cmd_args)->www_dir = NULL;
                }
                free(*cmd_args);
                *cmd_args = NULL;
        }
}


static const char *cmd_args_get_www_dir(CmdArgs *cmd_args) {
        if(cmd_args){
                if(cmd_args->www_dir) {
                        return cmd_args->www_dir;
                }
        }
        return default_www_dir;
}

static CmdArgs* cmd_args_new (int argc, char **argv) {
        CmdArgs* cmd_args;
        int short_arg;

        opterr = 0;

        cmd_args = malloc(sizeof(CmdArgs));
        cmd_args->session_bus = false;
        cmd_args->http_port = 80;
        cmd_args->www_dir = NULL;

        while ((short_arg = getopt (argc, argv, "sp:v:w:h")) != -1) {
                switch (short_arg)
                {
                case 's':
                        cmd_args->session_bus = true;
                        break;

                case 'p': {
                        char *tail_ptr;
                        unsigned long port;
                        port = strtoul(optarg, &tail_ptr, 10);
                        if(cmd_args->http_port <= 32768 && *tail_ptr == 0) {
                                cmd_args->http_port = port;
                        } else {
                                puts("port must be 0..32768 (upper ports are reserved for random port numbers assigned by Linux)");
                                cmd_args_free(&cmd_args);
                                return NULL;
                        }
                        break;
                }
                case 'v':
                        if(log_set_level_str(optarg) < 0) {
                                printf("log level must be in range: ");
                                log_print_levels();
                                puts("");
                                cmd_args_free(&cmd_args);
                                return NULL;
                        }
                        break;
                case 'w':
                        if(access(optarg, R_OK) == 0) {
                                cmd_args->www_dir = malloc(strlen(optarg) + 1);
                                strcpy(cmd_args->www_dir, optarg);
                        } else {
                                printf("Error: invalid www directory %s\n", optarg);
                                cmd_args_free(&cmd_args);
                                return NULL;
                        }
                        break;
                // Invalid argument or -h -?...
                default:
                        puts("-s run on session DBUS");
                        puts("-p 0..32767 HTTP port (default 80)");
                        printf("-w folder exported by file server (default %s)\n", default_www_dir);
                        printf("-v [");
                        log_print_levels();
                        puts("]");
                        cmd_args_free(&cmd_args);
                        return NULL;
                }
        }
        return cmd_args;
}


HttpGetHandler *get_handlers[] = {
                handle_get_dbus,
                NULL
};

HttpPostHandler *post_handlers[] = {
                handle_post_dbus,
                NULL
};

int main(int argc, char **argv) {
        _cleanup_(sd_event_unrefp) sd_event *loop = NULL;
        _cleanup_(sd_bus_unrefp) sd_bus *bus = NULL;
        _cleanup_(http_server_freep) HttpServer *server = NULL;
        int r;
        Environment *env = NULL;
        CmdArgs *cmd_args;
        const char *session_bus = "session";
        const char *system_bus = "system";
        const char *bus_name = NULL;

        cmd_args = cmd_args_new(argc, argv);
        if (!cmd_args){
                r = -1;
                goto finish;
        }

        r = sd_event_default(&loop);
        if (r < 0)
                goto finish;

        if(cmd_args->session_bus) {
                bus_name = session_bus;
                r = sd_bus_open_user(&bus);
        }
        else {
                bus_name = system_bus;
                r = sd_bus_open_system(&bus);
        }
        if (r < 0)
                goto finish;

        log_notice("dbus-http starting on %s dbus, port %u", bus_name, cmd_args->http_port);

        r = sd_bus_attach_event(bus, loop, 0);
        if (r < 0)
                goto finish;

        // Initialize the Server Environment
        env = malloc(sizeof *env);
        if (env == NULL) {
                puts("failed to allocate memory.");
                goto finish;
        }
        env->bus = bus;
        env->dbus_prefix = "/dbus/";

        r = http_server_new(&server, cmd_args->http_port, loop, get_handlers, post_handlers, env,
                        cmd_args_get_www_dir(cmd_args));
        if (r < 0)
                goto finish;

        r = sd_event_loop(loop);
        if (r < 0)
                goto finish;

finish:
        if (r < 0)
                log_emerg("Failure: %s\n", strerror(-r));

        cmd_args_free(&cmd_args);
        if(env)
                free(env);

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
