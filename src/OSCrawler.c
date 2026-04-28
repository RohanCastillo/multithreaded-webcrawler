/*
   OSCrawler.c — Multithreaded web crawler.

   Maintains a bounded, thread-safe URL frontier (url_queue_t).
   Runs a fixed-size pthread worker pool.
   Fetches pages via libcurl, saves them to disk, parses outgoing links,
   and sends document metadata to the indexer over a UNIX domain socket.

   Usage:
       ./crawler --seed <url> --max-depth <D> --max-pages <N> \
                 -t <threads> --out <dir> --ipc <path>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include "url_queue.h"
#include "visited.h"
#include "fetcher.h"
#include "parser.h"
#include "doc_message.h"
#include "ipc.h"

/* -----------------------------
   Global Shared Structures
------------------------------*/

url_queue_t   queue;
visited_set_t visited;

/* IPC variables */
int  ipc_fd = -1;
char ipc_path[108] = "";
pthread_mutex_t ipc_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Configurable via CLI */
int  max_pages = 50;
int  max_depth = 3;
char out_dir[1024] = "data";
int  num_threads = 4;

/* Shared counters */
int pages_processed  = 0;
int pages_skipped    = 0;
int pages_failed     = 0;
int max_queue_depth  = 0;  /* High-water mark of queue.size across the crawl */

/*
   busy: number of threads currently doing work between a dequeue and
   their next attempt to dequeue. Shutdown is signaled when busy == 0
   AND the queue is empty — meaning there is nothing in flight and
   nothing waiting, so the crawl is truly done.

   Protected by queue.mutex so the (busy == 0 && queue.size == 0) check
   is always atomic with respect to enqueues from parse_and_enqueue.
*/
int busy = 0;

/* Protects pages_processed / pages_skipped / pages_failed */
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -----------------------------
   Logging
   Writes timestamped entries to both stdout and crawl.log inside out_dir.
   log_mutex protects the FILE* so multiple threads don't interleave writes.
------------------------------*/
static FILE           *log_fp    = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
   Open the log file at <out_dir>/crawl.log.
   Creates out_dir if it doesn't already exist.
*/
static void log_open(const char *dir) {
    char path[1152];
    snprintf(path, sizeof(path), "%s/crawl.log", dir);
    log_fp = fopen(path, "w");
    if (!log_fp)
        perror("Warning: could not open crawl.log");
}

static void log_close(void) {
    if (log_fp) { fclose(log_fp); log_fp = NULL; }
}

/*
   Write a timestamped log line.
   Format: [YYYY-MM-DD HH:MM:SS] <message>
   Thread-safe: protected by log_mutex.
*/
static void log_write(const char *fmt, ...) {
    va_list ap;

    pthread_mutex_lock(&log_mutex);

    /* Always echo to stdout */
    va_start(ap, fmt);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);

    /* Mirror to log file if open */
    if (log_fp) {
        va_start(ap, fmt);
        vfprintf(log_fp, fmt, ap);
        fprintf(log_fp, "\n");
        fflush(log_fp);
        va_end(ap);
    }

    pthread_mutex_unlock(&log_mutex);
}

/* Convenience macros for common log levels */
#define LOG_INFO(fmt, ...)  log_write(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_write(fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_write(fmt, ##__VA_ARGS__)


/* -----------------------------
   Signal shutdown if nothing is left to do.
   MUST be called while holding queue.mutex.
------------------------------*/
static void check_done_locked(void) {
    if (busy == 0 && queue.size == 0) {
        queue.shutdown = 1;
        pthread_cond_broadcast(&queue.not_empty);
        pthread_cond_broadcast(&queue.not_full);
    }
}


/* -----------------------------
   Worker Thread
------------------------------*/

