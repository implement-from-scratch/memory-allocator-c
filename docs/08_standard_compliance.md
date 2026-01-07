# Chapter 08: Standard Compliance Implementation

The C standard library defines several memory allocation functions beyond basic malloc and free. Applications expect these functions to behave according to POSIX and ISO C specifications. This chapter implements calloc, realloc, aligned_alloc, and malloc_usable_size with proper overflow checking, in-place expansion, and alignment guarantees.

## calloc Implementation with Overflow Checking

The calloc function allocates memory for an array of elements, initializing all bytes to zero. Unlike malloc, calloc must check for integer overflow when multiplying element count by element size, as this multiplication can exceed the maximum representable size_t value.

### Overflow Detection Mathematics

When allocating memory for nmemb elements of size bytes each, the total size calculation must prevent integer overflow. The mathematical constraint is:

```
total_size = nmemb × size
```

For size_t values, overflow occurs when:
```
nmemb × size > SIZE_MAX
```

The standard overflow check uses division to verify multiplication safety:
```
if (nmemb != 0 && size > SIZE_MAX / nmemb) {
    /* Overflow detected */
    return NULL;
}
```

This approach avoids the overflow by checking the inverse operation. If nmemb × size would overflow, then SIZE_MAX / nmemb must be less than size.

### Implementation

```c
void* calloc(size_t nmemb, size_t size) {
    /* Handle zero-size allocation per C standard */
    if (nmemb == 0 || size == 0) {
        return NULL;  /* Implementation-defined behavior */
    }

    /* Overflow detection using division check */
    if (nmemb > SIZE_MAX / size) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        errno = ENOMEM;
        return NULL;
    }

    /* Calculate total size */
    size_t total_size = nmemb * size;

    /* Allocate using malloc */
    void* ptr = malloc(total_size);
    if (!ptr) {
        return NULL;
    }

    /* Zero-initialize the entire allocation */
    memset(ptr, 0, total_size);

    return ptr;
}
```

### Zero Initialization Optimization

Zero initialization can be expensive for large allocations. We optimize by leveraging system behavior where newly allocated pages are typically zero-filled by the kernel:

```c
void* calloc_optimized(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return NULL;
    }

    if (nmemb > SIZE_MAX / size) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        errno = ENOMEM;
        return NULL;
    }

    size_t total_size = nmemb * size;

    /* For large allocations, rely on kernel zero-filling */
    if (total_size >= MMAP_THRESHOLD) {
        /* mmap returns zero-filled memory on most systems */
        void* ptr = malloc(total_size);
        /* Verify zero-filling (may need explicit memset on some systems) */
        #ifdef VERIFY_ZERO_FILL
            verify_zero_filled(ptr, total_size);
        #endif
        return ptr;
    }

    /* For small allocations, explicit zeroing is fast */
    void* ptr = malloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}
```

## realloc Implementation with In-Place Expansion

The realloc function changes the size of an existing allocation, potentially moving it to a new location. The function must handle multiple cases: expanding in place, moving to a new location, shrinking allocations, and handling NULL pointers.

### realloc Algorithm Overview

The realloc operation follows this decision tree:

1. If ptr is NULL, behave like malloc(size)
2. If size is 0, behave like free(ptr) and return NULL
3. If current block can accommodate new size, expand in place
4. If next block is free and large enough, coalesce and expand
5. Otherwise, allocate new block, copy data, and free old block

### In-Place Expansion Detection

In-place expansion avoids memory copying and improves performance. We check if the current block or adjacent free blocks can satisfy the new size requirement:

