//#include <stdio.h>

#include "compact.h"
#include "compact_internal.h"

#include <stdlib.h>
#include <string.h>


#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define WITH_ITER(h, init, body...) {header_t *h = init; while (h != NULL) {body; h = h->next;}}

// These are from listsort.c
rm_header_t *header__sort(rm_header_t *list,  int is_circular, int is_double, compare_cb cmp);
ptr_t header__cmp(void *a, void *b);


#ifdef __MACH__
// OSX version
#include <CoreServices/CoreServices.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

static uint64_t uptime_nanoseconds(void)
{
    uint64_t start;
    uint64_t timeNano;
    static mach_timebase_info_data_t    sTimebaseInfo;

    // Start the clock.

    start = mach_absolute_time();

    // Convert to nanoseconds.

    // If this is the first time we've run, get the timebase.
    // We can use denom == 0 to indicate that sTimebaseInfo is 
    // uninitialised because it makes no sense to have a zero 
    // denominator is a fraction.

    if ( sTimebaseInfo.denom == 0 ) {
        (void) mach_timebase_info(&sTimebaseInfo);
    }

    // Do the maths. We hope that the multiplication doesn't 
    // overflow; the price you pay for working in fixed point.

    timeNano = start * sTimebaseInfo.numer / sTimebaseInfo.denom;

    return timeNano / 1000;
}

#elif defined(__BILLY__)

#include "../../bsp/timers.h"

static uint64_t uptime_nanoseconds(void)
{
    // TODO replace with start/elapsed
    return bsp_timer_start() * 1000;
}

#else

#include <time.h>

static uint64_t uptime_nanoseconds(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return t.tv_sec*1000000000 + t.tv_nsec;
}
#endif

#if 0 
#if __x86_64__
typedef uint64_t ptr_t;
#else
typedef uint32_t ptr_t;
#endif
#endif


#ifdef RMALLOC_DEBUG
#define fprintf(...) 
#define fputc(...)
#endif


static rmalloc_meta_t *g_state = NULL;




// code

// http://stackoverflow.com/questions/994593/how-to-do-an-integer-log2-in-c
// and http://gcc.gnu.org/onlinedocs/gcc-4.4.2/gcc/Other-Builtins.html
uint32_t rm_log2(uint32_t n)
{
    //return __builtin_ctz(n);
    return sizeof(n)*8 - 1 - __builtin_clz(n); // builtin_clz() is base 0
}



#ifdef RMALLOC_DEBUG
static void assert_handles_valid(rm_header_t *header_root) {

    (void)header_root;
    // dump_memory_layout();

#if 0
    rm_header_t *h = header_root;
    while (h != NULL) {
#if 0
        if ((ptr_t)h->memory == 0x8000000) {
        //if (!header_is_unused(h) && h->flags != HEADER_FREE_BLOCK && ((ptr_t)h->memory < (ptr_t)g_memory_bottom || (ptr_t)h->memory > (ptr_t)g_memory_top)) {
            abort();
        }
        }
#endif
        if ((ptr_t)h->memory == 0x42424242) abort();
        if ((ptr_t)h->memory == 0x43434343) abort();
        if ((ptr_t)h->memory == 0x44444444) abort();
        if ((ptr_t)h->memory == 0x45454545) abort();
        if ((ptr_t)h->memory == 0xBABEBEEF) abort();

        h = h->next;
    }
#endif
    rm_header_t *h = g_state->header_bottom;
    while (h != g_state->header_top) {
#if 0
        if ((ptr_t)h->memory == 0x8000000) {
        //if (!header_is_unused(h) && h->flags != HEADER_FREE_BLOCK && ((ptr_t)h->memory < (ptr_t)g_memory_bottom || (ptr_t)h->memory > (ptr_t)g_memory_top)) {
            abort();
        }
        }
#endif
        if ((ptr_t)h->memory == 0xbeefbabe || (ptr_t)h->next == 0xbeefbabe ||
            (ptr_t)h->memory == 0x42424242 || (ptr_t)h->next == 0x42424242 ||
            (ptr_t)h->memory == 0x43434343 || (ptr_t)h->next == 0x43434343 ||
            (ptr_t)h->memory == 0x44444444 || (ptr_t)h->next == 0x44444444 ||
            (ptr_t)h->memory == 0x45454545 || (ptr_t)h->next == 0x45454545)
            abort();

        h++;
    }

#if 0
#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    h = g_unused_header_root;
    while (h != NULL) {
        if ((ptr_t)h->memory == 0xbeefbabe || (ptr_t)h->next == 0xbeefbabe ||
            (ptr_t)h->memory == 0x42424242 || (ptr_t)h->next == 0x42424242 ||
            (ptr_t)h->memory == 0x43434343 || (ptr_t)h->next == 0x43434343 ||
            (ptr_t)h->memory == 0x44444444 || (ptr_t)h->next == 0x44444444 ||
            (ptr_t)h->memory == 0x45454545 || (ptr_t)h->next == 0x45454545)
            abort();

        h = h->next_unused;
    }
#endif
#endif
}


void freeblock_verify_lower_size() {
    for (int k=0; k<g_state->free_block_slot_count; k++) {
        free_memory_block_t *b = g_state->free_block_slots[k];
        ptr_t size = 1<<k;
        while (b) {
            if (b->header->size < size || b->header->memory == NULL) {
#ifdef RMALLOC_DEBUG
                fprintf(stderr, "\nfreeblock_verify_lower_size(): block %p at mem %p at k=%d has size %d < %d\n",
                        b, b->header->memory, k, b->header->size, size);
#endif
                abort();
            }

            b = b->next;
        }
    }
}


static uint8_t header_fillchar(rm_header_t *h) {
    return (ptr_t)h & 0xFF;
}


static bool assert_memory_contents(rm_header_t *h)
{
    uint8_t c = header_fillchar(h);
    uint8_t *memory = (uint8_t *)h->memory;

    for (ptr_t i=0; i<h->size; i++)
        if (memory[i] != c)
            return false;

    return true;
}


static void assert_blocks() {
    rm_header_t *node = g_header_root;
    int which = 0;

    while (node != NULL) {
        if (node->type != HEADER_FREE_BLOCK && assert_memory_contents(node) == false)
        {
            //uint8_t c = header_fillchar(node);
            abort();
        }

        which++;
        node = node->next;
    }
}


