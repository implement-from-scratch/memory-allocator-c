# Chapter 01: Memory Fundamentals and Architecture Design

The foundation of any memory allocator rests on understanding how programs interact with system memory and the mathematical constraints that govern efficient allocation. Before we write a single line of code, we need to establish the architectural principles that will guide our implementation.

## Virtual Memory and the Process Address Space

When your program calls `malloc(100)`, the request travels through multiple layers before physical RAM is allocated. Modern operating systems provide each process with a virtual address space, typically 48 bits on x86_64 systems, giving us $2^{48} = 256$ terabytes of addressable memory per process.

The process address space follows a standard layout:

```
High Addresses (0x7FFFFFFFFFFF)
+---------------------------+
| Kernel Space             |  <- Not accessible to user programs
+---------------------------+
| Stack (grows downward)   |  <- Local variables, function calls
|            |             |
|            v             |
+---------------------------+
| Memory Mapping Segment   |  <- mmap(), shared libraries
+---------------------------+
| Heap (grows upward)      |  <- malloc(), our focus
|            ^             |
|            |             |
+---------------------------+
| BSS Segment              |  <- Uninitialized global variables
+---------------------------+
| Data Segment             |  <- Initialized global variables
+---------------------------+
| Text Segment             |  <- Program code (read-only)
+---------------------------+
Low Addresses (0x000000000000)
```

Our allocator will manage memory in two distinct regions:

1. **Heap Segment**: Traditional `sbrk()` managed memory growing upward from BSS
2. **Memory Mapping Segment**: `mmap()` anonymous pages for large allocations

## The Mathematics of Memory Alignment

Memory alignment is not merely an optimization; it's a requirement for correctness on many architectures. When the CPU accesses memory, it operates most efficiently when addresses are aligned to natural boundaries.

### Alignment Requirements

For a data type of size n bytes, the address must be a multiple of n:

```
address ≡ 0 (mod n)
```

Common alignment requirements:
- `char`: 1-byte alignment (no requirement)
- `short`: 2-byte alignment
- `int`: 4-byte alignment
- `long`: 8-byte alignment
- `double`: 8-byte alignment

Our allocator enforces 16-byte alignment for all allocations, satisfying the strictest requirement of common data types and enabling SIMD instruction usage.

### Alignment Calculation

To align a size s to boundary a, we use the formula:

```
aligned_size = ⌈s/a⌉ × a
```

The efficient bit manipulation implementation:

```
aligned_size = (s + a - 1) & ~(a - 1)
```

For 16-byte alignment where a = 16 = 2^4:

```
aligned_size = (s + 15) & ~15
aligned_size = (s + 15) & 0xFFFFFFF0
```

### Example Alignment Calculations

| Requested Size | Calculation | Aligned Size |
|----------------|-------------|--------------|
| 1 byte | (1 + 15) & 0xFFF0 = 16 & 0xFFF0 | 16 bytes |
| 23 bytes | (23 + 15) & 0xFFF0 = 38 & 0xFFF0 | 32 bytes |
| 64 bytes | (64 + 15) & 0xFFF0 = 79 & 0xFFF0 | 64 bytes |

## Memory Acquisition Strategies

The allocator must request memory from the kernel using system calls. The choice between `sbrk()` and `mmap()` involves fundamental tradeoffs in performance, fragmentation, and memory management complexity.

### sbrk(): The Traditional Approach

The `sbrk()` system call adjusts the program break, which marks the end of the data segment. Memory allocated via `sbrk()` is contiguous and forms the traditional heap:

```c
void* sbrk(intptr_t increment);
```

**Advantages**:
- **Contiguous Memory**: All heap allocations are physically adjacent
- **Low System Call Overhead**: Single call can provide memory for many allocations
- **Automatic Return to System**: Memory automatically returns when program terminates
- **Cache Locality**: Adjacent allocations have good spatial locality

**Disadvantages**:
- **Fragmentation Risk**: Cannot return memory to OS until all higher addresses are freed
- **Size Limitations**: Constrained by available virtual address space in heap segment
- **Thread Safety**: Global program break requires synchronization

### mmap(): Anonymous Memory Mapping

The `mmap()` system call creates anonymous memory mappings that exist independently of the traditional heap:

```c
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
```

For anonymous allocations:
```c
void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
```

**Advantages**:
- **Immediate Return**: Memory returns to system immediately upon `munmap()`
- **No Fragmentation**: Each allocation is independent
- **Large Allocation Support**: Can allocate memory limited only by virtual address space
- **Memory Protection**: Can set per-page protection attributes

**Disadvantages**:
- **System Call Overhead**: Each allocation requires kernel interaction
- **Address Space Fragmentation**: Mappings scattered throughout virtual address space
- **Minimum Size**: Page-aligned allocations (typically 4KB minimum)

### Hybrid Strategy: The Best of Both Worlds

Our allocator employs a hybrid approach that leverages the strengths of both mechanisms:

```
Memory Source = {
    sbrk()  if size < 128 KB
    mmap()  if size ≥ 128 KB
}
```

