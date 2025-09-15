# Allocator excerpt

This file is an excerpt from a custom memory allocator (`alloc.c`).  
It shows how dynamic memory management can be built from scratch using metadata, free lists, and the system `sbrk` interface.

## What this shows
* Metadata packing of block size and free bit in a single `size_t`
* Free list management (insert, remove, next block navigation)
* Block splitting for efficient use of space
* Block coalescing to reduce fragmentation
* Core `mymalloc` and `myfree` implementations

## Design notes
* Each allocated block has a small metadata header storing size and free status.
* The free list is a simple linked structure embedded in the payload area of free blocks.
* Blocks are aligned to word boundaries (`ALIGNMENT`) and have a minimum size (`MIN_ALLOC`).
* On free, adjacent free blocks are coalesced recursively.
* Heap extension is done via `sbrk`, keeping the allocator self-contained.

## Why this excerpt
The full allocator also implements `calloc` and `realloc`, plus logic to return memory to the OS when possible.  
For brevity, this excerpt focuses on the core malloc/free path to highlight problem-solving and system-level reasoning.

I am happy to walk through the complete allocator file on a call if that is useful.
