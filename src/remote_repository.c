/**
 * remote_repository.c - Only useful (and available) for debugging the desktop build
 * Fetches remote resources using HTTPS and the CDN_BASE_PATH defined in secret.h just like
 * emscripten's fetch API would (kinda)
 * ENTIRELY AI GENERATED CODE BELOW because it's not important enough
 */

#include "remote_repository.h"
#include "repository.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#define MAX_URL_LEN 2048
#define HTTP_PORT 80
#define HTTPS_PORT 443
#define BUFFER_SIZE 4096

#ifdef __EMSCRIPTEN__
# error "Do not include remote_repository.c in the wasm build"
#endif

typedef struct {
    char protocol[8];
    char host[256];
    int port;
    char path[MAX_URL_LEN];
} URLComponents;

static int parse_url(const char *url, URLComponents *components) {
    if ( !url || !components )
        return -1;

    memset(components, 0, sizeof(URLComponents));
    components->port = -1;

    const char *ptr = url;

    const char *proto_end = strstr(ptr, "://");
    if ( proto_end ) {
        size_t len = proto_end - ptr;
        if ( len >= sizeof(components->protocol) )
            return -1;
        strncpy(components->protocol, ptr, len);
        ptr = proto_end + 3;
    } else {
        strcpy(components->protocol, "http");
    }

    const char *host_end = strpbrk(ptr, ":/");
    if ( !host_end ) {
        if ( strlen(ptr) >= sizeof(components->host) )
            return -1;
        strcpy(components->host, ptr);
        strcpy(components->path, "/");
        if ( components->port == -1 ) {
            components->port = (strcmp(components->protocol, "https") == 0) ? HTTPS_PORT : HTTP_PORT;
        }
        return 0;
    }

    size_t host_len = host_end - ptr;
    if ( host_len >= sizeof(components->host) )
        return -1;
    strncpy(components->host, ptr, host_len);
    ptr = host_end;

    if ( *ptr == ':' ) {
        ptr++;
        char *end_ptr;
        long port = strtol(ptr, &end_ptr, 10);
        if ( port <= 0 || port > 65535 )
            return -1;
        components->port = (int)port;
        ptr = end_ptr;
    }

    if ( components->port == -1 ) {
        components->port = (strcmp(components->protocol, "https") == 0) ? HTTPS_PORT : HTTP_PORT;
    }

    if ( *ptr == '\0' ) {
        strcpy(components->path, "/");
    } else {
        if ( strlen(ptr) >= sizeof(components->path) )
            return -1;
        strcpy(components->path, ptr);
    }

    return 0;
}

typedef struct {
    OWNING char *url;
    WEAK Resource_t *resource;
} FetchContext;

static void decode_chunked_body(ResourceBuffer_t *buffer) {
    unsigned char *data = buffer->data;
    uint64_t src_len = buffer->downloaded_bytes;
    uint64_t pos = 0;
    uint64_t dst_len = 0;

    while ( pos < src_len ) {
        // Find end of chunk-size line
        uint64_t line_start = pos;
        while ( pos < src_len && !(data[pos] == '\r' && pos + 1 < src_len && data[pos + 1] == '\n') )
            pos++;
        if ( pos >= src_len )
            break;

        char hex_buf[32];
        uint64_t hex_len = pos - line_start;
        if ( hex_len == 0 || hex_len >= sizeof(hex_buf) )
            break;
        memcpy(hex_buf, data + line_start, hex_len);
        hex_buf[hex_len] = '\0';

        unsigned long chunk_size = strtoul(hex_buf, NULL, 16);
        pos += 2; // skip \r\n after size

        if ( chunk_size == 0 )
            break; // last chunk

        if ( pos + chunk_size > src_len )
            break; // malformed

        memmove(data + dst_len, data + pos, chunk_size);
        dst_len += chunk_size;
        pos += chunk_size + 2; // skip chunk data and trailing \r\n
    }

    buffer->downloaded_bytes = dst_len;
}

