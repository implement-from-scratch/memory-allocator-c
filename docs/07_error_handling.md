# Chapter 07: Error Handling and Heap Integrity

Memory corruption bugs represent some of the most dangerous and difficult-to-debug issues in systems programming. A production-quality allocator must not only manage memory efficiently but also detect, diagnose, and handle corruption gracefully. This chapter develops comprehensive error detection mechanisms and robust error handling strategies.

## The Mathematics of Memory Corruption

Memory corruption occurs when programs write outside their allocated boundaries or use freed memory. Understanding the mathematical patterns of corruption helps design effective detection mechanisms.

### Corruption Pattern Analysis

Memory corruption follows predictable patterns based on programming errors:

```
Buffer Overflow Probability = f(buffer_size, write_length, boundary_checking)
Use-After-Free Risk = g(object_lifetime, pointer_retention, nullification_discipline)
Double-Free Likelihood = h(ownership_clarity, exception_handling, cleanup_discipline)
```

### Statistical Error Detection

For a heap with N blocks, the probability of detecting corruption depends on:

```
Detection Rate = (Checked Boundaries + Magic Numbers + Metadata Validation) / Total Possible Corruptions
False Positive Rate = Invalid Detections / Total Detections
Coverage = Detected Corruptions / Actual Corruptions
```

## Comprehensive Corruption Detection System

### Multi-Layer Detection Architecture

Our error detection system employs multiple independent layers:

```c
typedef enum {
    CORRUPTION_NONE = 0,
    CORRUPTION_BUFFER_OVERFLOW = 1,
    CORRUPTION_USE_AFTER_FREE = 2,
    CORRUPTION_DOUBLE_FREE = 4,
    CORRUPTION_METADATA_DAMAGE = 8,
    CORRUPTION_HEAP_STRUCTURE = 16,
    CORRUPTION_ALIGNMENT_VIOLATION = 32
} corruption_type_t;

typedef struct corruption_detector {
    /* Detection mechanisms */
    bool enable_magic_numbers;
    bool enable_canary_values;
    bool enable_red_zones;
    bool enable_metadata_checksums;
    bool enable_free_list_validation;
    bool enable_heap_walking;
    
    /* Detection statistics */
    size_t corruptions_detected;
    size_t false_positives;
    size_t detection_overhead_ns;
    
    /* Error reporting callback */
    void (*error_handler)(corruption_type_t, void*, const char*);
} corruption_detector_t;

static corruption_detector_t detector = {
    .enable_magic_numbers = true,
    .enable_canary_values = true,
    .enable_red_zones = false,      /* High memory overhead */
    .enable_metadata_checksums = true,
    .enable_free_list_validation = true,
    .enable_heap_walking = false,   /* High CPU overhead */
    .error_handler = default_corruption_handler
};
```

### Magic Number Protection

Magic numbers provide the first line of defense against buffer overflows:

```c
#define MAGIC_ALLOCATED   0xDEADBEEF
#define MAGIC_FREE        0xFEEDFACE
#define MAGIC_CORRUPTED   0xBADC0FFE
#define MAGIC_CANARY      0xCAFEBABE

typedef struct enhanced_block {
    size_t size;
    uint32_t magic_header;
    uint32_t checksum;
    uint32_t is_free;
    uint32_t allocation_id;    /* Unique identifier */
    
    /* Free list pointers (only valid when is_free) */
    struct enhanced_block* prev_free;
    struct enhanced_block* next_free;
    
    /* Canary value at end of user data */
    /* Located at: (char*)block + HEADER_SIZE + size - sizeof(uint32_t) */
} enhanced_block_t;

/* Calculate checksum for metadata integrity */
uint32_t calculate_block_checksum(enhanced_block_t* block) {
    uint32_t checksum = 0;
    checksum ^= block->size;
    checksum ^= block->magic_header;
    checksum ^= block->is_free;
    checksum ^= block->allocation_id;
    
    /* Simple XOR checksum - could use CRC32 for stronger protection */
    return checksum;
}

void set_block_checksum(enhanced_block_t* block) {
    block->checksum = 0;  /* Clear before calculation */
    block->checksum = calculate_block_checksum(block);
}

bool verify_block_checksum(enhanced_block_t* block) {
    uint32_t stored_checksum = block->checksum;
    block->checksum = 0;
    uint32_t calculated_checksum = calculate_block_checksum(block);
    block->checksum = stored_checksum;
    
    return stored_checksum == calculated_checksum;
}
```

