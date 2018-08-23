
#include "http-server.h"
#include "log.h"
#include "environment.h"

#include <errno.h>
#include <microhttpd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/stat.h>


static const char CONTENT_TYPE_HTML[] = "text/html; charset=utf-8";
static const char CONTENT_TYPE_CSS[] = "text/css; charset=utf-8";
static const char CONTENT_TYPE_JS[] = "application/javascript";
static const char CONTENT_TYPE_MAP[] = "application/octet-stream";
static const char CONTENT_TYPE_ICO[] = "image/x-icon";
static const char CONTENT_TYPE_PNG[] = "image/png";
static const char CONTENT_TYPE_JPG[] = "image/jpg";
static const char CONTENT_TYPE_GIF[] = "image/gif";
static const char CONTENT_TYPE_SVG[] = "image/svg+xml";
static const char CONTENT_TYPE_TTF[] = "application/x-font-ttf";
static const char CONTENT_TYPE_WOFF2[] = "font/woff2";
static const char CONTENT_TYPE_JSON[] = "application/json";

#define POSTBUFFERSIZE  512

#define _cleanup_(fn) __attribute__((__cleanup__(fn)))

typedef struct HttpRequest HttpRequest;

typedef enum { UNDEFINED, GET, POST, POST_FILE } ConnectionType;

struct HttpServer {
        struct MHD_Daemon *daemon;
        sd_event_source *http_event;
        HttpGetHandler **get_handlers;
        HttpPostHandler **post_handlers;
        void *userdata;
        const char *www_dir;
};

struct HttpRequest {
        FILE *f;  // memstream buffer or file written to disk
        // memstream buffer
        char *body;
        size_t size;
        // or post processor
        struct MHD_PostProcessor *postprocessor;
        // shortcut for strcmp(method)
        ConnectionType conn_type;
};

struct HttpResponse {
        struct MHD_Connection *connection;

        FILE *f;
        char *body;
        size_t size;

        char *content_type;

        void *user_data;
        void (*free_func)(void *);
};


static const char *get_extension(const char *path) {
        const char *dot = strrchr(path, '.');
        if (!dot) {
                return dot;
        } else {
                return dot + 1;
        }
}

static ssize_t file_reader_callback (void *cls, uint64_t pos, char *buf, size_t max) {
        FILE *file = cls;

        (void)  fseek (file, pos, SEEK_SET);
        return fread (buf, 1, max, file);
}

static void file_free_callback (void *cls) {
        FILE *file = cls;
        fclose (file);
}

static void free_full_path(char** full_path) {
        if(*full_path)
                free(*full_path);
}


void http_suspend_connection(HttpResponse *response){
        log_debug("Suspending connection 0x%p", (void*)response->connection);
        MHD_suspend_connection(response->connection);
}

