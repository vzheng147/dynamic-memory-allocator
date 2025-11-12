/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

#define WSIZE 8
#define DSIZE 16 
#define MIN_BLOCK_SIZE 32
#define MAX_QUICK_LIST_BLOCK_SIZE (16 + (NUM_QUICK_LISTS * 16))

// The GET/PUT/HEADER/FOOTER macros are from the CSE320 Textbook with slight adjustments
#define GET(p)        ((*(sf_header *)(p)) ^ MAGIC)
#define PUT(p, val)   (*(sf_header *)(p)) = ((val) ^ MAGIC)

#define IS_ALLOCATED(p)       ((GET(p) & THIS_BLOCK_ALLOCATED ) != 0)
#define IS_IN_QUICK_LIST(p)   ((GET(p) & IN_QUICK_LIST ) != 0)

#define GET_SIZE(p)    ((uint64_t)(GET(p) & 0x00000000FFFFFFFF) & ~0xF)
#define GET_PAYLOAD(p)  (GET(p) >> 32)

// pass in block pointer, get its header / footer
#define HEADER(bp) ((char *)(bp) - WSIZE)
#define FOOTER(bp) ((char *)(bp) + GET_SIZE(HEADER(bp)) - DSIZE)


int heap_initialized = 0;
double peak_payload_size = 0;
double current_payload_size = 0;

void initialize_heap();
void expand_heap(size_t requested);
void initialize_lists();
void set_block_meta_data(sf_block *bp, size_t payload, size_t size, size_t flags);
void set_block_flags(sf_block *bp, int alloc, int quicklist);
void create_epilogue();
sf_block *get_prev_block(sf_block *bp);
sf_block *get_next_block(sf_block *bp);
sf_block *coalesce(sf_block *bp);
sf_block *split_block(sf_block *bp, size_t split_size, size_t payload_size);
sf_block *search_free_list_for_block(size_t requested);
void insert_free_list(sf_block *bp, int index);
void remove_from_free_list(sf_block *bp);
void insert_quick_list(sf_block *bp, int index);
sf_block *pop_quick_list(int index);
int freelist_index(int n);
int quicklist_index(int n);
size_t calculate_block_size(size_t size);
int valid_pointer(sf_block *p);



void *sf_malloc(size_t size) {

    if (!heap_initialized){
        initialize_lists();
        initialize_heap();
        heap_initialized = 1;
    }

    if (size <= 0){
        return NULL;
    }

    size_t block_size = calculate_block_size(size);

    // First, check quicklist
    if (block_size <= MAX_QUICK_LIST_BLOCK_SIZE){
        int q_index = quicklist_index(block_size);
        sf_block *bp = pop_quick_list(q_index);
        if (bp){
            //size_t size = GET_PAYLOAD(bp->header);
            set_block_meta_data(bp, size, block_size, THIS_BLOCK_ALLOCATED);
            // No need to split; exactly the requested size

            // Tracking current payload; update max payload in lifetime
            current_payload_size += size;
            if (current_payload_size > peak_payload_size) {
                peak_payload_size = current_payload_size;
            }

            return (void *)((char *)bp + sizeof(sf_header));
        }
    }

    // If too large or not found in quicklist, search in free_list
    sf_block *bp = search_free_list_for_block(block_size);
    // If no available block found, expand heap
    if (!bp){
        expand_heap(block_size);
        bp = search_free_list_for_block(block_size);
        // Still not found; out of memory
        if (!bp){
            sf_errno = ENOMEM;
            return NULL; 
        }
    }

    // Check if we can split bp to avoid splinters
    size_t actual_size = GET_SIZE(&(bp->header));
    if (actual_size - block_size >= MIN_BLOCK_SIZE){
        bp = split_block(bp, block_size, size);
    } else {
        remove_from_free_list(bp);
        set_block_meta_data(bp, size, actual_size, THIS_BLOCK_ALLOCATED);
    }

    // Tracking current payload; update max payload in lifetime
    current_payload_size += size;
    if (current_payload_size > peak_payload_size) {
        peak_payload_size = current_payload_size;
    }

    return (void *)((char *)bp + sizeof(sf_header));
}

