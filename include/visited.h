#ifndef VISITED_H
#define VISITED_H

#include <pthread.h>

#define MAX_VISITED    100000     /* Maximum number of URLs we can track */
#define MAX_URL_LENGTH 1024       /* Maximum length of a URL string */

/*
   Hash table bucket for chained collision resolution.
   Each bucket is a singly-linked list of URL strings.
*/
typedef struct visited_node {
    char url[MAX_URL_LENGTH];
    struct visited_node *next;
} visited_node_t;

/*
   Thread-safe visited URL set backed by a hash table.
   Replaces the original O(n) linear-scan array so that
   check+add stays near O(1) even at 100k+ URLs.

   Hash table uses separate chaining: VISITED_BUCKETS buckets,
   each headed by a visited_node_t pointer.  Collisions are
   rare in practice because URLs are long and vary a lot.
*/
#define VISITED_BUCKETS 16381    /* Prime near 2^14 — good load factor for 100k URLs */

typedef struct {
    visited_node_t *buckets[VISITED_BUCKETS]; /* Array of bucket head pointers */
    int             count;                    /* Total URLs stored */
    pthread_mutex_t mutex;                    /* Protects all bucket access */
} visited_set_t;

/*
   Initialize the visited set.
   Sets all bucket heads to NULL and initializes the mutex.
*/
void visited_init(visited_set_t *set);

/*
   Atomically check if URL already exists; if not, add it.
   Returns:
      1 = successfully added (new URL)
      0 = already exists (duplicate)
*/
int visited_check_and_add(visited_set_t *set, const char *url);

/*
   Free all heap-allocated nodes in the visited set.
   Call once after the crawl is done.
*/
void visited_destroy(visited_set_t *set);

#endif
