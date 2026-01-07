# Chapter 05: Advanced Free with Coalescing

The `free()` function represents the most complex part of memory allocator design. Beyond simply returning memory to the system, it must detect corruption, prevent double-free attempts, and most critically, implement coalescing to merge adjacent free blocks. This chapter develops a sophisticated free implementation that maintains heap health through aggressive defragmentation.

## The Mathematics of Memory Fragmentation

Memory fragmentation occurs when free memory exists but cannot satisfy allocation requests due to size or location constraints. Understanding fragmentation mathematically guides our coalescing strategy.

### External Fragmentation Analysis

External fragmentation occurs when total free memory exceeds the largest contiguous block:

$$\text{External Fragmentation} = \frac{\text{Total Free Memory} - \text{Largest Free Block}}{\text{Total Free Memory}}$$

For $n$ free blocks of sizes $s_1, s_2, \ldots, s_n$:

$$\text{Fragmentation Ratio} = 1 - \frac{\max(s_i)}{\sum_{i=1}^{n} s_i}$$

### Coalescing Effectiveness

Coalescing combines adjacent free blocks to create larger contiguous regions. The effectiveness depends on spatial locality of deallocations:

$$\text{Coalescing Opportunity} = \frac{\text{Adjacent Free Pairs}}{\text{Total Free Blocks}}$$

Optimal coalescing achieves:
- **Immediate Coalescing**: $O(1)$ time complexity per free operation
- **Bidirectional Merging**: Merge with both previous and next adjacent blocks
- **Boundary Tag System**: Enable constant-time previous block detection

## Boundary Tag Implementation

To achieve $O(1)$ coalescing, we need constant-time access to both next and previous physical blocks. The boundary tag system places size information at both the beginning and end of each block.

### Extended Block Structure

```c
typedef struct block {
    size_t size;           /* Size of user data area */
    uint32_t is_free;      /* 0 = allocated, 1 = free */
    uint32_t magic;        /* Magic number for integrity */
    
    /* Free list pointers (only valid when is_free == 1) */
    struct block* prev_free;
    struct block* next_free;
    
    /* Boundary tag at end of block - contains size again */
    /* Located at: (char*)block + HEADER_SIZE + size - sizeof(size_t) */
} block_t;

#define FOOTER_SIZE sizeof(size_t)
#define MIN_BLOCK_SIZE (HEADER_SIZE + MIN_ALLOC_SIZE + FOOTER_SIZE)
```

### Footer Management

```c
static void set_footer(block_t* block) {
    if (!block || block->size < FOOTER_SIZE) return;
    
    /* Footer is located at end of user data area */
    char* footer_addr = (char*)block + HEADER_SIZE + block->size - FOOTER_SIZE;
    size_t* footer = (size_t*)footer_addr;
    *footer = block->size;
}

static size_t get_footer(block_t* block) {
    if (!block || block->size < FOOTER_SIZE) return 0;
    
    char* footer_addr = (char*)block + HEADER_SIZE + block->size - FOOTER_SIZE;
    size_t* footer = (size_t*)footer_addr;
    return *footer;
}

static bool verify_footer(block_t* block) {
    return get_footer(block) == block->size;
}
```

### Previous Block Detection

```c
block_t* get_prev_block(block_t* block) {
    if (!block) return NULL;
    
    /* Check if we're at the heap start */
    if ((char*)block <= (char*)heap.heap_start) {
        return NULL;
    }
    
    /* Previous block's footer immediately precedes our header */
    char* prev_footer_addr = (char*)block - sizeof(size_t);
    size_t* prev_footer = (size_t*)prev_footer_addr;
    size_t prev_size = *prev_footer;
    
    /* Calculate previous block header location */
    char* prev_block_addr = (char*)block - HEADER_SIZE - prev_size;
    block_t* prev_block = (block_t*)prev_block_addr;
    
    /* Verify integrity */
    if (verify_block_integrity(prev_block) != BLOCK_VALID) {
        return NULL;
    }
    
    if (!verify_footer(prev_block)) {
        return NULL;
    }
    
    return prev_block;
}
```

## Advanced Free Implementation

### Core Free Function

