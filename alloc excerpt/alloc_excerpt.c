/* Excerpt from alloc.c
Focus: metadata, free list management, split/coalesce, malloc/free*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// block metadata packs size + free bit
struct obj_metadata {
    size_t info;
};

// alignment + constants
#define ALIGNMENT sizeof(long)
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define METADATA_SIZE sizeof(struct obj_metadata)
#define MIN_ALLOC 24
#define FREE_BIT ((size_t)1 << (sizeof(size_t) * 8 - 1))
#define SIZE_MASK (~FREE_BIT)

static void *heap_start = NULL;
static struct obj_metadata *free_list = NULL;

// helpers
static inline size_t get_size(const struct obj_metadata *b) { return b->info & SIZE_MASK; }
static inline int is_free(const struct obj_metadata *b) { return (b->info & FREE_BIT) != 0; }
static inline void set_size_and_status(struct obj_metadata *b, size_t size, int free) {
    b->info = size | (free ? FREE_BIT : 0);
}

// free list helpers
static inline struct obj_metadata *get_next_freelist(struct obj_metadata *b) {
    return *((struct obj_metadata **)((char *)(b + 1)));
}
static inline void set_next_freelist(struct obj_metadata *b, struct obj_metadata *next) {
    *((struct obj_metadata **)((char *)(b + 1))) = next;
}

static void insert_to_freelist(struct obj_metadata *b) {
    set_size_and_status(b, get_size(b), 1);
    set_next_freelist(b, free_list);
    free_list = b;
}

static void remove_from_freelist(struct obj_metadata *b) {
    if (!free_list) return;
    if (free_list == b) {
        free_list = get_next_freelist(b);
        set_size_and_status(b, get_size(b), 0);
        return;
    }
    struct obj_metadata *curr = free_list;
    while (curr && get_next_freelist(curr) != b) curr = get_next_freelist(curr);
    if (curr) {
        set_next_freelist(curr, get_next_freelist(b));
        set_size_and_status(b, get_size(b), 0);
    }
}

// physical block navigation
static struct obj_metadata *get_next_physical_block(struct obj_metadata *b) {
    return (struct obj_metadata *)((char *)b + METADATA_SIZE + get_size(b));
}

// merge with next free blocks
static void coalesce(struct obj_metadata *b) {
    struct obj_metadata *next = get_next_physical_block(b);
    void *heap_end = sbrk(0);
    if ((void *)next < heap_end && is_free(next)) {
        remove_from_freelist(next);
        set_size_and_status(b, get_size(b) + METADATA_SIZE + get_size(next), 1);
        coalesce(b);
    }
}

// split if block is too large
static void split_block(struct obj_metadata *b, size_t size) {
    size_t total = get_size(b);
    if (total >= size + METADATA_SIZE + MIN_ALLOC) {
        struct obj_metadata *new_block =
            (struct obj_metadata *)((char *)b + METADATA_SIZE + size);
        set_size_and_status(new_block, total - size - METADATA_SIZE, 1);
        set_size_and_status(b, size, 0);
        insert_to_freelist(new_block);
    }
}

// malloc core
void *mymalloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN(size < MIN_ALLOC ? MIN_ALLOC : size);

    // find free block (simple first-fit)
    struct obj_metadata *b = free_list;
    while (b && (get_size(b) < size)) b = get_next_freelist(b);

    // extend heap if needed
    if (!b) {
        b = sbrk(size + METADATA_SIZE);
        if (b == (void *)-1) return NULL;
        if (!heap_start) heap_start = b;
        set_size_and_status(b, size, 0);
        return (void *)(b + 1);
    }

    remove_from_freelist(b);
    split_block(b, size);
    return (void *)(b + 1);
}

// free core
void myfree(void *ptr) {
    if (!ptr) return;
    struct obj_metadata *b = ((struct obj_metadata *)ptr) - 1;
    insert_to_freelist(b);
    coalesce(b);
}