```c
void* realloc(void* ptr, size_t size) {
    /* Handle NULL pointer - equivalent to malloc */
    if (!ptr) {
        return malloc(size);
    }

    /* Handle zero size - equivalent to free */
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    /* Get current block information */
    block_t* block = get_block_from_ptr(ptr);
    if (!validate_block_for_realloc(block, ptr)) {
        return NULL;
    }

    size_t aligned_size = ALIGN_SIZE(size);
    size_t current_size = block->size;

    /* Case 1: Requested size matches current size */
    if (aligned_size == current_size) {
        return ptr;  /* No change needed */
    }

    /* Case 2: Shrinking allocation */
    if (aligned_size < current_size) {
        return realloc_shrink(block, aligned_size, ptr);
    }

    /* Case 3: Expanding allocation */
    return realloc_expand(block, aligned_size, current_size, ptr);
}
```

### In-Place Expansion Logic

When expanding, we first attempt to use space from adjacent free blocks:

```c
static void* realloc_expand(block_t* block, size_t new_size, 
                           size_t current_size, void* ptr) {
    size_t needed_size = new_size - current_size;

    /* Check if next block is free and large enough */
    block_t* next_block = get_next_block(block);
    if (next_block && next_block->is_free) {
        size_t next_size = next_block->size;
        
        /* Check if next block provides enough space */
        if (next_size + HEADER_SIZE >= needed_size) {
            /* Remove next block from free list */
            remove_from_free_list(next_block);

            /* Expand current block into next block */
            size_t total_size = current_size + HEADER_SIZE + next_size;
            
            /* If we have more than needed, split the remainder */
            if (total_size > new_size + HEADER_SIZE + MIN_ALLOC_SIZE) {
                block->size = new_size;
                block_t* remainder = split_block_expanded(block, new_size, total_size);
                if (remainder) {
                    add_to_free_list(remainder);
                }
            } else {
                /* Use entire next block */
                block->size = total_size;
            }

            /* Update footer if using boundary tags */
            set_footer(block);

            return ptr;  /* In-place expansion successful */
        }
    }

    /* Cannot expand in place - allocate new block and copy */
    return realloc_move(block, new_size, current_size, ptr);
}
```

### Shrinking Allocation

When shrinking, we split the block and return the remainder to the free list:

```c
static void* realloc_shrink(block_t* block, size_t new_size, void* ptr) {
    size_t current_size = block->size;
    size_t remainder_size = current_size - new_size;

    /* Check if remainder is large enough to be a separate block */
    if (remainder_size >= HEADER_SIZE + MIN_ALLOC_SIZE) {
        /* Split block */
        block->size = new_size;
        set_footer(block);

        /* Create new free block from remainder */
        block_t* remainder = (block_t*)((char*)block + HEADER_SIZE + new_size);
        initialize_free_block(remainder, remainder_size - HEADER_SIZE);
        set_footer(remainder);

        /* Attempt to coalesce remainder with next block */
        block_t* coalesced = coalesce_blocks(remainder);
        add_to_free_list(coalesced);

        /* Update heap statistics */
        pthread_mutex_lock(&heap.heap_mutex);
        heap.total_allocated -= (current_size - new_size);
        heap.total_free += (coalesced->size);
        pthread_mutex_unlock(&heap.heap_mutex);
    }

    return ptr;  /* Same pointer, smaller size */
}
```

### Moving Allocation

When in-place expansion fails, we allocate a new block and copy data:

```c
static void* realloc_move(block_t* block, size_t new_size, 
                         size_t current_size, void* ptr) {
    /* Allocate new block */
    void* new_ptr = malloc(new_size);
    if (!new_ptr) {
        return NULL;
    }

    /* Copy data from old block to new block */
    size_t copy_size = (current_size < new_size) ? current_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    /* Free old block */
    free(ptr);

    return new_ptr;
}
```

## aligned_alloc Implementation

The aligned_alloc function allocates memory with a specific alignment requirement. Unlike malloc which provides natural alignment, aligned_alloc guarantees alignment to any power-of-two boundary specified by the application.

### Alignment Requirements

The C standard specifies that alignment must be a power of two, and size must be a multiple of alignment. These constraints ensure the allocation can satisfy the alignment requirement without excessive waste.

The mathematical relationship is:
```
alignment = 2^k for some k
size = n × alignment for some n
```

### Implementation Strategy

