# Chapter 02: Block Structure and Alignment Mathematics

Having established the architectural foundation in Chapter 01, we now implement the fundamental building blocks of our allocator: the block header structure and the mathematical operations that ensure proper memory alignment. This chapter transforms theoretical concepts into working C code.

## Block Header Implementation

The block header serves as the control structure for every allocation in our system. Its design must balance metadata completeness with memory efficiency while maintaining strict alignment requirements.

### Core Header Definition

```c
#include <stdint.h>
#include <stddef.h>

#define MAGIC_NUMBER 0xDEADBEEF
#define ALIGNMENT 16
#define HEADER_SIZE sizeof(block_t)

typedef struct block {
    size_t size;           // Size of user data area (excluding header)
    uint32_t is_free;      // 0 = allocated, 1 = free  
    uint32_t magic;        // Magic number for corruption detection
    
    // Free list pointers (only valid when is_free == 1)
    struct block* prev_free;
    struct block* next_free;
} block_t;
```

### Memory Layout Verification

The compiler must arrange our structure to satisfy alignment requirements. Let's verify the layout mathematically:

```c
#include <assert.h>

void verify_block_layout(void) {
    // Verify total structure size is 16-byte aligned
    assert(sizeof(block_t) % ALIGNMENT == 0);
    
    // Verify critical field offsets
    assert(offsetof(block_t, size) == 0);
    assert(offsetof(block_t, is_free) == 8);
    assert(offsetof(block_t, magic) == 12);
    assert(offsetof(block_t, prev_free) == 16);
    assert(offsetof(block_t, next_free) == 24);
    
    // Total size should be 32 bytes (16 + 16 for free list pointers)
    assert(sizeof(block_t) == 32);
}
```

### Block State Management

Each block exists in one of two states, with different memory layout implications:

**Allocated Block**:
- Uses only first 16 bytes of header
- Free list pointers are undefined and may contain user data
- Magic number must remain intact for integrity checking

**Free Block**:
- Uses full 32-byte header structure
- Free list pointers maintain doubly-linked list invariants
- User data area is available for temporary storage

## Alignment Mathematics Implementation

Memory alignment calculations form the performance-critical path of our allocator. We implement these operations using efficient bit manipulation techniques.

### Primary Alignment Function

```c
static inline size_t align_size(size_t size) {
    // For 16-byte alignment: (size + 15) & ~15
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}
```

### Mathematical Verification

Let's prove this formula works correctly for our 16-byte alignment requirement:

For alignment boundary $a = 16 = 2^4$:
- Alignment mask: $a - 1 = 15 = 0x0F$ 
- Inverse mask: $\sim(a - 1) = 0xFFFFFFF0$

The formula $(size + 15) \& 0xFFFFFFF0$ effectively:
1. Adds 15 to round up to next multiple of 16
2. Clears the low 4 bits to enforce 16-byte boundary

### Alignment Verification Tests

```c
void test_alignment_calculations(void) {
    // Test boundary cases
    assert(align_size(0) == 0);
    assert(align_size(1) == 16);
    assert(align_size(15) == 16);
    assert(align_size(16) == 16);
    assert(align_size(17) == 32);
    assert(align_size(31) == 32);
    assert(align_size(32) == 32);
    
    // Test larger sizes
    assert(align_size(100) == 112);  // (100 + 15) & ~15 = 115 & ~15 = 112
    assert(align_size(1000) == 1008); // (1000 + 15) & ~15 = 1015 & ~15 = 1008
    
    // Verify all results are 16-byte aligned
    for (size_t i = 1; i <= 1000; i++) {
        size_t aligned = align_size(i);
        assert(aligned % ALIGNMENT == 0);
        assert(aligned >= i);
        assert(aligned < i + ALIGNMENT);
    }
}
```

## Address Arithmetic and Pointer Manipulation

Converting between user pointers and block headers requires precise address arithmetic that maintains alignment invariants.

### User Pointer to Header Conversion

```c
static inline block_t* get_block_from_ptr(void* ptr) {
    if (!ptr) return NULL;
    
    // Header is exactly one header_size before user data
    return (block_t*)((char*)ptr - HEADER_SIZE);
}
```

### Header to User Pointer Conversion

```c
static inline void* get_ptr_from_block(block_t* block) {
    if (!block) return NULL;
    
    // User data begins immediately after header
    return (void*)((char*)block + HEADER_SIZE);
}
```

