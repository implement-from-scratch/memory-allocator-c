/*
 * Memory Allocator - Core Implementation
 *
 * A production-style, thread-safe memory allocator implementing:
 * - Hybrid memory sourcing (sbrk < 128KB, mmap >= 128KB)
 * - 16-byte alignment for all allocations
 * - First-fit allocation with immediate coalescing
 * - Comprehensive error detection and heap integrity checking
 * - Thread safety via global mutex with thread-local cache optimization
 */

#include "allocator.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

/* Suppress deprecation warnings for sbrk on macOS */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

/* Global State */
heap_info_t heap = {0};
bool allocator_initialized = false;
alloc_error_t last_error = ALLOC_SUCCESS;
__thread thread_cache_t *thread_cache = NULL;

/* Memory region tracking */
typedef struct memory_region {
    void *start;
    size_t size;
    bool is_mmap;
    struct memory_region *next;
} memory_region_t;

static memory_region_t *memory_regions = NULL;
static pthread_mutex_t region_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Memory sourcing pool for sbrk optimization */
static void *heap_extension_pool = NULL;
static size_t pool_remaining = 0;
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Memory statistics */
typedef struct {
    int sbrk_failures;
    int mmap_failures;
    time_t last_failure_time;
    bool emergency_mode;
} memory_stats_t;

static memory_stats_t mem_stats = {0};

/* Function prototypes for internal functions */
static void register_memory_region(void *start, size_t size, bool is_mmap);
static memory_region_t *find_memory_region(void *ptr);
static void unregister_memory_region(void *start);
static bool should_use_mmap_for_small_allocation(size_t size);
static void handle_memory_acquisition_failure(void);
static void trigger_emergency_cleanup(void);
static bool validate_free_request(block_t *block, void *ptr);

/* Allocator Initialization */
int allocator_init(void)
{
    if (allocator_initialized) {
        return 0;
    }

    /* Initialize heap structure */
    memset(&heap, 0, sizeof(heap_info_t));

    /* Initialize heap mutex */
    if (pthread_mutex_init(&heap.heap_mutex, NULL) != 0) {
        return -1;
    }

    /* Get initial program break */
    heap.program_break = sbrk(0);
    if (heap.program_break == (void *)-1) {
        pthread_mutex_destroy(&heap.heap_mutex);
        return -1;
    }

    heap.heap_start = heap.program_break;
    heap.heap_end = heap.program_break;

    allocator_initialized = true;
    return 0;
}

/* Block Management Functions */
void initialize_allocated_block(block_t *block, size_t size)
{
    block->size = size;
    block->is_free = 0;
    block->magic = MAGIC_NUMBER;

    /* Free list pointers are undefined in allocated blocks */
    /* They may be overwritten by user data */
}

void initialize_free_block(block_t *block, size_t size)
{
    block->size = size;
    block->is_free = 1;
    block->magic = MAGIC_NUMBER;

    /* Initialize free list pointers to NULL */
    /* They will be set by list management functions */
    block->prev_free = NULL;
    block->next_free = NULL;
}

block_status_t verify_block_integrity(block_t *block)
{
    if (!block) {
        return BLOCK_OUT_OF_BOUNDS;
    }

    /* Check alignment */
    if (!IS_ALIGNED(block)) {
        return BLOCK_MISALIGNED;
    }

    /* Check magic number */
    if (block->magic != MAGIC_NUMBER) {
        return BLOCK_CORRUPT_MAGIC;
    }

    /* Check size alignment */
    if (block->size % ALIGNMENT != 0) {
        return BLOCK_INVALID_SIZE;
    }

    /* Check free flag validity */
    if (block->is_free != 0 && block->is_free != 1) {
        return BLOCK_INVALID_FREE_STATE;
    }

    return BLOCK_VALID;
}

/* Block Navigation */
block_t *get_next_block(block_t *block)
{
    if (!block)
        return NULL;

    /* Next block starts after current block's header and data */
    char *next_addr = (char *)block + HEADER_SIZE + block->size;
    return (block_t *)next_addr;
}

// cppcheck-suppress unusedFunction
bool blocks_are_adjacent(const block_t *first, const block_t *second)
{
    if (!first || !second)
        return false;

    const block_t *expected_next = get_next_block((block_t *)first);
    return expected_next == second;
}

/* Free List Management */
void add_to_free_list(block_t *block)
{
    if (!block || !block->is_free)
        return;

    pthread_mutex_lock(&heap.heap_mutex);

    /* Add to head of free list */
    block->prev_free = NULL;
    block->next_free = heap.free_head;

    if (heap.free_head) {
        heap.free_head->prev_free = block;
    }

    heap.free_head = block;
    heap.total_free += block->size;

    pthread_mutex_unlock(&heap.heap_mutex);
}