**Rationale for 128KB Threshold**:
- **Page Alignment**: 128KB = 32 pages (assuming 4KB pages), well-suited for `mmap()`
- **Overhead Amortization**: System call overhead becomes negligible for large allocations
- **Fragmentation Mitigation**: Large allocations won't prevent smaller heap memory from returning to OS
- **Cache Behavior**: Large allocations likely to exhibit poor cache locality regardless of placement

## Block Management Architecture

Every allocation requires metadata to track its size, status, and relationships with other blocks. Our design uses a header-based approach where metadata precedes user data.

### Block Header Structure

Each allocated block begins with a 16-byte header:

```c
typedef struct block {
    size_t size;           // Size of user data area (excluding header)
    uint32_t is_free;      // 0 = allocated, 1 = free
    uint32_t magic;        // Magic number for corruption detection (0xDEADBEEF)

    // Free blocks extend with additional fields:
    struct block* prev_free;  // Previous block in free list
    struct block* next_free;  // Next block in free list
} block_t;
```

### Memory Layout Visualization

```
Allocated Block:
+------------------+ <- Returned to user
| User Data        |
| (size bytes)     |
+------------------+ <- Header address + sizeof(block_t)
| next_free (8B)   |    ^
| prev_free (8B)   |    | Only present in
+------------------+    | free blocks
| magic (4B)       |    |
| is_free (4B)     |    |
| size (8B)        |    v
+------------------+ <- Header address (16-byte aligned)

Free Block:
+------------------+
| Unused Data      |
| (size bytes)     |
+------------------+ <- Header address + sizeof(block_t) + 16
| next_free (8B)   |
| prev_free (8B)   |
+------------------+ <- Header address + sizeof(block_t)
| magic (4B)       |
| is_free (4B)     |
| size (8B)        |
+------------------+ <- Header address (16-byte aligned)
```

### Address Calculation

For a block header at address h, the user data begins at:

```
user_ptr = h + sizeof(block_t)
```

Given a user pointer p, we recover the header:

```
header_ptr = p - sizeof(block_t)
```

### Free List Management

Free blocks form a doubly-linked list to enable constant-time insertion and removal:

```
Free List Structure:
+--------+     +--------+     +--------+     +--------+
| Block1 | <-> | Block2 | <-> | Block3 | <-> | Block4 |
|  32B   |     |  64B   |     |  16B   |     | 128B   |
+--------+     +--------+     +--------+     +--------+
     ^                                            ^
     |                                            |
free_head                                    (NULL)
```

## Error Detection and Heap Integrity

Memory corruption bugs are among the most challenging to debug in systems programming. Our allocator incorporates multiple layers of protection:

### Magic Number Protection

Each block header contains a magic number `0xDEADBEEF`. Buffer overflows that write past the end of an allocation will corrupt the magic number of the subsequent block, enabling detection during the next allocation or free operation.

### Double-Free Detection

The `is_free` flag in each header enables detection of double-free attempts:

```c
if (block->is_free) {
    fprintf(stderr, "Double free detected at %p\n", ptr);
    abort();
}
```

### Boundary Validation

Before freeing a pointer, we verify it falls within the range of memory we've allocated:

```c
bool is_valid_pointer(void* ptr) {
    return (ptr >= heap_start && ptr < heap_end) ||
           is_mmap_allocation(ptr);
}
```

## Performance Considerations

The allocator's performance characteristics depend on fundamental algorithmic choices:

### Time Complexity Analysis

| Operation | Average Case | Worst Case | Determining Factor |
|-----------|--------------|------------|--------------------|
| malloc() | $O(1)$ | $O(n)$ | Free list traversal |
| free() | $O(1)$ | $O(1)$ | Direct header access |
| coalesce() | $O(1)$ | $O(1)$ | Fixed number of neighbors |

### Space Overhead

For an allocation of size s, the total memory footprint is:

```
Total Memory = sizeof(block_t) + align_16(s)
```

The overhead percentage:

```
Overhead = (sizeof(block_t) / (sizeof(block_t) + align_16(s))) × 100%
```

For small allocations, overhead dominates:
- 8-byte allocation: 16/(16 + 16) = 50% overhead
- 64-byte allocation: 16/(16 + 64) = 20% overhead  
- 1KB allocation: 16/(16 + 1024) ≈ 1.5% overhead

## Architecture Summary

Our memory allocator implements these core architectural decisions:

1. **Hybrid Memory Sourcing**: `sbrk()` for allocations under 128KB, `mmap()` for larger requests
2. **16-Byte Alignment**: Satisfies all common data type requirements
3. **Header-Based Metadata**: 16-byte headers with size, flags, and magic numbers
4. **Doubly-Linked Free Lists**: Constant-time insertion and removal
5. **Immediate Coalescing**: Merge adjacent free blocks to reduce fragmentation
6. **Multiple Error Detection**: Magic numbers, double-free detection, boundary validation

This foundation provides the mathematical rigor and architectural clarity necessary for building a production-quality allocator.
