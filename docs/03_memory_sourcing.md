# Chapter 03: Memory Sourcing Strategies

The allocator must obtain memory from the operating system before it can satisfy application requests. This chapter implements the hybrid memory sourcing strategy outlined in our architecture, combining the efficiency of `sbrk()` for small allocations with the flexibility of `mmap()` for large ones.

## Understanding System Memory Acquisition

When a program requests memory through our allocator, the request eventually reaches the kernel through one of two system calls. The choice between `sbrk()` and `mmap()` involves fundamental tradeoffs that directly impact performance, memory fragmentation, and system resource utilization.

### The Program Break and sbrk()

The program break represents the boundary between allocated and unallocated memory in the process heap segment. The `sbrk()` system call adjusts this boundary:

```c
#include <unistd.h>

void* sbrk(intptr_t increment);
```

When `increment > 0`, `sbrk()` extends the heap by moving the program break higher in memory. When `increment < 0`, it contracts the heap. The return value is the previous program break location.

### Memory Layout with sbrk()

```
Virtual Memory Layout (sbrk region):
+------------------+ <- Higher addresses
| Unallocated      |
| Virtual Memory   |
+------------------+ <- Current program break (sbrk(0))
| Heap Memory      |    ^
| (our allocator   |    | sbrk() expands upward
|  manages this)   |    |
+------------------+ <- Original program break
| BSS Segment      |
+------------------+
| Data Segment     |
+------------------+ <- Lower addresses
```

### Anonymous Memory Mapping with mmap()

The `mmap()` system call creates memory mappings that exist independently of the traditional heap:

```c
#include <sys/mman.h>

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void* addr, size_t length);
```

For anonymous allocations, we use:
```c
void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
```

## Implementing the Hybrid Strategy

Our allocator uses a 128KB threshold to determine the memory sourcing strategy:

$$\text{Memory Source} = \begin{cases}
\text{sbrk()} & \text{if aligned\_size} < 128 \text{ KB} \\
\text{mmap()} & \text{if aligned\_size} \geq 128 \text{ KB}
\end{cases}$$

### Memory Sourcing Interface

The memory sourcing interface provides the data structures and constants needed to implement our hybrid strategy. We maintain a registry of allocated memory regions to support pointer validation and debugging. The interface defines thresholds that determine when to use sbrk versus mmap, and tracks all memory regions for proper cleanup. The implementation:

```c
#include "allocator.h"
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

/* Memory sourcing thresholds and constants */
#define MMAP_THRESHOLD (128 * 1024)    /* 128KB threshold */
#define PAGE_SIZE 4096                 /* Assume 4KB pages */
#define HEAP_EXTENSION_SIZE (64 * 1024) /* Extend heap by 64KB chunks */

/* Track memory regions for validation */
typedef struct memory_region {
    void* start;
    size_t size;
    bool is_mmap;
    struct memory_region* next;
} memory_region_t;

static memory_region_t* memory_regions = NULL;
static pthread_mutex_t region_mutex = PTHREAD_MUTEX_INITIALIZER;
```

### sbrk() Implementation

The `sbrk()` function requests memory from the kernel by extending the program break. Our implementation must handle system call failures, ensure proper alignment of returned memory, and maintain heap tracking information. Error handling is critical because sbrk() can fail when system memory is exhausted or when the heap segment cannot grow further. The `sbrk()` implementation must handle alignment, error conditions, and heap extension efficiently:

```c
void* acquire_memory_sbrk(size_t size) {
    /* Ensure size is aligned to prevent fragmentation */
    size_t aligned_size = ALIGN_SIZE(size);

    /* Get current program break */
    void* current_break = sbrk(0);
    if (current_break == (void*)-1) {
        last_error = ALLOC_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    /* Attempt to extend heap */
    void* new_break = sbrk(aligned_size);
    if (new_break == (void*)-1) {
        /* Handle ENOMEM - system cannot provide more memory */
        if (errno == ENOMEM) {
            last_error = ALLOC_ERROR_OUT_OF_MEMORY;
        }
        return NULL;
    }

    /* Update heap tracking information */
    if (heap.heap_start == NULL) {
        heap.heap_start = current_break;
    }
    heap.heap_end = (char*)current_break + aligned_size;
    heap.program_break = heap.heap_end;

    /* Register memory region for validation */
    register_memory_region(current_break, aligned_size, false);

    return current_break;
}
```

