#ifndef __compact_internal_h
#define __compact_internal_h

#define  __STDC_LIMIT_MACROS

#include "compact.h"

/* header, see compact.h
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#ifndef JEFF_MAX_RAM_VS_SLOWER_MALLOC
#define JEFF_MAX_RAM_VS_SLOWER_MALLOC 0
#endif

#if __x86_64__
typedef uint64_t ptr_t;
#define PTR_T_MAX UINT64_MAX
#else
typedef uint32_t ptr_t;
#define PTR_T_MAX UINT32_MAX
#endif

typedef enum {
    BLOCK_TYPE_FREE         = 0,
    BLOCK_TYPE_UNLOCKED     = 1,
    BLOCK_TYPE_LOCKED       = 2,
    BLOCK_TYPE_WEAK_LOCKED  = 3,
} rm_block_type_t;


#pragma pack(1)
struct rm_header_t {
    void *memory;
    uint32_t size; // TODO why not size_t?
    uint8_t type;

    struct rm_header_t *next;
#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    struct rm_header_t *next_unused;
#endif
};
#pragma pack()

/* free memory block, see compact.h
 */
typedef struct free_memory_block_t {
    rm_header_t *header;
    struct free_memory_block_t *next; // null if no next block.
} free_memory_block_t;

struct rmalloc_meta_t {
    /* memory layout
     */
    void *memory_bottom;
    void *memory_top;
    uint32_t memory_size; // TODO why not size_t?

    /* linked list at each position
     * each stores 2^k - 2^(k+1) sized blocks
     */
    free_memory_block_t **free_block_slots;
    short free_block_slot_count; // log2(heap_size)
    int free_block_hits;
    uint32_t free_block_alloc;

    /* header */
    // headers grow down in memory
    rm_header_t *header_top;
    rm_header_t *header_bottom;
    rm_header_t *header_root; // linked list
    int header_used_count; // for spare headers in compact TODO why not unsigned?
    rm_header_t *last_free_header;

    rm_header_t *unused_header_root;

    rm_header_t *highest_address_header;

    #ifdef RMALLOC_DEBUG
    uint32_t g_memlayout_sequence = 0;
    static bool g_debugging = false;
    #endif
};

//uint32_t log2_(uint32_t n);
typedef ptr_t (*compare_cb)(void *a, void *b);

// TODO these should be static, except the tests want them
uint32_t rm_log2(uint32_t n);
rm_header_t *rm_header_find_free(void);
free_memory_block_t *rm_block_from_header(rm_header_t *header);
void rm_header_sort_all();
bool rm_header_is_unused(rm_header_t *header);
//static void freeblock_print();
bool rm_freeblock_exists_memory(void *ptr);

// stats and debug

uint32_t rm_stat_total_free_list();
uint32_t rm_stat_largest_free_block();
void *rm_stat_highest_used_address(bool full_calculation);
void rm_stat_print_headers(bool only_type); // only print the type, no headers
void rm_stat_set_debugging(bool enable);



#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __compact_internal_h

