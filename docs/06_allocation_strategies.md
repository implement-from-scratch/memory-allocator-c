# Chapter 06: Allocation Strategies Analysis

Memory allocation strategies determine how the allocator selects free blocks to satisfy allocation requests. The choice of strategy significantly impacts both performance and fragmentation characteristics. This chapter analyzes different allocation strategies, their mathematical properties, and implementation trade-offs.

## Mathematical Foundation of Allocation Strategies

Each allocation strategy represents a different optimization objective when searching through free blocks. Understanding these objectives helps predict performance characteristics under various workload patterns.

### Strategy Classification

Different allocation strategies optimize for different objectives. Some prioritize allocation speed by accepting the first suitable block, while others minimize fragmentation by carefully selecting blocks that best match request sizes. Understanding these trade-offs helps choose appropriate strategies for specific application requirements. Allocation strategies can be categorized by their selection criteria:

1. **Size-based strategies**: Select blocks based on size relationship to request
2. **Location-based strategies**: Prioritize spatial locality and memory organization  
3. **Hybrid strategies**: Combine multiple criteria for balanced performance

### Performance Metrics

Quantitative metrics provide objective comparison between allocation strategies. Allocation time measures how quickly we can satisfy requests, while fragmentation metrics indicate how efficiently we use available memory. Utilization measures the ratio of allocated to total memory, and locality scores reflect how well allocations cluster together for cache performance. We evaluate strategies using these quantitative measures:

```
Allocation Time = Search Time + Split Time + Metadata Update Time
Fragmentation = (Total Free - Largest Free Block) / Total Free
Utilization = Allocated Memory / (Allocated + Free Memory)
Locality Score = Adjacent Allocations / Total Allocations
```

## First-Fit Strategy Analysis

First-Fit selects the first free block that satisfies the size requirement. This strategy prioritizes allocation speed over optimal space utilization.

### Implementation

```c
block_t* first_fit_search(size_t size) {
    block_t* current = heap.free_head;
    
    while (current) {
        if (current->size >= size) {
            return current;  /* First suitable block found */
        }
        current = current->next_free;
    }
    
    return NULL;  /* No suitable block found */
}
```

### Mathematical Analysis

**Time Complexity**: O(n) worst-case, where n is the number of free blocks
**Average Case**: O(n/2) assuming uniform distribution of suitable blocks
**Best Case**: O(1) when first block satisfies request

**Space Utilization**: First-Fit tends to create small leftover blocks at the beginning of the heap, leading to external fragmentation over time.

### Fragmentation Characteristics

First-Fit exhibits predictable fragmentation patterns:

```
Early heap: [Large blocks available, fast allocation]
Middle lifecycle: [Mixed sizes, moderate fragmentation] 
Mature heap: [Small fragments at start, larger blocks at end]
```

The fragmentation tends to stabilize at approximately 25-30% under typical workloads.

### Performance Under Different Workloads

**Sequential Allocation Pattern**:
```c
void test_sequential_first_fit(void) {
    /* Allocate increasing sizes */
    for (size_t size = 16; size <= 1024; size *= 2) {
        void* ptr = malloc(size);
        /* First-fit performs well - O(1) for each allocation */
    }
}
```

**Random Size Pattern**:
```c
void test_random_first_fit(void) {
    for (int i = 0; i < 1000; i++) {
        size_t size = 16 + (rand() % 1000);
        void* ptr = malloc(size);
        /* Performance degrades as fragmentation increases */
    }
}
```

## Best-Fit Strategy Analysis

Best-Fit selects the smallest free block that satisfies the request, minimizing wasted space per allocation.

### Implementation

```c
block_t* best_fit_search(size_t size) {
    block_t* current = heap.free_head;
    block_t* best_block = NULL;
    size_t smallest_waste = SIZE_MAX;
    
    while (current) {
        if (current->size >= size) {
            size_t waste = current->size - size;
            
            if (waste < smallest_waste) {
                smallest_waste = waste;
                best_block = current;
                
                /* Perfect fit found - no waste */
                if (waste == 0) {
                    break;
                }
            }
        }
        current = current->next_free;
    }
    
    return best_block;
}
```