void dump_memory_layout() {

        rm_header_t *header_root = g_header_root;
            char buf[80];
            sprintf(buf, "/tmp/memory-layout-%.6d.txt", (int)g_memlayout_sequence);
            FILE *fp = fopen(buf, "wt");
            uint32_t smallest=1<<31;
            bool only_type = true;
            fprintf(stderr, "dump to %s\n", buf);

            {
            rm_header_t *h = header_root;
            while (h != NULL) {
                if (h->size < smallest)
                    smallest = h->size;
                h = h->next;
            }
            }

        if (smallest == 0) smallest = 1;
    WITH_ITER(h, header_root, 
        //int count = MAX(1024, h->size/smallest);
        int count = MAX(1, h->size/smallest);
        for (int i=0; i<count; i++) {
            if (i==0) {
                if (only_type == false) {
                    fputc(h->type == HEADER_FREE_BLOCK ? '_' : 'O', fp);
                    fprintf(fp, "(%p)(%p)", h, h->memory);
                } else {
                    fputc('.', fp);
                    if (h->type == HEADER_FREE_BLOCK)
                        fputc('_', fp);
                    else if (h->type == HEADER_UNLOCKED)
                        fputc('|', fp);
                    else if (h->type == HEADER_LOCKED)
                        fputc('X', fp);
                }
            }
            else if (h->type == HEADER_FREE_BLOCK)
                fputc('_', fp);
            else if (h->type == HEADER_UNLOCKED)
                fputc('|', fp);
            else if (h->type == HEADER_LOCKED)
                fputc('X', fp);

        }

    )
        fputc('\n', fp);
        fputc('\n', fp);

    int total = 0;
    WITH_ITER(h, g_header_root,//header_root, 
        //int count = MAX(1024, h->size/smallest);
        int count = MAX(1, h->size/smallest);
        fprintf(fp, "header 0x%X = {memory: 0x%X, size: %.4d, flags: %d, next: 0x%X}\n", h, h->memory, h->size, h->type, h->next);
        total += h->size;
    ) fputc('\n', fp);
    fprintf(fp, "Total: %d bytes (top - bottom = %d bytes)\n", total, (ptr_t)g_memory_top - (ptr_t)g_memory_bottom);

        fclose(fp);
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
    for (int i=0; i<g_state->free_block_slot_count; i++) {
        free_memory_block_t *b = g_state->free_block_slots[i];
        while (b) {
            if (b->header->memory == ptr)
                return true;

            b = b->next;
        }
    }
    return false;
}

static bool freeblock_exists(free_memory_block_t *block) {
    for (int i=0; i<g_state->free_block_slot_count; i++) {
        free_memory_block_t *b = g_state->free_block_slots[i];
        while (b) {
            if (b == block)
                return true;

            b = b->next;
        }
    }
    return false;
}

static void assert_memory_is_free(void *ptr) {
    /*
     * assert that there are no non-free blocks in which this pointer address exists.
     *
     * free_memory_block_t is always placed at the end of a free memory chunk,
     * i.e. [free memory area of N bytes | free_memory_block_t]
    */
    rm_header_t *h = g_state->header_root;
    ptr_t p = (ptr_t)ptr;
    while (h != NULL) {
        if (h->type != HEADER_FREE_BLOCK) {
            ptr_t start = (ptr_t)h->memory;
            ptr_t end = start + h->size;

            if (p >= start && p < end)
                abort();
        }
        h = h->next;
    }
}

#endif


free_memory_block_t *rm_block_from_header(rm_header_t *header) {
    /* free_memory_block_t for a chunk of free memory is stored at the very *end* of the block.
     *
     * this is done so that a recently freed block can be mixed together with
     * the block just behind the current one, if it is a valid free block.
     */
    return (free_memory_block_t *)((uint8_t *)header->memory + header->size) - 1;
}

#ifdef RMALLOC_DEBUG
static void freeblock_assert_sane(free_memory_block_t *block) {
    ptr_t pb = (ptr_t)block;
    ptr_t pbfh = (ptr_t)rm_block_from_header(block->header);
    if (pb != pbfh) {
        //int diff = (pb > pbfh) ? pb - pbfh : pbfh - pb;

#ifdef RMALLOC_DEBUG
        fprintf(stderr, "freeblock_assert_sane(%p size %d): diff %d bytes\n", block, block->header->size, diff);
#endif
        abort();
    }
}

static bool freeblock_checkloop(free_memory_block_t *block) {
    free_memory_block_t *a = block;
    while (block != NULL) {
        block = block->next;
        if (block == a) {
#ifdef RMALLOC_DEBUG
            fprintf(stderr, "loop in memory block %p slot %d\n", block, rm_log2(block->header->size));
#endif
            abort();
            return true;
        }
    }
    return false;
}
#endif


uint32_t rm_stat_total_free_list() {
    uint32_t total = 0;
    for (int i=0; i<g_state->free_block_slot_count; i++) {
        free_memory_block_t *b = g_state->free_block_slots[i];
        free_memory_block_t *a = b;
        while (b != NULL) {
            total += b->header->size;
            b = b->next;
            if (a == b) {
#ifdef RMALLOC_DEBUG
                fprintf(stderr, "stat_total_free_list(), panic - found a loop in slot %d item %p!\n", i, a);
                freeblock_print();
#endif
                abort();
            }
        }
    }
    return total;
}

uint32_t rm_stat_largest_free_block() {
    uint32_t largest = 0;
    for (int i=0; i<g_state->free_block_slot_count; i++) {
        free_memory_block_t *b = g_state->free_block_slots[i];
        free_memory_block_t *a = b;
        while (b != NULL) {
            if (b->header->size > largest)
                largest = b->header->size;
            b = b->next;
            if (a == b) {
#ifdef RMALLOC_DEBUG
                fprintf(stderr, "stat_total_free_list(), panic - found a loop in slot %d item %p!\n", i, a);
                freeblock_print();
#endif
                abort();
            }
        }
    }
    return largest;
}

void *rm_stat_highest_used_address(bool full_calculation) {
    if (full_calculation) {
        ptr_t highest = 0;

        rm_header_t *h = g_state->header_root;

        //printf("Highest: ");
        while (h != NULL) {
            if (h->type != BLOCK_TYPE_FREE) {
                //printf("*%p ", h->memory);
                if (h->size + (ptr_t)h->memory > highest) {
                    highest = h->size + (ptr_t)h->memory;
                }
            } else {
                //printf("%p ", h->memory);
            }
            h = h->next;
        }
        //printf("\n");

        return (void*)highest;
    } else {
        return (void *)((ptr_t)g_state->highest_address_header->memory + g_state->highest_address_header->size);
    }
}

static uint32_t get_block_count(uint32_t *free_count, uint32_t *locked_count, uint32_t *unlocked_count, uint32_t *weaklocked_count, uint32_t *size_unlocked)
{
    rm_header_t *node = g_state->header_root;
    uint32_t count = 0;

    while (node != NULL) {
        if (node->type == BLOCK_TYPE_FREE && free_count != NULL)
            *free_count += 1;
        else if (node->type == BLOCK_TYPE_LOCKED && locked_count != NULL)
        {
            *locked_count += 1;
        }
        else if (node->type == BLOCK_TYPE_UNLOCKED && unlocked_count != NULL) {
            *unlocked_count += 1;
            count++;
            if (size_unlocked)
                *size_unlocked += node->size;
        }
        else if (node->type == BLOCK_TYPE_WEAK_LOCKED && weaklocked_count != NULL)
            *weaklocked_count += 1;

        node = node->next;
    }
    return count;
}

uint32_t rmstat_get_used_block_count(void) {
    uint32_t fc=0, lc=0, uc=0, wc=0, su=0;
    get_block_count(&fc, &lc, &uc, &wc, &su);
    return fc + lc + uc + wc;
}

