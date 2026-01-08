#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Memory Allocator - Core Header
 *
 * A production-style, thread-safe memory allocator implementing:
 * - Hybrid memory sourcing (sbrk < 128KB, mmap >= 128KB)
 * - 16-byte alignment for all allocations
 * - First-fit allocation with immediate coalescing
 * - Comprehensive error detection and heap integrity checking
 * - Thread safety via global mutex with thread-local cache optimization
 */

/* Configuration Constants */
#define ALIGNMENT 16
#define MAGIC_NUMBER 0xDEADBEEF
#define MMAP_THRESHOLD ((size_t)(128 * 1024))         /* 128KB threshold for mmap vs sbrk */
#define MIN_ALLOC_SIZE (sizeof(void *) * 2) /* Minimum allocation size */
#define MAX_THREAD_CACHE_SIZE (64 * 1024)   /* Thread-local cache limit */

/* Alignment Macros */
#define ALIGN_SIZE(size) (((size) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))
#define IS_ALIGNED(ptr) (((uintptr_t)(ptr) % ALIGNMENT) == 0)

/* Block Header Structure
 *
 * Layout for allocated blocks (16 bytes):
 * +--------+--------+--------+--------+
 * |     size (8 bytes)      | is_free|
 * +--------+--------+--------+--------+
 * |  magic (4 bytes) |     unused     |
 * +--------+--------+--------+--------+
 *
 * Layout for free blocks (32 bytes):
 * +--------+--------+--------+--------+
 * |     size (8 bytes)      | is_free|
 * +--------+--------+--------+--------+
 * |  magic (4 bytes) |     unused     |
 * +--------+--------+--------+--------+
 * |      prev_free (8 bytes)          |
 * +--------+--------+--------+--------+
 * |      next_free (8 bytes)          |
 * +--------+--------+--------+--------+
 */
typedef struct block {
    size_t size;      /* Size of user data area (excluding header) */
    uint32_t is_free; /* 0 = allocated, 1 = free */
    uint32_t magic;   /* Magic number for corruption detection */

    /* Free list pointers - only valid when is_free == 1 */
    struct block *prev_free;
    struct block *next_free;
} block_t;

/* Heap Management Structure */
typedef struct heap_info {
    void *heap_start;    /* Start of heap region */
    void *heap_end;      /* End of heap region */
    void *program_break; /* Current program break (sbrk) */

    block_t *free_head;      /* Head of free block list */
    size_t total_allocated;  /* Total bytes allocated */
    size_t total_free;       /* Total bytes free */
    size_t allocation_count; /* Number of active allocations */

    pthread_mutex_t heap_mutex; /* Global heap protection */
} heap_info_t;

/* Thread-Local Cache Entry */
typedef struct cache_entry {
    void *ptr;
    size_t size;
    struct cache_entry *next;
} cache_entry_t;

/* Thread-Local Cache Structure */
typedef struct thread_cache {
    cache_entry_t *free_lists[8]; /* Size classes: 16, 32, 64, 128, 256, 512, 1024 */
    size_t cache_size;            /* Total cached memory */
    bool enabled;                 /* Cache enabled for this thread */
} thread_cache_t;

/* Error Codes */
typedef enum {
    ALLOC_SUCCESS = 0,
    ALLOC_ERROR_OUT_OF_MEMORY,
    ALLOC_ERROR_INVALID_SIZE,
    ALLOC_ERROR_DOUBLE_FREE,
    ALLOC_ERROR_CORRUPTION,
    ALLOC_ERROR_MISALIGNED,
    ALLOC_ERROR_INVALID_POINTER
} alloc_error_t;

/* Block Validation Status */
typedef enum {
    BLOCK_VALID,
    BLOCK_CORRUPT_MAGIC,
    BLOCK_INVALID_SIZE,
    BLOCK_MISALIGNED,
    BLOCK_INVALID_FREE_STATE,
    BLOCK_OUT_OF_BOUNDS
} block_status_t;

/* Function Declarations */

/* Standard Allocator Interface */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

/* Extended Interface */
void *aligned_alloc(size_t alignment, size_t size);
size_t malloc_usable_size(void *ptr);

/* Allocator Management */
int allocator_init(void);
void allocator_cleanup(void);
void allocator_stats(void);

/* Memory Sourcing */
void *acquire_memory_sbrk(size_t size);
void *acquire_memory_mmap(size_t size);
int release_memory_mmap(void *ptr, size_t size);

/* Block Management */
block_t *create_block(void *memory, size_t size);
void initialize_allocated_block(block_t *block, size_t size);
void initialize_free_block(block_t *block, size_t size);
block_status_t verify_block_integrity(block_t *block);

/* Address Conversion */
static inline block_t *get_block_from_ptr(void *ptr)
{
    return ptr ? (block_t *)((char *)ptr - sizeof(block_t)) : NULL;
}

static inline void *get_ptr_from_block(block_t *block)
{
    return block ? (void *)((char *)block + sizeof(block_t)) : NULL;
}

/* Block Navigation */
block_t *get_next_block(block_t *block);
block_t *get_prev_block(block_t *block);
bool blocks_are_adjacent(const block_t *first, const block_t *second);

/* Free List Management */
void add_to_free_list(block_t *block);
void remove_from_free_list(block_t *block);
block_t *find_free_block(size_t size);

/* Block Operations */
block_t *split_block(block_t *block, size_t size);
block_t *coalesce_blocks(block_t *block);
bool can_split_block(const block_t *block, size_t needed_size);

/* Thread-Local Cache */
extern __thread thread_cache_t *thread_cache;
int init_thread_cache(void);
void cleanup_thread_cache(void);
void *cache_alloc(size_t size);
void cache_free(void *ptr, size_t size);

/* Debugging and Validation */
bool is_valid_heap_pointer(const void *ptr);
void heap_consistency_check(void);
void print_heap_layout(void);
void print_free_list(void);

/* Error Handling */
extern alloc_error_t last_error;
const char *get_error_string(alloc_error_t error);
void set_error_handler(void (*handler)(alloc_error_t, const char *));

/* Performance Optimization Hints */
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FORCE_INLINE __attribute__((always_inline)) inline

/* Utility Macros */
#define HEADER_SIZE sizeof(block_t)
#define MIN_BLOCK_SIZE (HEADER_SIZE + MIN_ALLOC_SIZE)

/* Size Class Helpers for Thread Cache */
// cppcheck-suppress unusedFunction
static inline int get_size_class(size_t size)
{
    if (size <= 16)
        return 0;
    if (size <= 32)
        return 1;
    if (size <= 64)
        return 2;
    if (size <= 128)
        return 3;
    if (size <= 256)
        return 4;
    if (size <= 512)
        return 5;
    if (size <= 1024)
        return 6;
    return 7; /* Too large for cache */
}

// cppcheck-suppress unusedFunction
static inline size_t get_class_size(int class)
{
    static const size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024};
    return (class >= 0 && class < 7) ? sizes[class] : 0;
}

/* Global State */
extern heap_info_t heap;
extern bool allocator_initialized;

#endif /* ALLOCATOR_H */