### Mathematical Properties

**Time Complexity**: O(n) always - must examine entire free list
**Space Efficiency**: Minimizes internal fragmentation per allocation
**External Fragmentation**: Can create many small unusable fragments

**Waste Calculation**:
```
For allocation of size s in block of size b:
Internal Waste = b - s (minimized by Best-Fit)
External Fragmentation = Σ(unusable small blocks)
```

### Advanced Best-Fit with Size Classes

To improve Best-Fit performance, we can organize free blocks by size classes:

```c
#define SIZE_CLASSES 10
static block_t* size_class_lists[SIZE_CLASSES];

/* Size class boundaries: 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192+ */
static const size_t size_boundaries[SIZE_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096, SIZE_MAX
};

int get_size_class(size_t size) {
    for (int i = 0; i < SIZE_CLASSES; i++) {
        if (size <= size_boundaries[i]) {
            return i;
        }
    }
    return SIZE_CLASSES - 1;  /* Largest class */
}

block_t* best_fit_with_size_classes(size_t size) {
    int start_class = get_size_class(size);
    
    /* Search from appropriate size class upward */
    for (int class = start_class; class < SIZE_CLASSES; class++) {
        block_t* current = size_class_lists[class];
        
        while (current) {
            if (current->size >= size) {
                return current;  /* First fit within size class */
            }
            current = current->next_free;
        }
    }
    
    return NULL;
}
```

This approach reduces search time from O(n) to O(k) where k is the average blocks per size class.

## Worst-Fit Strategy Analysis

Worst-Fit selects the largest available free block, attempting to minimize external fragmentation by keeping large blocks available.

### Implementation

```c
block_t* worst_fit_search(size_t size) {
    block_t* current = heap.free_head;
    block_t* largest_block = NULL;
    size_t largest_size = 0;
    
    while (current) {
        if (current->size >= size && current->size > largest_size) {
            largest_size = current->size;
            largest_block = current;
        }
        current = current->next_free;
    }
    
    return largest_block;
}
```

### Performance Characteristics

**Advantages**:
- Keeps large blocks available for future large allocations
- Reduces external fragmentation in scenarios with mixed allocation sizes
- Leftover blocks after splitting are typically large enough to be useful

**Disadvantages**:
- Always requires full free list traversal O(n)
- Can quickly consume large blocks, making subsequent large allocations fail
- Higher internal fragmentation for small allocations

### Experimental Analysis

```c
void compare_worst_fit_fragmentation(void) {
    /* Allocate mixed sizes and measure fragmentation */
    size_t sizes[] = {32, 1024, 64, 2048, 128, 512};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (int strategy = 0; strategy < 3; strategy++) {
        heap_reset();
        
        for (int i = 0; i < 100; i++) {
            size_t size = sizes[i % num_sizes];
            
            void* ptr = (strategy == 0) ? malloc_first_fit(size) :
                       (strategy == 1) ? malloc_best_fit(size) :
                                       malloc_worst_fit(size);
                                       
            if (i % 10 == 9) {  /* Free every 10th allocation */
                free(ptr);
            }
        }
        
        measure_fragmentation(strategy);
    }
}
```

## Next-Fit Strategy Analysis

Next-Fit remembers the location of the last allocation and begins the next search from that point, attempting to improve locality.

### Implementation

```c
static block_t* last_allocated = NULL;

block_t* next_fit_search(size_t size) {
    block_t* start_block = last_allocated ? last_allocated->next_free : heap.free_head;
    block_t* current = start_block;
    
    /* Search from last position to end of list */
    do {
        if (current && current->size >= size) {
            last_allocated = current;
            return current;
        }
        
        current = current ? current->next_free : heap.free_head;
        
    } while (current != start_block);
    
    return NULL;  /* Full cycle completed, no suitable block */
}
```

### Locality Analysis

Next-Fit improves spatial locality by clustering allocations:

```
Memory Layout with Next-Fit:
[Block A][Block B][Block C] <- Related allocations clustered
[Free   ][Block D][Block E] <- Continued from last position
```

