# Chapter 10: Thread-Local Optimization

Global mutex serialization limits parallelism and creates contention bottlenecks in multi-threaded applications. Thread-local caches provide a high-performance allocation path that avoids global lock contention for common allocation sizes. This chapter implements per-thread memory caches that maintain local pools of free blocks, dramatically improving scalability while maintaining thread safety.

## Thread-Local Cache Design

Thread-local caches maintain separate free block pools for each thread, allowing most allocations to proceed without acquiring the global heap mutex. Each thread manages its own cache, reducing contention and improving cache locality.

### Cache Architecture Overview

A thread-local cache consists of multiple size-class lists, each holding free blocks of similar sizes. When a thread needs memory, it first checks its local cache. Only when the cache is empty or full does the thread interact with the global heap.

The cache structure includes:
- Size-class segregated free lists
- Cache fill and flush thresholds
- Statistics for cache efficiency monitoring
- Integration with global heap for cache refill

### Thread-Local Storage

We use thread-local storage to maintain per-thread cache state:

```c
#include <pthread.h>

#define THREAD_CACHE_SIZE_CLASSES 16
#define CACHE_FILL_THRESHOLD 4
#define CACHE_FLUSH_THRESHOLD 32

typedef struct thread_cache {
    block_t* size_class_lists[THREAD_CACHE_SIZE_CLASSES];
    size_t size_class_counts[THREAD_CACHE_SIZE_CLASSES];
    
    /* Cache management */
    size_t total_cached_blocks;
    size_t cache_hits;
    size_t cache_misses;
    size_t cache_fills;
    size_t cache_flushes;
    
    /* Size class boundaries */
    size_t size_class_boundaries[THREAD_CACHE_SIZE_CLASSES];
} thread_cache_t;

/* Thread-local cache instance */
static __thread thread_cache_t* thread_cache = NULL;

/* Global cache initialization flag */
static pthread_once_t cache_init_once = PTHREAD_ONCE_INIT;
```

### Cache Initialization

Each thread initializes its cache on first use:

```c
static void init_thread_cache_once(void) {
    /* One-time initialization of size class boundaries */
    size_t boundary = 16;
    for (int i = 0; i < THREAD_CACHE_SIZE_CLASSES; i++) {
        global_size_class_boundaries[i] = boundary;
        boundary *= 2;  /* Exponential size classes */
    }
}

thread_cache_t* get_thread_cache(void) {
    if (thread_cache == NULL) {
        pthread_once(&cache_init_once, init_thread_cache_once);
        
        thread_cache = calloc(1, sizeof(thread_cache_t));
        if (!thread_cache) {
            return NULL;
        }
        
        /* Initialize size class boundaries */
        for (int i = 0; i < THREAD_CACHE_SIZE_CLASSES; i++) {
            thread_cache->size_class_boundaries[i] = 
                global_size_class_boundaries[i];
        }
    }
    
    return thread_cache;
}
```

## Size Class Optimization

Size classes group allocations into power-of-two buckets, enabling fast cache lookups and efficient block reuse. The size class system maps allocation requests to appropriate cache lists.

### Size Class Calculation

We map allocation sizes to size classes using binary search or bit manipulation:

```c
int get_size_class(size_t size) {
    /* Find smallest size class that fits */
    for (int i = 0; i < THREAD_CACHE_SIZE_CLASSES; i++) {
        if (size <= thread_cache->size_class_boundaries[i]) {
            return i;
        }
    }
    return THREAD_CACHE_SIZE_CLASSES - 1;  /* Largest class */
}

/* Optimized version using bit manipulation for power-of-two classes */
int get_size_class_fast(size_t size) {
    if (size <= 16) return 0;
    if (size <= 32) return 1;
    if (size <= 64) return 2;
    
    /* For larger sizes, use log2 calculation */
    int log2_size = 0;
    size_t temp = size;
    while (temp > 1) {
        temp >>= 1;
        log2_size++;
    }
    
    /* Map to size class (assuming 16-byte minimum) */
    int class = log2_size - 4;  /* 2^4 = 16 */
    if (class < 0) class = 0;
    if (class >= THREAD_CACHE_SIZE_CLASSES) {
        class = THREAD_CACHE_SIZE_CLASSES - 1;
    }
    
    return class;
}
```

### Cache Allocation Path

The fast allocation path checks the thread-local cache first:

