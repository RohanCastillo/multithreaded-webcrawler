#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "url_queue.h"

/*
   A basic HTML parser that searches for 'href="http..."'.
   It extracts the URL, wraps it in a url_item_t struct, adds depth, and enqueues it.

   Uses a non-blocking enqueue: if the queue is full, the URL is dropped rather than
   blocking. This prevents deadlock when all worker threads are busy fetching and
   simultaneously trying to enqueue new links — a full blocking enqueue would cause
   every thread to stall waiting for space that nobody can free.
*/

/* Declared in OSCrawler.c — updated here while holding queue.mutex */
extern int max_queue_depth;
void parse_and_enqueue(const char *html, url_queue_t *q, int new_depth) {
    if (html == NULL) return;

    const char *p = html;
    const char *tag = "href=\"";

    // Loop through the entire HTML string looking for the href tag
    while ((p = strstr(p, tag)) != NULL) {
        p += strlen(tag); // Move pointer to the start of the actual URL

        // Find the closing quote of the href attribute
        const char *end = strchr(p, '\"');

        if (end) {
            size_t len = end - p;

            // Validate URL length and ensure it's an absolute HTTP link
            if (len > 0 && len < MAX_URL_LENGTH) {
                url_item_t new_item;
                strncpy(new_item.url, p, len);
                new_item.url[len] = '\0';
                new_item.depth = new_depth; // set depth for the new URL

                // Only enqueue links starting with http to avoid mailto: or relative paths
                if (strncmp(new_item.url, "http", 4) == 0) {

                    /*
                       Non-blocking enqueue: lock the queue, and only insert if
                       there is space. If the queue is full, drop the URL and move on.
                       This is safe because the visited set prevents re-crawling, and
                       dropping overflow URLs only means we crawl fewer pages — which
                       is fine since --max-pages caps us anyway.
                    */
                    pthread_mutex_lock(&q->mutex);

                    if (!q->shutdown && q->size < q->capacity) {
                        q->items[q->rear] = new_item;
                        q->rear = (q->rear + 1) % q->capacity;
                        q->size++;
                        pthread_cond_signal(&q->not_empty);

                        /* Update high-water mark while we already hold the lock */
                        if (q->size > max_queue_depth) {
                            max_queue_depth = q->size;
                        }
                    }

                    pthread_mutex_unlock(&q->mutex);
                }
            }
            p = end; // Move pointer forward to continue searching
        }
    }
}