**Locality Metrics**:
```
Spatial Locality = Adjacent Allocations / Total Allocations
Cache Performance = Cache Hits / Total Memory Accesses  
Working Set Size = Unique Pages Touched / Total Pages
```

### Performance Comparison

| Strategy | Avg Search Time | Fragmentation | Locality | Cache Performance |
|----------|----------------|---------------|----------|-------------------|
| First-Fit | O(n/2) | Medium | Poor | Poor |
| Best-Fit | O(n) | Low internal | Poor | Poor |
| Worst-Fit | O(n) | High internal | Poor | Poor |
| Next-Fit | O(n/2) | Medium | Good | Good |

## Segregated Free List Strategy

Segregated free lists organize blocks by size ranges, providing O(1) allocation for exact matches and O(k) for range searches.

### Implementation Architecture

```c
#define SEGREGATED_LISTS 16

typedef struct segregated_allocator {
    block_t* size_lists[SEGREGATED_LISTS];
    size_t size_thresholds[SEGREGATED_LISTS];
    pthread_mutex_t list_mutexes[SEGREGATED_LISTS];  /* Fine-grained locking */
    
    /* Statistics for each list */
    size_t list_counts[SEGREGATED_LISTS];
    size_t allocation_counts[SEGREGATED_LISTS];
    size_t search_times[SEGREGATED_LISTS];
} segregated_allocator_t;

/* Initialize with exponential size classes */
void init_segregated_allocator(segregated_allocator_t* alloc) {
    for (int i = 0; i < SEGREGATED_LISTS; i++) {
        alloc->size_thresholds[i] = 16 << i;  /* 16, 32, 64, 128, ... */
        alloc->size_lists[i] = NULL;
        pthread_mutex_init(&alloc->list_mutexes[i], NULL);
        alloc->list_counts[i] = 0;
    }
}

int find_size_list(size_t size) {
    for (int i = 0; i < SEGREGATED_LISTS - 1; i++) {
        if (size <= segregated_alloc.size_thresholds[i]) {
            return i;
        }
    }
    return SEGREGATED_LISTS - 1;  /* Largest size class */
}

void* segregated_malloc(size_t size) {
    int list_index = find_size_list(size);
    
    /* Try exact size class first */
    pthread_mutex_lock(&segregated_alloc.list_mutexes[list_index]);
    
    if (segregated_alloc.size_lists[list_index]) {
        block_t* block = segregated_alloc.size_lists[list_index];
        segregated_alloc.size_lists[list_index] = block->next_free;
        segregated_alloc.list_counts[list_index]--;
        
        pthread_mutex_unlock(&segregated_alloc.list_mutexes[list_index]);
        return get_ptr_from_block(block);
    }
    
    pthread_mutex_unlock(&segregated_alloc.list_mutexes[list_index]);
    
    /* Search larger size classes */
    for (int i = list_index + 1; i < SEGREGATED_LISTS; i++) {
        pthread_mutex_lock(&segregated_alloc.list_mutexes[i]);
        
        if (segregated_alloc.size_lists[i]) {
            block_t* block = segregated_alloc.size_lists[i];
            segregated_alloc.size_lists[i] = block->next_free;
            segregated_alloc.list_counts[i]--;
            
            /* Split if block is significantly larger */
            if (block->size > size + MIN_SPLIT_SIZE) {
                block_t* remainder = split_block(block, size);
                segregated_free_block(remainder);
            }
            
            pthread_mutex_unlock(&segregated_alloc.list_mutexes[i]);
            return get_ptr_from_block(block);
        }
        
        pthread_mutex_unlock(&segregated_alloc.list_mutexes[i]);
    }
    
    /* No suitable block found - allocate new memory */
    return allocate_new_block(size);
}
```

### Mathematical Analysis of Segregated Lists

**Time Complexity**:
- Best case: O(1) for exact size match
- Average case: O(log k) where k is number of size classes
- Worst case: O(k) when searching all larger classes

