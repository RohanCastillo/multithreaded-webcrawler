/*
   query.c — Command-line search tool for the inverted index.

   Usage:
       ./query --index <dir> <term1> [term2 ...]

   Loads docs.tsv (document map) and postings.tsv (inverted index) from the
   index directory, then performs an AND search across all query terms.

   Output:
       Matching docids and their URLs, or a "no results" message.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -------------------------
   Constants
--------------------------*/
#define MAX_DOCS      20000
#define MAX_TERMS     50000
#define MAX_WORD_LEN  128
#define MAX_URL_LEN   1024
#define MAX_PATH_LEN  1024
#define MAX_POSTINGS  1000
#define MAX_LINE      4096


/* -------------------------
   Data Structures
--------------------------*/

/* One entry in the document map (from docs.tsv) */
typedef struct {
    int  docid;
    char url[MAX_URL_LEN];
    char filepath[MAX_PATH_LEN];
    int  depth;
} doc_record_t;

/* One entry in the in-memory inverted index (from postings.tsv) */
typedef struct {
    char term[MAX_WORD_LEN];
    int  docids[MAX_POSTINGS];
    int  doc_count;
} posting_entry_t;


/* -------------------------
   Global Storage
--------------------------*/
doc_record_t    doc_map[MAX_DOCS];
int             doc_map_count = 0;

posting_entry_t index_table[MAX_TERMS];
int             index_term_count = 0;


/* -------------------------
   Load docs.tsv
   Format: docid<TAB>url<TAB>filepath<TAB>depth
--------------------------*/
int load_docs(const char *index_dir) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/docs.tsv", index_dir);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", path);
        return -1;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (doc_map_count >= MAX_DOCS) break;

        doc_record_t *rec = &doc_map[doc_map_count];

        /* Parse tab-separated fields */
        char *tok = strtok(line, "\t");
        if (tok == NULL) continue;
        rec->docid = atoi(tok);

        tok = strtok(NULL, "\t");
        if (tok == NULL) continue;
        strncpy(rec->url, tok, MAX_URL_LEN - 1);
        rec->url[MAX_URL_LEN - 1] = '\0';

        tok = strtok(NULL, "\t");
        if (tok == NULL) continue;
        strncpy(rec->filepath, tok, MAX_PATH_LEN - 1);
        rec->filepath[MAX_PATH_LEN - 1] = '\0';

        tok = strtok(NULL, "\t\n");
        rec->depth = (tok != NULL) ? atoi(tok) : 0;

        doc_map_count++;
    }

    fclose(fp);
    return 0;
}


/* -------------------------
   Load postings.tsv
   Format: term<TAB>docid1,docid2,...
--------------------------*/
int load_postings(const char *index_dir) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/postings.tsv", index_dir);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", path);
        return -1;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (index_term_count >= MAX_TERMS) break;

        posting_entry_t *entry = &index_table[index_term_count];
        entry->doc_count = 0;

        /* First token is the term */
        char *tok = strtok(line, "\t");
        if (tok == NULL) continue;
        strncpy(entry->term, tok, MAX_WORD_LEN - 1);
        entry->term[MAX_WORD_LEN - 1] = '\0';

        /* Second token is comma-separated docids */
        tok = strtok(NULL, "\t\n");
        if (tok == NULL) {
            index_term_count++;
            continue;
        }

        /* Split on commas to get individual docids */
        char *id_tok = strtok(tok, ",");
        while (id_tok != NULL && entry->doc_count < MAX_POSTINGS) {
            entry->docids[entry->doc_count++] = atoi(id_tok);
            id_tok = strtok(NULL, ",");
        }

        index_term_count++;
    }

    fclose(fp);
    return 0;
}


/* -------------------------
   Look up a term in the loaded index.
   Returns pointer to posting_entry_t or NULL if not found.
--------------------------*/
posting_entry_t *lookup_term(const char *word) {
    for (int i = 0; i < index_term_count; i++) {
        if (strcmp(index_table[i].term, word) == 0) {
            return &index_table[i];
        }
    }
    return NULL;
}


/* -------------------------
   Look up a docid in the document map.
   Returns pointer to doc_record_t or NULL.
--------------------------*/
doc_record_t *lookup_doc(int docid) {
    for (int i = 0; i < doc_map_count; i++) {
        if (doc_map[i].docid == docid) {
            return &doc_map[i];
        }
    }
    return NULL;
}