void rmstat_get_used_blocks(ptr_t *blocks) {
    uint32_t i=0;
    uint32_t count = rmstat_get_used_block_count();
    rm_header_t *h = g_state->header_root;
    while (h != NULL) {
        if (h->type != BLOCK_TYPE_FREE) 
            blocks[i++] = (ptr_t)h->memory;

        if (i == count)
            break;

        h = h->next;
    }
}


#ifdef RMALLOC_DEBUG
static void assert_list_is_sorted(rm_header_t *root)
{
    rm_header_t *prev = root;
    while (root != NULL)
    {
        prev = root;
        if (prev->memory > root->memory)
            abort();

        root = root->next;
    }
}
#endif


//volatile bool dummy = false;


#if RMALLOC_DEBUG

void rmstat_set_debugging(bool enable)
{
    g_debugging = enable;
}

#endif


#if 0
void rmstat_print_headers(bool only_type)
{
    rm_header_sort_all(); 

    // decide smallest block size for printing
    uint32_t smallest=1<<31, largest=0, total=0;
    WITH_ITER(h, g_header_root,
        //if (h->flags == HEADER_FREE_BLOCK) {
            if (h->size < smallest)
                smallest = h->size;
            if (h->size > largest)
                largest = h->size;
            total += h->size;
        //}
    )
    printf("\n\n\n\n==========================================================================\n");

    int total_header_size = 0;
    // print map
    WITH_ITER(h, g_header_root, 
        //int count = MAX(1024, h->size/smallest);
        int count = MAX(1, h->size/smallest);
        for (int i=0; i<count; i++) {
            if (i==0) {
                if (only_type == false) {
                    putchar(h->type == HEADER_FREE_BLOCK ? '_' : 'O');
                    printf("(%p)(%x)(%p)", h, *(uint8_t *)h->memory, h->memory);
                } else {
                    putchar('.');
                    if (h->type == HEADER_FREE_BLOCK)
                        putchar('_');
                    else if (h->type == HEADER_UNLOCKED)
                        putchar('|');
                    else if (h->type == HEADER_LOCKED)
                        putchar('X');
                }
            }
            else if (h->type == HEADER_FREE_BLOCK)
                putchar('_');
            else if (h->type == HEADER_UNLOCKED)
                putchar('|');
            else if (h->type == HEADER_LOCKED)
                putchar('X');

        }
        if (rm_header_is_unused(h))
            total_header_size += h->size;
    ) putchar('\n');
    printf("--------------------------------------------------------------------------\n");
    // display free blocks
    freeblock_print();
    printf("==========================================================================\n");
    int diff = (ptr_t)g_header_top - (ptr_t)g_header_bottom;
    printf("Total %ld live blocks, occupying %d bytes/%d kb = %.2d%% of total heap size\n", g_header_top - g_header_bottom, diff, diff/1024, (int)((float)diff*100.0/(float)g_memory_size));
}
#endif


/*******************************************************************************
 *
 * client code
 *
 *******************************************************************************/

/* header */
bool rm_header_is_unused(rm_header_t *header) {
    return header && header->memory == NULL;
}

static void header_clear(rm_header_t *h) {
    h->memory = NULL;
    h->size = 0;
    h->next = NULL;
#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    h->next_unused = NULL;
#endif
}


static rm_header_t *header_set_unused(rm_header_t *header) {

    header_clear(header);

#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    if (g_state->unused_header_root == NULL)
    {
        g_state->unused_header_root = header;
    }
    else
    {
        header->next_unused = g_state->unused_header_root;
        g_state->unused_header_root = header;
    }
    g_state->unused_header_root->next_unused = NULL;
#endif

#ifdef RMALLOC_DEBUG
    assert_handles_valid(g_header_root);
#endif

    return header;
}

rm_header_t *rm_header_find_free(void) {
    const int limit = 2; // for compact
    rm_header_t *h = NULL;

#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    if (g_state->unused_header_root != NULL)
    {
        h = g_state->unused_header_root;

        g_state->unused_header_root = g_state->unused_header_root->next_unused;

        goto finish;
    }
#else
    h = g_header_top;
    do {
        // guaranteed to be OK
        if (rm_header_is_unused(h))
            goto finish;
        h--;
    } while (h != g_header_bottom);
#endif

    // nothing found
    if ((void*)(g_state->header_bottom - limit) > g_state->memory_top) {
        g_state->header_bottom--;

        h = g_state->header_bottom;
        header_clear(h);
        goto finish;
    }

    return NULL;

finish:

#ifdef RMALLOC_DEBUG
    if (h && rm_header_is_unused(h) == false)
        abort();
#endif

    return h;
}


static rm_header_t *header_new(bool insert_in_list) {
    rm_header_t *header = rm_header_find_free();
    if (header) {
        header->type = BLOCK_TYPE_UNLOCKED;
        header->memory = NULL;
        if (insert_in_list) {
            // is it in list?
            if ((header->next < g_state->header_bottom || header->next > g_state->header_top) && header != g_state->header_root) {
                // nope, insert.
                header->next = g_state->header_root;

                g_state->header_root = header;
            }
            // it's already in the list!
        } else {
            // don't insert into list. this is the new_free thingy which we manually insert at the correct
            // position in the chain based on header_t::memory.
            header->next = NULL;
        }
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "== header_new() = %p\n", header);
#endif
    } else {
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "== header_new() = NULL\n", header);
#endif
    }
    return header;
}

/* memory block */

static void update_highest_address_if_needed(rm_header_t *h) {
    if (h)
    {
        g_state->highest_address_header = h;
    }
}

static rm_header_t *freeblock_find(uint32_t size);
static rm_header_t *block_new(ptr_t size) {
#ifdef RMALLOC_DEBUG
    fprintf(stderr, "block new: %d\n", size);
#endif
    // minimum size for later use in free list: header pointer, next pointer
    if (size < sizeof(free_memory_block_t))
        size = sizeof(free_memory_block_t);

#ifdef RMALLOC_DEBUG
    freeblock_verify_lower_size();
    //assert_blocks();
    assert_handles_valid(g_header_root);
#endif

    rm_header_t *h = NULL;

    // XXX: Is this really the proper fix?
    if ((uint8_t *)g_state->memory_top+size+sizeof(rm_header_t) < (uint8_t *)g_state->header_bottom) {
    //if ((uint8_t *)g_memory_top+size < (uint8_t *)g_header_bottom) {
        h = header_new(/*insert_in_list*/true);
        if (!h) {
#ifdef RMALLOC_DEBUG
            fprintf(stderr, "header_new: oom.\n");
#endif
            return NULL;
        }
#ifdef RMALLOC_DEBUG
        if ((uint8_t *)g_memory_top+size >= (uint8_t *)g_header_bottom) {
            fprintf(stderr, "memory top (%p) + size (%d) >= %p\n", g_memory_top, size, g_header_bottom);
            abort();
        }
        assert_handles_valid(g_header_root);
#endif


        // just grab off the top
        h->size = size;
        h->memory = g_state->memory_top;
        h->type = BLOCK_TYPE_UNLOCKED;

        if ((ptr_t)h->memory < (ptr_t)g_state->memory_bottom)
            abort();

        g_state->header_used_count++;
        g_state->memory_top = (uint8_t *)g_state->memory_top + size;
    } else {
        // nope. look through existing blocks
        h = freeblock_find(size);

        // okay, we're *really* out of memory
        if (!h) {
#ifdef RMALLOC_DEBUG
            fprintf(stderr, "freeblock_find: oom\n");
#endif
            return NULL;
        }
        if ((ptr_t)h->memory <= (ptr_t)g_state->memory_bottom)
            abort();

        g_state->header_used_count++;
        h->type = BLOCK_TYPE_UNLOCKED;

        g_state->free_block_hits++;
        g_state->free_block_alloc += size;
    }

    update_highest_address_if_needed(h);

    return h;
}