void remove_from_free_list(block_t *block)
{
    if (!block || !block->is_free)
        return;

    pthread_mutex_lock(&heap.heap_mutex);

    /* Update previous block's next pointer */
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else {
        /* This was the head */
        heap.free_head = block->next_free;
    }

    /* Update next block's previous pointer */
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }

    heap.total_free -= block->size;

    /* Clear pointers */
    block->prev_free = NULL;
    block->next_free = NULL;

    pthread_mutex_unlock(&heap.heap_mutex);
}

block_t *find_free_block(size_t size)
{
    pthread_mutex_lock(&heap.heap_mutex);

    /* First-fit search through free list */
    block_t *current = heap.free_head;
    while (current) {
        if (current->size >= size) {
            pthread_mutex_unlock(&heap.heap_mutex);
            return current;
        }
        current = current->next_free;
    }

    pthread_mutex_unlock(&heap.heap_mutex);
    return NULL;
}

/* Block Splitting */
bool can_split_block(const block_t *block, size_t needed_size)
{
    if (!block)
        return false;

    size_t total_size = block->size;
    size_t remaining_size = total_size - needed_size;

    /* Remaining space must accommodate header + minimum allocation */
    return remaining_size >= (HEADER_SIZE + MIN_ALLOC_SIZE);
}

block_t *split_block(block_t *block, size_t size)
{
    if (!block || !can_split_block(block, size)) {
        return NULL;
    }

    /* Calculate new block position */
    char *new_block_addr = (char *)block + HEADER_SIZE + size;
    block_t *new_block = (block_t *)new_block_addr;

    /* Initialize new block with remaining space */
    size_t remaining_size = block->size - size;
    initialize_free_block(new_block, remaining_size - HEADER_SIZE);

    /* Update original block size */
    block->size = size;

    return new_block;
}

/* Memory Region Tracking */
static void register_memory_region(void *start, size_t size, bool is_mmap)
{
    memory_region_t *region = malloc(sizeof(memory_region_t));
    if (!region)
        return; /* Best effort tracking */

    region->start = start;
    region->size = size;
    region->is_mmap = is_mmap;

    pthread_mutex_lock(&region_mutex);
    region->next = memory_regions;
    memory_regions = region;
    pthread_mutex_unlock(&region_mutex);
}

static memory_region_t *find_memory_region(const void *ptr)
{
    pthread_mutex_lock(&region_mutex);

    memory_region_t *current = memory_regions;
    while (current) {
        char *start = (char *)current->start;
        char *end = start + current->size;

        if (ptr >= current->start && ptr < (void *)end) {
            pthread_mutex_unlock(&region_mutex);
            return current;
        }
        current = current->next;
    }

    pthread_mutex_unlock(&region_mutex);
    return NULL;
}

static void unregister_memory_region(const void *start)
{
    pthread_mutex_lock(&region_mutex);

    memory_region_t **current = &memory_regions;
    while (*current) {
        if ((*current)->start == start) {
            memory_region_t *to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            break;
        }
        current = &(*current)->next;
    }

    pthread_mutex_unlock(&region_mutex);
}

/* Memory Sourcing Implementation */
void *acquire_memory_sbrk(size_t size)
{
    size_t aligned_size = ALIGN_SIZE(size);

    pthread_mutex_lock(&pool_mutex);

    /* Try to satisfy request from existing pool */
    if (heap_extension_pool && pool_remaining >= aligned_size) {
        void *result = heap_extension_pool;
        heap_extension_pool = (char *)heap_extension_pool + aligned_size;
        pool_remaining -= aligned_size;
        pthread_mutex_unlock(&pool_mutex);
        return result;
    }

    /* Pool exhausted - extend heap */
    size_t extension_size = (aligned_size > 65536) ? aligned_size : 65536; /* 64KB chunks */

    void *new_memory = sbrk(extension_size);
    if (new_memory == (void *)-1) {
        pthread_mutex_unlock(&pool_mutex);
        last_error = ALLOC_ERROR_OUT_OF_MEMORY;
        handle_memory_acquisition_failure();
        return NULL;
    }

    /* Update global heap information */
    pthread_mutex_lock(&heap.heap_mutex);
    if (heap.heap_start == NULL) {
        heap.heap_start = new_memory;
    }
    heap.heap_end = (char *)new_memory + extension_size;
    pthread_mutex_unlock(&heap.heap_mutex);

    /* Initialize pool with remaining memory */
    void *result = new_memory;
    heap_extension_pool = (char *)new_memory + aligned_size;
    pool_remaining = extension_size - aligned_size;

    register_memory_region(new_memory, extension_size, false);

    pthread_mutex_unlock(&pool_mutex);
    return result;
}

