#include "compact.h"
#include "compact_internal.h"
#include <stdio.h>
#include <stdlib.h>

/* memory layout
 */
void *g_memory_bottom;
void *g_memory_top;
uint32_t g_memory_size;

/* linked list at each position
 * each stores 2^k - 2^(k+1) sized blocks
 */
free_memory_block_t **g_free_block_slots;
short g_free_block_slot_count; // log2(heap_size)
int g_free_block_hits = 0;
uint32_t g_free_block_alloc = 0;

/* header */
// headers grow down in memory
header_t *g_header_top;
header_t *g_header_bottom;
header_t *g_header_root; // linked list

header_t *g_free_header_root;
header_t *g_free_header_end; // always NULL as its last element.

// code

/* utility */
uint32_t log2_(uint32_t n)
{
    int r = 0;
    while (n >>= 1)
        r++;
    return r;
}

/* header */

bool header_is_unused(header_t *header) {
    // FIXME: Make use of g_free_header_root / g_free_header_end -- linked
    // list. grab the first element, relink?
    //
    // if memory <= g_header_top && memory >= g_header_bottom, then it is also
    // unused.
    //
    return header && header->memory == NULL;
}

header_t *header_set_unused(header_t *header) {
    //fprintf(stderr, "header_set_unused (%d kb)\n", header->size/1024);

    // 1. root == NULL => end == NULL; root = end = header;
    // 2. root == end => end->memory = header; end = header
    // 3. root != end => end->memory = header; end = header

#if 0
    if (!g_free_header_root) {
        g_free_header_root = header;
        g_free_header_end = header;
    } else {
        g_free_header_end->memory = header;
        g_free_header_end = header;
    }
#endif

    header->memory = NULL;
    // note: do not reset the next pointer. once it's in the list, it should
    // stay there.
    return header;
}
/* find first free header. which is always the *next* header.
 */
header_t *header_find_free() {
    // FIXME: make use of g_free_header_root! the first element is always the
    // one to use?
    header_t *h = g_header_top;
    while (h >= g_header_bottom) {
        if (header_is_unused(h))
            return h;
        h--;
    }
    return NULL;
}

header_t *header_new(bool insert_in_list) {
    header_t *header = header_find_free();
    if (!header) {
        if (g_header_bottom-1 > g_memory_top) {
            g_header_bottom--;
            header = g_header_bottom;
        } else
            header = NULL;
    }
    if (header) {
        header->flags = HEADER_UNLOCKED;
        header->memory = NULL;

        if (insert_in_list && header->next == NULL && header != g_header_root) {
            header->next = g_header_root;

            //fprintf(stderr, "g_header_root = %p, next = %p, header = %p -then- ", g_header_root, g_header_root->next, header);
            g_header_root = header;
            //fprintf(stderr, "g_header_root = %p, next = %p, header = %p\n", g_header_root, g_header_root->next, header);
        }

    }
    return header;
}

/* memory block */

free_memory_block_t *block_from_header(header_t *header) {
    return (free_memory_block_t *)((uint8_t *)header->memory + header->size) - 1;
}

header_t *freeblock_find(uint32_t size);
header_t *block_new(int size) {
    // minimum size for later use in free list: header pointer, next pointer
    if (size < sizeof(free_memory_block_t))
        size = sizeof(free_memory_block_t);

    if ((uint8_t *)g_memory_top+size < (uint8_t *)g_header_bottom) {
        header_t *h = header_new(true);
        if (!h)
            return NULL;

        // just grab off the top
        h->size = size;
        h->memory = g_memory_top;
        h->flags = HEADER_UNLOCKED;

        g_memory_top = (uint8_t *)g_memory_top + size;

        return h;
    } else {
        // nope. look through existing blocks
        header_t *h = freeblock_find(size);

        // okay, we're *really* out of memory
        if (!h)
            return NULL;

        h->flags = HEADER_UNLOCKED;

        g_free_block_hits++;
        g_free_block_alloc += size;

        return h;
    }
}

void freeblock_print() {
    for (int i=0; i<g_free_block_slot_count; i++) {
        fprintf(stderr, "%d / %d bytes / %d kb: ", i, 1<<i, (1<<i)/1024);
        free_memory_block_t *b = g_free_block_slots[i];
        while (b) {
            fprintf(stderr, "{%p size %d kb} ", b, b->header->size/1024);
            b = b->next;
        }
        fprintf(stderr, "\n");
    }
}