static HttpServerHandlerStatus handle_get_file(void *cls, const char *url, HttpResponse *response) {
        HttpServer *server = cls;
        _cleanup_(free_full_path) char *full_path = NULL;
        struct stat path_stat;
        FILE *file;
        const char *content_type = NULL;

        log_info("handle_get_file for URL: %s", url);

        // prepend www folder
        full_path = malloc(strlen(server->www_dir) + strlen(url) + 12); // leave enough space to append "/index.html" if necessary.

        strcpy(full_path, server->www_dir);
        strcat(full_path, url);

        if (access(full_path, R_OK) == -1) {
                http_response_end(response, 404);
                return HTTP_SERVER_HANDLED_ERROR;
        }

        lstat(full_path, &path_stat);
        if (S_ISDIR(path_stat.st_mode)) {
                strcat(full_path, "/index.html");
        }

        lstat(full_path, &path_stat);
        if (S_ISREG(path_stat.st_mode)) {
                const char *extension = get_extension(full_path);

                if (strcmp(extension, "html") == 0) {
                        content_type = CONTENT_TYPE_HTML;
                } else if (strcmp(extension, "css") == 0) {
                        content_type = CONTENT_TYPE_CSS;
                } else if (strcmp(extension, "js") == 0) {
                        content_type = CONTENT_TYPE_JS;
                } else if (strcmp(extension, "json") == 0) {
                        content_type = CONTENT_TYPE_JSON;
                } else if (strcmp(extension, "map") == 0) {
                        content_type = CONTENT_TYPE_MAP;
                } else if (strcmp(extension, "ico") == 0) {
                        content_type = CONTENT_TYPE_ICO;
                } else if (strcmp(extension, "png") == 0) {
                        content_type = CONTENT_TYPE_PNG;
                } else if (strcmp(extension, "jpg") == 0) {
                        content_type = CONTENT_TYPE_JPG;
                } else if (strcmp(extension, "gif") == 0) {
                        content_type = CONTENT_TYPE_GIF;
                } else if (strcmp(extension, "svg") == 0) {
                        content_type = CONTENT_TYPE_SVG;
                } else if (strcmp(extension, "ttf") == 0) {
                        content_type = CONTENT_TYPE_TTF;
                } else if (strcmp(extension, "woff2") == 0) {
                        content_type = CONTENT_TYPE_WOFF2;
                } else {
                        // content type not recognized else
                        log_warning("Content type is unknown");
                        http_response_end(response, MHD_HTTP_NOT_FOUND);
                        return HTTP_SERVER_HANDLED_ERROR;
                }
        }

        file = fopen (full_path, "rb");
        if (file == NULL) {
                // content type not recognized
                http_response_end(response, MHD_HTTP_INTERNAL_SERVER_ERROR);
                return HTTP_SERVER_HANDLED_ERROR;
        } else {
                struct MHD_Response *mhd_response;
                int ret;

                if (response->f)
                               fclose(response->f);

                mhd_response = MHD_create_response_from_callback (path_stat.st_size, 32 * 1024,     /* 32k page size */
                                &file_reader_callback, file, &file_free_callback);
                if (mhd_response == NULL) {
                        fclose (file);
                        log_err("Error in handle_get_file while handling path: %s (file found).", full_path);
                        return HTTP_SERVER_HANDLED_ERROR;
                }

                if(content_type) {
                        MHD_add_response_header(mhd_response, "Content-Type", content_type);
                }

                ret = MHD_queue_response (response->connection, MHD_HTTP_OK, mhd_response);
                if(ret != MHD_YES)
                        log_err("Enqueueing failed!");
                log_debug("file served for URL: %s, path: %s", url, full_path);

                MHD_destroy_response (mhd_response);
                if (response->free_func)
                        response->free_func(response->user_data);
                free(response);

                return HTTP_SERVER_HANDLED_SUCCESS;
        }
}


static int iterate_post (void *cls, enum MHD_ValueKind kind, const char *key,
                const char *filename, const char *content_type, const char *transfer_encoding,
                const char *data, uint64_t off, size_t size) {
        HttpRequest *request = cls;
        _cleanup_(free_full_path) char *full_path = NULL;

        if (! request->f) {
                if(asprintf(&full_path, "/tmp/%s", filename) < 0) {
                        log_crit("Concatenation of filename failed");
                        return MHD_NO;
                }
                request->f = fopen (full_path, "w+");
                if (!request->f) {
                        log_crit("Opening file %s failed", full_path);
                        return MHD_NO;
                }
        }

        if (size > 0) {
                if (! fwrite (data, sizeof (char), size, request->f)) {
                        log_crit("Writing to file %s failed", full_path);
                        return MHD_NO;
                }
        }
        return MHD_YES;
}


static void request_completed(void *cls, struct MHD_Connection *connection,
                              void **connection_cls, enum MHD_RequestTerminationCode toe) {
        HttpRequest *request = *connection_cls;

        log_debug("request_completed");

        if (request->f)
                fclose(request->f);

        if (request->postprocessor)
                MHD_destroy_post_processor (request->postprocessor);

        free(request->body);
        free(request);

        log_debug("Completed connection request 0x%p",(void*)request);
}

