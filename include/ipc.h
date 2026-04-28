#ifndef IPC_H
#define IPC_H

#include "doc_message.h"


/* Indexer-side functions */

/* indexer creates and binds the Unix domain socket server */
int ipc_server_start(const char *path);

/* indexer waits for crawler to connect */
int ipc_server_accept(int server_fd);




/* Crawler-side function */

/* crawler connects to the Unix domain socket server */
int ipc_client_connect(const char *path);


/* Crawler sends one metadata message */
int ipc_send_doc_message(int fd, const doc_message_t *msg);

/* indexer receives one metadata message */
int ipc_receive_doc_message(int fd, doc_message_t *msg);

/* closes the Unix domain socket */
void ipc_close(int fd);

#endif