bool freeblock_exists_memory(void *ptr) {
    for (int i=0; i<g_free_block_slot_count; i++) {
        free_memory_block_t *b = g_free_block_slots[i];
        while (b) {
            if (b->header->memory == ptr)
                return true;

            b = b->next;
        }
    }
    return false;
}

bool freeblock_exists(free_memory_block_t *block) {
    for (int i=0; i<g_free_block_slot_count; i++) {
        free_memory_block_t *b = g_free_block_slots[i];
        while (b) {
            if (b == block)
                return true;

            b = b->next;
        }
    }
    return false;
}

void freeblock_assert_sane(free_memory_block_t *block) {
    if (block != block_from_header(block->header)) {
        int diff;
        if ((uint8_t *)block > (uint8_t *)block_from_header(block->header))
            diff = (uint8_t *)block - (uint8_t *)block_from_header(block->header);
        else
            diff = (uint8_t *)block_from_header(block->header) - (uint8_t *)block;

        fprintf(stderr, "freeblock_assert_sane(%p size %d): diff %d bytes\n", block, block->header->size, diff);
        abort();
    }
}

bool freeblock_checkloop(free_memory_block_t *block) {
    free_memory_block_t *a = block;
    while (block != NULL) {
        block = block->next;
        if (block == a) {
            fprintf(stderr, "loop in memory block %p slot %d\n", block, log2_(block->header->size));
            abort();
            return true;
        }
    }
    return false;
}

void freeblock_verify_lower_size() {
    for (int k=0; k<g_free_block_slot_count; k++) {
        free_memory_block_t *b = g_free_block_slots[k];
        int size = 1<<k;
        while (b) {
            if (b->header->size < size) {
                fprintf(stderr, "\nfreeblock_verify_lower_size(): block %p at mem %p at k=%d has size %d < %d\n",
                        b, b->header->memory, k, b->header->size, size);

                abort();
            }

            b = b->next;
        }
    }
}

/* 1. mark the block's header as free
 * 2. insert block info
 * 3. extend the free list
 */
