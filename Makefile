CC = gcc

# M1 Macs store ARM libraries in /opt/homebrew
# On Linux, remove the -I and -L flags below (libcurl is found automatically)
CFLAGS  = -Wall -pthread -I/opt/homebrew/opt/curl/include
LDFLAGS = -L/opt/homebrew/opt/curl/lib -lcurl

# --- Source files for each binary ---

CRAWLER_SRC  = OSCrawler.c url_queue.c visited.c fetcher.c parser.c ipc.c
INDEXER_SRC  = indexer.c ipc.c
QUERY_SRC    = query.c

# --- Targets ---

all: crawler indexer query

crawler: $(CRAWLER_SRC)
	$(CC) $(CFLAGS) $(CRAWLER_SRC) -o crawler $(LDFLAGS)

indexer: $(INDEXER_SRC)
	$(CC) $(CFLAGS) $(INDEXER_SRC) -o indexer $(LDFLAGS)

query: $(QUERY_SRC)
	$(CC) $(CFLAGS) $(QUERY_SRC) -o query

# --- Run: start indexer in background, then run crawler ---
# Edit SEED, MAX_PAGES, DEPTH, THREADS, and IPC_PATH as needed.

SEED      = https://en.wikipedia.org/wiki/Linux
MAX_PAGES = 50
DEPTH     = 2
THREADS   = 4
IPC_PATH  = /tmp/crawl.sock
OUT_DIR   = data

run: all
	mkdir -p $(OUT_DIR)/index pages
	./indexer --ipc $(IPC_PATH) --out $(OUT_DIR)/index &
	sleep 1
	./crawler --seed $(SEED) --max-depth $(DEPTH) --max-pages $(MAX_PAGES) \
	          -t $(THREADS) --out $(OUT_DIR) --ipc $(IPC_PATH)

# --- Clean up all build artifacts and crawled data ---

clean:
	rm -rf crawler indexer query pages data