```c
void* malloc_with_thread_cache(size_t size) {
    if (size == 0) {
        return NULL;
    }
    
    size_t aligned_size = ALIGN_SIZE(size);
    
    /* Get thread-local cache */
    thread_cache_t* cache = get_thread_cache();
    if (!cache) {
        /* Fallback to global malloc */
        return malloc_global(aligned_size);
    }
    
    /* Determine size class */
    int size_class = get_size_class_fast(aligned_size);
    
    /* Check thread-local cache */
    if (cache->size_class_lists[size_class]) {
        /* Cache hit - fast path */
        block_t* block = cache->size_class_lists[size_class];
        cache->size_class_lists[size_class] = block->next_free;
        cache->size_class_counts[size_class]--;
        cache->total_cached_blocks--;
        cache->cache_hits++;
        
        /* Initialize allocated block */
        initialize_allocated_block(block, aligned_size);
        return get_ptr_from_block(block);
    }
    
    /* Cache miss - refill from global heap */
    cache->cache_misses++;
    return malloc_with_cache_refill(aligned_size, size_class, cache);
}
```

### Cache Refill Strategy

When the cache is empty, we batch-allocate multiple blocks from the global heap:

```c
static void* malloc_with_cache_refill(size_t size, int size_class, 
                                     thread_cache_t* cache) {
    /* Acquire global heap lock */
    pthread_mutex_lock(&heap.heap_mutex);
    
    /* Fill cache with multiple blocks */
    size_t blocks_to_cache = CACHE_FILL_THRESHOLD;
    block_t* cached_blocks = NULL;
    size_t cached_count = 0;
    
    for (size_t i = 0; i < blocks_to_cache; i++) {
        block_t* block = find_free_block_unsafe(size);
        if (!block) {
            /* Allocate new memory if no free blocks */
            void* new_mem = allocate_new_memory_unsafe(size);
            if (!new_mem) {
                break;
            }
            block = (block_t*)new_mem;
            initialize_free_block(block, size);
        } else {
            remove_from_free_list_unsafe(block);
        }
        
        /* Add to cache list */
        block->next_free = cached_blocks;
        cached_blocks = block;
        cached_count++;
    }
    
    pthread_mutex_unlock(&heap.heap_mutex);
    
    if (cached_count == 0) {
        return NULL;  /* Out of memory */
    }
    
    /* Use first block for allocation */
    block_t* result_block = cached_blocks;
    cached_blocks = cached_blocks->next_free;
    cached_count--;
    
    initialize_allocated_block(result_block, size);
    void* result = get_ptr_from_block(result_block);
    
    /* Add remaining blocks to cache */
    if (cached_blocks) {
        cache->size_class_lists[size_class] = cached_blocks;
        cache->size_class_counts[size_class] = cached_count;
        cache->total_cached_blocks += cached_count;
        cache->cache_fills++;
    }
    
    return result;
}
```

## Cache Coherency Considerations

Thread-local caches create multiple copies of free blocks across threads. We must ensure these caches remain consistent with the global heap and handle cache overflow appropriately.

### Cache Flush Strategy

When a thread's cache grows too large, we return excess blocks to the global heap:

```c
static void flush_thread_cache(thread_cache_t* cache) {
    if (cache->total_cached_blocks < CACHE_FLUSH_THRESHOLD) {
        return;  /* Cache not large enough to flush */
    }
    
    pthread_mutex_lock(&heap.heap_mutex);
    
    /* Flush each size class */
    for (int i = 0; i < THREAD_CACHE_SIZE_CLASSES; i++) {
        if (cache->size_class_counts[i] > CACHE_FILL_THRESHOLD) {
            /* Return excess blocks to global heap */
            size_t excess = cache->size_class_counts[i] - CACHE_FILL_THRESHOLD;
            block_t* current = cache->size_class_lists[i];
            
            /* Skip blocks to keep */
            for (size_t j = 0; j < CACHE_FILL_THRESHOLD && current; j++) {
                current = current->next_free;
            }
            
            if (current) {
                /* Add excess to global free list */
                block_t* excess_head = current;
                while (current->next_free) {
                    current = current->next_free;
                }
                current->next_free = heap.free_head;
                if (heap.free_head) {
                    heap.free_head->prev_free = current;
                }
                heap.free_head = excess_head;
                heap.free_head->prev_free = NULL;
                
                /* Update cache */
                cache->size_class_counts[i] = CACHE_FILL_THRESHOLD;
                cache->total_cached_blocks -= excess;
            }
        }
    }
    
    cache->cache_flushes++;
    pthread_mutex_unlock(&heap.heap_mutex);
}
```

### Free Operation with Cache

When freeing memory, we add blocks to the thread-local cache first:

```c
void free_with_thread_cache(void* ptr) {
    if (!ptr) {
        return;
    }
    
    block_t* block = get_block_from_ptr(ptr);
    if (!validate_block_for_free(block, ptr)) {
        return;
    }
    
    thread_cache_t* cache = get_thread_cache();
    if (!cache) {
        free_global(ptr);
        return;
    }
    
    size_t size = block->size;
    int size_class = get_size_class_fast(size);
    
    /* Check if cache has room */
    if (cache->size_class_counts[size_class] < CACHE_FLUSH_THRESHOLD) {
        /* Add to thread-local cache */
        initialize_free_block(block, size);
        block->next_free = cache->size_class_lists[size_class];
        cache->size_class_lists[size_class] = block;
        cache->size_class_counts[size_class]++;
        cache->total_cached_blocks++;
        return;
    }
    
    /* Cache full - flush and add to global heap */
    flush_thread_cache(cache);
    
    /* Add to global heap */
    pthread_mutex_lock(&heap.heap_mutex);
    add_to_free_list_unsafe(block);
    heap.total_free += size;
    pthread_mutex_unlock(&heap.heap_mutex);
}
```

## NUMA Awareness

Non-Uniform Memory Access architectures require careful memory placement to optimize performance. Thread-local caches can be enhanced to prefer local NUMA node memory.

### NUMA Node Detection

We detect the current thread's NUMA node:

```c
#include <numa.h>

int get_current_numa_node(void) {
    #ifdef HAVE_NUMA
        if (numa_available() >= 0) {
            return numa_node_of_cpu(sched_getcpu());
        }
    #endif
    return 0;  /* Single node or NUMA not available */
}

void* malloc_numa_aware(size_t size) {
    int preferred_node = get_current_numa_node();
    
    /* Try to allocate from preferred node first */
    void* ptr = malloc_from_numa_node(size, preferred_node);
    if (ptr) {
        return ptr;
    }
    
    /* Fallback to any available node */
    return malloc_global(size);
}
```

### NUMA-Optimized Cache

Cache blocks can be tagged with NUMA node information:

```c
typedef struct numa_block {
    block_t block;
    int numa_node;
} numa_block_t;

void* malloc_with_numa_cache(size_t size) {
    thread_cache_t* cache = get_thread_cache();
    int current_node = get_current_numa_node();
    int size_class = get_size_class_fast(size);
    
    /* Prefer blocks from current NUMA node */
    numa_block_t* best_block = NULL;
    numa_block_t* current = (numa_block_t*)cache->size_class_lists[size_class];
    
    while (current) {
        if (current->numa_node == current_node) {
            best_block = current;
            break;  /* Perfect match */
        }
        if (!best_block) {
            best_block = current;  /* Fallback */
        }
        current = (numa_block_t*)current->block.next_free;
    }
    
    if (best_block) {
        /* Remove from cache */
        remove_from_cache_list(&cache->size_class_lists[size_class], 
                              (block_t*)best_block);
        cache->size_class_counts[size_class]--;
        cache->total_cached_blocks--;
        
        initialize_allocated_block((block_t*)best_block, size);
        return get_ptr_from_block((block_t*)best_block);
    }
    
    /* Cache miss - allocate from preferred node */
    return malloc_numa_aware(size);
}
```

## Performance Scaling Analysis

Thread-local caches dramatically improve scalability by reducing lock contention. We measure this improvement through comprehensive benchmarking.

### Scalability Metrics

We track cache efficiency and performance scaling:

```c
typedef struct cache_performance {
    double cache_hit_rate;
    double avg_allocation_time_ns;
    double cache_refill_time_ns;
    double cache_flush_time_ns;
    size_t allocations_per_second;
    double speedup_over_global;
} cache_performance_t;

cache_performance_t measure_cache_performance(int thread_count) {
    cache_performance_t perf = {0};
    
    const int allocations = 100000;
    struct timespec start, end;
    
    /* Reset statistics */
    reset_cache_statistics();
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    /* Run allocation workload */
    run_allocation_workload(thread_count, allocations);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) + 
                    (end.tv_nsec - start.tv_nsec) / 1e9;
    
    /* Calculate metrics */
    size_t total_hits = get_total_cache_hits();
    size_t total_misses = get_total_cache_misses();
    size_t total_ops = total_hits + total_misses;
    
    perf.cache_hit_rate = (double)total_hits / total_ops;
    perf.allocations_per_second = (thread_count * allocations) / elapsed;
    perf.avg_allocation_time_ns = (elapsed * 1e9) / (thread_count * allocations);
    
    /* Compare to global mutex baseline */
    double baseline_time = measure_global_mutex_time(thread_count, allocations);
    perf.speedup_over_global = baseline_time / elapsed;
    
    return perf;
}
```