header_t *block_free(header_t *header) {
    if (!header || header->flags == HEADER_FREE_BLOCK)
        return header;

    freeblock_verify_lower_size();

    bool in_free_list = false;

    // FIXME: merge with previous block places blocks in a too small slot.

    // TODO: merge cannot work, period, since the block is already in the free list
    // and thus has the incorrect address.
#if 0
    // are there blocks before this one?
    free_memory_block_t *prevblock = (free_memory_block_t *)header->memory - 1;
    if (prevblock >= g_memory_bottom) {
        // is it a valid header?
        if (prevblock->header >= g_header_bottom
            && prevblock->header <= g_header_top
            && prevblock->header->flags == HEADER_FREE_BLOCK) {

            // does it point to the same block?
            if ((uint8_t *)prevblock->header->memory + prevblock->header->size == header->memory) {
                //fprintf(stderr, "merging previous block %p with block %p\n", prevblock, block_from_header(header));

                fprintf(stderr, "\nmerging block headers %p (%d bytes) and %p (%d bytes)  to new size %d, block %p\n", header, header->size, prevblock->header, prevblock->header->size, prevblock->header->size+header->size, block_from_header(prevblock->header));

                // yup, merge previous and this block
                prevblock->header->size += header->size;

                // kill off the current block
                header_set_unused(header);

                // set new current block
                header = prevblock->header;

                // put the extended block info in place at the end
                free_memory_block_t *endblock = block_from_header(header);
                endblock->header = header;
                endblock->next = prevblock->next;
                //memcpy(endblock, prevblock, sizeof(free_memory_block_t));

                freeblock_assert_sane(endblock);

                // it's already in the free list, no need to insert it again.
                in_free_list = true;

                // NOTE: do not insert it into the free slot list -- move it
                // to the right location at alloc, if needed.
            }
        }
    }
#endif

#if 0 // TODO: Future work - needs a prev pointer!
    // are there blocks after this one?
    free_memory_block_t *nextblock = (free_memory_block_t *)((uint8_t *)header->memory+header->size) + 1;
    if (nextblock <= g_memory_top) {
        // is it a valid header?
        if (nextblock->header >= g_header_bottom && nextblock->header <= g_header_top && nextblock->header->flags == HEADER_FREE_BLOCK) {
            // does it point to the same block?
            if ((uint8_t *)nextblock->header->memory == nextblock) {
                // yup, merge this block and the next.
                header->size += nextblock->header->size;

                // kill off the next block
                header_set_unused(nextblock);

                // set new current block
                header = prevblock->header;

                // it's already in the free list, no need to insert it again.
                in_free_list = true;
            }
        }
    }
#endif

    // our work is done here
    if (in_free_list)
        return header;

    // alright, no previous or next block to merge with.
    // update the free list
    free_memory_block_t *block = block_from_header(header);
    //fprintf(stderr, "block from header in free(): %p\n", block);

    // header's tracking a block in the free list
    header->flags = HEADER_FREE_BLOCK;

    // FIXME: block->header = header won't do?
    /* crash at the line below.
     *
     * output from gdb:
     * (gdb) print block
     * $8 = (free_memory_block_t *) 0x90909088
     *
     * which is just the *contents* of block. That's wrong. Block should point
     * to the address!
     *
     * also, header is wrong to begin with:
     *
     * (gdb) print header
     * $12 = (header_t *) 0xaecf0498
     * (gdb) print header->memory
     * $13 = (void *) 0x48484848
     * (gdb) print header->size
     * $14 = 1212696648
     * (gdb) 
     * (gdb) print g_header_bottom
     * $16 = (header_t *) 0xaecf0498 <-- same as header
     * (gdb) print g_memory_top
     * $17 = (void *) 0xaecf0179
     */

    // FIXME: I want to change the values of 'block'. should not that be
    // block->foo = 42?  or do I have to memcpy()
    //
    // after a block is freed and the free_memory_block_t is written 
    free_memory_block_t b;
    b.header = header;
    b.next = NULL;
    memcpy((void *)block, (void *)&b, sizeof(free_memory_block_t));

    ///block->header = header;
    //block->next = NULL;

    if (block->header->size + (uint8_t *)block->header->memory >= (void *)g_header_bottom)
        abort();

    //fprintf(stderr, "block_free(): block = %p, block->header = %p (header = %p) size %d memory %p\n", block, block->header, header, block->header->size, block->header->memory);

    // insert into free size block list, at the start.
    int index = log2_(header->size);

    if (block->header->size != header->size)
        abort();

    block->next = g_free_block_slots[index];
    g_free_block_slots[index] = block;

    /*
    free_memory_block_t *current = g_free_block_slots[index];
    //if (g_free_block_slots[index] && g_free_block_slots[index]->next) fprintf(stderr, "block_free(), current = %p g_free_block_slots[%d] = %p next = %p block = %p\n", current, index, g_free_block_slots[index], g_free_block_slots[index]->next, block);
    //else fprintf(stderr, "block_free(), current = %p g_free_block_slots[%d] = %p block = %p\n", current, index, g_free_block_slots[index], block);

    g_free_block_slots[index] = block;
    //fprintf(stderr, "block_free(), current = %p g_free_block_slots[%d] = %p block = %p\n", current, index, g_free_block_slots[index], block);
    g_free_block_slots[index]->next = current;
    //fprintf(stderr, "block_free(), current = %p g_free_block_slots[%d] = %p next = %p block = %p\n", current, index, g_free_block_slots[index], g_free_block_slots[index]->next, block);
    */


    // FIXME: crashes here. at some point, this block is overwritten, i.e.
    // it's been used even though it's freed. Try disabling cfree()'s prev block merge?

    //freeblock_checkloop(g_free_block_slots[index]);
    freeblock_checkloop(block);

#if 0 // FUTURE WORK for forward merges
    // insert duplicate back-pointers for large blocks
    if (header->size >= 12)
        memcpy(header->memory, header, sizeof(header_t));
#endif


#if 0 // FUTURE WORK
    // mark header as free in 'free header' bitmap
#endif
    return header;
}

/* free block list */

/* insert item at the appropriate location.
 * don't take into consideration that it can exist elsewhere
 */
void freeblock_insert(free_memory_block_t *block) {

    if (block->header->size + (uint8_t *)block->header->memory >= (void *)g_header_bottom)
        abort();

    int k = log2_(block->header->size);

    /*
    free_memory_block_t *b = g_free_block_slots[k];
    g_free_block_slots[k] = block;
    g_free_block_slots[k]->next = b;
    */
    block->next = g_free_block_slots[k];
    g_free_block_slots[k] = block;

    freeblock_verify_lower_size();

#if 0
    if (b) {
        g_free_block_slots[k]->next = b;
        //fprintf(stderr, "inserting block %p at slot %d before block %p (size %d kb), slot[k] = %p next = %p\n", block, k, b, b->header->size/1024, g_free_block_slots[k], g_free_block_slots[k]->next);
    } else {
        //fprintf(stderr, "inserting block %p at slot %d, slot[k] = %p next = %p\n", block, k, g_free_block_slots[k], g_free_block_slots[k]->next);
        g_free_block_slots[k]->next = NULL;
    }
#endif
}

