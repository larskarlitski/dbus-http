#pragma once

#include <stdio.h>
#include <stdint.h>
#include <systemd/sd-event.h>

typedef struct HttpServer HttpServer;
typedef struct HttpResponse HttpResponse;


// return value for http handlers. The server calls available handlers until one handler does not return ignored state.
typedef enum{ HTTP_SERVER_HANDLED_SUCCESS, HTTP_SERVER_HANDLED_IGNORED, HTTP_SERVER_HANDLED_ERROR} HttpServerHandlerStatus;

typedef HttpServerHandlerStatus HttpGetHandler(const char *path, HttpResponse *response, void *userdata);
typedef HttpServerHandlerStatus HttpPostHandler(const char *path, void *data, size_t len, HttpResponse *response, void *userdata);

int http_server_new(HttpServer **serverp, uint16_t port, sd_event *loop,
                    HttpGetHandler **get_handlers, HttpPostHandler **post_handlers,
                    void *userdata, const char *www_dir);
HttpServer * http_server_free(HttpServer *server);
void http_server_freep(HttpServer **serverp);

void http_response_end(HttpResponse *response, int status);
FILE * http_response_get_stream(HttpResponse *response, const char *content_type);
void http_response_set_user_data(HttpResponse *response, void *data, void (*free_func)(void *));
void * http_response_get_user_data(HttpResponse *response);

void http_suspend_connection(HttpResponse *response);