**Space Overhead**:
```
Metadata Overhead = Number of Lists × Pointer Size
Memory Overhead = Internal Fragmentation per Size Class
```

**Fragmentation Analysis**:
```
Internal Fragmentation per Class i = 
    (Threshold[i] - Average Request Size in Class i) / Threshold[i]

Total Internal Fragmentation = 
    Σ(Class Usage[i] × Internal Fragmentation[i])
```

## Buddy System Allocation

The buddy system maintains blocks in power-of-2 sizes, enabling efficient splitting and coalescing operations.

### Theoretical Foundation

The buddy system divides memory into blocks of size 2^k where k ranges from minimum to maximum block size exponents.

```
Block Sizes: 2^min_k, 2^(min_k+1), ..., 2^max_k
Example: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes
```

### Implementation

```c
#define MIN_BLOCK_EXPONENT 4   /* 2^4 = 16 bytes minimum */
#define MAX_BLOCK_EXPONENT 20  /* 2^20 = 1MB maximum */
#define NUM_BUDDY_LISTS (MAX_BLOCK_EXPONENT - MIN_BLOCK_EXPONENT + 1)

typedef struct buddy_allocator {
    block_t* free_lists[NUM_BUDDY_LISTS];
    void* heap_start;
    size_t heap_size;
    unsigned char* allocation_bitmap;  /* Track allocated blocks */
} buddy_allocator_t;

/* Find the buddy of a block */
void* find_buddy(void* block, int order) {
    size_t block_size = 1 << (MIN_BLOCK_EXPONENT + order);
    uintptr_t block_addr = (uintptr_t)block;
    uintptr_t heap_start = (uintptr_t)buddy_alloc.heap_start;
    
    /* Calculate relative position in heap */
    size_t relative_addr = block_addr - heap_start;
    
    /* Buddy is at relative_addr XOR block_size */
    size_t buddy_relative = relative_addr ^ block_size;
    
    return (void*)(heap_start + buddy_relative);
}

void* buddy_malloc(size_t size) {
    /* Round up to next power of 2 */
    int order = 0;
    size_t block_size = 1 << MIN_BLOCK_EXPONENT;
    
    while (block_size < size && order < NUM_BUDDY_LISTS - 1) {
        block_size <<= 1;
        order++;
    }
    
    /* Find available block of required order or larger */
    for (int current_order = order; current_order < NUM_BUDDY_LISTS; current_order++) {
        if (buddy_alloc.free_lists[current_order]) {
            block_t* block = buddy_alloc.free_lists[current_order];
            buddy_alloc.free_lists[current_order] = block->next_free;
            
            /* Split block down to required size */
            while (current_order > order) {
                current_order--;
                
                /* Split block and add buddy to appropriate free list */
                void* buddy = find_buddy(block, current_order);
                block_t* buddy_block = (block_t*)buddy;
                
                initialize_free_block(buddy_block, 1 << (MIN_BLOCK_EXPONENT + current_order));
                buddy_block->next_free = buddy_alloc.free_lists[current_order];
                buddy_alloc.free_lists[current_order] = buddy_block;
            }
            
            initialize_allocated_block(block, block_size);
            return get_ptr_from_block(block);
        }
    }
    
    return NULL;  /* No suitable block available */
}

void buddy_free(void* ptr) {
    block_t* block = get_block_from_ptr(ptr);
    int order = 0;
    size_t block_size = block->size;
    
    /* Calculate order from block size */
    while ((1 << (MIN_BLOCK_EXPONENT + order)) < block_size) {
        order++;
    }
    
    /* Attempt to coalesce with buddy */
    while (order < NUM_BUDDY_LISTS - 1) {
        void* buddy_ptr = find_buddy(block, order);
        block_t* buddy = (block_t*)buddy_ptr;
        
        /* Check if buddy is free and same size */
        if (!buddy->is_free || buddy->size != block_size) {
            break;  /* Cannot coalesce */
        }
        
        /* Remove buddy from free list */
        remove_from_buddy_list(buddy, order);
        
        /* Coalesce blocks */
        if (buddy < block) {
            block = buddy;  /* Use lower address as merged block */
        }
        
        order++;
        block_size <<= 1;
        block->size = block_size;
    }
    
    /* Add coalesced block to appropriate free list */
    initialize_free_block(block, block_size);
    block->next_free = buddy_alloc.free_lists[order];
    buddy_alloc.free_lists[order] = block;
}
```