void sf_free(void *pp) {
    /*
    if (valid_pointer(pp) == 0){
        abort();
    }*/

    if (pp == NULL){
        abort();
    }

    sf_block *bp = (sf_block *)((char *)pp - sizeof(sf_header));

    if (!IS_ALLOCATED(&(bp->header)) || IS_IN_QUICK_LIST(&(bp->header))) {
        abort();
    }

    // Tracking current payload; remove payload amount from the freed block
    size_t freed_payload = GET_PAYLOAD(&(bp->header));
    current_payload_size -= freed_payload;

    // Get block size
    size_t block_size = GET_SIZE(&(bp->header));

    // Insert into quicklist for delayed coalesce if small block
    if (block_size <= MAX_QUICK_LIST_BLOCK_SIZE){
        set_block_meta_data(bp, 0 , block_size, IN_QUICK_LIST | THIS_BLOCK_ALLOCATED);
        int q_index = quicklist_index(block_size);
        insert_quick_list(bp, q_index);
    } else { // Coalesce and then insert into respective list if large block
        set_block_meta_data(bp, 0, block_size, 0);
        bp = coalesce(bp);

        int index = freelist_index(GET_SIZE(&(bp->header)));
        insert_free_list(bp, index);
    }
}

void *sf_realloc(void *pp, size_t rsize) {

    if (rsize < 0){
        return NULL;
    }
    if (rsize == 0) {
        sf_free(pp);
        return NULL;
    }
    if (valid_pointer(pp) == 0){
        return NULL;
    }

    sf_block *bp = (sf_block *)((char *)pp - sizeof(sf_header));

    size_t old_size = GET_SIZE(&(bp->header));

    size_t block_size = calculate_block_size(rsize);
    size_t old_payload_size = GET_PAYLOAD(&(bp->header));

    if (block_size > old_size){
        // Larger size requested; adjust malloc bytes to remove overhead
        void *ptr = sf_malloc(block_size - sizeof(sf_header) - sizeof(sf_footer));

        if (ptr == NULL){
            // out of memory
            return NULL;
        }

        // -16 gets rid of the header/footer overhead in size
        memcpy(ptr, pp, old_size - 16);

        // Free the old ptr
        sf_free(pp);

        return ptr;
    } 

    // Smaller size requested; shrinking the block
    if (old_size - block_size >= MIN_BLOCK_SIZE){
        // Split the block

        // Set the new header / footer
        set_block_meta_data(bp, rsize, block_size, THIS_BLOCK_ALLOCATED);

        // Track current payload; shrink the payload
        current_payload_size -= old_payload_size;
        current_payload_size += rsize;

        // The left over becomes a new free block
        sf_block *remain = (sf_block *)((char *)bp + block_size);
        set_block_meta_data(remain, 0, (old_size-block_size), 0);

        // Coalesce and insert into appropriate free list
        remain = coalesce(remain);
        int index = freelist_index(GET_SIZE(&(remain->header)));
        insert_free_list(remain, index);

        return pp;

    } else {

        // Track current payload; shrink the payload
        current_payload_size -= old_payload_size;
        current_payload_size += rsize;

        // Keep the splinter
        return pp;
    }
}

double sf_fragmentation() {

    if (!heap_initialized){
        return 0;
    }

    double total_block_size = 0;  
    double total_allocated_payloads = 0;

    sf_block *current = (sf_block *)((char *)sf_mem_start() + 40);

    while ((char *)current < (char *)sf_mem_end() - 8){
        size_t block_size = GET_SIZE(&(current->header));
        if (block_size == 0){ // reached end; epilogue has block size 0
            break;
        }
        if (IS_ALLOCATED(&(current->header))){
            total_allocated_payloads += GET_PAYLOAD(&(current->header));
            total_block_size += block_size;
        }

        char *next_address = ((char *)current + block_size);

        if (next_address >= (char *) sf_mem_end() - 8){
            break;
        }
        // Go to next block
        current = (sf_block *)next_address;
    }

    if (total_allocated_payloads == 0){ // no allocated blocks, return 0
        return 0;
    }

    return total_allocated_payloads / total_block_size;
}

double sf_utilization() {

    if (!heap_initialized){
        return 0;
    }
    double heap_size = (double)((char *)sf_mem_end() - (char *)sf_mem_start());

    return (double)peak_payload_size / heap_size;
}

/*
    End of required functions to implement;
    Start of helper functions
*/

void initialize_heap(){
    char *heap_start = (char *)sf_mem_grow();
    // Reserve 8 byte padding for alignment
    sf_block *prologue = (sf_block *)(heap_start + 8);

    // Build prologue
    set_block_meta_data(prologue, 0, MIN_BLOCK_SIZE, THIS_BLOCK_ALLOCATED);

    // Find size of free block
    // free block size = page size - padding - prologue - epilogue
    size_t free_size = PAGE_SZ - WSIZE - MIN_BLOCK_SIZE - WSIZE;

    // Build one giant free block
    sf_block *free_block = (sf_block *)((char *)prologue + MIN_BLOCK_SIZE);
    set_block_meta_data(free_block, 0, free_size, 0);

    // Place block in free_list
    int index = freelist_index(free_size);
    insert_free_list(free_block, index);

    // Build epilogue
    create_epilogue();
}