/* splits up a block by size.
 * returns rest iff rest-size >= sizeof(free_memory_block_t)
 * input block is always the block that's going to be used in client code
 *
 * input:  [                        block]
 * output: [     rest|              block]
 */
free_memory_block_t *freeblock_shrink_with_header(free_memory_block_t *block, header_t *h, uint32_t size) {
    if (!block)
        return NULL;

    int diff = block->header->size - size;
    if (diff < sizeof(free_memory_block_t))
        return NULL;

    if (!h) {
        h = header_new(true);
        if (h == NULL)
            return NULL;
    }

    if (size > block->header->size)
        abort();

    if (h == block->header)
        fprintf(stderr, "ERROR: freeblock_shrink, new header %p same as block header %p\n", h, block->header);

    freeblock_assert_sane(block);

    fprintf(stderr, "freeblockshrink: address of block->memory = %p with size = %d, address of block = %p == %p (or error!)\n", block->header->memory, block->header->size, block, (uint8_t *)block->header->memory + block->header->size - sizeof(free_memory_block_t));

    h->flags = HEADER_FREE_BLOCK;
    h->memory = block->header->memory;
    h->size = diff;

    block->header->memory = (uint8_t *)block->header->memory + diff;
    block->header->size = size;

    //fprintf(stderr, "freeblock_shrink, h memory %p size %d block h memory %p size %p\n", h->memory, h->size, block->header->memory, block->header->size);

    free_memory_block_t *b = block_from_header(h);
    b->next = NULL; 
    b->header = h;

    fprintf(stderr, "    3. freeblockshrink withheader h: %p  %d  %p  %d\n", h, h->size, h->memory, h->flags);

    if (b == block)
        fprintf(stderr, "ERROR: freeblock_shrink, new block %p (memory %p size %d) old block %p (memory %p size %d)\n",
                b, b->header->memory, b->header->size,
                block, block->header->memory, block->header->size);

    return b;
}
free_memory_block_t *freeblock_shrink(free_memory_block_t *block, uint32_t size) {
    return freeblock_shrink_with_header(block, NULL, size);
}


/* look for a block of the appropriate size in the 2^k list.
 *
 * any block that are larger than the slot's size will be moved upon traversal!
 */
