#ifndef DOC_MESSAGE_H
#define DOC_MESSAGE_H

#define MAX_URL_LENGTH 1024
#define MAX_PATH_LENGTH 1024



// Structure to define metadata for indexer

typedef struct {
    int docid;
    int depth;
    char url[MAX_URL_LENGTH];
    char filepath[MAX_PATH_LENGTH];
} doc_message_t;

#endif