static int handle_request(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method, const char *version,
                          const char *upload_data, size_t *upload_data_size,
                          void **connection_cls) {
        HttpServer *server = cls;
        HttpRequest *request = *connection_cls;
        HttpResponse *response;
        HttpServerHandlerStatus handler_r = HTTP_SERVER_HANDLED_IGNORED;
        const char filepost_url[] = "/filepost";

        // new connection
        if (request == NULL) {
                request = calloc(1, sizeof(HttpRequest));
                *connection_cls = request;

                if (strcasecmp(method, MHD_HTTP_METHOD_GET) == 0){
                        request->conn_type = GET;
                } else if (strcasecmp(method, MHD_HTTP_METHOD_POST) == 0){
                        if(strncmp(filepost_url, url, strlen(filepost_url)) == 0){
                                request->conn_type = POST_FILE;
                        } else {
                                request->conn_type = POST;
                        }
                } else {
                        request->conn_type = UNDEFINED;
                }

                /* Support file upload to /tmp folder (multipart/form-data , application/x-www-form-urlencoded) */
                if ( request->conn_type == POST_FILE) {
                        request->postprocessor = MHD_create_post_processor(connection, POSTBUFFERSIZE, iterate_post, (void *) request);
                        if (request->postprocessor == NULL) {
                                log_crit("Cannot allocate post processor");
                                return MHD_YES;
                        }
                }

                log_debug("Created new connection request 0x%p",(void*)request);
                return MHD_YES;
        }

        if (*upload_data_size) {
                // If there is a postprocessor run the file upload
                if(request->postprocessor != NULL) {
                        if (MHD_post_process (request->postprocessor, upload_data, *upload_data_size) != MHD_YES) {
                                log_crit("failure while post processing data.");
                                return MHD_YES;
                        }
                // otherwise store the post data into a memstream object to be used for a dbus call
                } else {
                        if (!request->f) {
                                log_debug("upload start (%d bytes)", *upload_data_size);
                                request->f = open_memstream(&request->body, &request->size);
                        } else {
                                log_debug("upload continue (%d bytes)", *upload_data_size);
                        }
                        fwrite(upload_data, 1, *upload_data_size, request->f);
                }
                *upload_data_size = 0;
                return MHD_YES;
        }

        if (request->f) {
                fclose(request->f);
                request->f = NULL;
        }

        response = calloc(1, sizeof(HttpResponse));
        response->connection = connection;

        if (request->conn_type == GET) {
                for(HttpGetHandler **handler_ptr = server->get_handlers; *handler_ptr != NULL; handler_ptr++) {
                        handler_r = (*handler_ptr)(url, response, server->userdata);
                        if(handler_r != HTTP_SERVER_HANDLED_IGNORED) {
                                break;
                        }
                }
                // If no get handler is responsible, the file handler is called.
                if(handler_r == HTTP_SERVER_HANDLED_IGNORED) {
                        log_debug("Calling the file handler for GET request to %s.", url);
                        handler_r = handle_get_file(cls, url, response);
                }
        } else if (request->conn_type == POST) {
                for(HttpPostHandler **handler_ptr = server->post_handlers; *handler_ptr != NULL; handler_ptr++) {
                        handler_r = (*handler_ptr)(url, request->body, request->size, response, server->userdata);
                        if(handler_r != HTTP_SERVER_HANDLED_IGNORED) {
                                break;
                        }
                }
        } else if(request->conn_type == POST_FILE) {
                FILE *f = http_response_get_stream(response, "application/json");
                fputs("{\"result\":\"success\"}", f);
                log_debug("Finalizing file upload");
                http_response_end(response, MHD_HTTP_OK);
        } else {
                log_err("Handling of %s is not implemented.", method);
                http_response_end(response, MHD_HTTP_NOT_ACCEPTABLE);
        }

        return MHD_YES;
}


static void mhd_run_to_end(void *userdata) {
        MHD_UNSIGNED_LONG_LONG to;
        do {
                log_debug("MHD_run");
                MHD_run(userdata);
        } while(MHD_get_timeout(userdata, &to) == MHD_YES && !to);
}


static int handle_http_event(sd_event_source *event, int fd, uint32_t revents, void *userdata) {
        log_debug("MHD_run, handle_http_event");
        mhd_run_to_end(userdata);
        return 1;
}

static void http_server_log(void * arg, const char * fmt, va_list ap) {
        printf("microhttpd: ");
        vprintf(fmt, ap);
}