static void *fetch_thread_func(void *arg) {
    FetchContext *ctx = (FetchContext *)arg;
    Resource_t *resource = ctx->resource;
    char *url = ctx->url;

    URLComponents components;
    if ( parse_url(url, &components) != 0 ) {
        printf("Error parsing URL: %s\n", url);
        resource->status = LOAD_ERROR;
        goto cleanup_ctx;
    }

    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", components.port);

    if ( getaddrinfo(components.host, port_str, &hints, &res) != 0 ) {
        printf("Error resolving host: %s\n", components.host);
        resource->status = LOAD_ERROR;
        goto cleanup_ctx;
    }

    int sockfd = -1;
    for ( p = res; p != NULL; p = p->ai_next ) {
        if ( (sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1 ) {
            continue;
        }

        if ( connect(sockfd, p->ai_addr, p->ai_addrlen) == -1 ) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        break;
    }

    freeaddrinfo(res);

    if ( sockfd == -1 ) {
        perror("Connection failed");
        resource->status = LOAD_ERROR;
        goto cleanup_ctx;
    }

    SSL_CTX *ssl_ctx = NULL;
    SSL *ssl = NULL;
    int is_https = (strcmp(components.protocol, "https") == 0);

    if ( is_https ) {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        const SSL_METHOD *method = TLS_client_method();
        ssl_ctx = SSL_CTX_new(method);
        if ( !ssl_ctx ) {
            printf("SSL_CTX creation failed\n");
            close(sockfd);
            resource->status = LOAD_ERROR;
            goto cleanup_ctx;
        }
        ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, sockfd);

        SSL_set_tlsext_host_name(ssl, components.host);

        if ( SSL_connect(ssl) != 1 ) {
            printf("SSL connection failed\n");
            ERR_print_errors_fp(stdout);
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(sockfd);
            resource->status = LOAD_ERROR;
            goto cleanup_ctx;
        }
    }

    char request[MAX_URL_LEN + 512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             components.path, components.host);

    int sent_bytes = 0;
    if ( is_https ) {
        sent_bytes = SSL_write(ssl, request, strlen(request));
    } else {
        sent_bytes = send(sockfd, request, strlen(request), 0);
    }

    if ( sent_bytes < 0 ) {
        perror("Failed to send request");
        if ( is_https ) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
        }
        close(sockfd);
        resource->status = LOAD_ERROR;
        goto cleanup_ctx;
    }

    char buffer[BUFFER_SIZE];
    int bytes_received;
    int header_ended = 0;
    int is_chunked = 0;

    char *header_accum = NULL;
    size_t header_accum_size = 0;

    while ( 1 ) {
        if ( is_https ) {
            bytes_received = SSL_read(ssl, buffer, sizeof(buffer));
        } else {
            bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
        }

        if ( bytes_received <= 0 )
            break;

        if ( header_ended ) {
            append_data_to_buffer(resource->buffer, buffer, bytes_received);
        } else {
            char *new_acc = realloc(header_accum, header_accum_size + bytes_received + 1);
            if ( !new_acc ) {
                if ( header_accum )
                    free(header_accum);
                header_accum = NULL;
                break;
            }
            header_accum = new_acc;
            memcpy(header_accum + header_accum_size, buffer, bytes_received);
            header_accum_size += bytes_received;
            header_accum[header_accum_size] = '\0';

            // Search for \r\n\r\n
            if ( header_accum_size >= 4 ) {
                for ( size_t i = 0; i <= header_accum_size - 4; ++i ) {
                    if ( header_accum[i] == '\r' && header_accum[i + 1] == '\n' && header_accum[i + 2] == '\r' &&
                         header_accum[i + 3] == '\n' ) {

                        int status_code = 0;
                        if ( sscanf(header_accum, "%*s %d", &status_code) == 1 ) {
                            if ( status_code != 200 ) {
                                printf("Fetch failed for '%s': HTTP Status %d\n", url, status_code);
                                resource->status = LOAD_ERROR;
                                free(header_accum);
                                header_accum = NULL;
                                header_accum_size = 0;
                                break;
                            }
                        }

                        is_chunked = (strcasestr(header_accum, "Transfer-Encoding: chunked") != NULL);
                        header_ended = 1;
                        size_t header_len = i + 4;
                        size_t body_len = header_accum_size - header_len;
                        if ( body_len > 0 ) {
                            append_data_to_buffer(resource->buffer, header_accum + header_len, body_len);
                        }
                        free(header_accum);
                        header_accum = NULL;
                        header_accum_size = 0;
                        break;
                    }
                }
                if ( resource->status == LOAD_ERROR )
                    break;
            }
        }
    }

    if ( header_accum )
        free(header_accum);

    if ( is_https ) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
    }
    close(sockfd);

    if ( resource->status != LOAD_ERROR ) {
        if ( is_chunked )
            decode_chunked_body(resource->buffer);
        resource->status = LOAD_DONE;
        if ( resource->on_resource_loaded ) {
            resource->on_resource_loaded(resource);
        }
    }

cleanup_ctx:
    if ( ctx->url )
        free(ctx->url);
    free(ctx);
    return NULL;
}

void remote_load_resource(const char *path, Resource_t *resource) {
    if ( !path || !resource )
        return;

    pthread_t thread;
    FetchContext *ctx = malloc(sizeof(FetchContext));
    if ( !ctx ) {
        resource->status = LOAD_ERROR;
        return;
    }
    // Make a copy of the path because it will be freed after this call
    ctx->url = strdup(path);
    ctx->resource = resource;

    if ( pthread_create(&thread, NULL, fetch_thread_func, ctx) != 0 ) {
        perror("Failed to create thread");
        free(ctx->url);
        free(ctx);
        resource->status = LOAD_ERROR;
    } else {
        pthread_detach(thread);
    }
}