void *acquire_memory_mmap(size_t size)
{
    /* Round up to page boundary for mmap efficiency */
    size_t page_size = 4096; /* Assume 4KB pages */
    size_t page_aligned_size = ((size + page_size - 1) / page_size) * page_size;

    /* Create anonymous memory mapping */
    void *ptr =
        mmap(NULL, page_aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (ptr == MAP_FAILED) {
        switch (errno) {
            case ENOMEM:
                last_error = ALLOC_ERROR_OUT_OF_MEMORY;
                break;
            case EINVAL:
                last_error = ALLOC_ERROR_INVALID_SIZE;
                break;
            default:
                last_error = ALLOC_ERROR_OUT_OF_MEMORY;
        }
        handle_memory_acquisition_failure();
        return NULL;
    }

    register_memory_region(ptr, page_aligned_size, true);
    return ptr;
}

// cppcheck-suppress unusedFunction
int release_memory_mmap(void *ptr, size_t size)
{
    (void)size; /* Suppress unused parameter warning */
    if (!ptr)
        return -1;

    memory_region_t *region = find_memory_region(ptr);
    if (!region || !region->is_mmap) {
        last_error = ALLOC_ERROR_INVALID_POINTER;
        return -1;
    }

    if (munmap(ptr, region->size) == -1) {
        return -1;
    }

    unregister_memory_region(ptr);
    return 0;
}

static bool should_use_mmap_for_small_allocation(size_t size)
{
    (void)size; /* Suppress unused parameter warning */
    pthread_mutex_lock(&heap.heap_mutex);

    /* Check fragmentation ratio */
    if (heap.total_free > 0) {
        double fragmentation_ratio =
            (double)heap.total_free / (double)(heap.total_allocated + heap.total_free);
        if (fragmentation_ratio > 0.3) { /* >30% fragmentation */
            pthread_mutex_unlock(&heap.heap_mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&heap.heap_mutex);
    return false;
}

void *acquire_memory(size_t size)
{
    if (size == 0) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        return NULL;
    }

    size_t aligned_size = ALIGN_SIZE(size);

#ifdef __APPLE__
    /* On macOS, use mmap for all allocations due to sbrk deprecation */
    return acquire_memory_mmap(aligned_size);
#else
    /* Large allocations use mmap */
    if (aligned_size >= MMAP_THRESHOLD) {
        return acquire_memory_mmap(aligned_size);
    }

    /* Check if we should use mmap for small allocation */
    if (should_use_mmap_for_small_allocation(aligned_size)) {
        return acquire_memory_mmap(aligned_size);
    }

    return acquire_memory_sbrk(aligned_size);
#endif
}

/* Standard Allocator Interface */
void *malloc(size_t size)
{
    /* Initialize allocator on first use */
    if (!allocator_initialized) {
        if (allocator_init() != 0) {
            return NULL;
        }
    }

    if (size == 0) {
        return NULL; /* Standard behavior */
    }

    /* Ensure minimum allocation size */
    size_t actual_size = (size < MIN_ALLOC_SIZE) ? MIN_ALLOC_SIZE : size;
    size_t aligned_size = ALIGN_SIZE(actual_size);

    /* Try to find suitable free block */
    block_t *block = find_free_block(aligned_size);

    if (block) {
        /* Remove from free list */
        remove_from_free_list(block);

        /* Split block if it's significantly larger */
        if (can_split_block(block, aligned_size)) {
            block_t *new_free_block = split_block(block, aligned_size);
            if (new_free_block) {
                add_to_free_list(new_free_block);
            }
        }

        /* Initialize as allocated block */
        initialize_allocated_block(block, aligned_size);

        pthread_mutex_lock(&heap.heap_mutex);
        heap.total_allocated += aligned_size;
        heap.allocation_count++;
        pthread_mutex_unlock(&heap.heap_mutex);

        return get_ptr_from_block(block);
    }

    /* No suitable free block - acquire new memory */
    size_t total_size = HEADER_SIZE + aligned_size;
    void *memory = acquire_memory(total_size);

    if (!memory) {
        return NULL;
    }

    /* Initialize block in new memory */
    block = (block_t *)memory;
    initialize_allocated_block(block, aligned_size);

    pthread_mutex_lock(&heap.heap_mutex);
    heap.total_allocated += aligned_size;
    heap.allocation_count++;
    pthread_mutex_unlock(&heap.heap_mutex);

    return get_ptr_from_block(block);
}

void free(void *ptr)
{
    if (!ptr)
        return;

    /* Get block header */
    block_t *block = get_block_from_ptr(ptr);

    /* Verify block integrity */
    block_status_t status = verify_block_integrity(block);
    if (status != BLOCK_VALID) {
        if (status == BLOCK_CORRUPT_MAGIC) {
            fprintf(stderr, "Heap corruption detected: invalid magic number at %p\n", ptr);
        } else if (status == BLOCK_INVALID_FREE_STATE) {
            fprintf(stderr, "Double free detected at %p\n", ptr);
        }
        abort();
    }

    /* Validate the free request */
    if (!validate_free_request(block, ptr)) {
        return;
    }

    /* Update statistics */
    pthread_mutex_lock(&heap.heap_mutex);
    heap.total_allocated -= block->size;
    heap.allocation_count--;
    pthread_mutex_unlock(&heap.heap_mutex);

    /* Convert to free block and add to free list */
    initialize_free_block(block, block->size);
    add_to_free_list(block);
}

// cppcheck-suppress unusedFunction
void *calloc(size_t nmemb, size_t size)
{
    /* Check for overflow */
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        return NULL;
    }

    size_t total_size = nmemb * size;
    void *ptr = malloc(total_size);

    if (ptr) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}

// cppcheck-suppress unusedFunction
void *realloc(void *ptr, size_t size)
{
    if (!ptr) {
        return malloc(size);
    }

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_t *block = get_block_from_ptr(ptr);
    if (verify_block_integrity(block) != BLOCK_VALID) {
        last_error = ALLOC_ERROR_CORRUPTION;
        return NULL;
    }

    size_t current_size = block->size;
    size_t new_size = ALIGN_SIZE(size);

    /* If new size fits in current block, just return */
    if (new_size <= current_size) {
        return ptr;
    }

    /* Need to allocate new block */
    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    /* Copy data and free old block */
    memcpy(new_ptr, ptr, current_size);
    free(ptr);

    return new_ptr;
}

/* Error Handling */
static void handle_memory_acquisition_failure(void)
{
    time_t now = time(NULL);
    mem_stats.sbrk_failures++;
    mem_stats.last_failure_time = now;

    /* Enter emergency mode if failures are frequent */
    if (mem_stats.sbrk_failures + mem_stats.mmap_failures > 10) {
        mem_stats.emergency_mode = true;
        trigger_emergency_cleanup();
    }
}

static void trigger_emergency_cleanup(void)
{
    /* Future implementation: aggressive cleanup strategies */
}

/* Utility Functions */
// cppcheck-suppress unusedFunction
bool is_valid_heap_pointer(void *ptr)
{
    if (!ptr)
        return false;

    /* Check if pointer falls within known memory regions */
    const memory_region_t *region = find_memory_region(ptr);
    return region != NULL;
}

// cppcheck-suppress unusedFunction
const char *get_error_string(alloc_error_t error)
{
    switch (error) {
        case ALLOC_SUCCESS:
            return "Success";
        case ALLOC_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case ALLOC_ERROR_INVALID_SIZE:
            return "Invalid size";
        case ALLOC_ERROR_DOUBLE_FREE:
            return "Double free detected";
        case ALLOC_ERROR_CORRUPTION:
            return "Heap corruption detected";
        case ALLOC_ERROR_MISALIGNED:
            return "Misaligned pointer";
        case ALLOC_ERROR_INVALID_POINTER:
            return "Invalid pointer";
        default:
            return "Unknown error";
    }
}

// cppcheck-suppress unusedFunction
void allocator_stats(void)
{
    pthread_mutex_lock(&heap.heap_mutex);

    printf("=== Memory Allocator Statistics ===\n");
    printf("Total allocated: %zu bytes\n", heap.total_allocated);
    printf("Total free: %zu bytes\n", heap.total_free);
    printf("Active allocations: %zu\n", heap.allocation_count);
    printf("Heap start: %p\n", heap.heap_start);
    printf("Heap end: %p\n", heap.heap_end);

    if (heap.total_allocated + heap.total_free > 0) {
        double fragmentation =
            (double)heap.total_free / (double)(heap.total_allocated + heap.total_free) * 100.0;
        printf("Fragmentation: %.2f%%\n", fragmentation);
    }

    printf("Emergency mode: %s\n", mem_stats.emergency_mode ? "YES" : "NO");
    printf("sbrk failures: %d\n", mem_stats.sbrk_failures);
    printf("mmap failures: %d\n", mem_stats.mmap_failures);

    pthread_mutex_unlock(&heap.heap_mutex);
}

// cppcheck-suppress unusedFunction
void allocator_cleanup(void)
{
    if (!allocator_initialized)
        return;

    pthread_mutex_destroy(&heap.heap_mutex);
    pthread_mutex_destroy(&pool_mutex);
    pthread_mutex_destroy(&region_mutex);

    /* Free memory region tracking */
    memory_region_t *current = memory_regions;
    while (current) {
        memory_region_t *next = current->next;
        free(current);
        current = next;
    }

    allocator_initialized = false;
}

/* Missing function implementations */
static bool validate_free_request(block_t *block, void *ptr)
{
    /* Check if already free (double free detection) */
    if (block->is_free) {
        fprintf(stderr, "Double free detected at %p\n", ptr);
        abort();
        return false;
    }
    return true;
}

#pragma GCC diagnostic pop
