#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "ipc.h"
#include "doc_message.h"

/*
   Caps sized for a crawl of up to ~15,000 pages.
   term_entry_t now uses a heap-allocated docids pointer that grows
   on demand, so MAX_TERMS * sizeof(term_entry_t) is small (~7MB for
   50k terms) and the old "relocation truncated" / OOM errors are gone.
*/
#define MAX_DOCS      20000
#define MAX_TERMS     50000
#define MAX_WORD_LEN  128
#define POSTINGS_INIT 8      /* initial docids capacity per term — doubles when full */



/* One entry in the inverted index.
   docids is heap-allocated and doubled when full. */
typedef struct {
    char  term[MAX_WORD_LEN];
    int  *docids;       /* heap-allocated postings list */
    int   doc_count;    /* number of docids stored */
    int   capacity;     /* allocated slots in docids */
} term_entry_t;


/* Global storage — heap-allocated in main() */
doc_message_t *docs        = NULL;
int            doc_count   = 0;

term_entry_t  *index_table = NULL;
int            term_count  = 0;



/* Make sure output folder exists */
void ensure_dir(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == -1) {
        mkdir(dir, 0777);
    }
}

/* Add document metadata to docs array */
void add_doc_record(doc_message_t *msg) {
    if (doc_count >= MAX_DOCS) return;
    docs[doc_count++] = *msg;
}

/* Find a term in the index table; returns index or -1 */
int find_term(const char *word) {
    for (int i = 0; i < term_count; i++) {
        if (strcmp(index_table[i].term, word) == 0)
            return i;
    }
    return -1;
}

/* Add a word -> docid mapping, growing the postings list as needed */
void add_term_to_index(const char *word, int docid) {
    int idx = find_term(word);

    if (idx == -1) {
        if (term_count >= MAX_TERMS) return;

        idx = term_count;
        strncpy(index_table[idx].term, word, MAX_WORD_LEN - 1);
        index_table[idx].term[MAX_WORD_LEN - 1] = '\0';
        index_table[idx].doc_count = 0;
        index_table[idx].capacity  = POSTINGS_INIT;
        index_table[idx].docids    = malloc(POSTINGS_INIT * sizeof(int));
        if (!index_table[idx].docids) return;
        term_count++;
    }

    /* Avoid duplicate docids for the same term */
    for (int i = 0; i < index_table[idx].doc_count; i++) {
        if (index_table[idx].docids[i] == docid) return;
    }

    /* Grow postings list if full */
    if (index_table[idx].doc_count == index_table[idx].capacity) {
        int new_cap = index_table[idx].capacity * 2;
        int *tmp = realloc(index_table[idx].docids, new_cap * sizeof(int));
        if (!tmp) return;
        index_table[idx].docids   = tmp;
        index_table[idx].capacity = new_cap;
    }

    index_table[idx].docids[index_table[idx].doc_count++] = docid;
}

/* Read entire saved file into memory */
char *read_file_contents(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) { perror("fopen"); return NULL; }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *buffer = malloc(size + 1);
    if (!buffer) { fclose(fp); return NULL; }

    fread(buffer, 1, size, fp);
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

/* Tokenize text and add words to inverted index */
void tokenize_and_index(const char *text, int docid) {
    char word[MAX_WORD_LEN];
    int j = 0;

    for (int i = 0; text[i] != '\0'; i++) {
        if (isalnum((unsigned char)text[i])) {
            if (j < MAX_WORD_LEN - 1)
                word[j++] = tolower((unsigned char)text[i]);
        } else {
            if (j > 0) {
                word[j] = '\0';
                add_term_to_index(word, docid);
                j = 0;
            }
        }
    }
    if (j > 0) {
        word[j] = '\0';
        add_term_to_index(word, docid);
    }
}

/* Write doc map to docs.tsv */
void write_docs_file(const char *out_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/docs.tsv", out_dir);

    FILE *fp = fopen(path, "w");
    if (!fp) { perror("fopen docs.tsv"); return; }

    for (int i = 0; i < doc_count; i++) {
        fprintf(fp, "%d\t%s\t%s\t%d\n",
                docs[i].docid, docs[i].url,
                docs[i].filepath, docs[i].depth);
    }
    fclose(fp);
}