header_t *freeblock_find(uint32_t size) {
    // there can be blocks of 2^k <= n < 2^(k+1)
    int target_k = log2_(size)+1;
    int k = target_k;
   
    // any blocks of >= upper_size will be moved be de-linked and moved to the
    // appropriate slot
    int upper_size = 1<<(k+1);

    free_memory_block_t *block = NULL;
    free_memory_block_t *found_block = NULL;
    free_memory_block_t *fallback_block = NULL;

    freeblock_verify_lower_size();

    // slot too large, need to do a full scan.
    if (k == g_free_block_slot_count) {
        k--;
        block = g_free_block_slots[k];
        free_memory_block_t *prevblock = block, *nextblock = block;
        while (block) {
            if (block->header->size >= size) {
                // found the block. remove it from the list.
                if (block == g_free_block_slots[k])
                    g_free_block_slots[k] = block->next;
                else
                    prevblock->next = block->next;

                found_block = block;
                break; 
            }

            prevblock = block;
            block = block->next;
        }
    } else {
        // k is within the bounds of the slots

        // does this slot have any free blocks?
        block = g_free_block_slots[k];
        //fprintf(stderr, "freeblock_find() block at %d = %p (at pos %p)\n", k, block, &block);
        while (!block && k < g_free_block_slot_count) {
            // nope, move up to the next block
            k++;
            if (k < g_free_block_slot_count) {
                upper_size = 1<<(k+1);
                block = g_free_block_slots[k];
                //fprintf(stderr, "freeblock_find() block at %d = %p (at pos %p)\n", k, block, &block);
            } 
        }

        if (block) {
            // yeah, there's a block here. it's also guaranteed to fit.
            
            // remove the item from list.
            block = g_free_block_slots[k];
            g_free_block_slots[k] = block->next;

            fprintf(stderr,"*1. %p -> %p (%c)?\n", block, block->header, (uint32_t)block->header&0x000000FF);

            if (block->header->size > upper_size) {
                // current next block. when moved, the next block will point to something else.
                free_memory_block_t *nextblock = block->next;
                fallback_block = block;

                freeblock_insert(block);

                block = nextblock;
            } else {
                if (block->header->size < size) {
                    fprintf(stderr, "block %p too small (%d vs %d) in slot %d vs actual k = %d\n",
                            block, block->header->size, size, k, log2_(size));
                    abort();
                }

                found_block = block;
            }
        } else {
            // didn't find anything. do a full scan of the actual sized-k.
            k = log2_(size);
            upper_size = 1<<(k+1);

            fprintf(stderr, "freeblock_find(%d) scanning in %d\n", size, k);

            block = g_free_block_slots[k];
            free_memory_block_t *prevblock = block, *nextblock = block;
            while (block) {
                // there's a block here. it's also guaranteed to fit.

                fprintf(stderr,"*2. %p -> %p (%c) size %d (%s)?\n", block, block->header, (uint32_t)block->header&0x000000FF, block->header->size, block->header->size >= size ? "yes" : "no");

#if 1 // does not work, freeblock_insert()
                if (block->header->size >= size) {
                    // remove from the root? (easier)
                    if (g_free_block_slots[k] == block) {
                        // figure out what to do with the block
                        if (block->header->size > upper_size) {
                            fprintf(stderr, "-> root, too large, moving %p, next = %p.\n", block, block->next);
                            fallback_block = block;
                            g_free_block_slots[k] = block->next;

                            freeblock_insert(block);

                            block = g_free_block_slots[k];
                            fprintf(stderr, "->-> block = %p\n", block);
                        } else {
                            // found it!
                            fprintf(stderr, "-> root, correct size.\n");
                            found_block = block;
                            g_free_block_slots[k] = block->next;
                            break;
                        }
                    } else {
                        // not at root. (trickier)

                        // figure out what to do with the block
                        if (block->header->size > upper_size) {
                            fprintf(stderr, "-> not root, too large, moving %p, next = %p.\n", block, block->next);
                            fallback_block = block;

                            nextblock = block->next;
                            prevblock->next = nextblock;

                            freeblock_insert(block);

                            block = nextblock;
                            fprintf(stderr, "->-> block = %p\n", block);

                            if (prevblock->next != block) abort();
                        } else {
                            // found it!
                            fprintf(stderr, "-> not root, correct size.\n");
                            prevblock->next = block->next;
                            found_block = block;
                            break;
                        }
                    } 
                } else {
                    prevblock = block;
                    block = block->next;
                }
#endif
            }
        }
    }

    if (found_block) {
        // resize & insert
        free_memory_block_t *rest = freeblock_shrink(found_block, size);
        if (rest)
            freeblock_insert(rest);

        return found_block->header;
    } else if (fallback_block) {
        // rats, no block found.
        int fallback_k = log2_(fallback_block->header->size);

        // since we just inserted the fallback block, we know that it's
        // placed at the very start of this slot.  remove it by relinking the
        // slot, and use it.
        g_free_block_slots[fallback_k] = fallback_block->next;

        //fprintf(stderr, "No go, using fallback block %p of %d kb\n", fallback_block, fallback_block->header->size/1024);
        free_memory_block_t *rest = freeblock_shrink(fallback_block, size);
        if (rest)
            freeblock_insert(rest);

        return fallback_block->header;
    }
    
    // no block found.
    return NULL;
}


uint32_t stat_total_free_list() {
    uint32_t total = 0;
    for (int i=0; i<g_free_block_slot_count; i++) {
        free_memory_block_t *b = g_free_block_slots[i];
        free_memory_block_t *a = b;
        while (b != NULL) {
            total += b->header->size;
            b = b->next;
            if (a == b) {
                fprintf(stderr, "stat_total_free_list(), panic - found a loop in slot %d item %p!\n", i, a);
                freeblock_print();
            }
        }
    }
    return total;
}

void header_sort_all() {
    g_header_root = header__sort(g_header_root, header__cmp);
}

