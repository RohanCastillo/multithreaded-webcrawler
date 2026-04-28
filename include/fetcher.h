#ifndef FETCHER_H
#define FETCHER_H

#include <stddef.h>

struct MemoryStruct {
    char *memory;
    size_t size;
};

char* fetch_page(const char *url);
void save_page(const char *html, int docid, const char *out_dir);

#endif