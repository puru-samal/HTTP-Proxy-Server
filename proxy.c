/*
A concurrent web proxy server that accepts incoming connections,
reads and parses HTTP/1.0 GET requests,
forwards requests to web servers, r
eads the servers’ responses,
and forwards the responses to the corresponding clients.
*/

/* Some useful includes to help you get started */

#include "csapp.h"
#include "http_parser.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/* Typedef for convenience */
typedef struct sockaddr SA;

/* Information about a connected client. */
typedef struct {
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[MAXLINE];      // Client host
    char serv[MAXLINE];      // Client service (port)
} client_info;

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "User-Agent: Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20230411 Firefox/63.0.\r\n";
static const char *header_conn = "Connection: close\r\n";
static const char *header_proxy = "Proxy-Connection: close\r\n";
static const char *default_version = "HTTP/1.0\r\n";
static const char *default_port = "80";

/* Helper declarations */
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg);
int doit(int client_fd);
void *thread(void *vargp);

int main(int argc, char **argv) {

    signal(SIGPIPE, SIG_IGN);

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    int listenfd, *connfdp;
    pthread_t tid;

    // Open listening file descriptor
    // argv[1] -> Listening port
    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        close(listenfd);
        exit(1);
    }

    while (1) {

        /* Allocate space on the stack for client info */
        client_info client_data;
        client_info *client = &client_data;

        /* Initialize the length of the address */
        client->addrlen = sizeof(client->addr);

        /* Wait for client to connect */
        /* accept() will block until a client connects to the port */
        client->connfd =
            accept(listenfd, (SA *)&client->addr, &client->addrlen);
        if (client->connfd < 0) {
            perror("accept");
            continue;
        }

        printf("A client has connected fd: %d\n", client->connfd);

        // (Optional) Get some extra info about the client (hostname/port)
        int res = getnameinfo((SA *)&client->addr, client->addrlen,
                              client->host, sizeof(client->host), client->serv,
                              sizeof(client->serv), 0);
        if (res == 0) {
            printf("Accepted connection from %s:%s\n", client->host,
                   client->serv);
        } else {
            fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));
        }

        connfdp = Malloc(sizeof(int));
        *connfdp = client->connfd;
        pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}

void *thread(void *vargp) {

    int client_fd = *((int *)vargp);
    pthread_detach(pthread_self());
    Free(vargp);
    doit(client_fd);
    close(client_fd);
    return NULL;
}

int doit(int client_fd) {

    rio_t rp;
    char buf[MAXLINE];
    rio_readinitb(&rp, client_fd);

    if (rio_readlineb(&rp, buf, sizeof(buf)) <= 0) {
        close(client_fd);
        return -1;
    }

    printf("Request: %s", buf);

    /* Parse the request line and check if it's well-formed */
    parser_t *parser = parser_new();
    parser_state parse_state = parser_parse_line(parser, buf);
    if (parse_state != REQUEST) {
        parser_free(parser);
        clienterror(client_fd, "400", "Bad Request",
                    "Proxy received a malformed request");
        close(client_fd);
        return -1;
    }

    /* Proxy only cares about HOST and PATH and
     PORT (if provided) from the request */
    const char *host, *path, *port, *method;
    int err = 0;
    if ((err = parser_retrieve(parser, METHOD, &method)) < 0) {
        fprintf(stderr, "Error while retreiving METHOD: %d\n", err);
        parser_free(parser);
        close(client_fd);
        return -1;
    }

    /* Check if the method is POST */
    if (strcmp(method, "GET") != 0) {
        parser_free(parser);
        clienterror(client_fd, "501", "Not Implemented",
                    "Proxy does not implement this method");
        close(client_fd);
        return -1;
    }

    if ((err = parser_retrieve(parser, HOST, &host)) < 0) {
        fprintf(stderr, "Error while retreiving HOST: %d\n", err);
        parser_free(parser);
        close(client_fd);
        return -1;
    }

    if ((err = parser_retrieve(parser, PATH, &path)) < 0) {
        fprintf(stderr, "Error while retreiving PATH: %d\n", err);
        parser_free(parser);
        close(client_fd);
        return -1;
    }

    if ((err = parser_retrieve(parser, PORT, &port)) < 0) {
        fprintf(stderr, "Error while retreiving PORT: %d\n", err);
        port = default_port; // Default
    }

    // Create HTTP Request
    // Add Mandatory headers
    char request[MAXBUF];
    sprintf(request, "%s %s %s", method, path, default_version);
    char *host_header = Malloc(MAXLINE);
    sprintf(host_header, "Host: %s:%s\r\n", host, port);
    strcat(request, host_header);
    Free(host_header);
    strcat(request, header_user_agent);
    strcat(request, header_conn);
    strcat(request, header_proxy);
    printf("Adding other headers\n");

    /* Other headers */
    while (true) {
        if (rio_readlineb(&rp, buf, sizeof(buf)) <= 0) {
            break;
        }
        /* Check for end of request headers */
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }

        // Parse the request header with parser
        parser_state parse_state = parser_parse_line(parser, buf);
        if (parse_state != HEADER) {
            clienterror(client_fd, "400", "Bad Request",
                        "Proxy could not parse request headers");
            break;
        }

        header_t *header = parser_retrieve_next_header(parser);
        if (!(strcmp(header->name, "Host") == 0 ||
              strcmp(header->name, "User-Agent") == 0 ||
              strcmp(header->name, "Connection") == 0 ||
              strcmp(header->name, "Proxy-Connection") == 0)) {
            char *hdr = Malloc(MAXLINE);
            sprintf(hdr, "%s: %s\r\n", header->name, header->value);
            strcat(request, hdr);
            Free(hdr);
        }
    }
    strcat(request, "\r\n");

    printf("Generated Request:\n");
    printf("%s", request);

    /* Forward request to server  */
    // Open client file descriptor
    printf("Sending to Host:%s, Port:%s\n", host, port);
    int serverfd;
    serverfd = open_clientfd(host, port);
    if (serverfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", port);
        clienterror(client_fd, "400", "Bad Request",
                    "Proxy could not parse request headers");
        close(serverfd);
        parser_free(parser);
        return -1;
    }

    parser_free(parser);

    /* Send request to server */
    rio_t srp;
    rio_readinitb(&srp, serverfd);
    char server_buf[MAX_OBJECT_SIZE];
    if (rio_writen(serverfd, request, strlen(request)) <= 0) {
        fprintf(stderr, "Failed to send request! \n");
        close(serverfd);
        return -1;
    }

    int m = 0;
    while ((m = rio_readnb(&srp, server_buf, MAX_OBJECT_SIZE)) > 0) {
        rio_writen(client_fd, server_buf, m);
        memset(server_buf, 0, sizeof(server_buf));
    }

    close(serverfd);
    return 0;
}

void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
                       "<!DOCTYPE html>\r\n"
                       "<html>\r\n"
                       "<head><title>Tiny Error</title></head>\r\n"
                       "<body bgcolor=\"ffffff\">\r\n"
                       "<h1>%s: %s</h1>\r\n"
                       "<p>%s</p>\r\n"
                       "<hr /><em>The Tiny Web server</em>\r\n"
                       "</body></html>\r\n",
                       errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
                      "HTTP/1.0 %s %s\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}
