/* compact.h
 *
 * rmalloc compacting allocator for interactive systems
 */
#ifndef __compact_h
#define __compact_h

#include <stddef.h>
#include <stdint.h>

/* memory layout:
 * if (op_time) *op_time = 0;
 * - header list grows from top of stack and downwards
 * - objects grow from bottom of stack and upwards
 * - TODO: switch around, such that the stack always grows from bottom? that
 *   way, the heap could be grown.
 *
 * header:
 * - lock type: unlocked = 0, locked = 1, weak = 2
 * - pointer to memory block
 * - size of memory block
 *
 * free memory block:
 * - 4 bytes at N-4: header address, points back to this block
 * - 4 bytes at N-8: next free memory block.
 *
 * handle:
 * - alias of header, opaque type. means the header block cannot be compacted!
 *
 * header list:
 * - insert new at the first free location
 * - keep bitmap (64 bytes) to narrow down the search of first free block
 * - 64*8=512 bits, initially 8 headers per bit = 4096 entries.
 * - bitmap scaled to number of objects, when limit reached, double and scale
 *   down (b0=b0*b1, b2=b2*b3, ...).
 * - 
 */

typedef struct rm_header_t rm_header_t;
typedef rm_header_t* rm_handle_t;

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

typedef struct rmalloc_meta_t rmalloc_meta_t;

void rminit(void *heap, uint32_t size);
void rmdestroy();

rmalloc_meta_t* rmalloc_get_state(void);
void rmalloc_set_state(rmalloc_meta_t *state);
size_t rmalloc_state_size(void);

rm_handle_t rmmalloc(int size);
void rmfree(rm_handle_t);
void *rmlock(rm_handle_t);
void *rmweaklock(rm_handle_t);
void rmunlock(rm_handle_t);
void rmcompact(uint32_t maxtime);


/* internal */

uint32_t rmstat_total_free_list();
uint32_t rmstat_largest_free_block();
void *rmstat_highest_used_address(bool full_calculation);
void rmstat_print_headers(bool only_type); // only print the type, no headers
void rmstat_set_debugging(bool enable);

#ifdef __cplusplus
};
#endif

#endif // __compact_h