/* 1. mark the block's header as free
 * 2. insert block info
 * 3. extend the free list
 */
static rm_header_t *block_free(rm_header_t *header) {
    if (!header || header->type == BLOCK_TYPE_FREE)
        return header;

#ifdef RMALLOC_DEBUG
    fprintf(stderr, "block free: 0x%X\n", header);
    freeblock_verify_lower_size();
    //assert_blocks();
#endif


    // FIXME: merge with previous block places blocks in a too small slot.

    // TODO: merge cannot work, period, since the block is already in the free list
    // and thus has the incorrect address.
#if 0
    bool in_free_list = false;

    // are there blocks before this one?
    free_memory_block_t *prevblock = (free_memory_block_t *)header->memory - 1;
    if (prevblock >= g_memory_bottom) {
        // is it a valid header?
        if (prevblock->header >= g_header_bottom
            && prevblock->header <= g_header_top
            && prevblock->header->type == HEADER_FREE_BLOCK) {

            // does it point to the same block?
            if ((uint8_t *)prevblock->header->memory + prevblock->header->size == header->memory) {
                //fprintf(stderr, "merging previous block %p with block %p\n", prevblock, block_from_header(header));

                fprintf(stderr, "\nmerging block headers %p (%d bytes) and %p (%d bytes)  to new size %d, block %p\n", header, header->size, prevblock->header, prevblock->header->size, prevblock->header->size+header->size, rm_block_from_header(prevblock->header));

                // yup, merge previous and this block
                prevblock->header->size += header->size;

                // kill off the current block
                header_set_unused(header);

                // set new current block
                header = prevblock->header;

                // put the extended block info in place at the end
                free_memory_block_t *endblock = rm_block_from_header(header);
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
        if (nextblock->header >= g_header_bottom && nextblock->header <= g_header_top && nextblock->header->type == HEADER_FREE_BLOCK) {
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
    // our work is done here
    if (in_free_list)
        return header;
#endif


    // alright, no previous or next block to merge with.
    // update the free list
    free_memory_block_t *block = rm_block_from_header(header);
    //fprintf(stderr, "block from header in free(): %p\n", block);

    // header's tracking a block in the free list
    header->type = BLOCK_TYPE_FREE;

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

#if 0
    free_memory_block_t b;
    b.header = header;
    b.next = NULL;
    memcpy((void *)block, (void *)&b, sizeof(free_memory_block_t));
#else
    block->header = header;
    block->next = NULL;
#endif

    if (block->header->size + (ptr_t)block->header->memory >= (ptr_t)g_state->header_bottom)
#ifdef RMALLOC_DEBUG
        abort();
#else
        return NULL;
#endif


    //fprintf(stderr, "block_free(): block = %p, block->header = %p (header = %p) size %d memory %p\n", block, block->header, header, block->header->size, block->header->memory);

    // insert into free size block list, at the start.
    int index = rm_log2(header->size);

    if (block->header->size != header->size)
#ifdef RMALLOC_DEBUG
        abort();
#else
        return NULL;
#endif

#ifdef RMALLOC_DEBUG
    freeblock_assert_sane(block);
    //assert_blocks();
#endif

    block->next = g_state->free_block_slots[index];
    g_state->free_block_slots[index] = block;

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

#ifdef RMALLOC_DEBUG
    freeblock_checkloop(block);
    //assert_blocks();
#endif

#if 0 // FUTURE WORK for forward merges
    // insert duplicate back-pointers for large blocks
    if (header->size >= 12)
        memcpy(header->memory, header, sizeof(rm_header_t));
#endif


#if 0 // FUTURE WORK
    // mark header as free in 'free header' bitmap
#endif
    g_state->header_used_count--;
    return header;
}


/* free block list */

/* insert item at the appropriate location.
 * don't take into consideration that it can exist elsewhere
 */
static void freeblock_insert(free_memory_block_t *block) {

    if (block->header->size + (ptr_t)block->header->memory >= (ptr_t)g_state->header_bottom) {
        abort();
    }

    int k = rm_log2(block->header->size);

    /*
    free_memory_block_t *b = g_free_block_slots[k];
    g_free_block_slots[k] = block;
    g_free_block_slots[k]->next = b;
    */
    block->next = g_state->free_block_slots[k];
    g_state->free_block_slots[k] = block;

#ifdef RMALLOC_DEBUG
    freeblock_verify_lower_size();
#endif

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
static free_memory_block_t *freeblock_shrink_with_header(free_memory_block_t *block, rm_header_t *h, uint32_t size) {
    if (!block)
        return NULL;

    int diff = block->header->size - size;
    if (diff < (int)sizeof(free_memory_block_t)) {
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "    1. freeblockshrink withheader block->header->size %d - size %d = diff = %d\n",
                block->header->size, size, diff);
#endif
        return NULL;
    }

    if (!h) {
        h = header_new(/*insert_in_list*/true);
        if (h == NULL) {
#ifdef RMALLOC_DEBUG
            fprintf(stderr, "    2. couldn't allocate new header.\n");
#endif
            return NULL;
        }
    }

    if (size > block->header->size) {
        abort();
    }

    if (h == block->header) {
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "ERROR: freeblock_shrink, new header %p same as block header %p\n", h, block->header);
#endif
        // TODO abort?
    }

#ifdef RMALLOC_DEBUG
    freeblock_assert_sane(block);
    //assert_blocks();

    fprintf(stderr, "freeblockshrink: address of block->memory = %p with size = %d, address of block = %p == %p (or error!)\n", block->header->memory, block->header->size, block, (uint8_t *)block->header->memory + block->header->size - sizeof(free_memory_block_t));
#endif

    h->type = BLOCK_TYPE_FREE;
    h->memory = block->header->memory;
    h->size = diff;

    block->header->memory = (uint8_t *)block->header->memory + diff;
    block->header->size = size;

    //fprintf(stderr, "freeblock_shrink, h memory %p size %d block h memory %p size %p\n", h->memory, h->size, block->header->memory, block->header->size);

    free_memory_block_t *b = rm_block_from_header(h);
    b->next = NULL; 
    b->header = h;

#ifdef RMALLOC_DEBUG
    fprintf(stderr, "    3. freeblockshrink withheader h: %p  %d  %p  %d\n", h, h->size, h->memory, h->type);

    fprintf(stderr, "    4. freeblockshrink withheader block %p header %p size %d\n", block, block->header, block->header->size);

    if (b == block) {
        fprintf(stderr, "ERROR: freeblock_shrink, new block %p (memory %p size %d) old block %p (memory %p size %d)\n",
                b, b->header->memory, b->header->size,
                block, block->header->memory, block->header->size);
    }
#endif

    if (block->header->size != size) {
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "ERROR: freeblock_shrink, new block's header %p (h = %p) size %d not new size %d\n", b->header, h, b->header->size, size);
#endif
        abort();
    }

    return b;
}


static free_memory_block_t *freeblock_shrink(free_memory_block_t *block, uint32_t size) {
    return freeblock_shrink_with_header(block, NULL, size);
}


/* look for a block of the appropriate size in the 2^k list.
 *
 * any block that are larger than the slot's size will be moved upon traversal!
 */
static rm_header_t *freeblock_find(uint32_t size) {
    // there can be blocks of 2^k <= n < 2^(k+1)
    int target_k = rm_log2(size)+1;
    int k = target_k;

    // any blocks of >= upper_size will be moved be de-linked and moved to the
    // appropriate slot
    ptr_t upper_size = 1<<(k+1);

    free_memory_block_t *block = NULL;
    free_memory_block_t *found_block = NULL;
    free_memory_block_t *fallback_block = NULL;

#ifdef RMALLOC_DEBUG
    freeblock_verify_lower_size();
#endif

    // slot too large, need to do a full scan.
    if (k == g_state->free_block_slot_count) {
        k--;
        block = g_state->free_block_slots[k];
        free_memory_block_t *prevblock = block;
        while (block) {
            if (block->header->size >= size) {
                // found the block. remove it from the list.
                if (block == g_state->free_block_slots[k])
                    g_state->free_block_slots[k] = block->next;
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
        block = g_state->free_block_slots[k];
        //fprintf(stderr, "freeblock_find() block at %d = %p (at pos %p)\n", k, block, &block);
        while (!block && k < g_state->free_block_slot_count) {
            // nope, move up to the next block
            k++;
            if (k < g_state->free_block_slot_count) {
                upper_size = 1<<(k+1);
                block = g_state->free_block_slots[k];
                //fprintf(stderr, "freeblock_find() block at %d = %p (at pos %p)\n", k, block, &block);
            } 
        }

        if (block) {
            // yeah, there's a block here. it's also guaranteed to fit.

            // remove the item from list.
            block = g_state->free_block_slots[k];
            g_state->free_block_slots[k] = block->next;

            //fprintf(stderr,"*1. %p -> %p (%c)?\n", block, block->header, (block->header&0x000000FF));

            if (block->header->size > upper_size) {
                // current next block. when moved, the next block will point to something else.
                free_memory_block_t *nextblock = block->next;
                fallback_block = block;

                freeblock_insert(block);

                block = nextblock;
            } else {
                if (block->header->size < size) {
#ifdef RMALLOC_DEBUG
                    fprintf(stderr, "block %p too small (%d vs %d) in slot %d vs actual k = %d\n",
                            block, block->header->size, size, k, rm_log2(size));
#endif
                    abort();
                }

                found_block = block;
            }
        } else {
            // didn't find anything. do a full scan of the actual sized-k.
            k = rm_log2(size);
            upper_size = 1<<(k+1);

#if RMALLOC_DEBUG
            fprintf(stderr, "freeblock_find(%d) scanning in %d\n", size, k);
#endif

            block = g_state->free_block_slots[k];
            free_memory_block_t *prevblock = block, *nextblock = block;
            while (block) {
                // there's a block here. it's also guaranteed to fit.

                if (block->header->size >= size) {
#ifdef RMALLOC_DEBUG
                    fprintf(stderr, "freeblock_find: block->header->size %d >= (requested) size %d\n", block->header->size, size);
#endif
                    // remove from the root? (easier)
                    if (g_state->free_block_slots[k] == block) {
                        // figure out what to do with the block
                        if (block->header->size > upper_size) {
#ifdef RMALLOC_DEBUG
                            fprintf(stderr, "-> root, too large, moving %p, next = %p.\n", block, block->next);
#endif
                            fallback_block = block;
                            g_state->free_block_slots[k] = block->next;

                            freeblock_insert(block);

                            block = g_state->free_block_slots[k];
#ifdef RMALLOC_DEBUG
                            fprintf(stderr, "->-> block = %p\n", block);
#endif
                        } else {
                            // found it!
#ifdef RMALLOC_DEBUG
                            fprintf(stderr, "-> root, correct size.\n");
#endif
                            found_block = block;
                            g_state->free_block_slots[k] = block->next;
                            break;
                        }
                    } else {
                        // not at root. (trickier)

                        // figure out what to do with the block
                        if (block->header->size > upper_size) {
#ifdef RMALLOC_DEBUG
                            fprintf(stderr, "-> not root, too large, moving %p, next = %p.\n", block, block->next);
#endif
                            fallback_block = block;

                            nextblock = block->next;
                            prevblock->next = nextblock;

                            freeblock_insert(block);

                            block = nextblock;
#ifdef RMALLOC_DEBUG
                            fprintf(stderr, "->-> block = %p\n", block);
#endif

                            if (prevblock->next != block) { 
                                abort();
                            }
                        } else {
                            // found it!
#ifdef RMALLOC_DEBUG
                            fprintf(stderr, "-> not root, correct size.\n");
#endif
                            prevblock->next = block->next;
                            found_block = block;
                            break;
                        }
                    } 
                } else {
                    prevblock = block;
                    block = block->next;
                }
            }
        }
    }

    if (found_block) {
        // resize & insert
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "-> shrinking found_block (header %p size %d) to new size %d\n",
                found_block->header, found_block->header->size, size);
#endif
        free_memory_block_t *rest = NULL;
        rest = freeblock_shrink(found_block, size);
        if (rest == NULL) {
            // this can, oddly enough happen, if there are no headers left.
            return NULL;
        }
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "-> after shrinkage, found_block size %d, returning header %p\n", found_block->header->size, found_block->header);
#endif
        if (rest)
            freeblock_insert(rest);

        return found_block->header;
    } else if (fallback_block) {
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "fallback block of header %p.\n", fallback_block->header);
#endif
        // rats, no block found.
        int fallback_k = rm_log2(fallback_block->header->size);

        // since we just inserted the fallback block, we know that it's
        // placed at the very start of this slot.  remove it by relinking the
        // slot, and use it.
        g_state->free_block_slots[fallback_k] = fallback_block->next;

        //fprintf(stderr, "No go, using fallback block %p of %d kb\n", fallback_block, fallback_block->header->size/1024);
        free_memory_block_t *rest = NULL;
        rest = freeblock_shrink(fallback_block, size);
        if (rest)
            freeblock_insert(rest);

        return fallback_block->header;
    }


#ifdef RMALLOC_DEBUG
    fprintf(stderr, "freeblock_find(): no block found.\n");
#endif
    // no block found.
    return NULL;
}


void rm_header_sort_all() {
#ifdef RMALLOC_DEBUG
    fprintf(stderr, "g_header_root before header_sort_all(): %p\n", g_header_root);
#endif
    //g_header_root = header__sort(g_header_root, header__cmp);
    //header_t *header__sort(header_t *list, int is_circular, int is_double, compare_cb cmp) {
    g_state->header_root = header__sort(g_state->header_root, 0, 0, header__cmp);
}


static uint32_t /*size*/ get_free_header_range(rm_header_t *start, rm_header_t **first, rm_header_t **last, rm_header_t **block_before_last)
{
    // Find first free block.

    while (start != NULL && start->type != BLOCK_TYPE_FREE)
    {
        start = start->next;
    }

    if (start == NULL) {
        *first = NULL;
        *last = NULL;

        return 0;
    }

    *first = start;
    *last = start;
    *block_before_last = NULL;

    uint32_t size = 0;
    while (start != NULL && start->type == BLOCK_TYPE_FREE)
    {
        *block_before_last = *last;
        *last = start;
        size += start->size;

        start = start->next;
    }

    return size;
}

/* 
 * Starting from 'start', find a range of unlocked headers. Store in first and last.
 * 
 * TODO: If max_size > 0, set a limit on the total size of the used blocks.
 * TODO: If max_size > 0 and no blocks were found that fits, set first = NULL and last to be the last checked block. 
 */
static uint32_t /*size*/ get_unlocked_header_range(rm_header_t *start, rm_header_t **first, rm_header_t **last, rm_header_t **block_before_first, uint32_t max_size, bool *passed_free_blocks)
{
    // Find first unlocked block.
    while (start != NULL && start->type != BLOCK_TYPE_UNLOCKED)
    {
        if (start && start->type == BLOCK_TYPE_FREE)
            *passed_free_blocks = true;
        *block_before_first = start;
        start = start->next;
    }

    // No unlocked blocks found! We're done.
    if (start == NULL)
    {
        *first = NULL;
        *last = NULL;

        return 0;
    }

    if (start->type == BLOCK_TYPE_FREE)
        *passed_free_blocks = true;


    // We've found an unlocked block.

    // If there's a size limit, skip blocks that don't fit.
    if (max_size > 0)
    {
        // Search through the entire list, bypassing any locked blocks, and to the first unlocked header.
        // Stop at any free block.
        bool found = false;
        while (start != NULL && start->type != BLOCK_TYPE_FREE)
        {
            if (start && start->type == BLOCK_TYPE_FREE)
                *passed_free_blocks = true;

            if (start->type == BLOCK_TYPE_UNLOCKED && start->size <= max_size)
            {
                found = true;
                break;
            }

            *block_before_first = start;

            start = start->next;
        }
        if (start && start->type == BLOCK_TYPE_FREE)
            *passed_free_blocks = true;

        if (found == false)
        {
        //if (start == NULL || start->flags != HEADER_UNLOCKED || start->size > max_size) {
            // ouch!  mark last visited block and return.
            //
            //
            //
            //
            // XXX: *last here is a free block, and not even the first block.
            // What should it /really/ return?
            //
            //
            //
            *first = NULL;
            //*last = start;
            *last = NULL;
            *block_before_first = NULL;
            return 0;
        }
    }

    // We have a starting point.

    *first = start;
    *last = start;

    uint32_t size = 0;
    while (start != NULL && start->type == BLOCK_TYPE_UNLOCKED)
    {
        if (start && start->type == BLOCK_TYPE_FREE)
            *passed_free_blocks = true;

        if (max_size > 0 && size + start->size > max_size)
            break;

        *last = start;
        size += start->size;

        start = start->next;
    }
    if (start && start->type == BLOCK_TYPE_FREE)
        *passed_free_blocks = true;

    return size;
}

static uint32_t /*size*/ header_memory_offset(rm_header_t *first, rm_header_t *last)
{
    ptr_t f = (ptr_t)first->memory;
    ptr_t l = (ptr_t)last->memory;

    if (f > l)
        return 0;

    return l - f;
}

static rm_header_t *find_last_nonfree_header(rm_header_t *root)
{
    rm_header_t *last_nonfree = NULL;
    while (root != NULL && root->type != BLOCK_TYPE_FREE)
    {
        last_nonfree = root;
        root = root->next;
    }

    if (last_nonfree && last_nonfree->type == BLOCK_TYPE_FREE)
        last_nonfree = NULL;

    return last_nonfree;
}

static void rebuild_free_block_slots() {

    // rebuild free list
    memset((void *)g_state->free_block_slots, 0, sizeof(free_memory_block_t *) * g_state->free_block_slot_count);

    uint32_t steps = 0;

    uint32_t free_block_count = 0;
    rm_header_t *h = g_state->header_root;
    while (h != NULL) {
        if (!h->memory || h->type != BLOCK_TYPE_FREE) {
            h = h->next;
            steps++;
            continue;
        }
        steps++;
        int k = rm_log2(h->size);

        // just let the smaller headers be, in case there are any.
        // there should not be any, and so this test is invalid.
        if (h->size >= sizeof(free_memory_block_t)) {

            free_block_count++;

            free_memory_block_t *block = rm_block_from_header(h);

            block->header = h; 
            block->next = NULL;

#ifdef RMALLOC_DEBUG
            // this should _always_ point to (h->memory+h->size - sizeof(free_block_memory_t))
            assert_memory_is_free((void *)block);
#endif

            // crash on g_free_block_slots[k]->next->header->size, because ->next->header == 0
            if (g_state->free_block_slots[k] == NULL)
                g_state->free_block_slots[k] = block;
            else {
                if (block == g_state->free_block_slots[k])
                {
                    block = g_state->free_block_slots[k];
                }


                block->next = g_state->free_block_slots[k];
                g_state->free_block_slots[k] = block;
            }
        }

        h = h->next;
    }

#ifdef RMALLOC_DEBUG
    //assert_blocks();
#endif
}

void rm_init(void *heap, uint32_t size) {
    if ( g_state == NULL ) {
        // in case the user hasn't set a state pointer, allocate a new state block
        g_state = calloc(1, sizeof(rmalloc_meta_t));
    }

    g_state->memory_size = size;

    // +1 to round up. e.g. log2(15)==3
    // => 0, 1, 2, but later log2(13) would map to 3!
    // in practice, will there be such a large block?
    g_state->free_block_slot_count = rm_log2(size) + 1; 
    g_state->free_block_slots = (free_memory_block_t **)heap;
    uint32_t count = sizeof(free_memory_block_t *)*g_state->free_block_slot_count;
    memset((void *)g_state->free_block_slots, 0, count);

    g_state->memory_bottom = (void *)((ptr_t)heap + (g_state->free_block_slot_count * sizeof(free_memory_block_t *)));
    g_state->memory_top = g_state->memory_bottom;

    // header top is located at the top of the heap space and grows downward.
    // header bottom points to the bottom, including the last one!
    g_state->header_top = (rm_header_t *)((ptr_t)heap + size - sizeof(rm_header_t));
    g_state->header_bottom = g_state->header_top - 1;
    g_state->header_root = g_state->header_top;
    g_state->header_root->next = NULL;
    g_state->header_used_count = 0;

    // newly unused headers are prepended, i.e. placed first, and g_unused_header_root is re-pointed.
#if JEFF_MAX_RAM_VS_SLOWER_MALLOC == 0
    g_state->unused_header_root = NULL;
    //g_unused_header_root->next_unused = NULL;
#endif

    header_set_unused(g_state->header_top);
    g_state->header_top->size = 0;

    g_state->highest_address_header = g_state->header_top; // to make sure it points to _something_

    memset(heap, 0, size);
}


size_t rm_state_size(void) {
    return sizeof(rmalloc_meta_t);
}


rmalloc_meta_t* rm_get_state(void) {
    return g_state;
}

void rm_set_state(rmalloc_meta_t *state) {
    g_state = state;
}

void rm_destroy() {
    // nop.
    return;
}

rm_handle_t rm_malloc(int size) {
    rm_header_t *h = block_new(size);
#ifdef RMALLOC_DEBUG
    g_memlayout_sequence++;
    dump_memory_layout();
    assert_handles_valid(g_header_root);
#endif

    if (h == NULL) {
#ifdef RMALLOC_DEBUG
        fprintf(stderr, "h = NULL.\n");
#endif
        return NULL;
    }

#ifdef RMALLOC_DEBUG
    //memset(h->memory, header_fillchar(h), h->size);
    //rebuild_free_block_slots();
    //assert_blocks();
#endif

    return (rm_handle_t)h;
}

void rm_free(rm_handle_t h) {
    block_free((rm_header_t *)h);

#ifdef RMALLOC_DEBUG
    g_memlayout_sequence++;
    dump_memory_layout();
    assert_handles_valid(g_header_root);
    //rebuild_free_block_slots();
    //assert_blocks();
#endif
}

void *rm_lock(rm_handle_t h) {
    rm_header_t *f = (rm_header_t *)h;
    f->type = BLOCK_TYPE_LOCKED;

    return f->memory;
}

void *rm_weaklock(rm_handle_t h) {
    rm_header_t *f = (rm_header_t *)h;
    f->type = BLOCK_TYPE_WEAK_LOCKED;

    return f->memory;
}

void rm_unlock(rm_handle_t h) {
    rm_header_t *f = (rm_header_t *)h;
    f->type = BLOCK_TYPE_UNLOCKED;
}

void rm_compact(uint32_t maxtime) {
    // sort headers in ascending memory order. headers with ->memory == NULL are in the end.
    rm_header_sort_all();

#ifdef RMALLOC_DEBUG
    uint32_t start_free = 0, start_locked = 0, start_unlocked = 0, start_size_unlocked = 0;
    get_block_count(&start_free, &start_locked, &start_unlocked, NULL, &start_size_unlocked);

    if (g_debugging)
    {
        rmstat_print_headers(false);
    }
#endif

    rm_header_t *root = g_state->header_root;

    uint64_t start_time, now;
    uint64_t time_diff;
    start_time = uptime_nanoseconds();

    bool done = false;
    while (!done) {
        now = uptime_nanoseconds();
        time_diff = now - start_time;

        if (maxtime > 0 && time_diff >= maxtime) {
            done = true;
            break;
        } 

        // only run once!
        //if (root != g_header_root) break;

        ///////////////////////////////////////////////////////////////////////////////////////////

#ifdef RMALLOC_DEBUG
        assert_handles_valid(g_header_root);
#endif

        // Find first free
        rm_header_t *root_last_nonfree = find_last_nonfree_header(root);

        // Find ranges of free and unlocked blocks

        rm_header_t *free_first, *free_last, *block_before_last_free_UNUSED;
        uint32_t free_size = get_free_header_range(root, &free_first, &free_last, &block_before_last_free_UNUSED);
        if (free_size == 0)
        {
            done = true;
            continue;
        }

        rm_header_t *start = free_last->next;

        if (start == NULL)
        {
            // nothing beyond this point, stop.
            done = true;
            continue;
        }

        uint32_t max_size = 0;
        if (start->type != BLOCK_TYPE_UNLOCKED)
        {
            max_size = free_size;
        }

        rm_header_t *unlocked_first, *unlocked_last, *block_before_first_unlocked=NULL;
        bool passed_free_blocks = false;
        uint32_t unlocked_size = get_unlocked_header_range(start, &unlocked_first, &unlocked_last, &block_before_first_unlocked, max_size, &passed_free_blocks);
        if (max_size > 0 && unlocked_first == NULL)
        {
            // no blocks that fit inside current free found. try again!
            if (unlocked_last == NULL)
            {
                if (free_last->next != NULL && passed_free_blocks)
                {
                    // there might be another chance.
                    root = free_last->next;
                }
                else
                {
                    done = true;
                }
                continue;
            }
        }

        if (unlocked_size == 0)
        {
            break;
        }

        bool adjacent = free_last->next == unlocked_first;

        // Move unlocked blocks, squish free blocks.

        uint32_t used_offset = header_memory_offset(free_first, unlocked_first);
        if (used_offset == 0)
        {
#ifdef RMALLOC_DEBUG
            //abort();
            used_offset = 0;
#endif
            break;
        }

        rm_header_t *unlocked_last_next = unlocked_last->next;
        rm_header_t *free_last_next = free_last->next;

        // Move used blocks

        rm_header_t *h = unlocked_first;
        unlocked_size = 0;
        ptr_t unlocked_first_memory = (ptr_t)unlocked_first->memory;
        while (h != NULL && h != unlocked_last->next)
        {
            ptr_t src = (ptr_t)h->memory;
            ptr_t dest = src - used_offset;
            h->memory = (void *)dest;
            unlocked_size += h->size;


            memmove((void *)dest, (void *)src, h->size);
            h = h->next;
        }

        // Squish free blocks

        ptr_t free_memory_start = (ptr_t)free_first->memory;

        h = free_first;
        while (h && h != free_first->next)
        {
            header_set_unused(h);

            h = h->next;
        }

        if (adjacent)
        {
            // Place free memory in new free block header

            rm_header_t *free_memory = header_new(/*insert_in_list*/false);
            free_memory->type = BLOCK_TYPE_FREE;
            free_memory->memory = (void *)(free_memory_start + unlocked_size);
            free_memory->size = free_size;
#ifdef RMALLOC_DEBUG
            assert_handles_valid(g_header_root);
#endif
#if 0
            // XXX: overwrites memory. bleh.
            memset(free_memory->memory, 0x42, free_size);
            if ((ptr_t)free_memory == 0x807131c && (ptr_t)((rm_header_t *)0x806f2c4)->memory == 0x42424242)
            {
                adjacent = true;
            }
#endif
#if 0
            if ((ptr_t)free_memory->memory >= 0x806f2c4 && (ptr_t)free_memory->memory+free_size <= 0x806f2c4)
            {
                abort();
            }
            memset(free_memory->memory, 0x42, free_size-sizeof(rm_header_t));
#endif


            // Place free blocks at the location where the unlocked blocks were
            // Re-link the blocks pointing to the unlocked blocks to point to the new free block.

            // easy case
            unlocked_last->next = free_memory;
            free_memory->next = unlocked_last_next;
#ifdef RMALLOC_DEBUG
            assert_handles_valid(unlocked_first);
#endif
        }
        else
        {
            // [F1 | F2 | F3 | F4 | X1/C | X2/B | U1 | U2 | A]
            // =>
            // [U1 | U2 | F5 | X1/C | X2/B | (possible too big block U3) | F6 | A]
            //
            // * Create F6
            // *
            // * Possible too big block U3?
            // * - Link B to U3
            // * - Link U3 to F6
            // * Else:
            // * - Link B to F6
            //
            // * Link F6 to A
            //
            // A * Create F5
            //   * Link LU to F5
            //   * Link F5 to C
            // B * Extend LU
            //   * Link LU to C

            // Create a new block F6   from the space where the used blocks were.
            rm_header_t *free_unlocked = header_new(/*insert_in_list*/false);
            free_unlocked->type = BLOCK_TYPE_FREE;


            free_unlocked->memory = (void *)unlocked_first_memory;
            free_unlocked->size = unlocked_size;
            #ifdef RMALLOC_DEBUG
            //memset(free_unlocked->memory, 0x43, free_unlocked->size); // TODO: Can be safely removed.
            #endif

            if ((ptr_t)free_unlocked->memory <= (ptr_t)g_state->memory_bottom)
                abort();

            // Link B to F6
            block_before_first_unlocked->next = free_unlocked;

            // Link F6 to A
            free_unlocked->next = unlocked_last_next;
#ifdef RMALLOC_DEBUG
            assert_handles_valid(free_unlocked);
#endif

            if (free_size >= unlocked_size + sizeof(free_memory_block_t))
            {
                // Create F5
                rm_header_t *spare_free = header_new(/*insert_in_list*/false);
                spare_free->type = BLOCK_TYPE_FREE;
                spare_free->memory = (void *)((ptr_t)unlocked_first->memory + unlocked_size);
                spare_free->size = free_size - unlocked_size;
                #ifdef RMALLOC_DEBUG
                //memset(spare_free->memory, 0x44, spare_free->size); // TODO: Can be safely removed.
                #endif

                if ((ptr_t)spare_free->memory <= (ptr_t)g_state->memory_bottom)
                    abort();

                // Link LU to F5
                unlocked_last->next = spare_free;

                // Link F5 to C
                spare_free->next = free_last_next;
#ifdef RMALLOC_DEBUG
                assert_handles_valid(unlocked_first);
#endif
            }
            else
            {
#ifdef RMALLOC_DEBUG
                fprintf(stderr, "extending LU with %d bytes\n", free_size - unlocked_size);
#endif

                // Extend LU
                unlocked_last->size += (free_size - unlocked_size);

                // Link LU to C
                unlocked_last->next = free_last_next;


                #ifdef RMALLOC_DEBUG
                //memset(h->memory, 0x45, h->size);
                //memset(h->memory, header_fillchar(h), h->size);
                assert_handles_valid(unlocked_first);
                #endif
            }
        }
#ifdef RMALLOC_DEBUG
        assert_handles_valid(g_header_root);
#endif

        // Next round

        // 
        //
        //
        //
        //
        //
        //
        // XXX: If root's next (cached) is not the same as root's next _now_, that means we have changed something.
        // Thus, relink.  Or maybe that won't work. Hmm. Check if we've done any operation with root->next, and if so, relink.
        // Is there a difference between doing this if it's g_header_root or not?
        //
        //
        //
        //
        //
        //
        //
        //






        // first block after root is free. this means we've changed it.
        if (root_last_nonfree == NULL)
        {
            // there is a very very very very very small that unlocked_first 
            g_state->header_root = unlocked_first;
        }
        else
        {
            root_last_nonfree->next = unlocked_first;
        }

        update_highest_address_if_needed(unlocked_last);

        root = unlocked_last;


#ifdef RMALLOC_DEBUG
        assert_list_is_sorted(g_header_root);
        assert_handles_valid(g_header_root);
#endif

    }

#ifdef RMALLOC_DEBUG
    if (g_debugging)
    {
        rmstat_print_headers(false);
    }
#endif

    // TODO: integrate into main loop.
    ptr_t highest_used_address = (ptr_t)g_state->memory_bottom;
#ifdef RMALLOC_DEBUG
    fprintf(stderr, "Previous top: 0x%X (bottom 0x%X)\n", g_memory_top, g_memory_bottom);
#endif
    rm_header_t *h = g_state->header_root;
    rm_header_t *largest_header = h;
    int count = 0;
    int total_size = 0;
    while (h != NULL) {
        if (!rm_header_is_unused(h) && h->type != BLOCK_TYPE_FREE) {
            if ((ptr_t)h->memory + h->size > highest_used_address) {
                highest_used_address = (ptr_t)h->memory + h->size;
                //ptr_t offset = highest_used_address - (ptr_t)g_memory_bottom;
                //fprintf(stderr, "=> USED 0x%X size %04d offset from bottom: %d bytes (%d kb)\n", h->memory, h->size, offset, offset/1024);
                largest_header = h;
            }
            count++;
           total_size += h->size;
        }
        //else fprintf(stderr, "=> FREE 0x%X size %04d offset from bottom: %d bytes (%d kb)\n", h->memory, h->size, offset, offset/1024);

        h = h->next;
    }

    // prune all free blocks above highest address.
    h = g_state->header_root;
    while (h != NULL) {
        rm_header_t *h2 = h;
        bool kill = false;
        if (!rm_header_is_unused(h) && h->type == BLOCK_TYPE_FREE && (ptr_t)h->memory >= highest_used_address) {
            header_set_unused(h);
            kill = true;
        }
        h = h->next;
        if (kill)
            h2->next = NULL;
    }

    // remove from list.
    largest_header->next = NULL;

    // adjust g_header_bottom
    while (rm_header_is_unused(g_state->header_bottom)) {
        g_state->header_bottom++;
    }

    rebuild_free_block_slots();

    // Let's hope this works!
    g_state->memory_top = (void *)highest_used_address;

#ifdef RMALLOC_DEBUG
    fprintf(stderr, "New top (after %d items of total size %d bytes): 0x%X, topmost header at 0x%X + %d = 0x%X\n", count, total_size, g_memory_top, largest_header->memory, largest_header->size, (ptr_t)largest_header->memory + largest_header->size);

    uint32_t end_free = 0, end_locked = 0, end_unlocked = 0, end_size_unlocked=0;
    get_block_count(&end_free, &end_locked, &end_unlocked, NULL, &end_size_unlocked);
    assert_handles_valid(g_header_root);

    if (start_unlocked != end_unlocked && start_locked != end_locked) {
        //printf("unlocked headers: start %d != end %d\n", start_unlocked, end_unlocked);
        abort();
    } else
    {
        //printf("unlocked headers: start %d == end %d, diff size start vs end = %d\n", start_unlocked, end_unlocked, (int32_t)start_size_unlocked - (int32_t)end_size_unlocked);
        }

    g_memlayout_sequence++;
    dump_memory_layout();
#endif
}