static bool ipv6_test(void) {
          struct stat buffer;
          return (stat ("/proc/net/if_inet6", &buffer) == 0);
}

int http_server_new(HttpServer **serverp, uint16_t port, sd_event *loop,
                    HttpGetHandler **get_handlers, HttpPostHandler **post_handlers,
                    void *userdata, const char *www_dir) {
        _cleanup_(http_server_freep) HttpServer *server = NULL;
        int flags;
        const union MHD_DaemonInfo *info;
        int r;

        server = calloc(1, sizeof(HttpServer));
        server->get_handlers = get_handlers;
        server->post_handlers = post_handlers;
        server->userdata = userdata;
        server->www_dir = www_dir;

        flags = MHD_ALLOW_SUSPEND_RESUME |
                MHD_USE_PEDANTIC_CHECKS |
                MHD_USE_EPOLL | MHD_USE_TURBO |
                /* MHD_USE_TCP_FASTOPEN | */
                MHD_USE_PIPE_FOR_SHUTDOWN;
        if(ipv6_test()) {
                flags |= MHD_USE_DUAL_STACK;
        }
        flags |= MHD_USE_ERROR_LOG;
        if(log_get_level() >= LOG_DEBUG ) {
                flags |= MHD_USE_DEBUG;
        }

        server->daemon = MHD_start_daemon(flags, port, NULL, NULL, handle_request, server,
                                          MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
                                          MHD_OPTION_EXTERNAL_LOGGER, http_server_log, NULL,
                                          MHD_OPTION_END);

        if (server->daemon == NULL)
                return -EOPNOTSUPP;

        info = MHD_get_daemon_info(server->daemon, MHD_DAEMON_INFO_EPOLL_FD);
        if (info == NULL)
                return -EOPNOTSUPP;

        if (info->listen_fd < 0)
                return -EOPNOTSUPP;

        r = sd_event_add_io(loop, &server->http_event, info->listen_fd, EPOLLIN, handle_http_event, server->daemon);
        if (r < 0)
                return r;

        *serverp = server;
        server = NULL;

        return 0;
}

HttpServer * http_server_free(HttpServer *server) {
        if (server->http_event)
                sd_event_source_unref(server->http_event);

        if (server->daemon)
                MHD_stop_daemon(server->daemon);

        free(server);
        return NULL;
}

void http_server_freep(HttpServer **serverp) {
        if (*serverp)
                http_server_free(*serverp);
}

void http_response_end(HttpResponse *response, int status) {
        struct MHD_Response *mhd_response;
        const union MHD_ConnectionInfo *info;
        int ret;

        if (response->f)
                fclose(response->f);

        mhd_response = MHD_create_response_from_buffer(response->size, (void *)response->body, MHD_RESPMEM_MUST_FREE);

        if (response->content_type) {
                MHD_add_response_header(mhd_response, "Content-Type", response->content_type);
                free(response->content_type);
        }

        log_debug("Enqueueing response and resuming connection 0x%p", (void*)(&(response->connection)));
        ret = MHD_queue_response(response->connection, status, mhd_response);
        if(ret != MHD_YES){
                log_err("Enqueueing failed!");
        }
        log_debug("Resuming connection");
        MHD_resume_connection(response->connection);

        info = MHD_get_connection_info(response->connection, MHD_CONNECTION_INFO_DAEMON);
        log_debug("mhd_run_to_end, http_response_end 1");
        mhd_run_to_end(info->daemon);

        MHD_destroy_response(mhd_response);

        if (response->free_func)
                response->free_func(response->user_data);
        free(response);
}

FILE * http_response_get_stream(HttpResponse *response, const char *content_type) {
        if (response->f)
                return response->f;

        response->f = open_memstream(&response->body, &response->size);
        response->content_type = strdup(content_type);

        return response->f;
}

void http_response_set_user_data(HttpResponse *response, void *data, void (*free_func)(void *)) {
        if (response->free_func)
                response->free_func(response->user_data);

        response->user_data = data;
        response->free_func = free_func;
}

void * http_response_get_user_data(HttpResponse *response) {
        return response->user_data;
}
