#ifndef URL_QUEUE_H
#define URL_QUEUE_H

#include <pthread.h>

#define MAX_URL_LENGTH 1024
#define MAX_QUEUE_SIZE 1000


/* struct for storing URL and depth information */
typedef struct {
    char url[MAX_URL_LENGTH];
    int depth;
} url_item_t;


/*
   Thread-safe bounded circular queue.
   Used as the URL frontier.
*/
typedef struct {

    url_item_t items[MAX_QUEUE_SIZE];  // Queue storage

    int front;      // Index of next item to dequeue
    int rear;       // Index of next slot to enqueue
    int size;       // Current number of items
    int capacity;   // Maximum allowed size

    pthread_mutex_t mutex;          // Protects queue access
    pthread_cond_t not_empty;       // Signaled when queue gets item
    pthread_cond_t not_full;        // Signaled when queue has space

    int shutdown;   // Signals threads to exit cleanly

} url_queue_t;



/*
   Initialize queue with capacity.
*/
void queue_init(url_queue_t *q, int capacity);

/*
   Add URL to queue.
   Blocks if queue is full.
*/
void queue_enqueue(url_queue_t *q, const url_item_t *item);

/*
   Remove URL from queue.
   Blocks if queue is empty.
   Returns 0 if shutdown is signaled.
*/
int queue_dequeue(url_queue_t *q, url_item_t *item);

/*
   Signal shutdown to all waiting threads.
*/
void queue_signal_shutdown(url_queue_t *q);

#endif