### Advanced Canary Protection

Canaries detect buffer overflows by placing known values at allocation boundaries:

```c
void set_canary_values(enhanced_block_t* block) {
    /* Header canary already set via magic number */
    
    /* Footer canary at end of user data */
    char* user_end = (char*)block + HEADER_SIZE + block->size;
    uint32_t* footer_canary = (uint32_t*)(user_end - sizeof(uint32_t));
    *footer_canary = MAGIC_CANARY;
    
    /* Optional: Set pattern in padding bytes */
    if (detector.enable_red_zones) {
        set_red_zone_pattern(block);
    }
}

corruption_type_t check_canary_values(enhanced_block_t* block) {
    /* Check header magic */
    if (block->magic_header != MAGIC_ALLOCATED && block->magic_header != MAGIC_FREE) {
        return CORRUPTION_METADATA_DAMAGE;
    }
    
    /* Check footer canary */
    char* user_end = (char*)block + HEADER_SIZE + block->size;
    uint32_t* footer_canary = (uint32_t*)(user_end - sizeof(uint32_t));
    
    if (*footer_canary != MAGIC_CANARY) {
        return CORRUPTION_BUFFER_OVERFLOW;
    }
    
    /* Check red zones if enabled */
    if (detector.enable_red_zones && !verify_red_zones(block)) {
        return CORRUPTION_BUFFER_OVERFLOW;
    }
    
    return CORRUPTION_NONE;
}
```

### Red Zone Implementation

Red zones create buffer areas around allocations filled with known patterns:

```c
#define RED_ZONE_SIZE 32
#define RED_ZONE_PATTERN 0xCC

void set_red_zone_pattern(enhanced_block_t* block) {
    /* Pre-red zone (before user data) */
    char* pre_zone = (char*)block + HEADER_SIZE - RED_ZONE_SIZE;
    memset(pre_zone, RED_ZONE_PATTERN, RED_ZONE_SIZE);
    
    /* Post-red zone (after user data) */
    char* post_zone = (char*)block + HEADER_SIZE + block->size;
    memset(post_zone, RED_ZONE_PATTERN, RED_ZONE_SIZE);
}

bool verify_red_zones(enhanced_block_t* block) {
    /* Check pre-red zone */
    char* pre_zone = (char*)block + HEADER_SIZE - RED_ZONE_SIZE;
    for (int i = 0; i < RED_ZONE_SIZE; i++) {
        if (pre_zone[i] != RED_ZONE_PATTERN) {
            return false;
        }
    }
    
    /* Check post-red zone */
    char* post_zone = (char*)block + HEADER_SIZE + block->size;
    for (int i = 0; i < RED_ZONE_SIZE; i++) {
        if (post_zone[i] != RED_ZONE_PATTERN) {
            return false;
        }
    }
    
    return true;
}

size_t calculate_red_zone_overhead(size_t user_size) {
    if (!detector.enable_red_zones) {
        return 0;
    }
    
    return 2 * RED_ZONE_SIZE;  /* Pre and post zones */
}
```

## Use-After-Free Detection

Use-after-free bugs occur when programs access deallocated memory. Detection requires tracking freed memory and monitoring access patterns.

### Quarantine System

Instead of immediately returning freed memory, maintain a quarantine of recently freed blocks:

```c
#define QUARANTINE_SIZE 1024
#define POISON_PATTERN 0xDEADDEAD

typedef struct quarantine_entry {
    void* ptr;
    size_t size;
    uint32_t allocation_id;
    time_t free_time;
    void* stack_trace[8];  /* Capture free() call stack */
} quarantine_entry_t;

typedef struct quarantine_system {
    quarantine_entry_t entries[QUARANTINE_SIZE];
    size_t head;
    size_t count;
    pthread_mutex_t mutex;
    
    /* Statistics */
    size_t total_quarantined;
    size_t use_after_free_detected;
    size_t memory_overhead;
} quarantine_system_t;

static quarantine_system_t quarantine = {0};

void quarantine_freed_block(enhanced_block_t* block) {
    pthread_mutex_lock(&quarantine.mutex);
    
    /* If quarantine is full, release oldest entry */
    if (quarantine.count == QUARANTINE_SIZE) {
        quarantine_entry_t* oldest = &quarantine.entries[quarantine.head];
        actually_free_block(oldest->ptr);
        quarantine.memory_overhead -= oldest->size;
    } else {
        quarantine.count++;
    }
    
    /* Add new entry */
    quarantine_entry_t* entry = &quarantine.entries[quarantine.head];
    entry->ptr = get_ptr_from_block(block);
    entry->size = block->size;
    entry->allocation_id = block->allocation_id;
    entry->free_time = time(NULL);
    capture_stack_trace(entry->stack_trace, 8);
    
    /* Poison the freed memory */
    memset(entry->ptr, POISON_PATTERN, entry->size);
    
    /* Mark block as poisoned */
    block->magic_header = MAGIC_CORRUPTED;
    
    quarantine.head = (quarantine.head + 1) % QUARANTINE_SIZE;
    quarantine.memory_overhead += entry->size;
    quarantine.total_quarantined++;
    
    pthread_mutex_unlock(&quarantine.mutex);
}

bool check_use_after_free(void* ptr) {
    pthread_mutex_lock(&quarantine.mutex);
    
    for (size_t i = 0; i < quarantine.count; i++) {
        quarantine_entry_t* entry = &quarantine.entries[i];
        
        if (ptr >= entry->ptr && 
            ptr < (char*)entry->ptr + entry->size) {
            
            /* Use-after-free detected */
            quarantine.use_after_free_detected++;
            pthread_mutex_unlock(&quarantine.mutex);
            
            report_use_after_free(ptr, entry);
            return true;
        }
    }
    
    pthread_mutex_unlock(&quarantine.mutex);
    return false;
}
```

### Stack Trace Capture

Capturing call stacks helps identify the source of corruption:

```c
#include <execinfo.h>  /* For backtrace() on Linux/macOS */

void capture_stack_trace(void** trace, int max_frames) {
    #ifdef __GNUC__
        int frames = backtrace(trace, max_frames);
        if (frames < max_frames) {
            trace[frames] = NULL;
        }
    #else
        /* Fallback for other compilers */
        trace[0] = __builtin_return_address(0);
        trace[1] = __builtin_return_address(1);
        for (int i = 2; i < max_frames; i++) {
            trace[i] = NULL;
        }
    #endif
}

void print_stack_trace(void** trace, int max_frames) {
    #ifdef __GNUC__
        char** symbols = backtrace_symbols(trace, max_frames);
        if (symbols) {
            for (int i = 0; i < max_frames && trace[i]; i++) {
                fprintf(stderr, "  [%d] %s\n", i, symbols[i]);
            }
            free(symbols);
        }
    #else
        for (int i = 0; i < max_frames && trace[i]; i++) {
            fprintf(stderr, "  [%d] %p\n", i, trace[i]);
        }
    #endif
}
```

## Double-Free Detection

Double-free vulnerabilities occur when programs call free() twice on the same pointer. Detection requires tracking allocation state and free operation history.

### Enhanced Free State Tracking