void expand_heap(size_t requested){
    size_t total = 0;

    while (total < requested){
        sf_block *old_epilogue = (sf_block *)((char *)sf_mem_end() - 8);

        if (sf_mem_grow() == NULL) {
            sf_errno = ENOMEM;
            return;
        }

        total += PAGE_SZ;

        char *new_end = (char *)sf_mem_end();  
        size_t block_size = new_end - (char *)old_epilogue;
        set_block_meta_data(old_epilogue, 0, block_size, 0);

        sf_block *coalesced = coalesce(old_epilogue);

        total = GET_SIZE(&(coalesced->header));

        int index = freelist_index(total);
        insert_free_list(coalesced, index);

        create_epilogue();
    }
}

void initialize_lists(){
    for (int i = 0; i<NUM_QUICK_LISTS; i++){
        sf_quick_lists[i].length = 0;
        sf_quick_lists[i].first = NULL;
    }
    for (int j = 0; j<NUM_FREE_LISTS; j++){
        sf_free_list_heads[j].body.links.next = &(sf_free_list_heads[j]);
        sf_free_list_heads[j].body.links.prev = &(sf_free_list_heads[j]);
    }
}

/**
 * Sets up the header and footer for a block at pointer bp
 * with the given size and flags
 */

void set_block_meta_data(sf_block *bp, size_t payload, size_t size, size_t flags) {
    // Create value that goes into header / footer
    size_t value = (payload << 32) | size | flags;

    // Write the header
    PUT(&(bp->header), value);

    // Compute footer address
    char *footer_addr = (char *)bp + size - WSIZE;

    // Write the footer
    PUT(footer_addr, value);
}

void set_block_flags(sf_block *bp, int alloc, int quicklist){
    // Read old header
    size_t old = GET(&(bp->header));
    // Extract the size
    uint64_t size = ((uint64_t)(old & 0x00000000FFFFFFFF) & ~0xF);

    size_t flags = 0;

    if (alloc){
        flags |= THIS_BLOCK_ALLOCATED;
    }
    if (quicklist){
        flags |= IN_QUICK_LIST;
    }

    size_t new = size | flags;

    // Write the new header
    PUT(&(bp->header), new);
    // And the new footer
    PUT((char *)bp + size - WSIZE, new);
}

/**
 * Create an epilogue at the end of the current heap.
 */

void create_epilogue() {
    // Computer epilogue address
    char *epilogue_ptr = (char *)sf_mem_end() - 8;
    // Mark it allocated
    PUT(epilogue_ptr, THIS_BLOCK_ALLOCATED); 
}

sf_block *get_prev_block(sf_block *bp) {
    if ((char *)bp <= (char *)sf_mem_start() + 40) {
        return NULL;
    }
    // Get previous block footer
    char *footer_ptr = (char *)bp - WSIZE;
    // Get previous block size
    size_t prev_size = GET_SIZE(footer_ptr);

    return (sf_block *)((char *)bp - prev_size);
}

sf_block *get_next_block(sf_block *bp) {
    if ((char *)bp >= (char *)sf_mem_end() - 8) {
        return NULL;
    }
    // Get current block size
    size_t b_size = GET_SIZE(&(bp->header));
    // Add block size to get next block
    return (sf_block *)((char *)bp + b_size);
}

sf_block *coalesce(sf_block *bp){
    size_t new_size = GET_SIZE(&(bp->header));

    sf_block *prev = get_prev_block(bp);
    if (prev != NULL && (char *)prev >= (char *)sf_mem_start() + 8 
        && !IS_ALLOCATED(&(prev->header))){
        remove_from_free_list(prev);
        size_t prev_size = GET_SIZE(&(prev->header));
        new_size += prev_size;
        bp = prev;
        set_block_meta_data(prev, 0, new_size, 0);
    }

    sf_block *next = get_next_block(bp);
    if (next != NULL && (char *)next < (char *)sf_mem_end() - 8
        && !IS_ALLOCATED(&(next->header))){
        remove_from_free_list(next);
        size_t next_size = GET_SIZE(&(next->header));
        new_size += next_size;
        set_block_meta_data(bp, 0, new_size, 0);
    }

    return bp;
}