/* compacting */
void compact() {

    /* 1. sort header list
     * 2. grab first block and slide it.
     * ...?
     */

    header_sort_all();
    
    /*
     * debug printouts
     */

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define WITH_ITER(h, init, body...) {header_t *h = init; while (h != NULL) {body; h = h->next;}}

    // decide smallest block size for printing
    uint32_t smallest=1<<31, largest=0, total=0;
    WITH_ITER(h, g_header_root,
        if (h->flags == HEADER_FREE_BLOCK) {
            if (h->size < smallest)
                smallest = h->size;
            if (h->size > largest)
                largest = h->size;
            total += h->size;
        }
    )

    // print map
    WITH_ITER(h, g_header_root, 
        int count = MAX(1, h->size/1024);
        for (int i=0; i<count; i++) {
            if (i==0)
                fputc('O', stderr);
            else
            if (h->flags == HEADER_FREE_BLOCK)
                fputc('_', stderr);
            else if (h->flags == HEADER_UNLOCKED)
                fputc('|', stderr);
            else if (h->flags == HEADER_LOCKED)
                fputc('X', stderr);
        }
    ) fputc('\n', stderr);

    WITH_ITER(h, g_header_root,
        fprintf(stderr, "%d kb %s\n", h->size, h->flags == HEADER_FREE_BLOCK ? "free" : "used");
    )
    fprintf(stderr, "largest free block %d kb vs total size %d kb yields coherence %.4f, relative coherence %.4f\n",
            largest, total, 1.0-(float)largest/(float)total, 1.0-(float)smallest/(float)largest);

    /* compacting */

    /* make one pass.
     *
     * a(f)->b(f)->c(f)->d(u)->e(u)->f(u)->g(x/u)
     * =>
     * a(nil)->b(nil)->[d..f]->c(f)->g(x/u)
     *
     * This means we need to find:
     *  - first free
     *  - last free
     *  - first used
     *  - last used
     *  - next
     *
     * set unused of affected headers,
     * link last free-1 to first free
     * link last free to last free
     *
     */

    header_t *root_header = g_header_root;
    header_t *prev = root_header;

repeat:
    if (!root_header) {
        fprintf(stderr, "******************** compact done\n");
        // print map
        WITH_ITER(h, g_header_root, 
            int count = MAX(1, h->size/1024);
            for (int i=0; i<count; i++) {
                if (i==0)
                    fputc('O', stderr);
                else
                if (h->flags == HEADER_FREE_BLOCK)
                    fputc('_', stderr);
                else if (h->flags == HEADER_UNLOCKED)
                    fputc('|', stderr);
                else if (h->flags == HEADER_LOCKED)
                    fputc('X', stderr);
            }
        ) fputc('\n', stderr);

        return;
    }

    header_t *h = root_header;

    while (h && h->flags != HEADER_FREE_BLOCK) {
        prev = h;
        h = h->next;
    }

    // free blocks: first and last.
    header_t *first_free = h;
    header_t *last_free = h;
    int total_free = 0;
    while (h && h->flags == HEADER_FREE_BLOCK) {
        last_free = h;
        total_free += h->size;
        h = h->next;
    }

    if (!h) {fprintf(stderr, "no free blocks\n"); root_header = NULL; goto repeat;}

    // fast forward to used blocks, skipping over any non-used blocks
    while (h && h->flags != HEADER_UNLOCKED) {
        fprintf(stderr, "while != HEADER_UNLOCKED: %p flags %d\n", h, h->flags);
        h = h->next;
    }

    if (!h) {fprintf(stderr, "no fast forwarded blocks\n"); root_header = NULL; goto repeat;}

    header_t *first_used = h;
    header_t *last_used = h;
    int total_used = 0;
    bool adjacent = true;
    if (last_free->next != first_used)
        adjacent = false;

    while (h && h->flags == HEADER_UNLOCKED) {
        if (adjacent) {
            fprintf(stderr, "while == HEADER_UNLOCKED: %p flags %d, size %d\n", h, h->flags, h->size);
            total_used += h->size;
            last_used = h;
            h = h->next;
        } else {
            // must move, can't just push down
            // XXX: Correct?
            if (total_used + h->size <= total_free) {
                fprintf(stderr, "while == HEADER_UNLOCKED: %p flags %d, size %d\n", h, h->flags, h->size);
                total_used += h->size;
                last_used = h;
                h = h->next;
            } else
                break;
        }
    }

    if (!h) {fprintf(stderr, "no unlocked blocks\n"); root_header = NULL; goto repeat;}

    fprintf(stderr, "total used = %d, total free = %d\n"
            "first - last free = %p - %p, first - last used = %p - %p\n", total_used, total_free,
            first_free, last_free, first_used, last_used);

    /* ok, we now have a range of free blocks and a range of used blocks.
     *
     * if adjacent, just push them back.
     * if not adjacent, do other tricky magic.
     */
    if (adjacent) {
        smallest = 1024;
        /*
        fprintf(stderr, "before move: \n");
        header_t *h = prev->next;
        while (h && h != last_used->next) {
            int count = MAX(1024, h->size/smallest);
            for (int i=0; i<count; i++) {
                if (h->flags == HEADER_FREE_BLOCK) fputc('_', stderr);
                else if (h->flags == HEADER_UNLOCKED) fputc('|', stderr);
                else if (h->flags == HEADER_LOCKED) fputc('X', stderr);
            }

            h = h->next;
        }
        fprintf(stderr, "\n");
        */


        // easy case, just push everything back.
        void *to_addr = first_free->memory;
        void *from_addr = first_used->memory;

        // null out free headers
        h = first_free;
        while (h != last_free->next) {
            header_set_unused(h);
            h = h->next;
        }

        // relink into place
        if (prev == g_header_root) {
            g_header_root->next = first_used;
        } else
            prev->next = first_used;

        header_t *new_free = header_new(false);
        new_free->size = total_free;
        new_free->memory = (uint8_t *)first_free->memory + total_used;
        new_free->next = last_used->next;
        new_free->flags = HEADER_FREE_BLOCK;
        last_used->next = new_free;

        fprintf(stderr, "moving memory from %p to %p size %d\n", from_addr, to_addr, total_used);
        //memmove(to_addr, from_addr, total_used);

        /*
        fprintf(stderr, "after move: \n");
        h = prev->next;
        while (h && h != new_free->next) {
            int count = MAX(1024, h->size/smallest);
            for (int i=0; i<count; i++) {
                if (h->flags == HEADER_FREE_BLOCK)
                    fputc('_', stderr);
                else if (h->flags == HEADER_UNLOCKED)
                    fputc('|', stderr);
                else if (h->flags == HEADER_LOCKED)
                    fputc('X', stderr);
            }

            h = h->next;
        }
        fprintf(stderr, "\n");
        */

        prev = last_used;
        root_header = prev->next;
    }

    goto repeat;


#if 0
    int offset = (uint8_t *)first_used->memory - (uint8_t *)first_free->memory;
    free_memory_block_t block = {last_free, NULL};
    header_t h2;
    fprintf(stderr, "offset = %d (free size = %d kb, used size = %d kb)\n", offset, free_size/1024, used_size/1024);
    memmove(first_used->);
#endif


    // first_used - last_used of the blocks that can be moved.
    // 0. shrink last free block to appropriate size
    // 1. memmove()
    // 2. adjust offset of used block headers
    // 3. kill free headers
    // 4. done!

    // rebuild free list
    memset((void *)g_free_block_slots, 0, sizeof(free_memory_block_t *) * g_free_block_slot_count);

    WITH_ITER(h, g_header_root,
        if (!h->memory || h->flags != HEADER_FREE_BLOCK) {
            h = h->next;
            continue;
        }

        if (h->size >= sizeof(free_memory_block_t)) {
            int k = log2_(h->size);
            free_memory_block_t *block = block_from_header(h);
            if (g_free_block_slots[k] == NULL)
                g_free_block_slots[k] = block;
            else {
                block->next = g_free_block_slots[k];
                g_free_block_slots[k] = block;
            }
        }
    )
    
}