/* Write inverted index to postings.tsv */
void write_postings_file(const char *out_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/postings.tsv", out_dir);

    FILE *fp = fopen(path, "w");
    if (!fp) { perror("fopen postings.tsv"); return; }

    for (int i = 0; i < term_count; i++) {
        fprintf(fp, "%s\t", index_table[i].term);
        for (int j = 0; j < index_table[i].doc_count; j++) {
            fprintf(fp, "%d", index_table[i].docids[j]);
            if (j < index_table[i].doc_count - 1) fprintf(fp, ",");
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/*
   Write dictionary metadata to dict.tsv.
   Format: term <TAB> byte_offset <TAB> df

   byte_offset is the actual byte position of this term's line in
   postings.tsv, so a reader can fseek() directly to any term's
   postings without scanning the entire file.  This replaces the
   previous implementation that stored a line index (which defeats
   the purpose of a separate dictionary file).
*/
void write_dict_file(const char *out_dir) {
    char postings_path[1024];
    char dict_path[1024];
    snprintf(postings_path, sizeof(postings_path), "%s/postings.tsv", out_dir);
    snprintf(dict_path,     sizeof(dict_path),     "%s/dict.tsv",     out_dir);

    /* Open postings.tsv for reading so we can measure real byte offsets */
    FILE *postings_fp = fopen(postings_path, "r");
    if (!postings_fp) { perror("fopen postings.tsv for dict"); return; }

    FILE *dict_fp = fopen(dict_path, "w");
    if (!dict_fp) { perror("fopen dict.tsv"); fclose(postings_fp); return; }

    char line[1 << 20];  /* 1 MB line buffer — handles huge postings lines */
    int  term_idx = 0;

    while (fgets(line, sizeof(line), postings_fp) != NULL && term_idx < term_count) {
        /* Record byte offset BEFORE reading this line (ftell after fgets
           points past the newline, so we captured it before the call). */
        long byte_offset = ftell(postings_fp) - (long)strlen(line);

        /* df = doc_count for the term at this position in the table */
        fprintf(dict_fp, "%s\t%ld\t%d\n",
                index_table[term_idx].term,
                byte_offset,
                index_table[term_idx].doc_count);
        term_idx++;
    }

    fclose(postings_fp);
    fclose(dict_fp);
}

/* Free all heap-allocated postings lists then the table itself */
void free_index_table(void) {
    for (int i = 0; i < term_count; i++) {
        free(index_table[i].docids);
    }
    free(index_table);
}

/* Parse command-line arguments */
void parse_args(int argc, char *argv[], char *ipc_path, char *out_dir) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc)
            strcpy(ipc_path, argv[++i]);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            strcpy(out_dir, argv[++i]);
    }
    if (strlen(ipc_path) == 0) strcpy(ipc_path, "/tmp/crawl.sock");
    if (strlen(out_dir)  == 0) strcpy(out_dir,  "data/index");
}



int main(int argc, char *argv[]) {
    char ipc_path[108]  = "";
    char out_dir[1024]  = "";

    parse_args(argc, argv, ipc_path, out_dir);

    /*
       term_entry_t is now ~140 bytes (char[128] + pointer + 2 ints),
       so 50k entries = ~7MB. No more OOM or linker relocation errors.
    */
    docs        = calloc(MAX_DOCS,  sizeof(doc_message_t));
    index_table = calloc(MAX_TERMS, sizeof(term_entry_t));
    if (!docs || !index_table) {
        fprintf(stderr, "Failed to allocate index memory\n");
        return 1;
    }

    ensure_dir("data");
    ensure_dir(out_dir);

    int server_fd = ipc_server_start(ipc_path);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to start IPC server at %s\n", ipc_path);
        return 1;
    }

    printf("Indexer listening on %s\n", ipc_path);

    int client_fd = ipc_server_accept(server_fd);
    if (client_fd < 0) {
        fprintf(stderr, "Failed to accept crawler connection\n");
        ipc_close(server_fd);
        return 1;
    }

    printf("Crawler connected.\n");

    doc_message_t msg;
    int status;

    while ((status = ipc_receive_doc_message(client_fd, &msg)) > 0) {
        printf("Received: docid=%d depth=%d url=%s filepath=%s\n",
               msg.docid, msg.depth, msg.url, msg.filepath);

        add_doc_record(&msg);

        char *contents = read_file_contents(msg.filepath);
        if (!contents) {
            fprintf(stderr, "Could not read file %s\n", msg.filepath);
            continue;
        }

        tokenize_and_index(contents, msg.docid);
        free(contents);
    }

    if (status < 0)
        fprintf(stderr, "Error receiving document metadata\n");
    else
        printf("Crawler disconnected. Writing index files...\n");

    write_docs_file(out_dir);
    write_postings_file(out_dir);
    write_dict_file(out_dir);   /* dict is written after postings so offsets are real */

    printf("Indexing complete.\n");
    printf("Documents indexed: %d\n", doc_count);
    printf("Unique terms: %d\n", term_count);

    ipc_close(client_fd);
    ipc_close(server_fd);

    free(docs);
    free_index_table();

    return 0;
}