void *worker(void *arg) {

    url_item_t current_item;

    while (1) {

        /* ---- go idle: decrement busy and wait for a URL ---- */
        pthread_mutex_lock(&queue.mutex);
        busy--;

        /*
           Check done before sleeping: if we just finished the last
           piece of work and the queue is empty, signal shutdown now
           so we don't sleep forever.
        */
        check_done_locked();

        while (queue.size == 0 && !queue.shutdown) {
            pthread_cond_wait(&queue.not_empty, &queue.mutex);
        }

        if (queue.shutdown && queue.size == 0) {
            pthread_mutex_unlock(&queue.mutex);
            break;
        }

        /* Dequeue and mark busy BEFORE releasing lock so check_done_locked
           in another thread cannot see busy==0 while we have work. */
        current_item = queue.items[queue.front];
        queue.front  = (queue.front + 1) % queue.capacity;
        queue.size--;
        busy++;

        /* Update high-water mark while we still hold queue.mutex */
        if (queue.size > max_queue_depth)
            max_queue_depth = queue.size;

        pthread_cond_signal(&queue.not_full);
        pthread_mutex_unlock(&queue.mutex);

        /* ---- process the URL ---- */

        /* Skip duplicates */
        if (!visited_check_and_add(&visited, current_item.url)) {
            pthread_mutex_lock(&counter_mutex);
            pages_skipped++;
            pthread_mutex_unlock(&counter_mutex);
            continue;  /* loop back → busy-- → check_done */
        }

        /* Stop condition: max pages */
        pthread_mutex_lock(&counter_mutex);
        if (pages_processed >= max_pages) {
            pthread_mutex_unlock(&counter_mutex);
            queue_signal_shutdown(&queue);
            continue;
        }
        pthread_mutex_unlock(&counter_mutex);

        /* Stop condition: max depth — skip but keep draining queue */
        if (current_item.depth > max_depth) {
            pthread_mutex_lock(&counter_mutex);
            pages_skipped++;
            pthread_mutex_unlock(&counter_mutex);
            continue;
        }

        LOG_INFO("Thread processing: %s (depth: %d)", current_item.url, current_item.depth);

        /* 1. Download the HTML content from the current URL */
        char *html = fetch_page(current_item.url);

        if (html != NULL) {
            int docid;
            doc_message_t msg;

            pthread_mutex_lock(&counter_mutex);
            pages_processed++;
            docid = pages_processed;
            pthread_mutex_unlock(&counter_mutex);

            /* 2. Save the downloaded HTML to the pages directory */
            save_page(html, docid, out_dir);


            /* Fill doc_message_t struct for indexer */
            msg.docid = docid;
            msg.depth = current_item.depth;
            strcpy(msg.url, current_item.url);
            snprintf(msg.filepath, sizeof(msg.filepath), "%s/pages/doc_%d.html", out_dir, docid);

            pthread_mutex_lock(&ipc_mutex);
            if (ipc_send_doc_message(ipc_fd, &msg) < 0) {
                LOG_ERROR("Failed to send doc metadata for %s", current_item.url);
            }
            pthread_mutex_unlock(&ipc_mutex);

            /* 3. Parse the HTML to find new links and add them to the queue.
               parser.c uses a non-blocking enqueue so this never blocks. */
            if (current_item.depth < max_depth) {
                parse_and_enqueue(html, &queue, current_item.depth + 1);
            }

            /* Important: Free the memory allocated by libcurl in fetch_page */
            free(html);
        } else {
            LOG_ERROR("Failed to fetch: %s", current_item.url);
            pthread_mutex_lock(&counter_mutex);
            pages_failed++;
            pthread_mutex_unlock(&counter_mutex);
        }

        /* loop back to top → busy-- → check_done_locked → wait or dequeue */
    }

    return NULL;
}


/* -----------------------------
   CLI Argument Parsing
------------------------------*/

void parse_args(int argc, char *argv[], char *seed_url) {

    /* Show help if -h flag is passed */
    if (argc == 2 && strcmp(argv[1], "-h") == 0) {
        printf("USAGE: crawler --seed <url> --max-depth <D> --max-pages <N>"
               " -t <threads> --out <dir> --ipc <path>\n");
        exit(0);
    }

    if (argc < 3) {
        printf("Usage: ./crawler --seed <url> --max-pages <N> -t <threads>\n");
        exit(1);
    }

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            strcpy(seed_url, argv[++i]);
        }
        else if (strcmp(argv[i], "--max-pages") == 0 && i + 1 < argc) {
            max_pages = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--max-depth") == 0 && i + 1 < argc) {
            max_depth = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) {
            strcpy(ipc_path, argv[++i]);
        }
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            strcpy(out_dir, argv[++i]);
        }
    }

    /* Default socket path if user does not provide --ipc */
    if (strlen(ipc_path) == 0) {
        strcpy(ipc_path, "/tmp/crawl.sock");
    }
}


/* -----------------------------
   Main
------------------------------*/

int main(int argc, char *argv[]) {

    url_item_t seed_item;
    char seed_url[MAX_URL_LENGTH] = "";

    parse_args(argc, argv, seed_url);

    /* Open log file before any work starts */
    log_open(out_dir);

    strcpy(seed_item.url, seed_url);
    seed_item.depth = 0;

    /* Connect to the indexer before spawning threads */
    ipc_fd = ipc_client_connect(ipc_path);
    if (ipc_fd < 0) {
        LOG_ERROR("Failed to connect to indexer at %s", ipc_path);
        log_close();
        return 1;
    }

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);

    visited_init(&visited);
    queue_init(&queue, 1000);

    /*
       Start busy at num_threads so check_done_locked treats all threads
       as active at launch. Each thread immediately decrements busy before
       its first dequeue, bringing it to the correct count.
    */
    busy = num_threads;

    /* Record start time for runtime summary */
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    /* Add seed URL */
    queue_enqueue(&queue, &seed_item);

    /* Create thread pool */
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    /* Wait for all threads */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Stop the clock after all threads have finished */
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec  - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    /* Print and log final crawl summary */
    LOG_INFO("\nCrawl Summary:");
    LOG_INFO("  Pages fetched:    %d", pages_processed);
    LOG_INFO("  Pages skipped:    %d  (duplicates or over max-depth)", pages_skipped);
    LOG_INFO("  Pages failed:     %d  (fetch errors)", pages_failed);
    LOG_INFO("  Max queue depth:  %d", max_queue_depth);
    LOG_INFO("  Max pages limit:  %d", max_pages);
    LOG_INFO("  Max depth limit:  %d", max_depth);
    LOG_INFO("  Threads used:     %d", num_threads);
    LOG_INFO("  Runtime:          %.2f seconds", elapsed);

    free(threads);
    ipc_close(ipc_fd);
    visited_destroy(&visited);
    log_close();

    return 0;
}