#if 0
void compact() {
    /* the super-greedy find first block algorithm!
     *
     * since block_new() is silly, we want to move as much out of the way from
     * the end of our memory block space. let's do so!
     */

    // find the largest free block that also starts fairly early
    // cut-off point at 50%? make that a configurable variable to be testable
    // in benchmarks!
    free_memory_block_t *largest_block = g_free_list_root;
    free_memory_block_t *largest_block_prev = g_free_list_root;
    free_memory_block_t *block = g_free_list_root, *prev = NULL;
    void *lowfree = block->header->memory, *highfree = block->header->memory;
    float cutoff_ratio = 0.5;
    void *cutoff = NULL;

    // find the boundaries of the memory blocks
    while (block) {
        if (block->header->memory < lowfree)
            lowfree = block->header->memory;

        if (block->header->memory > highfree)
            highfree = block->header->memory;

        block = block->next;
    }

    cutoff = (void *)((uint32_t)lowfree + (uint32_t)(((uint8_t *)highfree-(uint8_t *)lowfree)*cutoff_ratio));

    block = g_free_list_root;
    prev = g_free_list_root;

    while (block) {
        if (block->header->size > largest_block->header->size && block->header->memory < cutoff) {
            largest_block_prev = prev;
            largest_block = block;
        } 

        prev = block;
        block = block->next;
    }

    // Panic. Can't happen, unless above is wrong. Which it isn't...?
    if (largest_block_prev->next != largest_block)
        fprintf(stderr, "******************* PREV->next != BLOCK!!! (%p -> %p)\n", largest_block_prev, largest_block);

    uint32_t size = (uint8_t *)highfree - (uint8_t *)lowfree;
    printf("free block range: lowest %p to highest %p (%d K) with cutoff %p\n", lowfree, highfree, size/1024, cutoff);
    printf("best suited free block at %p (%d kb from lowest) of size %d kb\n", largest_block->header->memory, ((uint32_t)largest_block->header->memory - (uint32_t)lowfree)/1024, largest_block->header->size/1024);

    // we have the largest free block.
    // memory grows up: look for highest addresses that will fit.
    header_t *h = g_header_top;
    header_t *highest = g_header_top;
    while (h != g_header_bottom) {
        if (h->flags == HEADER_UNLOCKED /* || h->flags == HEADER_WEAK_LOCKED */
            && h->memory > highest->memory && h->size <= largest_block->header->size) {
            // a winner!

            printf("larger header h %d, size %d kb, memory %p\n", h->flags, h->size/1024, h->memory);

            highest = h;
        } else
            printf("smaller header h %d, size %d kb, memory %p\n", h->flags, h->size/1024, h->memory);

        h--;
    }

    // 1. copy the used block to the free block.
    // 2. point the used block header to the free header's starting address
    // 3a. if free block minus used block larger than sizeof(free_memory_block)
    //     * adjust the free header's start adress and size
    // 3b. if free header less than sizeof(free_memory_block_t):
    //     * add that space to the used block (internal fragmentation)
    //     * mark free block as unused
    //     * point the free block's previous block's next to point to the free block's next block

    // 1. copy the used block to the free block.
    header_t *free_header = largest_block->header;
    free_memory_block_t *largest_block_next = largest_block->next;

    printf("moving block %p (size %d kb) to free block %p (size %d kb)\n", highest->memory, highest->size/1024, free_header->memory, free_header->size/1024);

    memcpy(free_header->memory, highest->memory, highest->size);

    // 2. point the used block header to the free header's starting address
    highest->memory = free_header->memory;
    
    // 3a. if free header larger than sizeof(free_memory_block)
    int diff = free_header->size - highest->size;
    if (diff >= sizeof(free_memory_block_t)) {
        free_header->memory = (void *)((uint32_t)free_header->memory + highest->size);
        free_header->size = diff;
    } else {
        // 3b. if free header less than sizeof(free_memory_block_t):
        highest->size = free_header->size;
        header_set_unused(free_header);

        largest_block_prev->next = largest_block_next;
    } 

}
#endif