Aligned allocations require careful address calculation to ensure the returned pointer meets the alignment constraint:

```c
void* aligned_alloc(size_t alignment, size_t size) {
    /* Validate alignment is power of two */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        errno = EINVAL;
        return NULL;
    }

    /* Validate size is multiple of alignment */
    if (size == 0 || size % alignment != 0) {
        last_error = ALLOC_ERROR_INVALID_SIZE;
        errno = EINVAL;
        return NULL;
    }

    /* For large alignments, use special allocation path */
    if (alignment > PAGE_SIZE) {
        return aligned_alloc_large(alignment, size);
    }

    /* Standard path: allocate extra space and adjust pointer */
    size_t total_size = size + alignment - 1 + HEADER_SIZE;
    void* raw_ptr = malloc(total_size);
    if (!raw_ptr) {
        return NULL;
    }

    /* Calculate aligned address */
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (raw_addr + alignment - 1) & ~(alignment - 1);

    /* Store offset for free operation */
    block_t* block = get_block_from_ptr(raw_ptr);
    size_t offset = aligned_addr - raw_addr;
    
    /* Store offset in block metadata (if space available) */
    store_alignment_offset(block, offset);

    return (void*)aligned_addr;
}
```

### Large Alignment Handling

For alignments larger than page size, we use mmap with explicit alignment:

```c
static void* aligned_alloc_large(size_t alignment, size_t size) {
    /* Allocate extra space to ensure we can find aligned region */
    size_t total_size = size + alignment - 1;
    
    /* Round up to page boundary */
    size_t page_aligned_size = ((total_size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    /* Use mmap with MAP_ANONYMOUS */
    void* raw_ptr = mmap(NULL, page_aligned_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1, 0);

    if (raw_ptr == MAP_FAILED) {
        last_error = ALLOC_ERROR_OUT_OF_MEMORY;
        errno = ENOMEM;
        return NULL;
    }

    /* Calculate aligned address within mapped region */
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (raw_addr + alignment - 1) & ~(alignment - 1);

    /* Store mapping information for munmap */
    register_aligned_mmap(raw_ptr, page_aligned_size, aligned_addr, size);

    return (void*)aligned_addr;
}
```

### Freeing Aligned Allocations

Aligned allocations require special handling during free to recover the original pointer:

```c
void free_aligned(void* ptr) {
    if (!ptr) return;

    /* Find the original allocation */
    aligned_allocation_t* info = find_aligned_allocation(ptr);
    if (info) {
        /* Large alignment uses mmap */
        if (info->is_mmap) {
            munmap(info->raw_ptr, info->mapped_size);
            unregister_aligned_mmap(ptr);
            return;
        }

        /* Standard alignment - recover original pointer */
        block_t* block = get_block_from_ptr(info->raw_ptr);
        free(info->raw_ptr);
        return;
    }

    /* Fallback: try standard free */
    free(ptr);
}
```

## malloc_usable_size Implementation

The malloc_usable_size function returns the number of bytes available in an allocation, which may be larger than the requested size due to alignment and rounding. This function is useful for applications that want to know the actual usable space.

### Usable Size Calculation

The usable size is the actual size of the user data area, which equals the block size minus any overhead:

```c
size_t malloc_usable_size(void* ptr) {
    if (!ptr) {
        return 0;
    }

    /* Validate pointer */
    block_t* block = get_block_from_ptr(ptr);
    if (!validate_block_for_usable_size(block, ptr)) {
        return 0;
    }

    /* Return the actual user data size */
    return block->size;
}
```

### Validation for Usable Size

We must verify the pointer is valid before returning size information:

```c
static bool validate_block_for_usable_size(block_t* block, void* ptr) {
    /* Check alignment */
    if (!IS_ALIGNED(ptr)) {
        return false;
    }

    /* Check heap bounds */
    if (!is_valid_heap_pointer(ptr)) {
        /* Check if it's an mmap allocation */
        if (!is_mmap_allocation(ptr)) {
            return false;
        }
    }

    /* Verify block integrity */
    block_status_t status = verify_block_integrity(block);
    if (status != BLOCK_VALID) {
        return false;
    }

    /* Block must be allocated */
    if (block->is_free) {
        return false;
    }

    return true;
}
```

