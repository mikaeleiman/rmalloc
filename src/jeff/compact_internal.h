#ifndef __compact_internal_h
#define __compact_internal_h

#define  __STDC_LIMIT_MACROS

#include "compact.h"

/* header, see compact.h
 */

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

#define HEADER_FREE_BLOCK   0
#define HEADER_UNLOCKED     (1<<0)
#define HEADER_LOCKED       (1<<1)
#define HEADER_WEAK_LOCKED  (1<<2)

#pragma pack(1)
struct header_t {
    void *memory;
    uint32_t size;
    uint8_t flags;

    struct header_t *next;
#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    struct header_t *next_unused;
#endif
};
#pragma pack()

/* free memory block, see compact.h
 */
typedef struct free_memory_block_t {
    header_t *header;
    struct free_memory_block_t *next; // null if no next block.
} free_memory_block_t;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct rmalloc_meta_t {
    /* memory layout
     */
    void *memory_bottom;
    void *memory_top;
    uint32_t memory_size;

    /* linked list at each position
     * each stores 2^k - 2^(k+1) sized blocks
     */
    free_memory_block_t **free_block_slots;
    short free_block_slot_count; // log2(heap_size)
    int free_block_hits;
    uint32_t free_block_alloc;

    /* header */
    // headers grow down in memory
    header_t *header_top;
    header_t *header_bottom;
    header_t *header_root; // linked list
    int header_used_count; // for spare headers in compact
    header_t *last_free_header;

    header_t *unused_header_root;

    header_t *highest_address_header;

    #ifdef RMALLOC_DEBUG
    uint32_t g_memlayout_sequence = 0;
    static bool g_debugging = false;
    #endif
};

//uint32_t log2_(uint32_t n);
typedef ptr_t (*compare_cb)(void *a, void *b);

uint32_t log2_(uint32_t n);
header_t *header_find_free(void);
free_memory_block_t *block_from_header(header_t *header);
void header_sort_all();
bool header_is_unused(header_t *header);
//static void freeblock_print();
bool freeblock_exists_memory(void *ptr);



#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __compact_internal_h

