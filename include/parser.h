#ifndef PARSER_H
#define PARSER_H

#include "url_queue.h"

// Scans HTML for hrefs and adds them to the queue
void parse_and_enqueue(const char *html, url_queue_t *q, int new_depth);

#endif