```c
void free(void* ptr) {
    if (!ptr) return;  /* free(NULL) is safe */
    
    /* Convert user pointer to block header */
    block_t* block = get_block_from_ptr(ptr);
    
    /* Comprehensive integrity verification */
    if (!validate_free_request(block, ptr)) {
        return;  /* Error handling performed in validation */
    }
    
    /* Update heap statistics */
    pthread_mutex_lock(&heap.heap_mutex);
    heap.total_allocated -= block->size;
    heap.allocation_count--;
    
    /* Convert to free block */
    initialize_free_block(block, block->size);
    set_footer(block);
    
    /* Perform immediate coalescing */
    block_t* coalesced_block = coalesce_blocks(block);
    
    /* Add final coalesced block to free list */
    add_to_free_list(coalesced_block);
    
    heap.total_free += coalesced_block->size;
    pthread_mutex_unlock(&heap.heap_mutex);
    
    /* Handle mmap allocations separately */
    if (is_mmap_allocation(ptr)) {
        handle_mmap_free(ptr);
    }
}
```

### Comprehensive Validation

```c
static bool validate_free_request(block_t* block, void* ptr) {
    /* Pointer alignment check */
    if (!IS_ALIGNED(ptr)) {
        heap_error_handler(ALLOC_ERROR_MISALIGNED, 
                          "free(): misaligned pointer", ptr);
        return false;
    }
    
    /* Heap bounds validation */
    if (!is_valid_heap_pointer(ptr)) {
        heap_error_handler(ALLOC_ERROR_INVALID_POINTER,
                          "free(): pointer not in heap", ptr);
        return false;
    }
    
    /* Block integrity verification */
    block_status_t status = verify_block_integrity(block);
    if (status != BLOCK_VALID) {
        switch (status) {
            case BLOCK_CORRUPT_MAGIC:
                heap_error_handler(ALLOC_ERROR_CORRUPTION,
                                 "free(): buffer overflow detected", ptr);
                break;
            case BLOCK_INVALID_FREE_STATE:
                heap_error_handler(ALLOC_ERROR_DOUBLE_FREE,
                                 "free(): double free detected", ptr);
                break;
            default:
                heap_error_handler(ALLOC_ERROR_CORRUPTION,
                                 "free(): block corruption", ptr);
        }
        return false;
    }
    
    /* Double-free detection */
    if (block->is_free) {
        heap_error_handler(ALLOC_ERROR_DOUBLE_FREE,
                          "free(): double free attempt", ptr);
        return false;
    }
    
    /* Footer integrity (if using boundary tags) */
    if (!verify_footer(block)) {
        heap_error_handler(ALLOC_ERROR_CORRUPTION,
                          "free(): footer corruption detected", ptr);
        return false;
    }
    
    return true;
}
```

### Error Handling System

```c
typedef void (*error_handler_t)(alloc_error_t, const char*, void*);
static error_handler_t user_error_handler = NULL;

void set_error_handler(error_handler_t handler) {
    user_error_handler = handler;
}

static void heap_error_handler(alloc_error_t error, const char* message, void* ptr) {
    /* Log error details */
    fprintf(stderr, "HEAP ERROR: %s at address %p\n", message, ptr);
    fprintf(stderr, "Error code: %s\n", get_error_string(error));
    
    /* Call user handler if set */
    if (user_error_handler) {
        user_error_handler(error, message, ptr);
        return;
    }
    
    /* Default behavior: print diagnostics and abort */
    print_heap_diagnostics(ptr);
    
    /* For production, might want to gracefully handle instead of abort */
    #ifdef DEBUG
        abort();  /* Immediate crash for debugging */
    #else
        /* Graceful handling - mark heap as corrupted */
        heap.corrupted = true;
        last_error = error;
    #endif
}
```

## Bidirectional Coalescing Algorithm

### Coalescing State Machine

Coalescing examines four possible states based on the freedom of previous and next blocks:

1. **Case 1**: Previous and next both allocated → No coalescing
2. **Case 2**: Previous allocated, next free → Forward coalesce
3. **Case 3**: Previous free, next allocated → Backward coalesce  
4. **Case 4**: Previous and next both free → Bidirectional coalesce

### Implementation

