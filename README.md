# Key-Value Store excerpt

This file is an excerpt from a larger key-value store (`kvstore.c`) implemented in C.  
It demonstrates concurrency control, request handling, and memory safety for a custom in-memory database.

## What this shows
* Hashtable initialization with fixed capacity (256 buckets)
* Per-bucket locking with mutexes to allow concurrent access
* Worker thread pool with a bounded job queue
* Safe `GET` path with read locks
* Safe `SET` path with write locks and value replacement
* `DEL` operation with proper unlinking and cleanup
* Request counters and throughput statistics (`STAT`)

## Design notes
* Buckets are protected by `pthread_mutex_t` locks to coordinate insertion and deletion.
* Each item has its own `pthread_rwlock_t` to allow multiple readers or a single writer.
* A thread pool dispatches client requests from a bounded queue, ensuring scalability.
* All memory allocations are checked and cleaned up to prevent leaks.
* Consistent error handling: missing keys, store errors, and key-in-use are mapped to responses.

## Why this excerpt
The full implementation is ~420 lines and includes additional details such as reset logic and connection handling.  
This excerpt highlights the concurrency model and core data store operations, which are the most relevant for evaluating systems programming skills.

I am happy to walk through the complete implementation on a call if that is useful.