sf_block *split_block(sf_block *bp, size_t split_size, size_t payload_size){
    size_t remain_size = GET_SIZE(&(bp->header)) - split_size;
    remove_from_free_list(bp);
    set_block_meta_data(bp, payload_size, split_size, THIS_BLOCK_ALLOCATED);
    sf_block *remain = (sf_block *)((char *)bp + split_size);
    set_block_meta_data(remain, 0, remain_size, 0);

    int remain_index = freelist_index(remain_size);
    insert_free_list(remain, remain_index);

    return bp;
}


sf_block *search_free_list_for_block(size_t requested){
    int start = freelist_index(requested);
    // Search from first size-eligible free_list, move to next if block not found
    for (int i = start; i<NUM_FREE_LISTS; i++){
        sf_block *head = &(sf_free_list_heads[i]);
        sf_block *current = head->body.links.next;

        // Traverse until reached back to head
        while (current != head){
            if (GET_SIZE(&(current->header)) >= requested) {
                // Block with sufficient size found
                return current;
            }
            current = current->body.links.next;
        }
    }
    // No block in the free lists could satisfy the size
    return NULL;
}

void insert_free_list(sf_block *bp, int index){
    set_block_flags(bp, 0 /*alloc*/, 0 /*quicklist*/);
    // LIFO principle
    bp->body.links.next = sf_free_list_heads[index].body.links.next;
    bp->body.links.prev = &(sf_free_list_heads[index]);
    sf_free_list_heads[index].body.links.next->body.links.prev = bp;
    sf_free_list_heads[index].body.links.next = bp;
}

void remove_from_free_list(sf_block *bp){
    // Update the links of the previous / next block in free list
    bp->body.links.next->body.links.prev = bp->body.links.prev;
    bp->body.links.prev->body.links.next = bp->body.links.next;

    // Remove links to previous / next block in free list
    bp->body.links.next = NULL;
    bp->body.links.prev = NULL;
}

void insert_quick_list(sf_block *bp, int index){
    // If exceed capacity, flush quicklist first
    if (sf_quick_lists[index].length == QUICK_LIST_MAX){
        for (int i = 0; i<QUICK_LIST_MAX; i++){

            sf_block *ptr = pop_quick_list(index);
            // Coalesce
            ptr = coalesce(ptr);

            // Insert back to free_list
            int size = GET_SIZE(&(ptr->header));
            int f_index = freelist_index(size);
            insert_free_list(ptr, f_index);
        }
    }
    // Update flags
    set_block_flags(bp, 1 /*alloc*/, 1 /*quicklist*/);
    // LIFO principle
    bp->body.links.next = sf_quick_lists[index].first;
    sf_quick_lists[index].first = bp;
    sf_quick_lists[index].length += 1;

}

sf_block *pop_quick_list(int index) {
    if (sf_quick_lists[index].length == 0)
        return NULL;

    // Pop the first block
    sf_block *bp = sf_quick_lists[index].first;
    sf_quick_lists[index].first = bp->body.links.next;
    sf_quick_lists[index].length--;

    bp->body.links.next = NULL;
    set_block_flags(bp, 0 /*free*/, 0 /*quicklist*/);
    set_block_meta_data(bp, 0, GET_SIZE(&(bp->header)), 0);

    return bp;
}

int freelist_index(int requested){
    int size = MIN_BLOCK_SIZE, index = 0;
    while (requested > size && index < (NUM_FREE_LISTS - 1)){
        size *= 2;
        index += 1;
    }
    return index;
}

int quicklist_index(int size){
    return (size - MIN_BLOCK_SIZE) / 16;
}

size_t calculate_block_size(size_t size){
    // Add overhead for header / footer
    size_t result = size + 16;

    // Make it at least MIN_BLOCK_SIZE if less than
    if (result < MIN_BLOCK_SIZE){
        result = MIN_BLOCK_SIZE;
    } else { // Round up to the nearest 16 multiple
        if (result % 16 != 0) {
            result += (16 - (result % 16));
        }
    }
    return result;
}

int valid_pointer(sf_block *p){
    // ptr is NULL
    if (p == NULL) return 0;
    // ptr is not 16 byte aligned
    //if ((uintptr_t)p % 16) return 0;

    // ptr is before prologue
    //if ((char *)p < (char *)sf_mem_start() + 40) return 0;
    // ptr is after epilogue
    //if ((char *)p > (char *)sf_mem_end() - 8) return 0;

    size_t header = GET(&(p->header));
    size_t size = header & ~0xF;

    // Must be at least MIN_BLOCK_SIZE
    if (size < MIN_BLOCK_SIZE) return 0;

    // Must be 16 byte aligned
    if (size % 16 != 0) return 0;

    return 1;
}