```c
typedef enum {
    BLOCK_STATE_ALLOCATED = 0,
    BLOCK_STATE_FREE = 1,
    BLOCK_STATE_QUARANTINED = 2,
    BLOCK_STATE_CORRUPTED = 3
} block_state_t;

void enhanced_free(void* ptr) {
    if (!ptr) return;
    
    enhanced_block_t* block = get_enhanced_block_from_ptr(ptr);
    
    /* Comprehensive validation before free */
    corruption_type_t corruption = validate_free_request(block, ptr);
    if (corruption != CORRUPTION_NONE) {
        handle_corruption(corruption, ptr, "free() validation failed");
        return;
    }
    
    /* Check current state */
    if (block->is_free == BLOCK_STATE_FREE) {
        /* Double-free detected */
        report_double_free(ptr, block);
        handle_corruption(CORRUPTION_DOUBLE_FREE, ptr, "double free detected");
        return;
    }
    
    if (block->is_free == BLOCK_STATE_QUARANTINED) {
        /* Attempted to free quarantined block */
        report_quarantine_violation(ptr, block);
        handle_corruption(CORRUPTION_DOUBLE_FREE, ptr, "free of quarantined block");
        return;
    }
    
    /* Record free operation */
    record_free_operation(block, ptr);
    
    /* Update block state */
    block->is_free = BLOCK_STATE_QUARANTINED;
    block->magic_header = MAGIC_CORRUPTED;
    
    /* Add to quarantine */
    quarantine_freed_block(block);
}

void record_free_operation(enhanced_block_t* block, void* ptr) {
    /* Maintain a history of recent free operations */
    static struct {
        void* ptr;
        uint32_t allocation_id;
        time_t free_time;
        void* stack_trace[4];
    } free_history[256];
    
    static size_t history_index = 0;
    
    free_history[history_index].ptr = ptr;
    free_history[history_index].allocation_id = block->allocation_id;
    free_history[history_index].free_time = time(NULL);
    capture_stack_trace(free_history[history_index].stack_trace, 4);
    
    history_index = (history_index + 1) % 256;
}
```

### Free List Integrity Checking

Corruption can damage free list structures, leading to crashes during allocation:

```c
bool validate_free_list_integrity(void) {
    enhanced_block_t* current = (enhanced_block_t*)heap.free_head;
    size_t blocks_traversed = 0;
    size_t total_free_size = 0;
    
    while (current && blocks_traversed < 10000) {  /* Prevent infinite loops */
        /* Validate current block */
        if (!validate_block_metadata(current)) {
            report_free_list_corruption(current, "invalid metadata");
            return false;
        }
        
        /* Check free state consistency */
        if (current->is_free != BLOCK_STATE_FREE) {
            report_free_list_corruption(current, "non-free block in free list");
            return false;
        }
        
        /* Validate forward link */
        if (current->next_free) {
            if (!is_valid_heap_pointer(current->next_free)) {
                report_free_list_corruption(current, "invalid next pointer");
                return false;
            }
            
            /* Check backward link consistency */
            if (current->next_free->prev_free != current) {
                report_free_list_corruption(current, "broken bidirectional links");
                return false;
            }
        }
        
        total_free_size += current->size;
        blocks_traversed++;
        current = current->next_free;
    }
    
    /* Verify total free size matches heap statistics */
    if (total_free_size != heap.total_free) {
        report_heap_inconsistency("free list size mismatch", 
                                 total_free_size, heap.total_free);
        return false;
    }
    
    return true;
}
```

## Heap Walking and Consistency Checking

Comprehensive heap validation walks the entire heap structure to detect inconsistencies:

```c
typedef struct heap_statistics {
    size_t total_blocks;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t total_allocated_size;
    size_t total_free_size;
    size_t largest_free_block;
    size_t heap_utilization_percent;
    size_t fragmentation_percent;
    size_t corruption_detected;
} heap_statistics_t;

heap_statistics_t walk_heap_and_validate(void) {
    heap_statistics_t stats = {0};
    enhanced_block_t* current = (enhanced_block_t*)heap.heap_start;
    
    while ((char*)current < (char*)heap.heap_end) {
        stats.total_blocks++;
        
        /* Validate block structure */
        corruption_type_t corruption = validate_block_structure(current);
        if (corruption != CORRUPTION_NONE) {
            stats.corruption_detected++;
            handle_corruption(corruption, current, "heap walk validation");
            break;  /* Stop walking corrupted heap */
        }
        
        /* Collect statistics */
        if (current->is_free == BLOCK_STATE_FREE) {
            stats.free_blocks++;
            stats.total_free_size += current->size;
            
            if (current->size > stats.largest_free_block) {
                stats.largest_free_block = current->size;
            }
        } else if (current->is_free == BLOCK_STATE_ALLOCATED) {
            stats.allocated_blocks++;
            stats.total_allocated_size += current->size;
        }
        
        /* Move to next block */
        current = get_next_block(current);
        if (!current) break;
    }
    
    /* Calculate derived metrics */
    size_t total_size = stats.total_allocated_size + stats.total_free_size;
    if (total_size > 0) {
        stats.heap_utilization_percent = 
            (stats.total_allocated_size * 100) / total_size;
            
        if (stats.total_free_size > 0) {
            stats.fragmentation_percent = 
                ((stats.total_free_size - stats.largest_free_block) * 100) / 
                stats.total_free_size;
        }
    }
    
    return stats;
}

corruption_type_t validate_block_structure(enhanced_block_t* block) {
    /* Alignment validation */
    if (!IS_ALIGNED(block)) {
        return CORRUPTION_ALIGNMENT_VIOLATION;
    }
    
    /* Magic number validation */
    if (block->magic_header != MAGIC_ALLOCATED && 
        block->magic_header != MAGIC_FREE &&
        block->magic_header != MAGIC_CORRUPTED) {
        return CORRUPTION_METADATA_DAMAGE;
    }
    
    /* Size sanity check */
    if (block->size == 0 || block->size > heap.heap_size) {
        return CORRUPTION_METADATA_DAMAGE;
    }
    
    /* Checksum validation */
    if (detector.enable_metadata_checksums && 
        !verify_block_checksum(block)) {
        return CORRUPTION_METADATA_DAMAGE;
    }
    
    /* Canary validation */
    if (detector.enable_canary_values) {
        corruption_type_t canary_result = check_canary_values(block);
        if (canary_result != CORRUPTION_NONE) {
            return canary_result;
        }
    }
    
    return CORRUPTION_NONE;
}
```

## Error Reporting and Diagnostics

### Comprehensive Error Reporting

```c
typedef struct corruption_report {
    corruption_type_t type;
    void* address;
    enhanced_block_t* block;
    const char* description;
    time_t detection_time;
    void* detection_stack[8];
    void* allocation_stack[8];
    void* free_stack[8];
    
    /* Block information at time of corruption */
    size_t block_size;
    uint32_t allocation_id;
    time_t allocation_time;
    time_t free_time;
} corruption_report_t;

void report_corruption(corruption_type_t type, void* address, 
                      const char* description) {
    corruption_report_t report = {0};
    
    report.type = type;
    report.address = address;
    report.description = description;
    report.detection_time = time(NULL);
    
    /* Capture current stack trace */
    capture_stack_trace(report.detection_stack, 8);
    
    /* Try to get block information */
    enhanced_block_t* block = get_enhanced_block_from_ptr(address);
    if (block && validate_block_metadata(block)) {
        report.block = block;
        report.block_size = block->size;
        report.allocation_id = block->allocation_id;
        
        /* Look up allocation history */
        allocation_record_t* record = find_allocation_record(block->allocation_id);
        if (record) {
            report.allocation_time = record->allocation_time;
            memcpy(report.allocation_stack, record->stack_trace, 
                   sizeof(report.allocation_stack));
        }
    }
    
    /* Generate detailed report */
    print_corruption_report(&report);
    
    /* Save to crash log */
    save_crash_log(&report);
    
    /* Call user error handler */
    if (detector.error_handler) {
        detector.error_handler(type, address, description);
    }
}

void print_corruption_report(corruption_report_t* report) {
    fprintf(stderr, "\n=== HEAP CORRUPTION DETECTED ===\n");
    fprintf(stderr, "Type: %s\n", corruption_type_name(report->type));
    fprintf(stderr, "Address: %p\n", report->address);
    fprintf(stderr, "Description: %s\n", report->description);
    fprintf(stderr, "Detection Time: %s", ctime(&report->detection_time));
    
    if (report->block) {
        fprintf(stderr, "\nBlock Information:\n");
        fprintf(stderr, "  Size: %zu bytes\n", report->block_size);
        fprintf(stderr, "  Allocation ID: %u\n", report->allocation_id);
        fprintf(stderr, "  State: %s\n", block_state_name(report->block->is_free));
        
        if (report->allocation_time > 0) {
            fprintf(stderr, "  Allocated: %s", ctime(&report->allocation_time));
        }
        
        if (report->free_time > 0) {
            fprintf(stderr, "  Freed: %s", ctime(&report->free_time));
        }
    }
    
    fprintf(stderr, "\nDetection Stack Trace:\n");
    print_stack_trace(report->detection_stack, 8);
    
    if (report->allocation_stack[0]) {
        fprintf(stderr, "\nAllocation Stack Trace:\n");
        print_stack_trace(report->allocation_stack, 8);
    }
    
    if (report->free_stack[0]) {
        fprintf(stderr, "\nFree Stack Trace:\n");
        print_stack_trace(report->free_stack, 8);
    }
    
    fprintf(stderr, "\nHeap Statistics:\n");
    heap_statistics_t stats = walk_heap_and_validate();
    print_heap_statistics(&stats);
    
    fprintf(stderr, "=================================\n\n");
}
```

