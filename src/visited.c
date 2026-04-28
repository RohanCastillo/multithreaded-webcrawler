#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "visited.h"

/*
   djb2 hash function — fast, low collision rate for URL strings.
   Maps a URL string to a bucket index in [0, VISITED_BUCKETS).
*/
static unsigned int hash_url(const char *url) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*url++))
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    return (unsigned int)(hash % VISITED_BUCKETS);
}

/*
   Initialize visited set.
   Called once at program startup.
*/
void visited_init(visited_set_t *set) {
    /* Zero all bucket head pointers */
    for (int i = 0; i < VISITED_BUCKETS; i++)
        set->buckets[i] = NULL;
    set->count = 0;
    pthread_mutex_init(&set->mutex, NULL); /* Initialize mutex */
}

/*
   Atomically check and add URL using a hash table.
   O(1) average case — no linear scan, safe at 100k+ URLs.

   Ensures no race conditions between threads: the lock is held
   for the entire check+insert so two threads can never both
   see a URL as new and both insert it.
*/
int visited_check_and_add(visited_set_t *set, const char *url) {
    unsigned int bucket = hash_url(url);

    pthread_mutex_lock(&set->mutex);  /* Enter critical section */

    /* Walk the chain in this bucket — check if URL already exists */
    visited_node_t *node = set->buckets[bucket];
    while (node != NULL) {
        if (strcmp(node->url, url) == 0) {
            pthread_mutex_unlock(&set->mutex);
            return 0;  /* Duplicate found */
        }
        node = node->next;
    }

    /* URL is new — insert at head of bucket chain */
    if (set->count < MAX_VISITED) {
        visited_node_t *new_node = malloc(sizeof(visited_node_t));
        if (new_node) {
            strncpy(new_node->url, url, MAX_URL_LENGTH - 1);
            new_node->url[MAX_URL_LENGTH - 1] = '\0';
            new_node->next       = set->buckets[bucket]; /* prepend */
            set->buckets[bucket] = new_node;
            set->count++;
        }
    }

    pthread_mutex_unlock(&set->mutex);  /* Exit critical section */

    return 1;  /* Successfully added */
}

/*
   Free all heap-allocated chain nodes.
   Call once after the crawl finishes.
*/
void visited_destroy(visited_set_t *set) {
    pthread_mutex_lock(&set->mutex);
    for (int i = 0; i < VISITED_BUCKETS; i++) {
        visited_node_t *node = set->buckets[i];
        while (node) {
            visited_node_t *next = node->next;
            free(node);
            node = next;
        }
        set->buckets[i] = NULL;
    }
    pthread_mutex_unlock(&set->mutex);
    pthread_mutex_destroy(&set->mutex);
}