/* client code */
void cinit(void *heap, uint32_t size) {
    g_memory_size = size;

    // +1 to round up. e.g. log2(15)==3
    // => 0, 1, 2, but later log2(13) would map to 3!
    // in practice, will there be such a large block?
    g_free_block_slot_count = log2_(size) + 1; 
    g_free_block_slots = (free_memory_block_t **)heap;
    memset((void *)g_free_block_slots, 0, sizeof(free_memory_block_t *)*g_free_block_slot_count);

    g_memory_bottom = (void *)((uint32_t)heap + (uint32_t)(g_free_block_slot_count * sizeof(free_memory_block_t *)));
    g_memory_top = g_memory_bottom;

    g_free_header_root = NULL;
    g_free_header_end = NULL;

    g_header_top = (header_t *)((uint32_t)heap + size);
    g_header_bottom = g_header_top;
    g_header_root = g_header_top;
    g_header_root->next = NULL;

    header_set_unused(g_header_top);

    memset(heap, 0, size);
}

void cdestroy() {
    // nop.
    return;
}

handle_t *cmalloc(int size) {
    header_t *h = block_new(size);

    return (handle_t *)h;
}

void cfree(handle_t *h) {
    block_free((header_t *)h);
}

void *clock(handle_t *h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_LOCKED;

    return f->memory;
}
void *cweaklock(handle_t *h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_WEAK_LOCKED;
    
    return f->memory;
}

void cunlock(handle_t *h) {
    header_t *f = (header_t *)h;
    f->flags = HEADER_UNLOCKED;
}

