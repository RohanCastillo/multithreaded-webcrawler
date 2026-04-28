#include <stdio.h>
#include <string.h>
#include "url_queue.h"

/*
   Initialize the queue.
   Sets up circular buffer indexes and synchronization primitives.
*/
void queue_init(url_queue_t *q, int capacity) {

    q->front = 0;              // Dequeue index
    q->rear = 0;               // Enqueue index
    q->size = 0;               // No items yet
    q->capacity = capacity;    // Max queue size
    q->shutdown = 0;           // Not shutting down initially

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}


/*
   Enqueue a URL item into the queue.
   Blocks if queue is full.
*/
void queue_enqueue(url_queue_t *q, const url_item_t *item) {

    pthread_mutex_lock(&q->mutex);

    while (q->size == q->capacity && !q->shutdown) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }

    if (q->shutdown) {
        pthread_mutex_unlock(&q->mutex);
        return;
    }

    q->items[q->rear] = *item;                    // fixed: was q->urls
    q->rear = (q->rear + 1) % q->capacity;
    q->size++;

    pthread_cond_signal(&q->not_empty);

    pthread_mutex_unlock(&q->mutex);
}


/*
   Dequeue a URL item from the queue.
   Blocks if queue is empty.
   Returns 0 if shutdown is signaled.
*/
int queue_dequeue(url_queue_t *q, url_item_t *item) {

    pthread_mutex_lock(&q->mutex);

    while (q->size == 0 && !q->shutdown) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    if (q->shutdown && q->size == 0) {
        pthread_mutex_unlock(&q->mutex);
        return 0;
    }

    *item = q->items[q->front];                   // fixed: was q->urls
    q->front = (q->front + 1) % q->capacity;
    q->size--;

    pthread_cond_signal(&q->not_full);

    pthread_mutex_unlock(&q->mutex);

    return 1;
}


/*
   Signal shutdown.
   Wakes up all waiting threads so they can exit.
*/
void queue_signal_shutdown(url_queue_t *q) {

    pthread_mutex_lock(&q->mutex);

    q->shutdown = 1;

    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);

    pthread_mutex_unlock(&q->mutex);
}