### Recovery Strategies

```c
typedef enum {
    RECOVERY_ABORT,           /* Terminate immediately */
    RECOVERY_CONTINUE,        /* Try to continue execution */
    RECOVERY_QUARANTINE,      /* Isolate corrupted region */
    RECOVERY_RESET_HEAP       /* Reinitialize heap */
} recovery_strategy_t;

recovery_strategy_t determine_recovery_strategy(corruption_type_t type) {
    switch (type) {
        case CORRUPTION_BUFFER_OVERFLOW:
            /* Buffer overflows can corrupt arbitrary memory */
            return RECOVERY_ABORT;
            
        case CORRUPTION_USE_AFTER_FREE:
            /* May be recoverable if caught early */
            return RECOVERY_QUARANTINE;
            
        case CORRUPTION_DOUBLE_FREE:
            /* Usually indicates logic error, but may be recoverable */
            return RECOVERY_CONTINUE;
            
        case CORRUPTION_METADATA_DAMAGE:
            /* Heap structure damage - very dangerous */
            return RECOVERY_ABORT;
            
        case CORRUPTION_HEAP_STRUCTURE:
            /* Complete heap corruption */
            return RECOVERY_RESET_HEAP;
            
        default:
            return RECOVERY_ABORT;
    }
}

void handle_corruption(corruption_type_t type, void* address, 
                      const char* description) {
    /* Always report the corruption */
    report_corruption(type, address, description);
    
    /* Determine recovery strategy */
    recovery_strategy_t strategy = determine_recovery_strategy(type);
    
    switch (strategy) {
        case RECOVERY_ABORT:
            fprintf(stderr, "Fatal corruption detected - aborting\n");
            abort();
            break;
            
        case RECOVERY_CONTINUE:
            fprintf(stderr, "Corruption detected - attempting to continue\n");
            detector.corruptions_detected++;
            break;
            
        case RECOVERY_QUARANTINE:
            fprintf(stderr, "Quarantining corrupted region\n");
            quarantine_corrupted_region(address);
            break;
            
        case RECOVERY_RESET_HEAP:
            fprintf(stderr, "Resetting heap due to severe corruption\n");
            emergency_heap_reset();
            break;
    }
}
```

## Performance Impact Analysis

### Overhead Measurement

