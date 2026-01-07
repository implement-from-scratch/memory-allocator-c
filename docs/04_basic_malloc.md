# Chapter 04: Basic malloc Implementation

Having established the memory sourcing infrastructure in Chapter 03, we now implement the core `malloc()` function that applications will use to request memory. This chapter focuses on the allocation algorithm, block management, and the critical path optimizations that determine allocator performance.

## The malloc() Algorithm Overview

The `malloc()` function must transform a size request into a properly aligned, valid memory pointer while maintaining allocator invariants. The algorithm follows this sequence:

1. **Input Validation**: Verify size parameters and handle edge cases
2. **Size Alignment**: Calculate aligned size respecting minimum allocation requirements
3. **Free Block Search**: Attempt to satisfy request from existing free blocks
4. **Block Management**: Split oversized blocks and update metadata
5. **Memory Acquisition**: Request new memory from kernel if no suitable free block exists
6. **Pointer Return**: Convert block header to user pointer with integrity checks

### Mathematical Foundation

For a user request of size $s$, the total memory footprint is:

$$\text{Total Memory} = \text{HEADER\_SIZE} + \text{align\_16}(\max(s, \text{MIN\_ALLOC\_SIZE}))$$

The alignment function ensures all allocations respect 16-byte boundaries:

$$\text{align\_16}(s) = \left\lceil \frac{s}{16} \right\rceil \times 16 = (s + 15) \land \neg 15$$

## Implementation Architecture

### Core malloc() Function

```c
void* malloc(size_t size) {
    /* Initialize allocator on first use - lazy initialization */
    if (UNLIKELY(!allocator_initialized)) {
        if (allocator_init() != 0) {
            last_error = ALLOC_ERROR_OUT_OF_MEMORY;
            return NULL;
        }
    }
    
    /* Handle zero-size allocation per C standard */
    if (size == 0) {
        return NULL;  /* Implementation-defined behavior */
    }
    
    /* Ensure minimum allocation size for free list pointers */
    size_t actual_size = (size < MIN_ALLOC_SIZE) ? MIN_ALLOC_SIZE : size;
    size_t aligned_size = ALIGN_SIZE(actual_size);
    
    /* Fast path: attempt allocation from free list */
    block_t* block = find_free_block(aligned_size);
    
    if (LIKELY(block != NULL)) {
        return allocate_from_free_block(block, aligned_size);
    }
    
    /* Slow path: acquire new memory from kernel */
    return allocate_new_memory(aligned_size);
}
```

### Free Block Search Algorithm

The allocator implements First-Fit search strategy for optimal average-case performance:

```c
block_t* find_free_block(size_t size) {
    pthread_mutex_lock(&heap.heap_mutex);
    
    /* Traverse free list for first suitable block */
    block_t* current = heap.free_head;
    block_t* best_fit = NULL;
    
    while (current) {
        /* Verify block integrity during traversal */
        if (UNLIKELY(verify_block_integrity(current) != BLOCK_VALID)) {
            heap_corruption_handler(current);
            pthread_mutex_unlock(&heap.heap_mutex);
            return NULL;
        }
        
        if (current->size >= size) {
            best_fit = current;
            break;  /* First-fit strategy */
        }
        
        current = current->next_free;
    }
    
    pthread_mutex_unlock(&heap.heap_mutex);
    return best_fit;
}
```

### Advanced Search Strategies

For performance-critical applications, we can implement alternative search strategies:

#### Best-Fit Search
```c
block_t* find_free_block_best_fit(size_t size) {
    pthread_mutex_lock(&heap.heap_mutex);
    
    block_t* current = heap.free_head;
    block_t* best_fit = NULL;
    size_t smallest_waste = SIZE_MAX;
    
    while (current) {
        if (current->size >= size) {
            size_t waste = current->size - size;
            if (waste < smallest_waste) {
                smallest_waste = waste;
                best_fit = current;
                
                /* Perfect fit - no waste */
                if (waste == 0) break;
            }
        }
        current = current->next_free;
    }
    
    pthread_mutex_unlock(&heap.heap_mutex);
    return best_fit;
}
```