### Advanced sbrk() Management

For production efficiency, we extend the heap in larger chunks to amortize system call overhead:

```c
static void* heap_extension_pool = NULL;
static size_t pool_remaining = 0;
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

void* acquire_memory_sbrk_optimized(size_t size) {
    size_t aligned_size = ALIGN_SIZE(size);

    pthread_mutex_lock(&pool_mutex);

    /* Try to satisfy request from existing pool */
    if (heap_extension_pool && pool_remaining >= aligned_size) {
        void* result = heap_extension_pool;
        heap_extension_pool = (char*)heap_extension_pool + aligned_size;
        pool_remaining -= aligned_size;
        pthread_mutex_unlock(&pool_mutex);
        return result;
    }

    /* Pool exhausted or insufficient - extend heap */
    size_t extension_size = (aligned_size > HEAP_EXTENSION_SIZE)
                           ? aligned_size
                           : HEAP_EXTENSION_SIZE;

    void* new_memory = sbrk(extension_size);
    if (new_memory == (void*)-1) {
        pthread_mutex_unlock(&pool_mutex);
        last_error = ALLOC_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    /* Update global heap information */
    if (heap.heap_start == NULL) {
        heap.heap_start = new_memory;
    }
    heap.heap_end = (char*)new_memory + extension_size;

    /* Initialize pool with remaining memory */
    void* result = new_memory;
    heap_extension_pool = (char*)new_memory + aligned_size;
    pool_remaining = extension_size - aligned_size;

    register_memory_region(new_memory, extension_size, false);

    pthread_mutex_unlock(&pool_mutex);
    return result;
}
```

### mmap() Implementation

Large allocations use `mmap()` to avoid heap fragmentation and enable immediate memory return:

```c
void* acquire_memory_mmap(size_t size) {
    /* Round up to page boundary for mmap efficiency */
    size_t page_aligned_size = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    /* Create anonymous memory mapping */
    void* ptr = mmap(NULL, page_aligned_size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);

    if (ptr == MAP_FAILED) {
        /* Handle various error conditions */
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
        return NULL;
    }

    /* Register memory region for validation */
    register_memory_region(ptr, page_aligned_size, true);

    return ptr;
}

int release_memory_mmap(void* ptr, size_t size) {
    if (!ptr) return -1;

    /* Find the memory region to get actual size */
    memory_region_t* region = find_memory_region(ptr);
    if (!region || !region->is_mmap) {
        last_error = ALLOC_ERROR_INVALID_POINTER;
        return -1;
    }

    /* Unmap the memory */
    if (munmap(ptr, region->size) == -1) {
        return -1;
    }

    /* Remove from tracking */
    unregister_memory_region(ptr);
    return 0;
}
```

## Memory Region Tracking

To support pointer validation and debugging, we maintain a registry of allocated memory regions:

```c
static void register_memory_region(void* start, size_t size, bool is_mmap) {
    memory_region_t* region = malloc(sizeof(memory_region_t));
    if (!region) return;  /* Best effort tracking */

    region->start = start;
    region->size = size;
    region->is_mmap = is_mmap;

    pthread_mutex_lock(&region_mutex);
    region->next = memory_regions;
    memory_regions = region;
    pthread_mutex_unlock(&region_mutex);
}

static memory_region_t* find_memory_region(void* ptr) {
    pthread_mutex_lock(&region_mutex);

    memory_region_t* current = memory_regions;
    while (current) {
        char* start = (char*)current->start;
        char* end = start + current->size;

        if (ptr >= current->start && ptr < (void*)end) {
            pthread_mutex_unlock(&region_mutex);
            return current;
        }
        current = current->next;
    }

    pthread_mutex_unlock(&region_mutex);
    return NULL;
}

static void unregister_memory_region(void* start) {
    pthread_mutex_lock(&region_mutex);

    memory_region_t** current = &memory_regions;
    while (*current) {
        if ((*current)->start == start) {
            memory_region_t* to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            break;
        }
        current = &(*current)->next;
    }

    pthread_mutex_unlock(&region_mutex);
}
```