```c
block_t* coalesce_blocks(block_t* block) {
    assert(block && block->is_free);
    
    block_t* prev_block = get_prev_block(block);
    block_t* next_block = get_next_block(block);
    
    bool prev_free = (prev_block && prev_block->is_free);
    bool next_free = (next_block && next_block->is_free);
    
    if (!prev_free && !next_free) {
        /* Case 1: No coalescing possible */
        return block;
    }
    else if (!prev_free && next_free) {
        /* Case 2: Forward coalesce with next block */
        return coalesce_forward(block, next_block);
    }
    else if (prev_free && !next_free) {
        /* Case 3: Backward coalesce with previous block */
        return coalesce_backward(prev_block, block);
    }
    else {
        /* Case 4: Bidirectional coalesce */
        return coalesce_bidirectional(prev_block, block, next_block);
    }
}
```

### Forward Coalescing

```c
static block_t* coalesce_forward(block_t* current, block_t* next) {
    assert(current && next && current->is_free && next->is_free);
    
    /* Remove next block from free list */
    remove_from_free_list(next);
    
    /* Expand current block to include next block */
    size_t total_size = current->size + HEADER_SIZE + next->size;
    current->size = total_size;
    
    /* Update footer */
    set_footer(current);
    
    /* Clear next block header for safety */
    memset(next, 0xDD, HEADER_SIZE);  /* Debug pattern */
    
    return current;
}
```

### Backward Coalescing

```c
static block_t* coalesce_backward(block_t* prev, block_t* current) {
    assert(prev && current && prev->is_free && current->is_free);
    
    /* Expand previous block to include current block */
    size_t total_size = prev->size + HEADER_SIZE + current->size;
    prev->size = total_size;
    
    /* Update footer */
    set_footer(prev);
    
    /* Clear current block header for safety */
    memset(current, 0xDD, HEADER_SIZE);
    
    return prev;
}
```

### Bidirectional Coalescing

```c
static block_t* coalesce_bidirectional(block_t* prev, block_t* current, block_t* next) {
    assert(prev && current && next);
    assert(prev->is_free && current->is_free && next->is_free);
    
    /* Remove next block from free list (prev will remain) */
    remove_from_free_list(next);
    
    /* Calculate total size of all three blocks */
    size_t total_size = prev->size + HEADER_SIZE + current->size + HEADER_SIZE + next->size;
    prev->size = total_size;
    
    /* Update footer */
    set_footer(prev);
    
    /* Clear merged block headers for safety */
    memset(current, 0xDD, HEADER_SIZE);
    memset(next, 0xDD, HEADER_SIZE);
    
    return prev;
}
```

## Advanced Coalescing Strategies

### Deferred Coalescing

For high-frequency allocation patterns, immediate coalescing may cause overhead. Deferred coalescing batches operations:

```c
typedef struct deferred_free {
    void* ptr;
    struct deferred_free* next;
} deferred_free_t;

static deferred_free_t* deferred_free_list = NULL;
static size_t deferred_free_count = 0;
static const size_t MAX_DEFERRED_FREES = 100;

void free_deferred(void* ptr) {
    if (!ptr) return;
    
    /* Add to deferred list */
    deferred_free_t* entry = malloc(sizeof(deferred_free_t));
    entry->ptr = ptr;
    entry->next = deferred_free_list;
    deferred_free_list = entry;
    deferred_free_count++;
    
    /* Process batch when threshold reached */
    if (deferred_free_count >= MAX_DEFERRED_FREES) {
        process_deferred_frees();
    }
}

static void process_deferred_frees(void) {
    /* Sort deferred frees by address for optimal coalescing */
    qsort_deferred_frees();
    
    /* Process in address order */
    deferred_free_t* current = deferred_free_list;
    while (current) {
        free(current->ptr);  /* Use immediate free with coalescing */
        deferred_free_t* next = current->next;
        free(current);
        current = next;
    }
    
    deferred_free_list = NULL;
    deferred_free_count = 0;
}
```

### Coalescing with Size Classes

For allocators using size classes, coalescing must consider alignment with class boundaries:

```c
static block_t* coalesce_with_size_classes(block_t* block) {
    block_t* coalesced = coalesce_blocks(block);
    
    /* Check if coalesced block should be split to match size classes */
    size_t coalesced_size = coalesced->size;
    
    /* Find optimal size class alignment */
    int size_class = get_optimal_size_class(coalesced_size);
    size_t class_size = get_class_size(size_class);
    
    if (coalesced_size > class_size * 2) {
        /* Split large coalesced block */
        block_t* remainder = split_block(coalesced, class_size);
        if (remainder) {
            add_to_free_list(remainder);
        }
    }
    
    return coalesced;
}
```

## Performance Analysis and Optimization

### Time Complexity Analysis

| Operation | Without Coalescing | With Immediate Coalescing |
|-----------|-------------------|---------------------------|
| free() | $O(1)$ | $O(1)$ |
| Coalescing | N/A | $O(1)$ per operation |
| Memory scan | $O(n)$ periodically | Not required |

### Space Overhead of Boundary Tags

Boundary tags add space overhead but enable $O(1)$ coalescing:

**Traditional Approach**: 16-byte header per block
**Boundary Tag Approach**: 16-byte header + 8-byte footer = 24 bytes per block

**Overhead Analysis**:
- Small allocations (≤64 bytes): 37.5% overhead
- Medium allocations (256 bytes): 9.4% overhead  
- Large allocations (≥1KB): <2.4% overhead

### Optimization Techniques

#### Footer Elimination for Allocated Blocks

```c
typedef struct optimized_block {
    size_t size;
    uint32_t is_free;
    uint32_t magic;
    
    /* Footer only exists for free blocks */
    /* For allocated blocks, user data starts immediately */
    union {
        struct {
            struct optimized_block* prev_free;
            struct optimized_block* next_free;
            /* Footer at end: size_t size_copy */
        } free_data;
        
        char user_data[1];  /* Flexible array member */
    } payload;
} optimized_block_t;
```

#### Aggressive Coalescing Heuristics

```c
static bool should_coalesce_aggressively(void) {
    pthread_mutex_lock(&heap.heap_mutex);
    
    /* Calculate fragmentation metrics */
    double fragmentation_ratio = calculate_fragmentation_ratio();
    size_t largest_free_block = find_largest_free_block();
    size_t average_allocation_size = heap.total_allocated / 
                                    (heap.allocation_count + 1);
    
    pthread_mutex_unlock(&heap.heap_mutex);
    
    /* Aggressive coalescing conditions */
    return fragmentation_ratio > 0.25 ||                    /* >25% fragmented */
           largest_free_block < average_allocation_size * 4 || /* Small free blocks */
           heap.total_free > heap.total_allocated;             /* More free than used */
}
```

## Testing Coalescing Correctness

### Unit Tests for Coalescing Logic

```c
void test_forward_coalescing(void) {
    /* Allocate three adjacent blocks */
    void* ptr1 = malloc(64);
    void* ptr2 = malloc(64);  
    void* ptr3 = malloc(64);
    
    assert(ptr1 && ptr2 && ptr3);
    
    /* Free middle block first */
    free(ptr2);
    
    /* Free first block - should coalesce forward with middle */
    free(ptr1);
    
    /* Verify coalescing occurred by checking free list */
    block_t* free_block = find_free_block(128);
    assert(free_block != NULL);
    assert(free_block->size >= 128);
    
    free(ptr3);
}

void test_bidirectional_coalescing(void) {
    /* Create fragmentation pattern */
    void* ptrs[5];
    for (int i = 0; i < 5; i++) {
        ptrs[i] = malloc(64);
    }
    
    /* Free outer blocks */
    free(ptrs[0]);
    free(ptrs[2]); 
    free(ptrs[4]);
    
    /* Free middle block - should trigger bidirectional coalescing */
    free(ptrs[1]);
    
    /* Should coalesce ptrs[0] and ptrs[1] */
    block_t* coalesced = find_free_block(128);
    assert(coalesced && coalesced->size >= 128);
    
    free(ptrs[3]);
}
```

### Stress Testing Coalescing