#### Quick-Fit for Common Sizes
```c
#define QUICK_FIT_SIZES 8
static block_t* quick_fit_lists[QUICK_FIT_SIZES];
static const size_t quick_fit_boundaries[] = {16, 32, 48, 64, 96, 128, 192, 256};

block_t* find_free_block_quick_fit(size_t size) {
    /* Check quick-fit lists for exact size matches */
    for (int i = 0; i < QUICK_FIT_SIZES; i++) {
        if (size <= quick_fit_boundaries[i] && quick_fit_lists[i]) {
            block_t* block = quick_fit_lists[i];
            quick_fit_lists[i] = block->next_free;
            return block;
        }
    }
    
    /* Fall back to general free list */
    return find_free_block(size);
}
```

## Block Management Operations

### Allocation from Free Block

```c
static void* allocate_from_free_block(block_t* block, size_t size) {
    /* Remove block from free list */
    remove_from_free_list(block);
    
    /* Determine if block should be split */
    if (can_split_block(block, size)) {
        block_t* remainder = split_block(block, size);
        if (remainder) {
            add_to_free_list(remainder);
        }
    }
    
    /* Initialize allocated block */
    initialize_allocated_block(block, size);
    
    /* Update heap statistics */
    pthread_mutex_lock(&heap.heap_mutex);
    heap.total_allocated += size;
    heap.allocation_count++;
    pthread_mutex_unlock(&heap.heap_mutex);
    
    return get_ptr_from_block(block);
}
```

### Block Splitting Logic

Block splitting reduces internal fragmentation by creating a new free block from excess space:

```c
block_t* split_block(block_t* block, size_t needed_size) {
    assert(can_split_block(block, needed_size));
    
    /* Calculate split point */
    void* split_addr = (char*)block + HEADER_SIZE + needed_size;
    block_t* new_block = (block_t*)split_addr;
    
    /* Calculate remaining size after accounting for new header */
    size_t remaining_size = block->size - needed_size - HEADER_SIZE;
    
    /* Initialize new free block */
    initialize_free_block(new_block, remaining_size);
    
    /* Update original block size */
    block->size = needed_size;
    
    return new_block;
}
```

### Split Feasibility Analysis

Mathematical analysis determines when splitting is beneficial:

```c
bool can_split_block(block_t* block, size_t needed_size) {
    if (!block || block->size <= needed_size) {
        return false;
    }
    
    /* Remaining space after allocation */
    size_t remaining = block->size - needed_size;
    
    /* Must have space for header + minimum payload */
    size_t minimum_remainder = HEADER_SIZE + MIN_ALLOC_SIZE;
    
    return remaining >= minimum_remainder;
}
```

The split threshold analysis:
- **Header Overhead**: 32 bytes (block_t structure)  
- **Minimum Payload**: 16 bytes (space for free list pointers)
- **Total Threshold**: 48 bytes minimum remainder to justify split

## Memory Acquisition Path

### New Memory Allocation

```c
static void* allocate_new_memory(size_t size) {
    /* Calculate total memory needed including header */
    size_t total_size = HEADER_SIZE + size;
    
    /* Acquire memory using hybrid sourcing strategy */
    void* memory = acquire_memory(total_size);
    if (!memory) {
        return NULL;
    }
    
    /* Initialize block header at start of memory */
    block_t* block = (block_t*)memory;
    initialize_allocated_block(block, size);
    
    /* Update heap statistics */
    pthread_mutex_lock(&heap.heap_mutex);
    heap.total_allocated += size;
    heap.allocation_count++;
    pthread_mutex_unlock(&heap.heap_mutex);
    
    return get_ptr_from_block(block);
}
```

### Memory Source Selection

The hybrid strategy automatically selects optimal memory source:

```c
void* acquire_memory(size_t total_size) {
    last_error = ALLOC_SUCCESS;
    
    /* Large allocations bypass heap and use mmap directly */
    if (total_size >= MMAP_THRESHOLD) {
        return acquire_memory_mmap(total_size);
    }
    
    /* Check heap fragmentation state */
    if (should_use_mmap_for_small_allocation(total_size)) {
        void* mmap_memory = acquire_memory_mmap(total_size);
        if (mmap_memory) {
            return mmap_memory;
        }
        /* Fall through to sbrk on mmap failure */
    }
    
    /* Default to sbrk for small allocations */
    return acquire_memory_sbrk(total_size);
}
```

## Performance Optimizations

### Critical Path Optimization

The malloc fast path must be highly optimized since it executes for every allocation:

```c
/* Optimized malloc with inlined fast path */
FORCE_INLINE void* malloc_fast_path(size_t size) {
    /* Skip initialization check in hot path - assume initialized */
    if (UNLIKELY(size == 0)) return NULL;
    
    size_t aligned_size = ALIGN_SIZE(size);
    
    /* Thread-local cache check (Chapter 10) */
    if (LIKELY(thread_cache && aligned_size <= 1024)) {
        void* cached = cache_alloc_fast(aligned_size);
        if (cached) return cached;
    }
    
    /* Inline free list head check */
    block_t* head = heap.free_head;
    if (LIKELY(head && head->size >= aligned_size)) {
        if (LIKELY(pthread_mutex_trylock(&heap.heap_mutex) == 0)) {
            /* Re-check under lock */
            if (heap.free_head && heap.free_head->size >= aligned_size) {
                return allocate_from_free_block_fast(heap.free_head, aligned_size);
            }
            pthread_mutex_unlock(&heap.heap_mutex);
        }
    }
    
    /* Fall back to full malloc implementation */
    return malloc_slow_path(aligned_size);
}
```

### Branch Prediction Optimization

```c
/* Provide hints to compiler about likely execution paths */
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/* Example usage in critical paths */
if (LIKELY(block->magic == MAGIC_NUMBER)) {
    /* Common case: valid block */
    return process_valid_block(block);
} else {
    /* Rare case: corruption detected */
    return handle_corruption(block);
}
```

### Memory Prefetching

```c
static void prefetch_free_list(block_t* block) {
    /* Prefetch next few blocks in free list */
    for (int i = 0; i < 3 && block; i++) {
        __builtin_prefetch(block, 0, 1);  /* Read prefetch, low locality */
        block = block->next_free;
    }
}
```

## Error Handling and Edge Cases

### Zero-Size Allocation Handling

Different implementations handle zero-size allocations differently:

```c
void* malloc_zero_size_variants(size_t size) {
    if (size == 0) {
        /* Option 1: Return NULL (our choice) */
        return NULL;
        
        /* Option 2: Return unique non-NULL pointer */
        // static char zero_size_marker;
        // return &zero_size_marker;
        
        /* Option 3: Allocate minimum size */
        // size = MIN_ALLOC_SIZE;
    }
    
    /* Continue with normal allocation */
    return malloc_implementation(size);
}
```

### Size Overflow Detection

```c
bool check_size_overflow(size_t size) {
    /* Check if size + header would overflow */
    if (size > SIZE_MAX - HEADER_SIZE) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        return true;
    }
    
    /* Check if alignment calculation would overflow */
    if (size > SIZE_MAX - ALIGNMENT + 1) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        return true;
    }
    
    return false;
}
```

### Memory Pressure Handling

```c
static void* handle_allocation_failure(size_t size) {
    /* Attempt emergency memory reclamation */
    if (emergency_memory_reclaim()) {
        /* Retry allocation after cleanup */
        return malloc(size);
    }
    
    /* Log allocation failure for monitoring */
    log_allocation_failure(size, get_memory_pressure_level());
    
    last_error = ALLOC_ERROR_OUT_OF_MEMORY;
    return NULL;
}
```

## Comprehensive Testing Framework

### Unit Tests for Core Functionality

```c
#include <assert.h>

void test_basic_malloc_functionality(void) {
    /* Test normal allocation */
    void* ptr1 = malloc(64);
    assert(ptr1 != NULL);
    assert(IS_ALIGNED(ptr1));
    
    /* Test zero allocation */
    void* ptr2 = malloc(0);
    assert(ptr2 == NULL);
    
    /* Test large allocation */
    void* ptr3 = malloc(256 * 1024);
    assert(ptr3 != NULL);
    
    /* Verify allocations are distinct */
    assert(ptr1 != ptr3);
    
    free(ptr1);
    free(ptr3);
}

void test_alignment_properties(void) {
    for (size_t size = 1; size <= 1000; size++) {
        void* ptr = malloc(size);
        assert(ptr != NULL);
        assert(IS_ALIGNED(ptr));
        
        /* Write to allocated memory to verify accessibility */
        memset(ptr, 0xAA, size);
        
        free(ptr);
    }
}

void test_free_list_management(void) {
    /* Allocate multiple blocks */
    void* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = malloc(64);
        assert(ptrs[i] != NULL);
    }
    
    /* Free every other block */
    for (int i = 0; i < 10; i += 2) {
        free(ptrs[i]);
    }
    
    /* Verify free blocks can be reused */
    void* reused = malloc(64);
    assert(reused != NULL);
    
    /* One of the freed blocks should be reused */
    bool found_reuse = false;
    for (int i = 0; i < 10; i += 2) {
        if (reused == ptrs[i]) {
            found_reuse = true;
            break;
        }
    }
    assert(found_reuse);
    
    /* Clean up remaining allocations */
    for (int i = 1; i < 10; i += 2) {
        free(ptrs[i]);
    }
    free(reused);
}
```

