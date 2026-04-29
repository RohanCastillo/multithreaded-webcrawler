# Multithreaded Webcrawler

A web crawler built in C that uses multithreading and a pipeline architecture to crawl, process, and index web pages.

---

## Features

- Multithreaded crawling using POSIX threads  
- Pipeline (Crawler → Fetcher → Parser → Indexer → Query)  
- Concurrent URL processing with thread-safe queues  
- Persistent storage of crawled pages  
- Duplicate URL detection using a visited set  
- Inter-process communication (IPC) for modularity  

---

## Project Architecture

### Components

#### Crawler
- Manages URL frontier  
- Spawns worker threads  
- Controls crawl depth  

#### Fetcher
- Uses libcurl to download web pages  
- Handles HTTP requests  

#### Parser
- Extracts links from HTML  
- Normalizes URLs  

#### Indexer
- Stores processed data  
- Writes metadata to disk  

#### Query Tool
- Loads the inverted index into memory  
- Performs search across multiple terms  
- Returns matching document IDs and URLs  

---

##  Project Structure
```
multithreaded-webcrawler/
│
├── include/
│
├── src/
│ ├── crawler.c
│ ├── fetcher.c
│ ├── parser.c
│ ├── indexer.c
│ ├── query.c
│ ├── ipc.c
│ ├── url_queue.c
│ └── visited.c
│
├── data/
│ ├── pages/
│ └── index/
│ ├── crawl.log
│
├── Makefile
└── README.md
```
