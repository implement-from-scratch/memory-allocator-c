# Chapter 09: Thread Safety with Global Mutex

Multi-threaded applications require memory allocators that can safely handle concurrent allocation and deallocation requests. Without proper synchronization, race conditions can corrupt heap metadata, cause double-free errors, or lead to memory leaks. This chapter implements thread safety using a global mutex, the simplest and most reliable approach for protecting shared heap state.

## Thread Safety Analysis

Concurrent access to heap data structures creates several categories of race conditions. Understanding these patterns helps design effective synchronization mechanisms.

### Race Condition Categories

Memory allocators face three primary types of race conditions:

1. **Metadata Corruption**: Multiple threads modifying block headers simultaneously
2. **Free List Corruption**: Concurrent insertion and removal from free lists
3. **Statistics Inconsistency**: Non-atomic updates to heap statistics counters

Each category requires different protection strategies, but a global mutex can protect all of them by serializing access to the entire heap.

### Critical Sections Identification

The critical sections that require protection include:

- Free list traversal and modification
- Block header updates (size, flags, pointers)
- Heap statistics updates
- Memory region tracking
- Coalescing operations

Allocating or freeing memory touches multiple shared data structures, making fine-grained locking complex. A global mutex simplifies this by protecting all operations atomically.

## Global Mutex Implementation

A global mutex serializes all heap operations, ensuring only one thread accesses the heap at a time. This approach trades some parallelism for simplicity and correctness.

### Mutex Initialization

We initialize the global mutex during allocator startup:

```c
#include <pthread.h>

typedef struct thread_safe_heap {
    pthread_mutex_t heap_mutex;
    bool mutex_initialized;
    
    /* Existing heap structures */
    block_t* free_head;
    void* heap_start;
    void* heap_end;
    size_t total_allocated;
    size_t total_free;
    size_t allocation_count;
} thread_safe_heap_t;

static thread_safe_heap_t heap = {0};

int allocator_init(void) {
    /* Initialize mutex with error checking */
    int result = pthread_mutex_init(&heap.heap_mutex, NULL);
    if (result != 0) {
        last_error = ALLOC_ERROR_INITIALIZATION;
        return -1;
    }
    
    heap.mutex_initialized = true;
    
    /* Initialize other heap structures */
    heap.free_head = NULL;
    heap.heap_start = NULL;
    heap.heap_end = NULL;
    
    return 0;
}
```

### Thread-Safe malloc Implementation

The malloc function acquires the mutex before any heap access:

```c
void* malloc(size_t size) {
    /* Lazy initialization with double-checked locking */
    if (!heap.mutex_initialized) {
        pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
        static bool init_attempted = false;
        
        if (!init_attempted) {
            pthread_mutex_lock(&init_mutex);
            if (!heap.mutex_initialized) {
                allocator_init();
            }
            init_attempted = true;
            pthread_mutex_unlock(&init_mutex);
        }
    }

    /* Acquire heap mutex */
    pthread_mutex_lock(&heap.heap_mutex);

    /* Perform allocation */
    void* result = malloc_unsafe(size);

    /* Release heap mutex */
    pthread_mutex_unlock(&heap.heap_mutex);

    return result;
}
```

The unsafe version performs the actual allocation logic without mutex protection, assuming the caller has already acquired the lock:

```c
static void* malloc_unsafe(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size_t aligned_size = ALIGN_SIZE(size);
    
    /* Search free list */
    block_t* block = find_free_block_unsafe(aligned_size);
    
    if (block) {
        return allocate_from_free_block_unsafe(block, aligned_size);
    }

    /* Acquire new memory */
    return allocate_new_memory_unsafe(aligned_size);
}
```

### Thread-Safe free Implementation

The free function similarly protects all heap modifications:

```c
void free(void* ptr) {
    if (!ptr) {
        return;
    }

    /* Acquire heap mutex */
    pthread_mutex_lock(&heap.heap_mutex);

    /* Perform deallocation */
    free_unsafe(ptr);

    /* Release heap mutex */
    pthread_mutex_unlock(&heap.heap_mutex);
}

static void free_unsafe(void* ptr) {
    block_t* block = get_block_from_ptr(ptr);
    
    if (!validate_free_request_unsafe(block, ptr)) {
        return;
    }

    /* Update statistics */
    heap.total_allocated -= block->size;
    heap.allocation_count--;

    /* Convert to free block */
    initialize_free_block(block, block->size);
    set_footer(block);

    /* Coalesce with adjacent blocks */
    block_t* coalesced = coalesce_blocks_unsafe(block);

    /* Add to free list */
    add_to_free_list_unsafe(coalesced);

    heap.total_free += coalesced->size;
}
```

## Lock Contention Analysis

Global mutex serialization creates contention when multiple threads allocate simultaneously. Understanding contention patterns helps optimize performance.

### Contention Measurement

We measure lock contention by tracking wait times and hold times:

```c
typedef struct mutex_statistics {
    uint64_t total_acquisitions;
    uint64_t total_wait_time_ns;
    uint64_t total_hold_time_ns;
    uint64_t max_wait_time_ns;
    uint64_t max_hold_time_ns;
    uint64_t contention_count;  /* Times thread had to wait */
} mutex_statistics_t;

static mutex_statistics_t mutex_stats = {0};

void* malloc_with_statistics(size_t size) {
    uint64_t wait_start = get_time_ns();
    
    pthread_mutex_lock(&heap.heap_mutex);
    
    uint64_t wait_time = get_time_ns() - wait_start;
    if (wait_time > 0) {
        mutex_stats.contention_count++;
        mutex_stats.total_wait_time_ns += wait_time;
        if (wait_time > mutex_stats.max_wait_time_ns) {
            mutex_stats.max_wait_time_ns = wait_time;
        }
    }

    uint64_t hold_start = get_time_ns();
    
    void* result = malloc_unsafe(size);
    
    uint64_t hold_time = get_time_ns() - hold_start;
    mutex_stats.total_hold_time_ns += hold_time;
    if (hold_time > mutex_stats.max_hold_time_ns) {
        mutex_stats.max_hold_time_ns = hold_time;
    }
    
    mutex_stats.total_acquisitions++;
    
    pthread_mutex_unlock(&heap.heap_mutex);
    
    return result;
}
```

### Contention Factors

Lock contention depends on several factors:

1. **Allocation Frequency**: Higher allocation rates increase contention
2. **Critical Section Duration**: Longer operations hold the lock longer
3. **Thread Count**: More threads competing for the same lock
4. **Workload Pattern**: Bursty allocations create contention spikes

The relationship can be approximated as:
```
Contention Probability ≈ (Threads × Allocation Rate × Hold Time) / Time Window
```

### Reducing Critical Section Duration

We minimize lock hold time by performing expensive operations outside the critical section:

```c
void* malloc_optimized(size_t size) {
    /* Pre-calculate values outside lock */
    if (size == 0) {
        return NULL;
    }
    size_t aligned_size = ALIGN_SIZE(size);

    /* Acquire lock */
    pthread_mutex_lock(&heap.heap_mutex);

    /* Fast path: check free list head */
    block_t* block = NULL;
    if (heap.free_head && heap.free_head->size >= aligned_size) {
        block = heap.free_head;
    } else {
        /* Search free list (still holding lock) */
        block = find_free_block_unsafe(aligned_size);
    }

    void* result = NULL;
    if (block) {
        result = allocate_from_free_block_unsafe(block, aligned_size);
        pthread_mutex_unlock(&heap.heap_mutex);
        return result;
    }

    pthread_mutex_unlock(&heap.heap_mutex);

    /* Expensive memory acquisition outside lock */
    void* new_memory = acquire_memory_system_call(aligned_size);
    if (!new_memory) {
        return NULL;
    }

    /* Re-acquire lock for metadata update */
    pthread_mutex_lock(&heap.heap_mutex);
    
    block_t* new_block = (block_t*)new_memory;
    initialize_allocated_block(new_block, aligned_size);
    heap.total_allocated += aligned_size;
    heap.allocation_count++;
    
    pthread_mutex_unlock(&heap.heap_mutex);
    
    return get_ptr_from_block(new_block);
}
```

## Deadlock Prevention

Global mutex implementations must avoid deadlocks, especially when interacting with other synchronization primitives or when error handling requires additional locking.

### Mutex Ordering

If multiple mutexes are needed, establish a consistent locking order:

```c
/* Mutex ordering: heap_mutex -> region_mutex -> stats_mutex */
void* malloc_with_multiple_locks(size_t size) {
    pthread_mutex_lock(&heap.heap_mutex);
    pthread_mutex_lock(&heap.region_mutex);
    
    /* Perform allocation */
    void* result = malloc_unsafe(size);
    register_memory_region_unsafe(result, size);
    
    pthread_mutex_unlock(&heap.region_mutex);
    pthread_mutex_unlock(&heap.heap_mutex);
    
    return result;
}
```

### Error Handling in Critical Sections

Error handling must not introduce deadlocks:

```c
void* malloc_with_error_handling(size_t size) {
    pthread_mutex_lock(&heap.heap_mutex);

    void* result = malloc_unsafe(size);
    
    if (!result) {
        /* Error handling - must not acquire additional locks */
        log_allocation_failure(size);
        /* Do not call functions that might acquire heap_mutex */
    }

    pthread_mutex_unlock(&heap.heap_mutex);
    return result;
}
```

### Recursive Mutex Alternative

For code that might re-enter allocation functions, consider recursive mutexes:

```c
static pthread_mutex_t heap_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

void* malloc_recursive_safe(size_t size) {
    /* Can be called from within critical section */
    pthread_mutex_lock(&heap_mutex);
    
    void* result = malloc_unsafe(size);
    
    pthread_mutex_unlock(&heap_mutex);
    return result;
}
```

However, recursive mutexes can mask design problems and should be used sparingly.

## Performance Impact Measurement

Global mutex serialization impacts performance differently depending on workload characteristics. Measuring this impact guides optimization decisions.

### Benchmarking Thread Scalability

We measure how performance scales with thread count:

```c
typedef struct scalability_results {
    int thread_count;
    double allocations_per_second;
    double avg_allocation_time_ns;
    double max_allocation_time_ns;
    double lock_contention_percent;
} scalability_results_t;

scalability_results_t benchmark_thread_scalability(int max_threads) {
    scalability_results_t results[max_threads];
    
    for (int threads = 1; threads <= max_threads; threads++) {
        struct timespec start, end;
        const int allocations_per_thread = 10000;
        const size_t allocation_size = 64;
        
        pthread_t thread_ids[threads];
        barrier_t barrier;
        barrier_init(&barrier, threads);
        
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        /* Launch threads */
        for (int i = 0; i < threads; i++) {
            pthread_create(&thread_ids[i], NULL, 
                          allocation_worker, 
                          &(worker_args_t){allocations_per_thread, 
                                          allocation_size, &barrier});
        }
        
        /* Wait for completion */
        for (int i = 0; i < threads; i++) {
            pthread_join(thread_ids[i], NULL);
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        /* Calculate metrics */
        double elapsed = (end.tv_sec - start.tv_sec) + 
                        (end.tv_nsec - start.tv_nsec) / 1e9;
        double total_allocations = threads * allocations_per_thread;
        
        results[threads - 1].thread_count = threads;
        results[threads - 1].allocations_per_second = total_allocations / elapsed;
        results[threads - 1].avg_allocation_time_ns = 
            (elapsed * 1e9) / total_allocations;
        results[threads - 1].lock_contention_percent = 
            (mutex_stats.contention_count * 100.0) / mutex_stats.total_acquisitions;
    }
    
    return results;
}
```

### Expected Performance Characteristics

With global mutex, performance typically shows:

- **Single Thread**: Minimal overhead, similar to non-thread-safe version
- **2-4 Threads**: Moderate contention, 20-40% performance degradation
- **8+ Threads**: High contention, 50-70% performance degradation
- **Many Threads**: Severe contention, performance plateaus or degrades

The relationship follows:
```
Throughput(threads) ≈ Base_Throughput / (1 + Contention_Factor × threads)
```

## Lock-Free Fast Path Optimization

We can optimize the common case by attempting lock-free operations before falling back to the mutex:

```c
void* malloc_with_trylock(size_t size) {
    if (size == 0) {
        return NULL;
    }
    
    size_t aligned_size = ALIGN_SIZE(size);

    /* Try to acquire lock without blocking */
    if (pthread_mutex_trylock(&heap.heap_mutex) == 0) {
        /* Fast path: lock acquired immediately */
        block_t* block = find_free_block_unsafe(aligned_size);
        void* result = NULL;
        
        if (block) {
            result = allocate_from_free_block_unsafe(block, aligned_size);
        } else {
            result = allocate_new_memory_unsafe(aligned_size);
        }
        
        pthread_mutex_unlock(&heap.heap_mutex);
        return result;
    }

    /* Slow path: lock contended, must block */
    pthread_mutex_lock(&heap.heap_mutex);
    void* result = malloc_unsafe(aligned_size);
    pthread_mutex_unlock(&heap.heap_mutex);
    return result;
}
```

## Testing Thread Safety

Comprehensive testing verifies thread safety under various conditions:

```c
void test_concurrent_allocations(void) {
    const int threads = 8;
    const int allocations_per_thread = 1000;
    pthread_t thread_ids[threads];
    
    /* Launch threads */
    for (int i = 0; i < threads; i++) {
        pthread_create(&thread_ids[i], NULL, 
                      concurrent_allocator, 
                      (void*)(intptr_t)allocations_per_thread);
    }
    
    /* Wait for completion */
    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }
    
    /* Verify heap integrity */
    assert(validate_heap_integrity());
}

void* concurrent_allocator(void* arg) {
    int count = (int)(intptr_t)arg;
    void* ptrs[count];
    
    /* Allocate */
    for (int i = 0; i < count; i++) {
        ptrs[i] = malloc(64);
        assert(ptrs[i] != NULL);
    }
    
    /* Free */
    for (int i = 0; i < count; i++) {
        free(ptrs[i]);
    }
    
    return NULL;
}

void test_race_conditions(void) {
    /* Test for double-free in concurrent scenario */
    void* shared_ptr = malloc(64);
    const int threads = 4;
    pthread_t thread_ids[threads];
    
    /* Multiple threads attempt to free same pointer */
    for (int i = 0; i < threads; i++) {
        pthread_create(&thread_ids[i], NULL, 
                      race_condition_tester, shared_ptr);
    }
    
    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }
    
    /* Should detect double-free, not crash */
    assert(heap.corruption_detected == threads - 1);
}
```

## Summary

This chapter implemented thread safety using a global mutex:

1. **Global Mutex Protection**: Serializes all heap operations for correctness
2. **Lock Contention Analysis**: Measures and understands performance impact
3. **Deadlock Prevention**: Consistent locking order and careful error handling
4. **Performance Measurement**: Benchmarks thread scalability characteristics
5. **Optimization Techniques**: Trylock fast paths and critical section minimization

The global mutex approach provides reliable thread safety with predictable behavior, though it limits parallelism.