### Buddy System Analysis

**Advantages**:
- Guaranteed coalescing: Adjacent free blocks always merge
- Predictable performance: O(log n) allocation and deallocation
- Simple implementation with elegant mathematical properties

**Disadvantages**:
- High internal fragmentation: Up to 50% waste for size just over power-of-2
- Memory alignment requirements: All blocks must be power-of-2 aligned
- Fixed size granularity: Cannot optimize for specific application patterns

**Fragmentation Analysis**:
```
Internal Fragmentation = (Allocated Block Size - Requested Size) / Allocated Block Size

For request of size s:
Allocated Block Size = 2^⌈log₂(s)⌉
Maximum Waste = 2^⌈log₂(s)⌉ - (2^⌈log₂(s)-1⌉ + 1) ≈ 50%
Average Waste ≈ 25% assuming uniform size distribution
```

## Strategy Selection and Adaptive Algorithms

### Dynamic Strategy Selection

An advanced allocator can adapt its strategy based on runtime characteristics:

```c
typedef struct adaptive_allocator {
    allocation_strategy_t current_strategy;
    
    /* Performance metrics */
    double avg_allocation_time;
    double fragmentation_ratio;
    size_t failed_allocations;
    
    /* Strategy performance history */
    strategy_metrics_t metrics[NUM_STRATEGIES];
    
    /* Adaptation parameters */
    size_t measurement_window;
    size_t adaptations_performed;
} adaptive_allocator_t;

allocation_strategy_t select_optimal_strategy(void) {
    /* Analyze current heap state */
    double fragmentation = calculate_fragmentation_ratio();
    double avg_block_size = calculate_average_block_size();
    size_t free_block_count = count_free_blocks();
    
    /* Decision tree for strategy selection */
    if (fragmentation > 0.3 && free_block_count > 100) {
        return STRATEGY_BEST_FIT;  /* High fragmentation needs precise fitting */
    }
    
    if (avg_block_size > 1024 && free_block_count < 50) {
        return STRATEGY_FIRST_FIT;  /* Large blocks, prioritize speed */
    }
    
    if (locality_score > 0.7) {
        return STRATEGY_NEXT_FIT;  /* Good locality, maintain it */
    }
    
    return STRATEGY_SEGREGATED;  /* Default to segregated for balanced performance */
}

void* adaptive_malloc(size_t size) {
    /* Periodically evaluate and adapt strategy */
    if (adaptive_alloc.measurement_window++ % 1000 == 0) {
        allocation_strategy_t new_strategy = select_optimal_strategy();
        
        if (new_strategy != adaptive_alloc.current_strategy) {
            transition_to_strategy(new_strategy);
            adaptive_alloc.current_strategy = new_strategy;
            adaptive_alloc.adaptations_performed++;
        }
    }
    
    /* Delegate to current strategy */
    switch (adaptive_alloc.current_strategy) {
        case STRATEGY_FIRST_FIT:
            return first_fit_malloc(size);
        case STRATEGY_BEST_FIT:
            return best_fit_malloc(size);
        case STRATEGY_WORST_FIT:
            return worst_fit_malloc(size);
        case STRATEGY_NEXT_FIT:
            return next_fit_malloc(size);
        case STRATEGY_SEGREGATED:
            return segregated_malloc(size);
        case STRATEGY_BUDDY:
            return buddy_malloc(size);
        default:
            return first_fit_malloc(size);  /* Fallback */
    }
}
```

### Performance Tuning Based on Application Patterns

Different application patterns benefit from different strategies:

**Web Server Pattern** (Many small, short-lived allocations):
```c
/* Optimize for allocation speed */
strategy_config.prefer_first_fit = true;
strategy_config.enable_size_classes = true;
strategy_config.small_block_threshold = 256;
```