```c
typedef struct detection_overhead {
    uint64_t magic_number_checks_ns;
    uint64_t canary_validation_ns;
    uint64_t checksum_calculation_ns;
    uint64_t free_list_validation_ns;
    uint64_t quarantine_management_ns;
    uint64_t total_overhead_ns;
    
    double overhead_percentage;
} detection_overhead_t;

detection_overhead_t measure_detection_overhead(void) {
    detection_overhead_t overhead = {0};
    
    const int iterations = 10000;
    void* ptrs[iterations];
    
    /* Measure baseline performance */
    uint64_t baseline_start = get_time_ns();
    
    for (int i = 0; i < iterations; i++) {
        ptrs[i] = malloc_without_detection(64);
    }
    for (int i = 0; i < iterations; i++) {
        free_without_detection(ptrs[i]);
    }
    
    uint64_t baseline_time = get_time_ns() - baseline_start;
    
    /* Measure with full detection */
    uint64_t detection_start = get_time_ns();
    
    for (int i = 0; i < iterations; i++) {
        ptrs[i] = malloc_with_detection(64);
    }
    for (int i = 0; i < iterations; i++) {
        free_with_detection(ptrs[i]);
    }
    
    uint64_t detection_time = get_time_ns() - detection_start;
    
    overhead.total_overhead_ns = detection_time - baseline_time;
    overhead.overhead_percentage = 
        ((double)(detection_time - baseline_time) / baseline_time) * 100.0;
    
    return overhead;
}

void optimize_detection_performance(void) {
    /* Adaptive detection based on corruption history */
    if (detector.corruptions_detected == 0 && 
        heap.allocation_count > 100000) {
        /* Reduce overhead for well-behaved applications */
        detector.enable_red_zones = false;
        detector.enable_heap_walking = false;
    }
    
    /* Enable aggressive detection after corruption */
    if (detector.corruptions_detected > 0) {
        detector.enable_red_zones = true;
        detector.enable_heap_walking = true;
    }
}
```

## Integration with Development Tools

### Debugger Integration

```c
void dump_allocator_state_for_debugger(void) {
    printf("=== Allocator State for Debugger ===\n");
    
    /* Heap overview */
    printf("Heap Start: %p\n", heap.heap_start);
    printf("Heap End: %p\n", heap.heap_end);
    printf("Total Allocated: %zu\n", heap.total_allocated);
    printf("Total Free: %zu\n", heap.total_free);
    printf("Allocation Count: %zu\n", heap.allocation_count);
    
    /* Free list state */
    printf("\nFree List:\n");
    enhanced_block_t* current = (enhanced_block_t*)heap.free_head;
    int count = 0;
    while (current && count < 20) {  /* Limit output */
        printf("  Block %d: %p, size=%zu, next=%p\n", 
               count, current, current->size, current->next_free);
        current = current->next_free;
        count++;
    }
    
    /* Quarantine state */
    printf("\nQuarantine State:\n");
    printf("  Entries: %zu/%d\n", quarantine.count, QUARANTINE_SIZE);
    printf("  Memory Overhead: %zu bytes\n", quarantine.memory_overhead);
    printf("  Use-after-free detected: %zu\n", quarantine.use_after_free_detected);
    
    /* Recent corruptions */
    printf("\nRecent Corruptions: %zu\n", detector.corruptions_detected);
    
    printf("====================================\n");
}

/* GDB helper functions */
void gdb_print_block(void* ptr) __attribute__((used));
void gdb_print_block(void* ptr) {
    enhanced_block_t* block = get_enhanced_block_from_ptr(ptr);
    if (!block) {
        printf("Invalid pointer: %p\n", ptr);
        return;
    }
    
    printf("Block at %p:\n", block);
    printf("  User pointer: %p\n", get_ptr_from_block(block));
    printf("  Size: %zu\n", block->size);
    printf("  Magic: 0x%08x\n", block->magic_header);
    printf("  Free: %s\n", block_state_name(block->is_free));
    printf("  Allocation ID: %u\n", block->allocation_id);
    
    corruption_type_t corruption = validate_block_structure(block);
    printf("  Integrity: %s\n", 
           corruption == CORRUPTION_NONE ? "OK" : corruption_type_name(corruption));
}
```

## Summary

This chapter implemented a comprehensive error detection and handling system:

1. **Multi-Layer Detection**: Magic numbers, canaries, checksums, and red zones provide overlapping protection
2. **Use-After-Free Protection**: Quarantine system with memory poisoning detects dangling pointer