### Expected Performance Characteristics

Thread-local caches provide:

- **Single Thread**: Minimal overhead, similar to global mutex
- **2-4 Threads**: 2-3x speedup due to reduced contention
- **8-16 Threads**: 4-8x speedup with high cache hit rates
- **Many Threads**: 8-15x speedup, limited by cache refill overhead

The relationship follows:
```
Speedup(threads) ≈ 1 + (Cache_Hit_Rate × Threads × (1 - Contention_Rate))
```

### Cache Tuning

Optimal cache parameters depend on workload characteristics:

```c
typedef struct cache_tuning {
    size_t fill_threshold;
    size_t flush_threshold;
    int size_class_count;
    bool enable_numa;
} cache_tuning_t;

cache_tuning_t tune_cache_for_workload(workload_profile_t* profile) {
    cache_tuning_t tuning = {
        .fill_threshold = CACHE_FILL_THRESHOLD,
        .flush_threshold = CACHE_FLUSH_THRESHOLD,
        .size_class_count = THREAD_CACHE_SIZE_CLASSES,
        .enable_numa = false
    };
    
    /* Adjust based on allocation pattern */
    if (profile->allocation_size_avg < 128) {
        /* Small allocations - prefer more size classes */
        tuning.size_class_count = 20;
        tuning.fill_threshold = 8;
    }
    
    if (profile->allocation_rate > 1000000) {
        /* High rate - larger caches */
        tuning.fill_threshold = 16;
        tuning.flush_threshold = 64;
    }
    
    if (profile->numa_nodes > 1) {
        tuning.enable_numa = true;
    }
    
    return tuning;
}
```

## Cache Memory Overhead

Thread-local caches consume additional memory to store cached blocks. We must balance cache size against memory efficiency.

### Overhead Calculation

Cache overhead includes:
- Per-thread cache structure: ~1KB
- Cached free blocks: varies by cache size
- Metadata per cached block: minimal

Total overhead per thread:
```
Cache_Overhead = Cache_Structure_Size + 
                 (Cached_Blocks × Block_Size) +
                 (Cached_Blocks × Metadata_Size)
```

### Adaptive Cache Sizing

We adjust cache size based on available memory:

```c
void adjust_cache_size_for_memory_pressure(void) {
    size_t available_memory = get_available_system_memory();
    size_t thread_count = get_active_thread_count();
    
    size_t memory_per_thread = available_memory / (thread_count + 1);
    size_t max_cache_size = memory_per_thread / 10;  /* 10% per thread */
    
    if (max_cache_size < CACHE_FLUSH_THRESHOLD * 64) {
        /* Reduce cache size under memory pressure */
        reduce_cache_flush_threshold(max_cache_size / 64);
    }
}
```

## Testing Thread-Local Caches

Comprehensive testing verifies cache correctness and performance:

```c
void test_cache_correctness(void) {
    const int threads = 4;
    const int allocations = 1000;
    
    pthread_t thread_ids[threads];
    
    /* Launch threads */
    for (int i = 0; i < threads; i++) {
        pthread_create(&thread_ids[i], NULL, 
                      cache_correctness_worker, 
                      (void*)(intptr_t)allocations);
    }
    
    /* Wait for completion */
    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }
    
    /* Verify no memory leaks */
    assert(validate_heap_integrity());
    assert(get_total_allocated() == 0);
}

void* cache_correctness_worker(void* arg) {
    int count = (int)(intptr_t)arg;
    void* ptrs[count];
    
    /* Allocate from cache */
    for (int i = 0; i < count; i++) {
        ptrs[i] = malloc_with_thread_cache(64);
        assert(ptrs[i] != NULL);
    }
    
    /* Free to cache */
    for (int i = 0; i < count; i++) {
        free_with_thread_cache(ptrs[i]);
    }
    
    return NULL;
}
```

## Summary

This chapter implemented thread-local optimization for memory allocation:

1. **Thread-Local Caches**: Per-thread free block pools reducing lock contention
2. **Size Class Optimization**: Fast cache lookups using size-based bucketing
3. **Cache Coherency**: Flush strategies maintaining consistency with global heap
4. **NUMA Awareness**: Memory placement optimization for multi-node systems
5. **Performance Scaling**: Measured improvements in multi-threaded scenarios

Thread-local caches provide significant performance improvements for multi-threaded applications while maintaining correctness and memory efficiency.
