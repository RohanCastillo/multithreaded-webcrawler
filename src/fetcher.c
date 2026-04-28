#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include "fetcher.h"

/* Callback function used by libcurl to write data into a memory buffer.
   realloc is used to ensure the buffer grows as more data is downloaded.
*/
size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0; // Null-terminate the string

    return realsize;
}

/*
   Uses libcurl to download the HTML content of a given URL.
   Returns a dynamically allocated string containing the HTML.
*/
char* fetch_page(const char *url) {
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk;

    chunk.memory = malloc(1); // Initial small allocation
    chunk.size = 0; 

    curl_handle = curl_easy_init();
    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        
        // Some websites block requests without a User-Agent
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        
        // Set a 5s total timeout — most pages respond well under this.
        // Keeps slow/dead hosts from blocking a thread for too long.
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 5L);

        // Separate connection timeout: fail fast if the host is unreachable
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 3L);

        res = curl_easy_perform(curl_handle);

        if (res != CURLE_OK) {
            fprintf(stderr, "CURL failed for %s: %s\n", url, curl_easy_strerror(res));
            free(chunk.memory);
            chunk.memory = NULL;
        }
        curl_easy_cleanup(curl_handle);
    }
    return chunk.memory;
}

/*
   Saves the provided HTML string into a file inside the 'pages' directory.
   The file is named based on the docid (e.g., doc_1.html).
*/
void save_page(const char *html, int docid, const char *out_dir) {
    if (html == NULL) return;

    /* Build pages subdirectory path under out_dir */
    char pages_dir[1152];
    snprintf(pages_dir, sizeof(pages_dir), "%s/pages", out_dir);

    /* Create out_dir and pages subdir if they don't exist */
    mkdir(out_dir, 0777);
    mkdir(pages_dir, 0777);

    char filename[1280];
    snprintf(filename, sizeof(filename), "%s/doc_%d.html", pages_dir, docid);

    FILE *fp = fopen(filename, "w");
    if (fp) {
        fputs(html, fp);
        fclose(fp);
    } else {
        perror("Error opening file for saving");
    }
}