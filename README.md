# Dynamic Memory Allocator

A custom implementation of `malloc`, `free`, and `realloc` in C, featuring segregated free lists, quick lists for fast allocation, and automatic memory coalescing to minimize fragmentation.

## Overview

This project implements a dynamic memory allocator that manages heap memory efficiently using advanced techniques found in production allocators. The allocator provides drop-in replacements for the standard C library functions `malloc()`, `free()`, and `realloc()`.

## Features

### Core Functionality
- **`sf_malloc(size_t size)`** - Allocates memory blocks with requested size
- **`sf_free(void *ptr)`** - Frees allocated memory and returns it to the heap
- **`sf_realloc(void *ptr, size_t size)`** - Resizes previously allocated memory blocks

### Memory Management Strategies

#### Segregated Free Lists
- Uses multiple free lists organized by block size classes
- Block sizes: 32, 64, 128, 256, 512, 1024, 2048, 4096+ bytes
- Each list manages blocks within a specific size range
- Enables O(1) insertion and fast size-appropriate block lookup

#### Quick Lists (Fast Allocation Cache)
- Maintains separate quick lists for frequently allocated small blocks (32-176 bytes)
- LIFO (Last-In-First-Out) allocation for cache efficiency
- Defers coalescing for recently freed blocks to speed up malloc/free cycles
- Automatically flushes to main free lists when capacity is reached

#### Coalescing
- **Immediate coalescing** for large blocks to reduce external fragmentation
- **Delayed coalescing** for small blocks via quick lists
- Bidirectional coalescing merges adjacent free blocks
- Prevents fragmentation by combining neighboring free memory

#### Block Splitting
- Splits large free blocks when a smaller allocation is requested
- Prevents internal fragmentation by avoiding wasted space
- Maintains minimum block size of 32 bytes to avoid splinters

## Implementation Details

### Block Structure
Each memory block contains:
- **Header** (8 bytes): Stores block size, allocation status, and payload size
- **Payload**: User-accessible memory
- **Footer** (8 bytes): Duplicate of header for backward traversal

### Memory Layout
```
[Padding: 8B] [Prologue: 32B] [Free/Allocated Blocks] [Epilogue: 8B]
```

### Block Metadata
- Blocks are 16-byte aligned
- Minimum block size: 32 bytes (header + footer + 16 bytes)
- Header format: `[32-bit payload size][32-bit size + flags]`
- Flags: `THIS_BLOCK_ALLOCATED`, `IN_QUICK_LIST`

### Obfuscation
- All metadata is XOR-encrypted with a magic number to detect corruption
- Provides basic protection against accidental overwrites

## Performance Metrics

### `sf_fragmentation()`
Calculates internal fragmentation ratio:
```
fragmentation = total_allocated_payloads / total_allocated_block_size
```

### `sf_utilization()`
Measures peak memory utilization:
```
utilization = peak_payload_size / total_heap_size
```

## Algorithm Complexity

| Operation | Average Case | Worst Case |
|-----------|--------------|------------|
| `sf_malloc()` | O(1) - O(log n) | O(n) |
| `sf_free()` | O(1) | O(1) |
| `sf_realloc()` | O(1) - O(n) | O(n) |
| Coalescing | O(1) | O(1) |

- **Quick list hits**: O(1)
- **Free list search**: O(log n) for segregated lists, O(n) worst case
- **Coalescing**: O(1) - only checks immediate neighbors

## Memory Allocation Strategy

1. **Check quick lists** - If block size ≤ 176 bytes, attempt quick list allocation
2. **Search free lists** - Find first-fit block in appropriate size class
3. **Expand heap** - If no suitable block exists, request more memory via `sf_mem_grow()`
4. **Split blocks** - Divide larger blocks to minimize waste
5. **Update metadata** - Track payload sizes for fragmentation metrics

## Error Handling

- Returns `NULL` and sets `sf_errno = ENOMEM` when out of memory
- Calls `abort()` for invalid operations:
  - Freeing `NULL` pointer
  - Double-free detection
  - Freeing already quick-listed blocks
  - Invalid pointer alignment or size

## Technical Specifications

- **Minimum block size**: 32 bytes
- **Alignment**: 16 bytes
- **Quick list capacity**: 5 blocks per size class
- **Quick list sizes**: 32, 48, 64, 80, 96, 112, 128, 144, 160, 176 bytes
- **Page size**: 4096 bytes (system-dependent)

## Building and Testing

This allocator is designed to be compiled as part of a larger system programming project. It requires:
- `sfmm.h` - Header file with structure definitions
- `debug.h` - Debugging utilities
- Test harness for validation

## Limitations

- Not thread-safe (requires external synchronization for concurrent access)
- No support for alignment requirements beyond 16 bytes
- Fixed heap growth policy (one page at a time)

## Implementation Notes

- Based on concepts from *Computer Systems: A Programmer's Perspective* (CS:APP)
- Uses boundary tags (headers/footers) for constant-time coalescing
- Segregated free lists reduce search time compared to single free list
- Quick lists trade memory for speed in common allocation patterns

## Future Improvements

- Thread-local caches for concurrent allocation
- Better heap growth heuristics
- Support for custom alignment requirements
- Memory defragmentation strategies