### Stress Testing

```c
void stress_test_malloc_performance(void) {
    const int iterations = 100000;
    const size_t max_size = 1024;
    void** allocations = calloc(iterations, sizeof(void*));
    
    /* Time allocation phase */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        size_t size = (rand() % max_size) + 1;
        allocations[i] = malloc(size);
        assert(allocations[i] != NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double alloc_time = (end.tv_sec - start.tv_sec) + 
                       (end.tv_nsec - start.tv_nsec) / 1e9;
    
    /* Time deallocation phase */
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        free(allocations[i]);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double free_time = (end.tv_sec - start.tv_sec) + 
                      (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("Allocation performance: %.2f μs per malloc\n", 
           alloc_time * 1e6 / iterations);
    printf("Deallocation performance: %.2f μs per free\n", 
           free_time * 1e6 / iterations);
    
    free(allocations);
}
```

## Performance Characteristics Analysis

### Time Complexity

| Operation | Average Case | Worst Case | Notes |
|-----------|--------------|------------|-------|
| malloc() | $O(1)$ | $O(n)$ | n = number of free blocks |
| Block splitting | $O(1)$ | $O(1)$ | Constant time operation |
| Free list insertion | $O(1)$ | $O(1)$ | Insert at head |
| Memory acquisition | $O(1)$ | $O(1)$ | System call overhead |

### Space Overhead Analysis

For an allocation of user size $s$:

**Header Overhead**: 32 bytes per allocation
**Internal Fragmentation**: Up to 15 bytes due to 16-byte alignment  
**External Fragmentation**: Varies with allocation pattern

**Overhead Percentage**:
$$\text{Overhead} = \frac{32 + \text{padding}}{\text{total\_allocation}} \times 100\%$$

Example calculations:
- 8-byte allocation: $\frac{32 + 8}{32 + 16} = 83.3\%$ overhead
- 64-byte allocation: $\frac{32 + 0}{32 + 64} = 33.3\%$ overhead  
- 1024-byte allocation: $\frac{32 + 0}{32 + 1024} = 3.0\%$ overhead

## Integration with Memory Sourcing

The malloc implementation seamlessly integrates with our hybrid memory sourcing:

```c
/* Memory source transparency */
void demonstrate_source_integration(void) {
    /* Small allocation - uses sbrk pool */
    void* small = malloc(1024);          /* ≈ 1KB */
    
    /* Large allocation - uses mmap directly */  
    void* large = malloc(256 * 1024);    /* 256KB */
    
    /* Both appear identical to application */
    assert(small != NULL);
    assert(large != NULL);
    assert(IS_ALIGNED(small));
    assert(IS_ALIGNED(large));
    
    /* But use different underlying mechanisms */
    memory_region_t* small_region = find_memory_region(small);
    memory_region_t* large_region = find_memory_region(large);
    
    assert(!small_region->is_mmap);  /* sbrk allocation */
    assert(large_region->is_mmap);   /* mmap allocation */
    
    free(small);
    free(large);
}
```

## Summary

This chapter implemented a robust `malloc()` function with the following key features:

1. **Efficient Algorithm**: First-fit search with $O(1)$ average case performance
2. **Smart Block Management**: Automatic splitting and free list maintenance  
3. **Hybrid Integration**: Seamless integration with sbrk/mmap memory sourcing
4. **Performance Optimization**: Branch prediction hints and critical path optimization
5. **Comprehensive Testing**: Unit tests and stress tests ensuring reliability
6. **Error Handling**: Graceful handling of edge cases and error conditions

The malloc implementation provides a solid foundation for memory allocation. In Chapter 05, we'll implement the `free()` function with advanced coalescing algorithms to minimize fragmentation and maximize memory reuse efficiency.

---

**Next**: [Chapter 05: Advanced Free with Coalescing](05-coalescing-free.md)
**Previous**: [Chapter 03: Memory Sourcing Strategies](03-memory-sourcing.md)