```c
void stress_test_coalescing(void) {
    const int iterations = 10000;
    void** allocations = malloc(iterations * sizeof(void*));
    
    /* Phase 1: Random allocation pattern */
    for (int i = 0; i < iterations; i++) {
        size_t size = (rand() % 1000) + 1;
        allocations[i] = malloc(size);
        assert(allocations[i] != NULL);
    }
    
    /* Phase 2: Random deallocation creating fragmentation */
    for (int i = 0; i < iterations; i += 2) {
        free(allocations[i]);
        allocations[i] = NULL;
    }
    
    /* Phase 3: Deallocate remaining - test coalescing under pressure */
    for (int i = 1; i < iterations; i += 2) {
        free(allocations[i]);
    }
    
    /* Verify heap is efficiently coalesced */
    allocator_stats();
    
    free(allocations);
}
```

### Property-Based Testing

```c
void property_test_coalescing_invariants(void) {
    const int test_runs = 1000;
    
    for (int run = 0; run < test_runs; run++) {
        /* Generate random allocation/deallocation sequence */
        int operations = rand() % 100 + 10;
        void** ptrs = malloc(operations * sizeof(void*));
        memset(ptrs, 0, operations * sizeof(void*));
        
        for (int i = 0; i < operations; i++) {
            if (rand() % 2 && ptrs[i] == NULL) {
                /* Allocate */
                size_t size = (rand() % 500) + 1;
                ptrs[i] = malloc(size);
            } else if (ptrs[i] != NULL) {
                /* Free */
                free(ptrs[i]);
                ptrs[i] = NULL;
            }
            
            /* Verify heap consistency after each operation */
            heap_consistency_check();
        }
        
        /* Clean up remaining allocations */
        for (int i = 0; i < operations; i++) {
            if (ptrs[i]) free(ptrs[i]);
        }
        
        free(ptrs);
    }
}
```

## Debugging and Diagnostics

### Coalescing Visualization

```c
void print_coalescing_stats(void) {
    pthread_mutex_lock(&heap.heap_mutex);
    
    printf("=== Coalescing Statistics ===\n");
    printf("Forward coalesces: %zu\n", heap.stats.forward_coalesces);
    printf("Backward coalesces: %zu\n", heap.stats.backward_coalesces);
    printf("Bidirectional coalesces: %zu\n", heap.stats.bidirectional_coalesces);
    printf("Total free operations: %zu\n", heap.stats.total_frees);
    
    double coalesce_rate = (double)(heap.stats.forward_coalesces + 
                                   heap.stats.backward_coalesces +
                                   heap.stats.bidirectional_coalesces) / 
                          (double)heap.stats.total_frees;
    
    printf("Coalescing rate: %.2f%%\n", coalesce_rate * 100.0);
    
    pthread_mutex_unlock(&heap.heap_mutex);
}

void visualize_heap_fragmentation(void) {
    const int scale = 64;  /* Each character represents 64 bytes */
    
    printf("Heap Layout (. = allocated, - = free, | = boundary):\n");
    
    block_t* current = (block_t*)heap.heap_start;
    while (current < (block_t*)heap.heap_end) {
        int blocks = (current->size + scale - 1) / scale;
        char symbol = current->is_free ? '-' : '.';
        
        for (int i = 0; i < blocks; i++) {
            printf("%c", symbol);
        }
        printf("|");
        
        current = get_next_block(current);
        if (!current) break;
    }
    printf("\n");
}
```

## Summary

This chapter implemented a sophisticated free operation with immediate bidirectional coalescing:

1. **Boundary Tag System**: Enables $O(1)$ coalescing through constant-time previous block detection
2. **Comprehensive Validation**: Multiple layers of error detection including corruption and double-free
3. **Bidirectional Coalescing**: Four-case analysis handling all adjacent block combinations
4. **Performance Optimization**: Deferred coalescing and size-class awareness for high-frequency patterns
5. **Robust Testing**: Unit tests, stress tests, and property-based verification of coalescing invariants

The advanced free implementation significantly reduces heap fragmentation while maintaining optimal performance characteristics. In Chapter 06, we'll explore allocation strategy alternatives and their impact on overall allocator performance.

---

**Next**: [Chapter 06: Allocation Strategies Analysis](06-allocation-strategies.md)
**Previous**: [Chapter 04: Basic malloc Implementation](04-basic-malloc.md)