### Address Validation

```c
static bool is_aligned(void* ptr) {
    return ((uintptr_t)ptr % ALIGNMENT) == 0;
}

static bool validate_block_address(block_t* block) {
    // Block header must be 16-byte aligned
    if (!is_aligned(block)) {
        return false;
    }
    
    // User pointer must also be 16-byte aligned
    void* user_ptr = get_ptr_from_block(block);
    if (!is_aligned(user_ptr)) {
        return false;
    }
    
    return true;
}
```

## Block Initialization and Management

Every block requires proper initialization to maintain allocator invariants and enable error detection.

### Allocated Block Initialization

```c
static void initialize_allocated_block(block_t* block, size_t size) {
    block->size = size;
    block->is_free = 0;
    block->magic = MAGIC_NUMBER;
    
    // Free list pointers are undefined in allocated blocks
    // They may be overwritten by user data
}
```

### Free Block Initialization

```c
static void initialize_free_block(block_t* block, size_t size) {
    block->size = size;
    block->is_free = 1;
    block->magic = MAGIC_NUMBER;
    
    // Initialize free list pointers to NULL
    // They will be set by list management functions
    block->prev_free = NULL;
    block->next_free = NULL;
}
```

### Block Integrity Verification

```c
typedef enum {
    BLOCK_VALID,
    BLOCK_CORRUPT_MAGIC,
    BLOCK_INVALID_SIZE,
    BLOCK_MISALIGNED,
    BLOCK_INVALID_FREE_STATE
} block_status_t;

static block_status_t verify_block_integrity(block_t* block) {
    // Check alignment
    if (!validate_block_address(block)) {
        return BLOCK_MISALIGNED;
    }
    
    // Check magic number
    if (block->magic != MAGIC_NUMBER) {
        return BLOCK_CORRUPT_MAGIC;
    }
    
    // Check size alignment
    if (block->size % ALIGNMENT != 0) {
        return BLOCK_INVALID_SIZE;
    }
    
    // Check free flag validity
    if (block->is_free != 0 && block->is_free != 1) {
        return BLOCK_INVALID_FREE_STATE;
    }
    
    return BLOCK_VALID;
}
```

## Memory Layout Calculations

Understanding the relationship between requested size, aligned size, and total memory footprint is crucial for allocator efficiency.

### Total Memory Calculation

For a user request of size $s$, the total memory consumed is:

$$\text{Total Memory} = \text{HEADER\_SIZE} + \text{align\_size}(s)$$

```c
static size_t calculate_total_size(size_t requested_size) {
    size_t aligned_user_size = align_size(requested_size);
    return HEADER_SIZE + aligned_user_size;
}
```

### Minimum Allocation Size

Our allocator enforces a minimum allocation size to ensure free blocks can store list pointers:

```c
#define MIN_ALLOC_SIZE (sizeof(void*) * 2)  // Space for prev_free and next_free

static size_t get_minimum_size(size_t requested_size) {
    size_t aligned_size = align_size(requested_size);
    return (aligned_size < MIN_ALLOC_SIZE) ? MIN_ALLOC_SIZE : aligned_size;
}
```

### Block Splitting Calculations

When splitting a free block, we must ensure both resulting blocks meet minimum size requirements:

```c
static bool can_split_block(block_t* block, size_t needed_size) {
    size_t total_size = block->size;
    size_t remaining_size = total_size - needed_size;
    
    // Remaining space must accommodate header + minimum allocation
    return remaining_size >= (HEADER_SIZE + MIN_ALLOC_SIZE);
}

static size_t calculate_split_remainder(block_t* block, size_t needed_size) {
    if (!can_split_block(block, needed_size)) {
        return 0;  // Cannot split
    }
    
    return block->size - needed_size;
}
```

## Block Navigation and Adjacency

Determining physical adjacency between blocks enables coalescing operations that reduce fragmentation.

### Next Block Calculation

```c
static block_t* get_next_block(block_t* block) {
    if (!block) return NULL;
    
    // Next block starts after current block's header and data
    char* next_addr = (char*)block + HEADER_SIZE + block->size;
    return (block_t*)next_addr;
}
```

### Previous Block Detection

Finding the previous block requires additional metadata or heap traversal. We'll implement a boundary tag approach in later chapters. For now, we define the interface:

```c
static block_t* get_prev_block(block_t* block) {
    // Implementation depends on boundary tag system
    // Will be completed in Chapter 05 (Coalescing)
    return NULL;  // Placeholder
}
```

### Adjacency Testing

```c
static bool blocks_are_adjacent(block_t* first, block_t* second) {
    if (!first || !second) return false;
    
    block_t* expected_next = get_next_block(first);
    return expected_next == second;
}
```

## Performance Optimizations

Block structure operations occur in the critical path of every allocation and must be highly optimized.

### Compiler Optimization Hints

```c
// Mark frequently called functions for inlining
#define FORCE_INLINE __attribute__((always_inline)) inline

FORCE_INLINE size_t align_size_fast(size_t size) {
    // Use compiler intrinsics when available
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

// Predict branch outcomes for better performance
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

static block_status_t verify_block_integrity_fast(block_t* block) {
    // Most blocks are valid in well-behaved programs
    if (LIKELY(block->magic == MAGIC_NUMBER)) {
        if (LIKELY(block->is_free <= 1)) {
            return BLOCK_VALID;
        }
        return BLOCK_INVALID_FREE_STATE;
    }
    return BLOCK_CORRUPT_MAGIC;
}
```

### Memory Prefetching

```c
static void prefetch_next_block(block_t* block) {
    if (block && block->size > 0) {
        block_t* next = get_next_block(block);
        __builtin_prefetch(next, 0, 1);  // Prefetch for read
    }
}
```

## Comprehensive Testing Framework

Robust testing ensures our block structure implementation maintains invariants under all conditions.

### Property-Based Testing

```c
#include <time.h>
#include <stdlib.h>

void test_alignment_properties(void) {
    srand(time(NULL));
    
    for (int i = 0; i < 10000; i++) {
        size_t random_size = rand() % 10000 + 1;
        size_t aligned = align_size(random_size);
        
        // Property 1: Result is always aligned
        assert(aligned % ALIGNMENT == 0);
        
        // Property 2: Result is at least as large as input
        assert(aligned >= random_size);
        
        // Property 3: Result is minimal (within one alignment boundary)
        assert(aligned < random_size + ALIGNMENT);
    }
}

void test_pointer_conversion_properties(void) {
    // Test round-trip conversion property
    for (int i = 0; i < 1000; i++) {
        size_t size = align_size(rand() % 1000 + 1);
        block_t* block = aligned_alloc(ALIGNMENT, HEADER_SIZE + size);
        
        initialize_allocated_block(block, size);
        
        void* user_ptr = get_ptr_from_block(block);
        block_t* recovered_block = get_block_from_ptr(user_ptr);
        
        // Round-trip conversion must be exact
        assert(recovered_block == block);
        
        free(block);
    }
}
```

### Edge Case Testing

```c
void test_edge_cases(void) {
    // Test zero size (implementation defined behavior)
    size_t zero_aligned = align_size(0);
    assert(zero_aligned == 0);
    
    // Test maximum representable size
    size_t max_size = SIZE_MAX - ALIGNMENT + 1;
    size_t max_aligned = align_size(max_size);
    assert(max_aligned == SIZE_MAX - (SIZE_MAX % ALIGNMENT));
    
    // Test alignment boundary values
    for (size_t i = 1; i <= ALIGNMENT * 2; i++) {
        size_t aligned = align_size(i);
        assert(aligned % ALIGNMENT == 0);
    }
}
```

## Summary

This chapter established the concrete implementation of our block structure with mathematically sound alignment operations. Key achievements:

1. **Robust Block Header**: 32-byte structure supporting both allocated and free states
2. **Efficient Alignment**: Bit manipulation providing $O(1)$ alignment calculations  
3. **Address Arithmetic**: Precise pointer conversions maintaining alignment invariants
4. **Integrity Checking**: Multiple validation layers detecting common corruption patterns
5. **Performance Optimization**: Compiler hints and prefetching for critical path operations

The block structure now provides a solid foundation for implementing allocation algorithms. In Chapter 03, we'll examine how to acquire memory from the kernel using our hybrid `sbrk()` and `mmap()` strategy.

---

**Next**: [Chapter 03: Memory Sourcing Strategies](03-memory-sourcing.md)
**Previous**: [Chapter 01: Memory Fundamentals and Architecture Design](01-memory-fundamentals.md)