/* -------------------------
   Normalize a query term to lowercase (mirrors indexer tokenization).
--------------------------*/
void normalize(char *word) {
    for (int i = 0; word[i] != '\0'; i++) {
        word[i] = (char)tolower((unsigned char)word[i]);
    }
}


/* -------------------------
   AND search across all query terms.

   Strategy:
     1. Find the posting list for the first term.
     2. For each docid in that list, check whether it appears
        in the posting lists of ALL remaining terms.
     3. Print matches.
--------------------------*/
void run_query(char **terms, int num_terms) {

    if (num_terms == 0) {
        printf("No query terms provided.\n");
        return;
    }

    /* Normalize all terms to lowercase */
    for (int i = 0; i < num_terms; i++) {
        normalize(terms[i]);
    }

    /* Look up each term — bail early if any term is missing */
    posting_entry_t *postings[MAX_TERMS];
    for (int i = 0; i < num_terms; i++) {
        postings[i] = lookup_term(terms[i]);
        if (postings[i] == NULL) {
            printf("No documents matched all query terms.\n");
            return;
        }
    }

    /* Collect AND results using first term's list as the candidate set */
    int results[MAX_POSTINGS];
    int result_count = 0;

    posting_entry_t *first = postings[0];

    for (int i = 0; i < first->doc_count; i++) {
        int candidate = first->docids[i];
        int match = 1;  /* Assume match until proven otherwise */

        /* Check this docid against every other term's posting list */
        for (int t = 1; t < num_terms && match; t++) {
            int found = 0;
            for (int j = 0; j < postings[t]->doc_count; j++) {
                if (postings[t]->docids[j] == candidate) {
                    found = 1;
                    break;
                }
            }
            if (!found) match = 0;
        }

        if (match && result_count < MAX_POSTINGS) {
            results[result_count++] = candidate;
        }
    }

    /* Print results */
    if (result_count == 0) {
        printf("No documents matched all query terms.\n");
        return;
    }

    printf("Found %d matching document%s (AND across terms):\n",
           result_count, result_count == 1 ? "" : "s");

    for (int i = 0; i < result_count; i++) {
        doc_record_t *doc = lookup_doc(results[i]);
        if (doc != NULL) {
            printf("  %d\t%s\n", doc->docid, doc->url);
        } else {
            /* docid exists in postings but not in doc map — print id only */
            printf("  %d\t(URL not found in doc map)\n", results[i]);
        }
    }
}


/* -------------------------
   Print usage/help message
--------------------------*/
void print_usage(const char *prog) {
    printf("USAGE: %s --index <dir> <term1> [term2 ...]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --index <dir>   Directory containing docs.tsv and postings.tsv\n");
    printf("  <term1> ...     One or more search terms (AND semantics)\n");
    printf("  -h              Show this help message\n");
}


/* -------------------------
   Main
--------------------------*/
int main(int argc, char *argv[]) {

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Handle -h flag */
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    char index_dir[MAX_PATH_LEN] = "";
    int  term_start = -1;   /* argv index where query terms begin */

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--index") == 0 && i + 1 < argc) {
            strncpy(index_dir, argv[++i], MAX_PATH_LEN - 1);
            index_dir[MAX_PATH_LEN - 1] = '\0';
        } else if (argv[i][0] != '-') {
            /* First non-flag argument after --index is the start of query terms */
            if (term_start == -1) {
                term_start = i;
            }
        }
    }

    if (strlen(index_dir) == 0) {
        fprintf(stderr, "Error: --index <dir> is required.\n");
        print_usage(argv[0]);
        return 1;
    }

    if (term_start == -1) {
        fprintf(stderr, "Error: at least one query term is required.\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Load index files from disk */
    if (load_docs(index_dir) < 0) {
        fprintf(stderr, "Failed to load document map from %s\n", index_dir);
        return 1;
    }

    if (load_postings(index_dir) < 0) {
        fprintf(stderr, "Failed to load postings from %s\n", index_dir);
        return 1;
    }

    printf("Index loaded: %d documents, %d terms.\n", doc_map_count, index_term_count);

    /* Run the AND query over remaining argv terms */
    run_query(&argv[term_start], argc - term_start);

    return 0;
}