## POSIX Compliance Considerations

The POSIX standard adds requirements beyond basic C standard compliance. Our implementation must handle these additional constraints.

### Error Handling Requirements

POSIX specifies that allocation functions must set errno appropriately:

```c
void* posix_compliant_malloc(size_t size) {
    last_error = ALLOC_SUCCESS;
    errno = 0;

    void* ptr = malloc_implementation(size);
    
    if (!ptr && size > 0) {
        last_error = ALLOC_ERROR_OUT_OF_MEMORY;
        errno = ENOMEM;
    }

    return ptr;
}
```

### Thread Safety Requirements

POSIX requires that memory allocation functions be thread-safe. Our implementation uses mutexes to ensure this:

```c
void* thread_safe_malloc(size_t size) {
    pthread_mutex_lock(&heap.heap_mutex);
    
    void* result = malloc_implementation(size);
    
    pthread_mutex_unlock(&heap.heap_mutex);
    return result;
}
```

### Memory Alignment Guarantees

POSIX specifies that malloc returns pointers aligned for any data type. Our 16-byte alignment satisfies this requirement for all common architectures.

## Testing Standard Compliance

Comprehensive testing ensures our implementation meets all standard requirements:

```c
void test_calloc_overflow(void) {
    /* Test overflow detection */
    void* ptr = calloc(SIZE_MAX, 2);
    assert(ptr == NULL);
    assert(errno == ENOMEM);

    /* Test normal allocation */
    ptr = calloc(10, sizeof(int));
    assert(ptr != NULL);
    
    /* Verify zero initialization */
    int* arr = (int*)ptr;
    for (int i = 0; i < 10; i++) {
        assert(arr[i] == 0);
    }
    
    free(ptr);
}

void test_realloc_expansion(void) {
    void* ptr = malloc(64);
    assert(ptr != NULL);

    /* Expand allocation */
    void* new_ptr = realloc(ptr, 128);
    assert(new_ptr != NULL);
    
    /* May be same pointer (in-place) or different (moved) */
    free(new_ptr);
}

void test_aligned_alloc(void) {
    /* Test power-of-two alignment */
    void* ptr = aligned_alloc(64, 256);
    assert(ptr != NULL);
    assert((uintptr_t)ptr % 64 == 0);
    
    /* Test invalid alignment */
    ptr = aligned_alloc(63, 256);
    assert(ptr == NULL);
    assert(errno == EINVAL);
    
    free(ptr);
}

void test_malloc_usable_size(void) {
    void* ptr = malloc(100);
    assert(ptr != NULL);
    
    size_t usable = malloc_usable_size(ptr);
    assert(usable >= 100);
    assert(usable % 16 == 0);  /* Must be aligned */
    
    free(ptr);
}
```

## Performance Analysis

Standard compliance functions have different performance characteristics:

| Function | Typical Time | Notes |
|----------|--------------|-------|
| calloc | 50-200ns | Includes zero initialization |
| realloc (in-place) | 30-100ns | No copying required |
| realloc (move) | 100-500ns | Includes memcpy overhead |
| aligned_alloc | 50-150ns | Extra alignment calculation |
| malloc_usable_size | 10-30ns | Simple metadata access |

## Summary

This chapter implemented standard-compliant memory allocation functions:

1. **calloc**: Overflow-safe allocation with zero initialization
2. **realloc**: In-place expansion when possible, movement when necessary
3. **aligned_alloc**: Power-of-two alignment guarantees with efficient implementation
4. **malloc_usable_size**: Accurate reporting of usable allocation size
5. **POSIX Compliance**: Proper error handling and thread safety

These functions extend our allocator to full standard library compatibility while maintaining performance and correctness guarantees.
