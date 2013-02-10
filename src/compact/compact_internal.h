#ifndef __compact_internal_h
#define __compact_internal_h

/* header, see compact.h
 */

#define HEADER_FREE_BLOCK   0
#define HEADER_UNLOCKED     1
#define HEADER_LOCKED       2
#define HEADER_WEAK_LOCKED  3

typedef struct header_t {
    void *memory;
    int size;
    int flags;

    struct header_t *next;
} header_t;

/* free memory block, see compact.h
 */
typedef struct free_memory_block_t {
    header_t *header;
    struct free_memory_block_t *next; // null if no next block.
} free_memory_block_t;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef int (*compare_cb)(void *a, void *b);
header_t *header__sort(header_t *list, /* int is_circular, int is_double, */ compare_cb cmp);
int header__cmp(void *a, void *b);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __compact_internal_h