## Memory Source Selection Logic

The allocator selects the appropriate memory source based on request size and system state:

```c
typedef enum {
    MEMORY_SOURCE_SBRK,
    MEMORY_SOURCE_MMAP,
    MEMORY_SOURCE_ERROR
} memory_source_t;

memory_source_t select_memory_source(size_t size) {
    size_t aligned_size = ALIGN_SIZE(size);

    /* Large allocations always use mmap */
    if (aligned_size >= MMAP_THRESHOLD) {
        return MEMORY_SOURCE_MMAP;
    }

    /* For smaller allocations, prefer sbrk unless heap is fragmented */
    if (should_use_mmap_for_small_allocation(aligned_size)) {
        return MEMORY_SOURCE_MMAP;
    }

    return MEMORY_SOURCE_SBRK;
}

static bool should_use_mmap_for_small_allocation(size_t size) {
    /* Use mmap for small allocations if:
     * 1. Heap fragmentation is severe
     * 2. sbrk() has been failing recently
     * 3. Memory pressure is high
     */

    pthread_mutex_lock(&heap.heap_mutex);

    /* Check fragmentation ratio */
    if (heap.total_free > 0) {
        double fragmentation_ratio = (double)heap.total_free /
                                   (double)(heap.total_allocated + heap.total_free);
        if (fragmentation_ratio > 0.3) {  /* >30% fragmentation */
            pthread_mutex_unlock(&heap.heap_mutex);
            return true;
        }
    }

    pthread_mutex_unlock(&heap.heap_mutex);
    return false;
}
```

## Unified Memory Acquisition Interface

The allocator provides a single interface that handles source selection automatically:

```c
void* acquire_memory(size_t size) {
    if (size == 0) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        return NULL;
    }

    memory_source_t source = select_memory_source(size);

    switch (source) {
        case MEMORY_SOURCE_SBRK:
            return acquire_memory_sbrk_optimized(size);

        case MEMORY_SOURCE_MMAP:
            return acquire_memory_mmap(size);

        default:
            last_error = ALLOC_ERROR_OUT_OF_MEMORY;
            return NULL;
    }
}

void release_memory(void* ptr, size_t size) {
    if (!ptr) return;

    memory_region_t* region = find_memory_region(ptr);
    if (!region) {
        last_error = ALLOC_ERROR_INVALID_POINTER;
        return;
    }

    if (region->is_mmap) {
        release_memory_mmap(ptr, region->size);
    }
    /* sbrk memory cannot be returned individually to the system */
}
```

## Performance Analysis

### System Call Overhead

The overhead of memory acquisition varies significantly between sources:

| Operation | System Call Cost | Typical Time |
|-----------|------------------|--------------|
| sbrk(large) | Single syscall | ~1-2 μs |
| sbrk(small) from pool | No syscall | ~10-50 ns |
| mmap() | Syscall + page fault | ~5-20 μs |
| munmap() | Syscall + TLB flush | ~2-10 μs |

### Memory Efficiency Analysis

The choice of memory source affects overall memory utilization:

**sbrk() Efficiency**:
- Internal fragmentation: Minimal due to precise sizing
- External fragmentation: Can accumulate over time
- Memory return: Delayed until heap contracts

**mmap() Efficiency**:
- Internal fragmentation: High due to page alignment
- External fragmentation: None (each allocation independent)
- Memory return: Immediate upon free

### Threshold Analysis

The 128KB threshold balances efficiency tradeoffs:

```c
/* Mathematical analysis of optimal threshold */
static size_t calculate_optimal_threshold(void) {
    /* Break-even point where mmap() overhead equals sbrk() fragmentation cost */
    const double syscall_cost_us = 10.0;         /* mmap syscall cost */
    const double fragmentation_factor = 0.15;    /* 15% fragmentation penalty */
    const double allocation_rate_hz = 100000.0;  /* Allocations per second */

    /* Threshold where fragmentation cost equals syscall cost */
    return (size_t)(syscall_cost_us * allocation_rate_hz / fragmentation_factor);
}
```

## Error Handling and Recovery

Memory acquisition failures require graceful handling and recovery strategies:

```c
typedef struct {
    int sbrk_failures;
    int mmap_failures;
    time_t last_failure_time;
    bool emergency_mode;
} memory_stats_t;

static memory_stats_t mem_stats = {0};

static void handle_memory_acquisition_failure(memory_source_t source) {
    time_t now = time(NULL);

    if (source == MEMORY_SOURCE_SBRK) {
        mem_stats.sbrk_failures++;
    } else {
        mem_stats.mmap_failures++;
    }

    mem_stats.last_failure_time = now;

    /* Enter emergency mode if failures are frequent */
    if (mem_stats.sbrk_failures + mem_stats.mmap_failures > 10 &&
        (now - mem_stats.last_failure_time) < 60) {
        mem_stats.emergency_mode = true;

        /* Attempt memory pressure relief */
        trigger_emergency_cleanup();
    }
}

static void trigger_emergency_cleanup(void) {
    /* Flush thread-local caches */
    cleanup_thread_cache();

    /* Attempt to coalesce free blocks more aggressively */
    aggressive_heap_consolidation();

    /* Consider releasing mmap regions back to system */
    gc_mmap_regions();
}
```

## Testing Memory Sourcing

Comprehensive testing ensures reliable memory acquisition under all conditions:

```c
#include <assert.h>
#include <sys/resource.h>

void test_memory_sourcing(void) {
    /* Test small allocation via sbrk */
    void* small_ptr = acquire_memory(1024);
    assert(small_ptr != NULL);
    memory_region_t* small_region = find_memory_region(small_ptr);
    assert(small_region && !small_region->is_mmap);

    /* Test large allocation via mmap */
    void* large_ptr = acquire_memory(256 * 1024);
    assert(large_ptr != NULL);
    memory_region_t* large_region = find_memory_region(large_ptr);
    assert(large_region && large_region->is_mmap);

    /* Test memory pressure conditions */
    struct rlimit rlim;
    getrlimit(RLIMIT_AS, &rlim);

    /* Temporarily limit virtual memory */
    rlim.rlim_cur = rlim.rlim_cur / 2;
    setrlimit(RLIMIT_AS, &rlim);

    /* Verify graceful failure */
    void* pressure_ptr = acquire_memory(1024 * 1024 * 1024);  /* 1GB */
    assert(pressure_ptr == NULL);
    assert(last_error == ALLOC_ERROR_OUT_OF_MEMORY);

    /* Restore limits */
    rlim.rlim_cur = rlim.rlim_max;
    setrlimit(RLIMIT_AS, &rlim);
}
```

## Summary

This chapter implemented a robust memory sourcing system that efficiently combines `sbrk()` and `mmap()` based on allocation size. Key achievements:

1. **Hybrid Strategy**: Automatic selection between `sbrk()` (<128KB) and `mmap()` (≥128KB)
2. **Optimized sbrk()**: Pool-based management reducing system call overhead
3. **Robust mmap()**: Page-aligned allocations with immediate return capability
4. **Region Tracking**: Comprehensive memory region registry for validation
5. **Error Handling**: Graceful failure handling with emergency recovery
6. **Performance Analysis**: Mathematical optimization of source selection threshold

The memory sourcing layer now provides a solid foundation for implementing allocation algorithms. In Chapter 04, we'll build the core `malloc()` implementation using these memory acquisition primitives.
