#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "ipc.h"

/* Start a UNIX domain socket server (used by indexer) */
int ipc_server_start(const char *path) {
    int server_fd;
    struct sockaddr_un addr;

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    /* Remove old socket file if it already exists */
    unlink(path);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

/* Accept one client connection (used by indexer) */
int ipc_server_accept(int server_fd) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        return -1;
    }
    return client_fd;
}

/* Connect to UNIX domain socket server (used by crawler) */
int ipc_client_connect(const char *path) {
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

/* Send one doc_message_t over the socket */
int ipc_send_doc_message(int fd, const doc_message_t *msg) {
    ssize_t bytes_sent = write(fd, msg, sizeof(doc_message_t));
    if (bytes_sent != sizeof(doc_message_t)) {
        perror("write");
        return -1;
    }
    return 0;
}

/*
   Receive one doc_message_t from the socket.

   A single read() on a stream socket is NOT guaranteed to return all
   sizeof(doc_message_t) bytes in one call — the kernel may deliver the
   data in fragments.  This loop retries until the full struct arrives,
   the connection closes (EOF), or a real error occurs.

   Returns:
      1  = full message received OK
      0  = EOF — crawler closed the connection cleanly
     -1  = error
*/
int ipc_receive_doc_message(int fd, doc_message_t *msg) {
    size_t  total  = 0;
    size_t  needed = sizeof(doc_message_t);
    char   *buf    = (char *)msg;

    while (total < needed) {
        ssize_t n = read(fd, buf + total, needed - total);

        if (n == 0) {
            /* EOF: crawler closed connection */
            if (total == 0)
                return 0;   /* clean EOF before any bytes — normal shutdown */
            /* EOF mid-message is a protocol error */
            fprintf(stderr, "ipc_receive: EOF mid-message (%zu/%zu bytes)\n",
                    total, needed);
            return -1;
        }

        if (n < 0) {
            perror("read");
            return -1;
        }

        total += (size_t)n;
    }

    return 1;  /* full message received */
}

/* Close a socket/file descriptor */
void ipc_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}