**Scientific Computing Pattern** (Large arrays, long-lived):
```c
/* Optimize for space efficiency */
strategy_config.prefer_best_fit = true;
strategy_config.enable_large_block_optimization = true;
strategy_config.coalesce_aggressively = true;
```

**Database Pattern** (Mixed sizes, medium lifetime):
```c
/* Balanced approach with good locality */
strategy_config.use_segregated_lists = true;
strategy_config.maintain_locality = true;
strategy_config.adaptive_threshold = 0.2;
```

## Experimental Evaluation Framework

### Benchmark Suite

```c
typedef struct benchmark_result {
    allocation_strategy_t strategy;
    double avg_allocation_time_ns;
    double avg_deallocation_time_ns;
    double peak_fragmentation;
    double avg_fragmentation;
    size_t failed_allocations;
    double memory_efficiency;
    double locality_score;
} benchmark_result_t;

void run_allocation_strategy_benchmarks(void) {
    allocation_strategy_t strategies[] = {
        STRATEGY_FIRST_FIT,
        STRATEGY_BEST_FIT,
        STRATEGY_WORST_FIT,
        STRATEGY_NEXT_FIT,
        STRATEGY_SEGREGATED,
        STRATEGY_BUDDY
    };
    
    workload_pattern_t patterns[] = {
        PATTERN_SEQUENTIAL,
        PATTERN_RANDOM,
        PATTERN_BIMODAL,
        PATTERN_EXPONENTIAL,
        PATTERN_REALISTIC_WEB,
        PATTERN_REALISTIC_DATABASE
    };
    
    for (int s = 0; s < NUM_STRATEGIES; s++) {
        for (int p = 0; p < NUM_PATTERNS; p++) {
            benchmark_result_t result = benchmark_strategy_with_pattern(
                strategies[s], patterns[p], 100000  /* iterations */
            );
            
            printf("Strategy: %s, Pattern: %s\n", 
                   strategy_name(strategies[s]), pattern_name(patterns[p]));
            printf("  Avg Alloc Time: %.2f ns\n", result.avg_allocation_time_ns);
            printf("  Peak Fragmentation: %.2f%%\n", result.peak_fragmentation * 100);
            printf("  Memory Efficiency: %.2f%%\n", result.memory_efficiency * 100);
            printf("  Failed Allocations: %zu\n", result.failed_allocations);
        }
    }
}
```

### Statistical Analysis

```c
void analyze_strategy_performance(benchmark_result_t* results, int num_results) {
    /* Calculate statistical significance of performance differences */
    for (int i = 0; i < num_results - 1; i++) {
        for (int j = i + 1; j < num_results; j++) {
            double t_statistic = calculate_t_test(
                results[i].avg_allocation_time_ns,
                results[j].avg_allocation_time_ns,
                results[i].sample_variance,
                results[j].sample_variance,
                results[i].sample_size,
                results[j].sample_size
            );
            
            double p_value = t_distribution_cdf(t_statistic, 
                results[i].sample_size + results[j].sample_size - 2);
            
            if (p_value < 0.05) {
                printf("Significant difference between %s and %s (p=%.4f)\n",
                       strategy_name(results[i].strategy),
                       strategy_name(results[j].strategy),
                       p_value);
            }
        }
    }
}
```

## Summary

This chapter provided comprehensive analysis of memory allocation strategies:

1. **First-Fit**: Fast allocation with moderate fragmentation, suitable for general-purpose use
2. **Best-Fit**: Minimizes internal fragmentation but requires full list traversal  
3. **Worst-Fit**: Keeps large blocks available but can increase internal fragmentation
4. **Next-Fit**: Improves locality by clustering allocations spatially
5. **Segregated Lists**: Provides O(1) allocation for common sizes with fine-grained control
6. **Buddy System**: Guarantees coalescing with predictable performance but higher fragmentation

The choice of allocation strategy significantly impacts both performance and memory efficiency. Modern allocators often combine multiple strategies or adapt dynamically based on runtime characteristics. Understanding these trade-offs enables optimal allocator design for specific application requirements.
