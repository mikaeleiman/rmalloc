#ifndef __compact_internal_h
#define __compact_internal_h

#define  __STDC_LIMIT_MACROS

#include <stdint.h>
#include <stdbool.h>

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

typedef struct header_t {
    void *memory;
    uint32_t size;
    uint8_t flags;

    struct header_t *next;
#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    struct header_t *next_unused;
#endif
} __attribute__ ((packed)) header_t;

/* free memory block, see compact.h
 */
typedef struct free_memory_block_t {
    header_t *header;
    struct free_memory_block_t *next; // null if no next block.
} free_memory_block_t;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

extern void *g_memory_bottom;
extern void *g_memory_top;
extern uint32_t g_memory_size;
extern free_memory_block_t **g_free_block_slots;
extern short g_free_block_slot_count; // log2(heap_size)
extern int g_free_block_hits;
extern uint32_t g_free_block_alloc;
extern uint32_t g_memlayout_sequence;
extern header_t *g_header_top;
extern header_t *g_header_bottom;
extern header_t *g_header_root; // linked list
extern int g_header_used_count; // for spare headers in compact
extern header_t *g_last_free_header;
extern header_t *g_unused_header_root;
extern header_t *g_highest_address_header;
//extern static bool g_debugging = false;

uint32_t log2_(uint32_t n);
typedef ptr_t (*compare_cb)(void *a, void *b);
header_t *header__sort(header_t *list,  int is_circular, int is_double, compare_cb cmp);
ptr_t header__cmp(void *a, void *b);
header_t *header_find_free(bool spare_two_for_compact);
uint32_t rmstat_total_free_list(void);
free_memory_block_t *block_from_header(header_t *header);
void header_sort_all();
bool header_is_unused(header_t *header);
void freeblock_print();
bool freeblock_exists_memory(void *ptr);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __compact_internal_h

