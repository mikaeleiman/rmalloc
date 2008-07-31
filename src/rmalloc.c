/*
 * rmmalloc.c
 *
 * Relocatable Memory Allocator
 *
 * Mikael Jansson <mail@mikael.jansson.be>
 */
#include "rmalloc.h"
#include <stdlib.h>
#include <stdio.h>

#define RMALLOC_INIT_RAM_SIZE (8 * 1024*1024)

#define RM_ASSERT(x) {uint8_t status = x; if (!status) {fprintf(stderr, "[%s:%d] RM_ASSERT(%s) failed!\n", __FILE__, __LINE__, #x);}}
#define RM_ASSERT_RET(x, err) {uint8_t status = (x); if (!status) {\
    fprintf(stderr, "[%s:%d] RM_ASSERT_RET(%s) failed!\n", __FILE__, __LINE__, #x); return err;}}

#define RM_MERGED        0
#define RM_NOT_MERGED    1
#define RM_CANNOT_SHRINK 2

#define MEMORY_AS_BLOCK(x) ((memory_block_t *)(x))
#define BLOCK_AS_MEMORY(x) ((memory_t *)(x))

/****************************************************************************/

static uint8_t *g_ram;
static uint8_t *g_ram_end;
static uint8_t *g_top; // memory grows up
static size_t g_ram_size;

/****************************************************************************/

typedef struct memory_block_t {
    memory_t memory;
    void *ptr;
    uint8_t used : 1;
    size_t size;
    struct memory_block_t *previous;
    struct memory_block_t *next;
} memory_block_t;

static memory_block_t *g_root;
static memory_block_t *g_root_end;

static memory_block_t *mb_alloc(size_t size);
static status_t mb_mark_as_free(memory_block_t *mb);
static memory_block_t *mb_find(void *ptr);

/* push a pointer onto the root
 *
 * return: the newly allocated block.
 * modify: g_top, g_root_end, g_root
 * depend: -
 */
memory_block_t *mb_alloc(size_t size) {
    memory_block_t *mb;
    if (g_top+size+sizeof(memory_block_t) < g_ram_end) {
        mb = (memory_block_t *)g_top;
        g_top += sizeof(memory_block_t);

        mb->ptr = g_top;
        mb->used = 1;
        mb->size = size;

        mb->next = NULL;
        mb->previous = g_root_end;
        g_root_end->next = mb;

        g_root_end = mb;

        g_top += size;

        return mb;
    } else
        return NULL;
}

/* find a memory block in the list
 *
 * return: the found block or NULL
 * modify: -
 * depend: g_root
 */
memory_block_t *mb_find(void *ptr) {
    memory_block_t *root;
    for (root = g_root;
         root != NULL && root->ptr != ptr;
         root = root->next);

    return root;
}


/* mark the black as unused.
 *
 * return: always RM_OK
 * modify: -
 * depend: -
 */
status_t mb_mark_as_free(memory_block_t *mb) {
    mb->used = 0;
}


/****************************************************************************/
void *rmalloc_ram_end(void) {
    return g_ram_end;
}
void *rmalloc_ram_top(void) {
    return g_top;
}

/* dump debug data about the allocation structures.
 * specifically, the blocks and the addresses within.
 */
void rmalloc_dump(void) {
    memory_block_t *mb = g_root->next;
    fprintf(stderr, "rmalloc_dump()\n");
    while (mb != NULL) {
        fprintf(stderr, "[%c] %6d %8p->ptr = %8p, end of pointer + memory_t = %8p = next = %8p\n",
                mb->used ? 'X' : ' ',
                mb->size,
                (uint8_t*)mb-g_ram,
                mb->ptr,
                (uint8_t*)(mb->ptr)-g_ram,
                (uint8_t*)mb->next-g_ram);
        mb = mb->next;
    }
}

status_t rmalloc_init(void) {
    g_ram_size = RMALLOC_INIT_RAM_SIZE;
    g_ram = (uint8_t *)malloc(g_ram_size);
    g_top = g_ram;
    g_ram_end = g_ram+g_ram_size;

    /* initialize in a special order, because
     * memory block's previous will be set to the root end in mb_alloc.
     * Which, for the very first allocation, will be NULL.
     */
    g_root = (memory_block_t *)g_ram; 
    g_root->ptr = NULL;
    g_root->size = 0;
    g_top += sizeof(memory_block_t);
    g_root->next = NULL;
    g_root->previous = NULL;

    g_root_end = g_root;

    return g_root != NULL ? RM_OK : RM_ERROR;
}

status_t rmalloc_destroy(void) {
     
}

void rmalloc_print(memory_t *memory) {
    memory_block_t *block = MEMORY_AS_BLOCK(memory);
    fprintf(stderr, "* Handle at %d has block at %d of size %d bytes\n",
            (int)memory-(int)g_ram,
            (int)(block)-(int)g_ram,
            block->size);
    
    fprintf(stdout, "(%s :start %d :size %d)\n",
            block->used ? "alloc" : "free",
            (int)memory-(int)g_ram,
            block->size);
}

/* allocate a chunk of memory
 *
 * separates a memory block (internal structure) from a memory handle (user
 * space code). this could potentially be used for aliasing (?) memory blocks,
 * by pointing two different memory handles to the same memory block.
 *
 * both memory blocks and handles live in the same memory space, and are
 * layout in memory as follows (with memory growing down the page)::
 *
 * rmalloc(size) => 
 * memory block     : sizeof(memory_block_t) + size
 * +- memory_t      : sizeof(memory_t)
 *    +- locks      : 1
 * +- ptr           : size
 * +- size          : sizeof(size_t)
 * +- previous      : sizeof(*)
 * +- next          : sizeof(*)
 *
 * Assuming a 32-bit architecture:
 *
 *  start of memory_block_t 
 *    (memory_t) locks = 1
 *    ptr = 4
 *    size = 4 + size
 *    previous = 4
 *    next = 4
 *  
 *  = 1+4+4+4 = 13 for a memory_block_t. to that comes the size of the memory
 *  chunk
 *
 *  Layout: memory_block_t/memory_t, chunk
 *
 */
status_t rmalloc(memory_t **memory, size_t size) {
    memory_block_t *block;
    double top = (long int)(g_top-g_ram);
    double end = (long int)(g_ram_end-g_ram);
    float filling = 100.0*top/end;
    fprintf(stderr, "%d%% heap fullness\n", (int)filling);
    if (g_top+sizeof(memory_block_t)+size < g_ram_end) {
        memory_t *m = (memory_t *)g_top;
        memory_block_t *mb = MEMORY_AS_BLOCK(m);
        //g_top += sizeof(memory_block_t);

        block = mb_alloc(size);
        if (block) {
            m->locks = 0;
            *memory = m;
            rmalloc_print(*memory);
            return RM_OK;
        } 
        g_top -= sizeof(memory_block_t);

        *memory = NULL;
        return RM_ERROR;
    }
    fprintf(stderr, "Failed allocating %d bytes with %d bytes left.\n",
            size,
            g_ram_end-g_top);
    return RM_INSUFFICIENT_MEMORY;
}

/* Merge two adjacent (in memory) blocks, i.e.::
 *
 *      (a->ptr+1) == b
 *
 * As the information related to a memory block and the actual pointer to the
 * memory block is intertwined, it's not as easy as just bunching together the
 * two pointers, instead we have to overwrite the memory blocks themselves.
 * Specifically, block b will be overwritten.
 *
 * Make sure both memory blocks are free, i.e., used=0.
 *
 * Layout:  memory_t first, then block, then chunk.
 */
status_t mb_merge(memory_block_t *a, memory_block_t *b) {
    RM_ASSERT_RET(a != NULL && !a->used && a->ptr != NULL, RM_NOT_MERGED);
    RM_ASSERT_RET(b != NULL && !b->used && b->ptr != NULL, RM_NOT_MERGED);

    /* an easier check for now */
    /*
    RM_ASSERT(a->next == b);
    RM_ASSERT(b->previous == a);
    */
    
    if (a->size > 0 && b->size > 0) {
        
        /* are they adjacent? */
        fprintf(stderr, "%lu+%lu+=%lu matches? %p\n", a->ptr, a->size, a->ptr+a->size, b);
        /* layout in memory: a, memory chunk of 'size' bytes, then b. */
        RM_ASSERT_RET(a->ptr+a->size == b, RM_NOT_MERGED);

        a->next = b->next;
        a->size += b->size + sizeof(memory_block_t);

        //return RM_NOT_MERGED;
        return RM_MERGED;
    }

    return RM_NOT_MERGED;
}


/* Shrink a block into a smaller size and save the leftovers, 
 * effectively splitting the original block into two.
 */
status_t mb_shrink(memory_block_t *block, size_t new_size, memory_block_t **leftover) {
    memory_block_t *leftover_block;
    int leftover_size;

    RM_ASSERT_RET(BLOCK_AS_MEMORY(block)->locks == 0, RM_CANNOT_SHRINK);
    RM_ASSERT_RET(block->size > new_size, RM_CANNOT_SHRINK);

    // make sure there is space left in the orignal block after it has been
    // shrunk to new_size
    if (block->size - new_size < sizeof(memory_block_t)) 
        return RM_CANNOT_SHRINK;

    
    // XXX: Not done yet?
    leftover_block = block + sizeof(memory_block_t) + new_size;
    leftover_block->size = block->size - new_size - sizeof(memory_block_t);
    
    block->size = new_size;

    *leftover = leftover_block;
    return RM_OK;
}


/* start compacting at and after the block addressed by the memory_t */
void rmalloc_compact(memory_t *memory) {
    memory_block_t *mb;
    status_t status;
    
    if (memory)
        mb = MEMORY_AS_BLOCK(memory);
    else
        mb = g_root->next;

    // start by merging adjacent free blocks
    while (mb->next != NULL) {
        if (!mb->used && !mb->next->used) {
            void *a = mb;
            void *b = mb->next;
            size_t as = mb->size;
            size_t bs = mb->next->size;
            status = mb_merge(mb, mb->next);
            if (status == RM_MERGED) {
                fprintf(stderr, "Merged %p (%d) and %p (%d) => %d bytes\n",
                        a, as, b, bs, mb->size);
            }
        }
        mb = mb->next;
    }

    /* 
    rmalloc_dump()
    [ ] 131072     0x1c->ptr = 0xb7585038, end of pointer + memory_t =     0x38 = next =  0x20038
    [X] 131072  0x20038->ptr = 0xb75a5054, end of pointer + memory_t =  0x20054 = next =  0x40054
    [X] 131072  0x40054->ptr = 0xb75c5070, end of pointer + memory_t =  0x40070 = next =  0x60070
    [ ] 131072  0x60070->ptr = 0xb75e508c, end of pointer + memory_t =  0x6008c = next =  0x8008c
    [X] 131072  0x8008c->ptr = 0xb76050a8, end of pointer + memory_t =  0x800a8 = next =  0xa00a8
    [X] 131072  0xa00a8->ptr = 0xb76250c4, end of pointer + memory_t =  0xa00c4 = next =  0xc00c4
    [ ] 262172  0xc00c4->ptr = 0xb76450e0, end of pointer + memory_t =  0xc00e0 = next = 0x1000fc
    [X] 131072 0x1000fc->ptr = 0xb7685118, end of pointer + memory_t = 0x100118 = next = 0x120118
    [ ] 131072 0x120118->ptr = 0xb76a5134, end of pointer + memory_t = 0x120134 = next = 0x140134
    [X] 131072 0x140134->ptr = 0xb76c5150, end of pointer + memory_t = 0x140150 = next = 0x160150
    [X] 131072 0x160150->ptr = 0xb76e516c, end of pointer + memory_t = 0x16016c = next = 0x18016c
    [ ] 131072 0x18016c->ptr = 0xb7705188, end of pointer + memory_t = 0x180188 = next = 0x1a0188
    [X] 131072 0x1a0188->ptr = 0xb77251a4, end of pointer + memory_t = 0x1a01a4 = next = 0x1c01a4
    [ ] 262172 0x1c01a4->ptr = 0xb77451c0, end of pointer + memory_t = 0x1c01c0 = next = 0x2001dc
    [X] 131072 0x2001dc->ptr = 0xb77851f8, end of pointer + memory_t = 0x2001f8 = next = 0x2201f8
    [X] 131072 0x2201f8->ptr = 0xb77a5214, end of pointer + memory_t = 0x220214 = next = 0x240214
    [ ] 131072 0x240214->ptr = 0xb77c5230, end of pointer + memory_t = 0x240230 = next = 0x260230
    [X] 131072 0x260230->ptr = 0xb77e524c, end of pointer + memory_t = 0x26024c = next = 0x48a7aff8
    */
    
    // find the first free slot
    mb = g_root;
    while (mb->next) {
        mb = mb->next;
    
    }

}


/* yay for naive solutions.
 */
status_t rmrealloc(memory_t **new, memory_t **old, size_t size) {
    rmfree(*old);
    return rmalloc(new, size);
}

status_t rmlock(memory_t *memory, void **ptr) {
    memory_block_t *block = MEMORY_AS_BLOCK(memory);
    if (memory != NULL) {
        // XXX: what happens when there are more than 255 locks?
        if (memory->locks < 255)
            memory->locks++;
        
        *ptr = block->ptr;

        return RM_OK;
    }
    return RM_ERROR;
}

status_t rmunlock(memory_t *memory) {
    // XXX: Signal an error if unlocked too many times?
    if (memory != NULL && memory->locks != 0) {
        memory->locks--;
        return RM_OK;
    }
    return RM_ERROR;
}

status_t rmfree(memory_t *memory) {
    if (memory != NULL) {
        mb_mark_as_free(MEMORY_AS_BLOCK(memory));
        memory->locks = 0;
        rmalloc_print(memory);
    }
    return RM_ERROR;
}

const char *rmstatus(status_t s) {
    switch (s) {
        case RM_OK: return "OK"; 
        default:    return "error"; 
    }
    return "Unknown";
}
