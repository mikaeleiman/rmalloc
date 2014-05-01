#include <stdlib.h>
#include "gtest/gtest.h"
//#include "compact.c" // to get implementation specific details
#include "compact.h"
#include "compact_internal.h"


#define MB(x) 1024*1024*x
#define KB(x) 1024*x
const int heap_size = MB(32); // was 16
int heap_size_small = MB(1);
rmalloc_meta_t *g_state;


class SmallAllocTest : public ::testing::Test {
protected:
    SmallAllocTest() {
        storage = (void *)malloc(heap_size_small);
    }
    ~SmallAllocTest() {
    }
    void SetUp() {
        fprintf(stderr, "* Heap size %d\n", heap_size_small);
        rm_init(storage, heap_size_small);
        g_state = rm_get_state();
        srand(time(NULL));
    }
    void TearDown() {
        rm_destroy();
    }

    void *storage;
};

class AllocTest : public ::testing::Test {
protected:
    AllocTest() {
        //storage = (void *)malloc(heap_size);
    }
    ~AllocTest() {
    }
    void SetUp() {
        fprintf(stderr, "* Heap size %d\n", heap_size);
        rm_init(storage, heap_size);
        g_state = rm_get_state();
        srand(time(NULL));
    }
    void TearDown() {
        rm_destroy();
    }

    //void *storage;
    unsigned char storage[heap_size];
};

TEST_F(AllocTest, Init) {
    ASSERT_EQ(g_state->memory_bottom, (void *)((ptr_t)storage + g_state->free_block_slot_count*sizeof(free_memory_block_t *)));
    ASSERT_EQ(g_state->memory_top, g_state->memory_bottom);
    ASSERT_EQ((void *)g_state->header_top, (void *)((ptr_t)g_state->free_block_slots+heap_size));
    ASSERT_EQ((ptr_t)g_state->free_block_slots, (ptr_t)g_state->memory_bottom - g_state->free_block_slot_count*sizeof(free_memory_block_t *));
    ASSERT_EQ(g_state->free_block_slot_count, rm_log2(heap_size)+1); // to accomodate 2^(k+1) sized blocks

    ASSERT_LT((void *)g_state->free_block_slots, g_state->memory_bottom);
    ASSERT_LT(g_state->memory_top, (void *)g_state->header_bottom);
}


TEST_F(AllocTest, HeaderFindFree) {
    rm_header_t *h = rm_header_find_free();
    h->type = BLOCK_TYPE_UNLOCKED;
    h->memory = (void *)1;
    ASSERT_TRUE(h != NULL);
    h = rm_header_find_free();
    ASSERT_TRUE(h == NULL);

    ASSERT_EQ(g_state->header_bottom, g_state->header_top);
}

void test_header_set_used(rm_header_t *h) {
    h->type = BLOCK_TYPE_UNLOCKED;
    h->memory = (void *)1;
}


// verify that memory top increases and header bottom decreases
TEST_F(AllocTest, MallocGrowsMemoryHeaders) {
    uint8_t *memory_top = (uint8_t *)g_state->memory_top;
    rm_header_t *header_bottom = g_state->header_bottom;

    int size = 1024;
    rm_handle_t h = rm_malloc(size);
    ASSERT_TRUE(h != NULL);
    ASSERT_EQ((uint8_t *)g_state->memory_top, memory_top+1024);
    // first header is always free - memory location [0]
    ASSERT_EQ(g_state->header_bottom, header_bottom);
    
    memory_top += 1024;
    header_bottom = g_state->header_bottom;
    rm_handle_t h2 = rm_malloc(size);
    ASSERT_TRUE(h2 != NULL);
    ASSERT_EQ((uint8_t *)g_state->memory_top, memory_top+1024);

    ASSERT_EQ(g_state->header_bottom, header_bottom-1);
}

TEST_F(AllocTest, MallocExhaust) {
    int size = 1024;
    rm_handle_t h = rm_malloc(size);
    uint8_t *memory_top = (uint8_t *)g_state->memory_top;
    rm_header_t *header_bottom = g_state->header_bottom;
    while ((uint8_t *)g_state->header_bottom - (uint8_t *)g_state->memory_top > size+sizeof(rm_header_t)) {
        h = rm_malloc(size);

        ASSERT_EQ((uint8_t *)g_state->memory_top, memory_top+1024);
        ASSERT_EQ(g_state->header_bottom, header_bottom-1);
                
        memory_top = (uint8_t *)g_state->memory_top;
        header_bottom = g_state->header_bottom;
    }
    h = rm_malloc(size);
    ASSERT_TRUE(h == NULL);
}

TEST_F(AllocTest, FreeAndMergeSimple) {
    rm_handle_t h, h2, h3, h5;
    int size = 1024;
    h = rm_malloc(size);
    h2 = rm_malloc(size);
    h3 = rm_malloc(size);
    h = rm_malloc(size);
    h5 = rm_malloc(size);
    h = rm_malloc(size);

    rm_header_t *f2 = (rm_header_t *)h2;
    rm_free(h2);

    free_memory_block_t *block2 = rm_block_from_header(f2);
    ASSERT_TRUE(g_state->free_block_slots[rm_log2(f2->size)] == block2);
    ASSERT_TRUE(block2->header == f2);
    ASSERT_TRUE(block2->next == NULL);

    rm_header_t *f5 = (rm_header_t *)h5;
    rm_free(h5);

    free_memory_block_t *block5 = rm_block_from_header(f5);

    ASSERT_EQ(g_state->free_block_slots[rm_log2(f5->size)], block5);
    ASSERT_TRUE(block5->next == block2);
    ASSERT_TRUE(block5->header == f5);

    // killing off h3 means merging with h2!
    rm_header_t *f3 = (rm_header_t *)h3;
    uint8_t *end3 = (uint8_t *)f3->memory + f3->size;
    rm_free(h3);

    ASSERT_EQ(end3, (uint8_t *)f2->memory+f2->size);
    ASSERT_TRUE(rm_header_is_unused(f3));

    // didn't touch anything else did we?
    ASSERT_TRUE(block5->header == f5);
    ASSERT_TRUE(block2->header == f2);
}

TEST_F(AllocTest, FreeAndMergeEverySecond) {
    rm_handle_t h1, h2;

    int size = 1024;
    int count = g_state->memory_size/(size+sizeof(rm_header_t));
    bool done = false;
    uint8_t *memtop = (uint8_t *)g_state->memory_top;
    for (int i=0; i<count/2; i++) {
        h1 = rm_malloc(size);
        ASSERT_EQ((uint8_t *)g_state->memory_top, memtop+size);
        memtop += size;

        h2 = rm_malloc(size);
        ASSERT_EQ((uint8_t *)g_state->memory_top, memtop+size);
        memtop += size;

        rm_free(h2);
    }

    int free_blocks = 0;
    for (int i=0; i<g_state->free_block_slot_count; i++) {
        free_memory_block_t *free_block = g_state->free_block_slots[i];
        while (free_block != NULL) {
            free_memory_block_t *this_block = rm_block_from_header(free_block->header);
            ASSERT_EQ(this_block, free_block);
            free_blocks++;

            free_block = free_block->next;
        }
    }

    ASSERT_EQ(free_blocks, count/2);
}

// alloc, free, free later, alloc, free, free later
// the free later are freed at a later pass.
TEST_F(AllocTest, FreeOneMergeTwo) {
    rm_handle_t h1, h2;
    rm_handle_t *free_later;

    int size = 1024;
    int count = g_state->memory_size/(size+sizeof(rm_header_t));

    free_later = (rm_handle_t *)malloc(count/3 * sizeof(rm_handle_t *));
    int later_i=0;
    bool done = false;
    uint8_t *memtop = (uint8_t *)g_state->memory_top;
    int allocs = 0;
    for (int i=0; i<count/3; i++) {
        h1 = rm_malloc(size);
        ASSERT_EQ((uint8_t *)g_state->memory_top, memtop+size);
        memtop += size;
        allocs++;

        h2 = rm_malloc(size);
        ASSERT_EQ((uint8_t *)g_state->memory_top, memtop+size);
        memtop += size;
        allocs++;

        ASSERT_FALSE(h1 == h2);

        rm_header_t *f2 = (rm_header_t *)h2;
        //printf("freeing header %p memory %p\n", f2, f2->memory);
        void *mem2 = f2->memory;
        rm_free(h2);

        free_later[later_i] = rm_malloc(size);

        rm_header_t *hl = (rm_header_t *)free_later[later_i];
        void *memlater = hl->memory;
        ASSERT_FALSE(mem2 == memlater);

        ASSERT_EQ((uint8_t *)g_state->memory_top, memtop+size);
        memtop += size;
        allocs++;

        later_i++;
    }

    int free_blocks = 0;
    for (int i=0; i<g_state->free_block_slot_count; i++) {
        free_memory_block_t *free_block = g_state->free_block_slots[i];
        while (free_block != NULL) {
            free_memory_block_t *this_block = rm_block_from_header(free_block->header);
            ASSERT_EQ(this_block, free_block);
            free_blocks++;

            free_block = free_block->next;
        }
    }

    // this is supposed to merge with the already freed blocks. there will be
    // exactly the same number of free blocks before and after.
    // their new size will be size*2
    for (int i=0; i<later_i; i++) {
        rm_header_t *h = (rm_header_t *)free_later[i];
        //printf("#%d freeing header %p memory %p\n", i, h, h->memory);
        rm_free(free_later[i]);
    }

    int free_size = 0;
    int free_blocks_after = 0;
    free_memory_block_t *free_block = g_state->free_block_slots[rm_log2(size)];
    while (free_block != NULL) {
        free_size += free_block->header->size;
        free_blocks_after++;
        //printf("second checking block %d at header %p memory %p\n", free_blocks_after, free_block->header, free_block->header->memory);
        ASSERT_EQ(free_block->header->size, size*2);

        free_block = free_block->next;
    }

    ASSERT_EQ(free_size, free_blocks*size*2);

    ASSERT_EQ(free_blocks, free_blocks_after);

    uint32_t total = rm_stat_total_free_list();
    printf("total free list size = %u kb (%u mb)\n", total/1024, total/1048576);
}

#define ALLOC(siz, free) {fprintf(stderr, "\n* Allocating %d bytes, ", siz);h=rm_malloc(siz); rm_header_t *f = (rm_header_t *)h; if (h) {foo = (uint8_t *)rm_lock(h); fprintf(stderr, "got back %d bytes at %p\n", f->size, f->memory); for (int i=0; i<f->size; i++) foo[i] = filling[f->size % maxfill];rm_unlock(h);fprintf(stderr, "filled %p of size %d vs %d req. with %c", f, f->size, siz, filling[f->size % maxfill]);if (free) {fprintf(stderr, " freeing."); rm_free(h);}} else { fprintf(stderr, "couldn't alloc.\n");}}

TEST_F(AllocTest, AllocFill1) {
const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
int maxfill = strlen(filling);
rm_handle_t h;
uint8_t *foo;

    ALLOC(89603, true);
}

TEST_F(AllocTest, RandomAllorm_freeCompactMin) {
    const int maxsize = 1024*1024;
    int largest = 0;

    uint32_t allocated = 0;

const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
int maxfill = strlen(filling);
rm_handle_t h;
uint8_t *foo;

ALLOC((1534165+1868563+1155625+1944917+1922844), false)

ALLOC(1678702, true);
ALLOC(2014776, true);
ALLOC(1314708, true);
ALLOC(1323091, true);
ALLOC(1754083, true);
ALLOC(2014502, true);
}

// alloc, free, free later, alloc, free, free later
// the free later are freed at a later pass.
// compact
TEST_F(AllocTest, RandomAllorm_freeCompact) {
    const int maxsize = 1024*1024;
    int largest = 0;

    uint32_t allocated = 0;

    bool done = false;
    int count = 0;
    while (!done) {
        int size = rand()%maxsize+maxsize;
        rm_handle_t h = rm_malloc(size);

        if (h == NULL) {
            done = true;
            break;
        }
        fprintf(stderr, "ALLOC(%d, ", size);
        count++;
        allocated += size;
        if (largest < size)
            largest = size;

        // free by 50% probability
        if (rand()%2 == 0) {
            fprintf(stderr, "true);\n");
            rm_header_t *f = (rm_header_t *)h;
            allocated -= f->size;
            rm_free(h);
        } else {
            fprintf(stderr, "false)\n");
        }
    }

//    freeblock_print();
    printf("largest block allocated %d kb, total allocated before death = %u (%d kb) in %d allocs, free block hits = %d (allocated %d kb), total heap size %d kb, %d kb free in free list, %ld bytes free above top\n", largest/1024, allocated, allocated/1024, count,g_state->free_block_hits, g_state->free_block_alloc/1024, heap_size/1024, rm_stat_total_free_list()/1024, ((uint8_t*)g_state->header_bottom-(uint8_t*)g_state->memory_top));

    //compact();
}

// random alloc, random free
// then another sweep with free
TEST_F(AllocTest, RandomAllorm_freeFreeHalf) {
    const int maxsize = 128*1024;
    int largest = 0;

    uint32_t allocated = 0;

    bool done = false;
    int count = 0;
    while (!done) {
        int size = rand()%maxsize;
        rm_handle_t h = rm_malloc(size);
        rm_header_t *f = (rm_header_t*)h;

        if (h == NULL) {
            done = true;
            break;
        }
        fprintf(stderr, "ALLOC(%d, ", size);
        count++;
        allocated += size;
        if (largest < size)
            largest = size;

        // free by 50% probability
        if (rand()%2 == 0) {
            fprintf(stderr, "true);\n");
            allocated -= f->size;

            //fprintf(stderr, "free %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            rm_free(h);
        } else {
            //fprintf(stderr, "alloc %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            fprintf(stderr, "false)\n");
        }

        // statistics
        rm_header_t *hh = g_state->header_top;
        int unused_h = 0, used_h = 0;
        while (hh != g_state->header_bottom) {
            if (hh->memory == NULL) unused_h++;
            else used_h++;
            hh--;
        }
        //fprintf(stderr, "used headers %d unused headers %d total %d percent %d\n", used_h, unused_h, used_h+unused_h, unused_h*100.0/(used_h+unused_h));
    }

    fprintf(stderr, "largest block allocated %d kb, total allocated before death = %u (%d kb) in %d allocs, free block hits = %d (allocated %d kb), total heap size %d kb, %d kb free in free list, %ld bytes free above top\n", largest/1024, allocated, allocated/1024, count,g_state->free_block_hits, g_state->free_block_alloc/1024, heap_size/1024, rm_stat_total_free_list()/1024, ((uint8_t*)g_state->header_bottom-(uint8_t*)g_state->memory_top));
    //freeblock_print();

    rm_compact(200);
    return;

    rm_header_t *h = g_state->header_top;
    int i=0; 
    while (h != g_state->header_bottom) {
        if (h->memory != NULL && h->type == BLOCK_TYPE_UNLOCKED) {
            if (i++%2 == 0) {
                //fprintf(stderr, "free %p size %d (slot %d) at location %d\n", rm_block_from_header(h), h->size, rm_log2(h->size), g_header_top - h);
                //freeblock_print();
                rm_free((rm_handle_t)h);
            }
        }
        h--;
    }

    fprintf(stderr, "largest block allocated %d kb, total allocated before death = %u (%d kb) in %d allocs, free block hits = %d (allocated %d kb), total heap size %d kb, %d kb free in free list, %ld bytes free above top\n", largest/1024, allocated, allocated/1024, count,g_state->free_block_hits, g_state->free_block_alloc/1024, heap_size/1024, rm_stat_total_free_list()/1024, ((uint8_t*)g_state->header_bottom-(uint8_t*)g_state->memory_top));
    //freeblock_print();

    rm_header_sort_all();

    h = g_state->header_root;
    while (h != NULL && h->memory != NULL) {
        if (h->next && h->memory != NULL && h->next->memory != NULL)
            ASSERT_TRUE(h->memory > h->next->memory);
        h = h->next;
    }
    while (h != NULL) {
        ASSERT_TRUE(h->memory == NULL);
        h = h->next;
    }

    rm_compact(200);
}


TEST_F(SmallAllocTest, WriteCompactData3) {
    const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
    int maxfill = strlen(filling);
    int size = 0;
    uint8_t *foo;
    rm_handle_t h;


#if 0 // reproduces
    ALLOC(168451, true); 
    ALLOC(206576, true);
    ALLOC(248822, true);
    ALLOC(140332, false);
    ALLOC(213566, false);
    ALLOC(234477, true);
    ALLOC(158549, true);
    ALLOC(203257, false);
    ALLOC(160475, false);
#endif

#if 0 // overwrites memory
    ALLOC(228925, true);
    ALLOC(238945, false);
    ALLOC(254140, false);
    ALLOC(260854, true);
    ALLOC(250464, true);
/*
* 0xb6ae6fa4 -> 0xb6af6fd8 (�)?
freeblockshrink withheader block size 260854 new size 250464
    1. freeblockshrink withheader h: 0xb6af6fc8 - 10390 - 0xb6aa74b6 - 0
     2. freeblockshrink withheader h: 0xb6af6fc8 - 10390 - 0xb6aa74b6 - 0
    3. freeblockshrink withheader h: 0xb6af6fc8  10390  0xb6aa74b6  0
allocated 250464 in header 0xb6af6fd8 (flags 1 at 0xb6ae4716) filling with 250464 O
***** at foo[75954] of 250464 bytes, clash with bottom-most header (0xb6af6fc8 -> 0xb6aa744f size 10390.

Program received signal SIGABRT, Aborted.
0xb7fdf424 in __kernel_vsyscall ()
(gdb)
*/
#endif

    fprintf(stderr, "g_header_bottom = %p\n", g_state->header_bottom);

    ALLOC(228925, true);
    ALLOC(493085, false);
    ALLOC(260854, true);
    ALLOC(250464, true); // crash

#if 0 // crash in block_free(), overwritten memory
    ALLOC(228925, true);
    ALLOC(493085, false);
    ALLOC(260854, true);
    ALLOC(250464, true); // crash
/*
* Allocating 228925 bytes, filled 0xb6cf9008 of size 228925 vs 228925 req. with A
* Allocating 493085 bytes, filled 0xb6cf8ff8 of size 493085 vs 493085 req. with K
* Allocating 260854 bytes, filled 0xb6cf8fe8 of size 260854 vs 260854 req. with E
* Allocating 250464 bytes, freeblock_find(250464) scanning in 17
* 0xb6ce8fa4 -> 0xb6cf8fe8 (�)?
freeblockshrink withheader block size 260854 new size 250464
    1. freeblockshrink withheader h: 0xb6cf8fd8 - 10390 - 0xb6ca94b6 - 0
     2. freeblockshrink withheader h: 0xb6cf8fd8 - 10390 - 0xb6ca94b6 - 0
    3. freeblockshrink withheader h: 0xb6cf8fd8  10390  0xb6ca94b6  0
filled 0xb6cf8fe8 of size 1330597711 vs 250464 req. with O

Program received signal SIGSEGV, Segmentation fault.
0xb7e42e99 in ?? () from /lib/i386-linux-gnu/libc.so.6
(gdb) bt
#0  0xb7e42e99 in ?? () from /lib/i386-linux-gnu/libc.so.6
#1  0x0804b774 in block_free (header=0xb6cf8fe8) at compact.c:321
#2  0x0804c7c5 in rm_free (h=0xb6cf8fe8) at compact.c:1055
#3  0x0804d363 in SmallAllocTest_WriteCompactData3_Test::TestBody (this=0x807f6b0) at test_internal.cpp:476
#4  0x0806cbc8 in void testing::internal::HandleExceptionsInMethodIfSupported<testing::Test, void>(testing::Test*, void (testing::Test::*)(), char const*) ()
#5  0x080653f8 in testing::Test::Run() ()
#6  0x080654c1 in testing::TestInfo::Run() ()
#7  0x080655e7 in testing::TestCase::Run() ()
#8  0x0806586e in testing::internal::UnitTestImpl::RunAllTests() ()
#9  0x0806c7d8 in bool testing::internal::HandleExceptionsInMethodIfSupported<testing::internal::UnitTestImpl, bool>(testing::internal::UnitTestImpl*, bool (testing::internal::UnitTestImpl::*)(), char const*) ()
#10 0x08064a5a in testing::UnitTest::Run() ()
#11 0x0804b1ec in main ()
(gdb) up
#1  0x0804b774 in block_free (header=0xb6cf8fe8) at compact.c:321
321         memcpy((void *)block, (void *)&b, sizeof(free_memory_block_t));
(gdb) print block
$1 = (free_memory_block_t *) 0x9e9e9e96
(gdb) 
*/
#endif // crash in block_free(), overwritten memory


#if 0 // crash elsewhere
    ALLOC(228925, true);
    ALLOC(493085, false);
    ALLOC(260854, true);
    ALLOC(250464, true); // crash
/*
* 0xb6ce8fa4 -> 0xb6cf8fe8 (�)?
freeblockshrink withheader block size 260854 new size 250464
    1. freeblockshrink withheader h: 0xb6cf8fd8 - 10390 - 0xb6ca94b6 - 0
     2. freeblockshrink withheader h: 0xb6cf8fd8 - 10390 - 0xb6ca94b6 - 0
    3. freeblockshrink withheader h: 0xb6cf8fd8  10390  0xb6ca94b6  0
filled 0xb6cf8fe8 of size 1330597711 vs 250464 req. with O

Program received signal SIGSEGV, Segmentation fault.
0xb7e42e99 in ?? () from /lib/i386-linux-gnu/libc.so.6
(gdb) quit
*/
#endif



    rm_header_t *f = g_state->header_top;
    while (f >= g_state->header_bottom) {
        if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
            uint8_t *foo = (uint8_t *)f->memory;
            char filler = filling[f->size % maxfill];
            fprintf(stderr, "Testing header %p mapping %p of size %d\n", f, f->memory, f->size);
            for (int i=0; i<f->size; i++) {
                if (foo[i] != filler)
                    fprintf(stderr, "mismatch at pos %d\n", i);
                ASSERT_EQ(foo[i], filler);
            }
        }
        f--;
    }

}

// test compact
TEST_F(SmallAllocTest, WriteCompactData2) {
    const int maxsize = 256*1024;
    int largest = 0;

    uint32_t allocated = 0;

    const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
    int maxfill = strlen(filling);
    int size = 0;
    uint8_t *foo;
    rm_handle_t h;

    ALLOC(462966, true);
    ALLOC(339887, true);
    ALLOC(421029, false);

    return;


    bool done = false;
    int count = 0;
    while (!done) {
        int size = rand()%maxsize+maxsize;
        fprintf(stderr, "trying to allocate %d (allocated %d)...", size, allocated);
        rm_handle_t h = rm_malloc(size);
        rm_header_t *f = (rm_header_t *)h;

        if (h == NULL) {
            done = true;
            break;
        }
        ASSERT_EQ(f->type, BLOCK_TYPE_UNLOCKED);

        char filler = filling[f->size % maxfill];
        fprintf(stderr, "allocated %d in header %p (type %d at %p) filling with %d %c", size, f, f->type, f->memory, f->size, filler);

        uint8_t *foo = (uint8_t *)rm_lock(h);
        ASSERT_FALSE(rm_freeblock_exists_memory(foo));
        for (int i=0; i<f->size; i++) {
            foo[i] = filler;
            ASSERT_GE(&foo[i], g_state->memory_bottom);
            if (&foo[i] == (void *)g_state->header_bottom) {
                fprintf(stderr, "\n***** at foo[%d] of %d bytes, clash with bottom-most header (%p -> %p size %d.\n", i, size,
                        g_state->header_bottom, g_state->header_bottom->memory, g_state->header_bottom->size);
                abort();
            }
            ASSERT_LT(&foo[i], (void *)g_state->header_bottom);
            //ASSERT_FALSE(freeblock_exists((free_memory_block_t *)(filler<<24 | filler<<16 | filler<<8 | filler)));
            /*
            for (int j=0; j<g_free_block_slot_count; j++)
                if (g_free_block_slots[j] < g_memory_bottom || g_free_block_slots[j] >= g_memory_top)
                    fprintf(stderr, "slot %d at %p not in range, pos %d at %p\n",
                            j, &g_free_block_slots[j], i, &foo[i]);
            */
        }
        rm_unlock(h);

        count++;
        allocated += size;
        if (largest < size)
            largest = size;

        // free by 50% probability
        if (rand()%2 == 0) {
            allocated -= f->size;

            //fprintf(stderr, "free %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            fprintf(stderr, "....freeing");
            rm_free(h);
        } else {
            //fprintf(stderr, "alloc %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
        }
        fprintf(stderr, "\n");

        // statistics
        rm_header_t *hh = g_state->header_top;
        int unused_h = 0, used_h = 0;
        while (hh != g_state->header_bottom) {
            if (hh->memory == NULL) unused_h++;
            else used_h++;
            hh--;
        }
        //fprintf(stderr, "used headers %d unused headers %d total %d percent %d\n", used_h, unused_h, used_h+unused_h, unused_h*100.0/(used_h+unused_h));
    }

    //compact();

    rm_header_t *f = g_state->header_top;
    while (f >= g_state->header_bottom) {
        if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
            uint8_t *foo = (uint8_t *)f->memory;
            char filler = filling[f->size % maxfill];
            for (int i=0; i<f->size; i++) {
                if (foo[i] != filler)
                    fprintf(stderr, "mismatch at pos %d\n", i);
                ASSERT_EQ(foo[i], filler);
            }
        }
        
        f--;
    }

}

TEST_F(AllocTest, CompactLock) {
    //const int maxsize = 128*1024; // yes rand
    //const int maxsize = 256*1024; // yes rand
    //const int maxsize = 384*1024; // yes rand
    //const int maxsize = 320*1024; // yes rand
    //const int maxsize = 1024*1024; // yes rand

    int largest = 0;

    uint32_t allocated = 0;

    const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
    int maxfill = strlen(filling);

    time_t t;

    time(&t); const int maxsize=512*1024;
    //t = 1330835020; const int maxsize=1024*1024;
    //t = 1330835020; const int maxsize=1024*1024;

    //t = 1330820285; const int maxsize=512*1024; // heap_size = 32M -- OK!
    //t = 1330783009; const int maxsize = 2097152; // crash! 32mb heap -- OK!
    //t = 1330809470; const int maxsize = 1048576; // crash 32M, run #2039 -- OK!
    // t = 1330785742; const int maxsize = 524288; // crash 32M, run #1590 -- OK!
    //t = 1330820285; const int maxsize = 524288; // crash 32M, run #5050 -- OK!

    //t = 1337020745; const int maxsize = 512*1024; // crash

#if 0

maxsize = 512k, heap = 32m
t = 1337326578

ALLOC(10399, true); // header 0xb74ea778 memory 0xb74e7d0e
freeblock_find(297430) scanning in 18
freeblock_find: block->header->size 461605 >= (requested) size 297430
-> root, correct size.
-> shrinking found_block (header 0xb74ea9d8 size 461605) to new size 297430
== header_new() = (nil)
    2. couldn't allocate new header.
-> after shrinkage, found_block size 461605, returning header 0xb74ea9d8
ALLOC(297430, test_internal.cpp:812: Failure
Value of: f->size
  Actual: 461605
Expected: size
Which is: 297430

#endif


    //t = 1330581720; const int maxsize=128*1024;

    //t = 1330343837; // OK!
    //t = 1330504491; // OK!

    //t = 1330505134;  // NOT ok.
    //t = 1330507550; // NOT ok with 256 kb
    //
    //t = 1330507693; //  ok with 320 kb

    //t = 1330738161;  const int maxsize = 1024*1024; // heap_size 256 CRASH!

    
    srand(t);

    fprintf(stderr, "*** Testing with seed %ld maxsize %d\n", t, maxsize);

    bool done = false;
    int count = 0;
    int blocks_before=0, blocks_after=0;
    while (!done) {
        int size = rand()%maxsize;
        rm_handle_t h = rm_malloc(size);
        //fprintf(stderr, "trying to allocate %d (allocated %d)...", size, allocated);
        fprintf(stderr, "ALLOC(%d, ", size);
        rm_header_t *f = (rm_header_t *)h;

        if (h == NULL) {
            done = true;
            fprintf(stderr, "false); // out of memory: h = NULL\n");
            break;
        }

        char filler = filling[f->size % maxfill];
        //fprintf(stderr, "allocated %d in header %p (flags %d at %p) filling with %d %c", size, f, f->flags, f->memory, f->size, filler);

        uint8_t *foo = (uint8_t *)rm_lock(h);

        // XXX: Test again but add "&& size < sizeof(header_t), because in all
        // other cases f->size === size!!!
        //bool initial_diff = f->size != size;
        bool initial_same_size = f->size == size;

        if (size >= sizeof(rm_header_t))
            ASSERT_GE(f->size, size);

        for (int i=0; i<f->size; i++) {
            foo[i] = filler;
            if (f->size != size && initial_same_size) {
                fprintf(stderr, "(inside loop %d) 0xb7cf0848 locked memory = %p, f->memory = %p of size %d (requested size %d), initial same size %d\n", i, foo, f->memory, f->size, size, initial_same_size);
                abort();
            }
        }
        rm_unlock(h);

        count++;
        allocated += size;
        if (largest < size)
            largest = size;

        blocks_before++;

        // free by 50% probability
        if (rand()%2 == 0) {
            allocated -= f->size;
            blocks_before--;

            //fprintf(stderr, "free %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            //fprintf(stderr, "....freeing");
            fprintf(stderr, "true); // header %p memory %p\n", f, f->memory);
            rm_free(h);
        } else if (rand()%4 == 0) {
            rm_lock(h);
            fprintf(stderr, "false); // header %p memory %p filler %c\n", f, f->memory, filler);
        } else {
            //fprintf(stderr, "alloc %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            fprintf(stderr, "false); // header %p memory %p filler %c\n", f, f->memory, filler);
        }

        // statistics
        rm_header_t *hh = g_state->header_top;
        int unused_h = 0, used_h = 0;
        while (hh != g_state->header_bottom) {
            if (hh->memory == NULL) unused_h++;
            else used_h++;
            hh--;
        }
    }

    rm_compact(0);

    fprintf(stderr, "Verifying data: ");
    rm_header_t *f = g_state->header_top;
    while (f >= g_state->header_bottom) {
        if (f && f->memory)
            if (f->type == BLOCK_TYPE_UNLOCKED || f->type == BLOCK_TYPE_LOCKED)
                blocks_after++;

        if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
            uint8_t *foo = (uint8_t *)f->memory;
            char filler = filling[f->size % maxfill];
            for (int i=0; i<f->size; i++)
                ASSERT_EQ(foo[i], filler);
        }
        
        f--;
    }
    ASSERT_EQ(blocks_before, blocks_after);
    fprintf(stderr, "OK!\n");

}

TEST_F(SmallAllocTest, HeaderListSort) {
}


TEST_F(SmallAllocTest, CompactLock1) {
    const int maxsize = 128*1024; // yes rand
    //const int maxsize = 256*1024; // yes rand
    //const int maxsize = 384*1024; // yes rand
    //const int maxsize = 320*1024; // yes rand
    int largest = 0;

    uint32_t allocated = 0;

    const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
    int maxfill = strlen(filling);

    time_t t;
    time(&t);

    //t = 1330343837; // OK!
    //t = 1330504491; // OK!

    //t = 1330505134;  // NOT ok.
    //t = 1330507550; // NOT ok with 256 kb
    //
    //t = 1330507693; //  ok with 320 kb
    //t = 1330606627;

    srand(t);

    fprintf(stderr, "*** Testing with seed %ld\n", t);

    bool done = false;
    int count = 0;
    while (!done) {
        int size = rand()%maxsize;
        rm_handle_t h = rm_malloc(size);
        //fprintf(stderr, "trying to allocate %d (allocated %d)...", size, allocated);
        fprintf(stderr, "ALLOC(%d, ", size);
        rm_header_t *f = (rm_header_t *)h;

        if (h == NULL) {
            done = true;
            fprintf(stderr, "false);\n");
            break;
        }

        char filler = filling[f->size % maxfill];
        //fprintf(stderr, "allocated %d in header %p (flags %d at %p) filling with %d %c", size, f, f->flags, f->memory, f->size, filler);

        uint8_t *foo = (uint8_t *)rm_lock(h);
        for (int i=0; i<f->size; i++)
            foo[i] = filler;
        rm_unlock(h);

        count++;
        allocated += size;
        if (largest < size)
            largest = size;

        // free by 50% probability
        if (rand()%2 == 0) {
            allocated -= f->size;

            //fprintf(stderr, "free %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            //fprintf(stderr, "....freeing");
            fprintf(stderr, "true); // header %p memory %p\n", f, f->memory);
            rm_free(h);
        } else if (rand()%4 == 0) {
            rm_lock(h);
            fprintf(stderr, "false); // header %p memory %p filler %c\n", f, f->memory, filler);
        } else {
            //fprintf(stderr, "alloc %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            fprintf(stderr, "false); // header %p memory %p filler %c\n", f, f->memory, filler);
        }

        // statistics
        rm_header_t *hh = g_state->header_top;
        int unused_h = 0, used_h = 0;
        while (hh != g_state->header_bottom) {
            if (hh->memory == NULL) unused_h++;
            else used_h++;
            hh--;
        }

        {
        fprintf(stderr, "Verifying data: ");
        rm_header_t *f = g_state->header_top;
        while (f >= g_state->header_bottom) {
            if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
                uint8_t *foo = (uint8_t *)f->memory;
                char filler = filling[f->size % maxfill];
                for (int i=0; i<f->size; i++)
                    ASSERT_EQ(foo[i], filler);
            }
            
            f--;
        }
        fprintf(stderr, "OK!\n");
        }

        //fprintf(stderr, "used headers %d unused headers %d total %d percent %d\n", used_h, unused_h, used_h+unused_h, unused_h*100.0/(used_h+unused_h));
    }

    rm_compact(0);

    fprintf(stderr, "Verifying data: ");
    rm_header_t *f = g_state->header_top;
    while (f >= g_state->header_bottom) {
        if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
            uint8_t *foo = (uint8_t *)f->memory;
            char filler = filling[f->size % maxfill];
            for (int i=0; i<f->size; i++)
                ASSERT_EQ(foo[i], filler);
        }
        
        f--;
    }
    fprintf(stderr, "OK!\n");

}

TEST_F(SmallAllocTest, WriteCompactData4) {
    const int maxsize = 128*1024;
    int largest = 0;

    uint32_t allocated = 0;

    const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
    int maxfill = strlen(filling);

    bool done = false;
    int count = 0;
    while (!done) {
        int size = rand()%maxsize;
        rm_handle_t h = rm_malloc(size);
        //fprintf(stderr, "trying to allocate %d (allocated %d)...", size, allocated);
        fprintf(stderr, "ALLOC(%d, ", size);
        rm_header_t *f = (rm_header_t *)h;

        if (h == NULL) {
            done = true;
            fprintf(stderr, "false);\n");
            break;
        }

        char filler = filling[f->size % maxfill];
        //fprintf(stderr, "allocated %d in header %p (flags %d at %p) filling with %d %c", size, f, f->flags, f->memory, f->size, filler);

        uint8_t *foo = (uint8_t *)rm_lock(h);
        for (int i=0; i<f->size; i++)
            foo[i] = filler;
        rm_unlock(h);

        count++;
        allocated += size;
        if (largest < size)
            largest = size;

        // free by 50% probability
        if (rand()%2 == 0) {
            allocated -= f->size;

            //fprintf(stderr, "free %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            //fprintf(stderr, "....freeing");
            fprintf(stderr, "true);\n");
            rm_free(h);
        } else {
            //fprintf(stderr, "alloc %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            fprintf(stderr, "false);\n");
        }

        // statistics
        rm_header_t *hh = g_state->header_top;
        int unused_h = 0, used_h = 0;
        while (hh != g_state->header_bottom) {
            if (hh->memory == NULL) unused_h++;
            else used_h++;
            hh--;
        }
        //fprintf(stderr, "used headers %d unused headers %d total %d percent %d\n", used_h, unused_h, used_h+unused_h, unused_h*100.0/(used_h+unused_h));
    }

    rm_compact(0);

    fprintf(stderr, "Verifying data: ");
    rm_header_t *f = g_state->header_top;
    while (f >= g_state->header_bottom) {
        if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
            uint8_t *foo = (uint8_t *)f->memory;
            char filler = filling[f->size % maxfill];
            for (int i=0; i<f->size; i++)
                ASSERT_EQ(foo[i], filler);
        }
        
        f--;
    }
    fprintf(stderr, "OK!\n");

}

TEST_F(SmallAllocTest, WriteCompactData4Fix) {
    const int maxsize = 128*1024;
    int largest = 0;

    uint32_t allocated = 0;

    const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
    int maxfill = strlen(filling);

    rm_handle_t h;
    rm_header_t *f;
    uint8_t *foo;

    ALLOC(106043, false);
    ALLOC(111793, false);
    ALLOC(46390, false);
    ALLOC(45030, true);
    ALLOC(77698, false);
    ALLOC(13285, true);
    ALLOC(21081, true);
    ALLOC(116974, true);
    ALLOC(32364, false);
    ALLOC(58771, true);
    ALLOC(81772, false);
    ALLOC(1520, true);
    ALLOC(23988, true);
    ALLOC(127542, false);
    ALLOC(13087, true);
    ALLOC(27466, true);
    ALLOC(61608, true);
    ALLOC(31584, true);
    ALLOC(80621, true);
    ALLOC(2790, true);
    ALLOC(50419, false);
    ALLOC(26578, false);
    ALLOC(29299, true);
    ALLOC(73455, false);

    rm_compact(200);

    fprintf(stderr, "Verifying data: ");
    f = g_state->header_top;
    while (f >= g_state->header_bottom) {
        if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
            foo = (uint8_t *)f->memory;
            char filler = filling[f->size % maxfill];
            for (int i=0; i<f->size; i++)
                ASSERT_EQ(foo[i], filler);
        }
        
        f--;
    }
    fprintf(stderr, "OK!\n");
}

TEST_F(SmallAllocTest, WriteCompactData5Fix) {
    const int maxsize = 128*1024;
    int largest = 0;

    uint32_t allocated = 0;

    const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
    int maxfill = strlen(filling);

    rm_handle_t h;
    rm_header_t *f;
    uint8_t *foo;

    ALLOC(6058, true);
    ALLOC(32990, true);
    ALLOC(47709, true);
    ALLOC(48052, false);
    ALLOC(92696, false);
    ALLOC(62651, false);
    ALLOC(44986, false);
    ALLOC(23528, true);
    ALLOC(39903, false);
    ALLOC(92385, true);
    ALLOC(109467, false);
    ALLOC(96967, true);
    ALLOC(5569, false);
    ALLOC(745, false);
    ALLOC(26570, true);
    ALLOC(4398, true);
    ALLOC(91367, true);
    ALLOC(4432, true);
    ALLOC(108400, true);

    rm_compact(200);

    fprintf(stderr, "Verifying data: ");
    f = g_state->header_top;
    while (f >= g_state->header_bottom) {
        if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
            foo = (uint8_t *)f->memory;
            char filler = filling[f->size % maxfill];
            for (int i=0; i<f->size; i++)
                ASSERT_EQ(foo[i], filler);
        }
        
        f--;
    }
    fprintf(stderr, "OK!\n");
}

// test compact
TEST_F(AllocTest, WriteCompactData) {
    const int maxsize = 512*1024;
    int largest = 0;

    uint32_t allocated = 0;

    const char *filling = "ABCDEFGHIJKLMNOPQRSTUVXYZ";
    int maxfill = strlen(filling);

    bool done = false;
    int count = 0;
    while (!done) {
        int size = rand()%maxsize;
        rm_handle_t h = rm_malloc(size);
        fprintf(stderr, "trying to allocate %d (allocated %d)...", size, allocated);
        rm_header_t *f = (rm_header_t *)h;

        if (h == NULL) {
            done = true;
            break;
        }

        char filler = filling[f->size % maxfill];
        fprintf(stderr, "allocated %d in block %p (type %d at %p) filling with %d %c", size, rm_block_from_header(f), f->type, f->memory, f->size, filler);

        uint8_t *foo = (uint8_t *)rm_lock(h);
        for (int i=0; i<f->size; i++)
            foo[i] = filler;
        rm_unlock(h);

        count++;
        allocated += size;
        if (largest < size)
            largest = size;

        // free by 50% probability
        if (rand()%2 == 0) {
            allocated -= f->size;

            //fprintf(stderr, "free %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
            fprintf(stderr, "....freeing");
            rm_free(h);
        } else {
            //fprintf(stderr, "alloc %p size %d (slot %d)\n", rm_block_from_header(f), f->size, rm_log2(f->size));
            //freeblock_print();
        }
        fprintf(stderr, "\n");

        rm_header_t *f2 = g_state->header_top;
        fprintf(stderr, "checking: ");
        while (f2 >= g_state->header_bottom) {
            if (f2 && f2->memory && f2->type == BLOCK_TYPE_UNLOCKED) {
                fputc('.', stderr);
                uint8_t *foo2 = (uint8_t *)f2->memory;
                char filler = filling[f2->size % maxfill];
                for (int i=0; i<f2->size; i++) {
                    if (foo2[i] != filler) 
                        fprintf(stderr, "\nByte at %d: '%c' != '%c', header %p (offset %ld), size %d, block %p\n",
                                i, foo2[i], filler, f2, g_state->header_top-f2, f2->size, rm_block_from_header(f2));

                    ASSERT_EQ(foo2[i], filler);
                }
            }
            f2--;
        }
        fprintf(stderr, "\n");

    }

    rm_compact(200);

    rm_header_t *f = g_state->header_top;
    while (f >= g_state->header_bottom) {
        if (f && f->memory && f->type == BLOCK_TYPE_UNLOCKED) {
            uint8_t *foo = (uint8_t *)f->memory;
            char filler = filling[f->size % maxfill];
            for (int i=0; i<f->size; i++) {
                    if (foo[i] != filler) 
                        fprintf(stderr, "\nByte at %d: '%c' != '%c', header %p (offset %ld) block %p\n",
                                i, foo[i], filler, f, g_state->header_top-f, rm_block_from_header(f));
                ASSERT_EQ(foo[i], filler);
            }
        }
        
        f--;
    }

}

#if 0
TEST_F(AllocTest, MakeInfoItem) {
    int n = up2(20000);
    ASSERT_EQ(n, 32768);
    info_item_t *ii = make_info_item(n);
    ASSERT_EQ(ii->k, log2(n/MIN_CHUNK_SIZE)); // MIN_CHUNK_SIZE==4096 -> 4=0, 8=1, 16=2, 32=3
    ASSERT_TRUE(ii->larger == NULL);
    ASSERT_TRUE(ii->smaller == NULL);
    ASSERT_TRUE(ii->free_list == NULL);
}

TEST_F(AllocTest, SplitRootChunk) {
    // Split the available (a or b) of the root chunk
    info_item_t *ii = split_chunk_item(groot, groot->free_list, NULL);
    ASSERT_TRUE(ii != NULL);
    ASSERT_TRUE(ii->free_list != NULL);
    ASSERT_EQ(ii->free_list->a, groot->free_list->a);
    ASSERT_EQ(ii->free_list->b, (void *)((uint8_t *)ii->free_list->a+chunk_size(ii)));
}

TEST_F(AllocTest, SplitChunkTwice) {
    // Split both a and b of a chunk
    info_item_t *ii = split_chunk_item(groot, groot->free_list, NULL);

    info_item_t *i1 = split_chunk_item(ii, ii->free_list, NULL);
    ASSERT_TRUE(i1 != NULL);
    ASSERT_TRUE(i1->free_list != NULL);
    ASSERT_EQ(i1->free_list->a, ii->free_list->a);
    ASSERT_EQ(i1->free_list->b, (void *)((uint8_t *)i1->free_list->a+chunk_size(i1)));

    info_item_t *i2 = split_chunk_item(ii, ii->free_list, NULL);
    ASSERT_TRUE(i2 != NULL);
    ASSERT_TRUE(i2->free_list != NULL);
    ASSERT_EQ(i2->free_list->a, ii->free_list->b);
    ASSERT_EQ(i2->free_list->b, (void *)((uint8_t *)i2->free_list->a+chunk_size(i2)));

    info_item_t *i3 = split_chunk_item(groot, groot->free_list, NULL);
    ASSERT_TRUE(i3 == NULL);
}

TEST_F(AllocTest, ExtendFreelist) {
    // Add a chunk to an existing freelist
    info_item_t *ii = split_chunk_item(groot, groot->free_list, NULL);

    info_item_t *i1 = split_chunk_item(ii, ii->free_list, NULL);
    ASSERT_TRUE(i1 != NULL);
    ASSERT_TRUE(i1->free_list != NULL);
    ASSERT_EQ(i1->free_list->a, ii->free_list->a);
    ASSERT_EQ(i1->free_list->b, (void *)((uint8_t *)i1->free_list->a+chunk_size(i1)));

    info_item_t *i2 = split_chunk_item(ii, ii->free_list, NULL);
    ASSERT_TRUE(i2 != NULL);
    ASSERT_TRUE(i2->free_list != NULL);
    ASSERT_EQ(i2->free_list->a, ii->free_list->b);
    ASSERT_EQ(i2->free_list->b, (void *)((uint8_t *)i2->free_list->a+chunk_size(i2)));

    info_item_t *i3 = split_chunk_item(groot, groot->free_list, NULL);
    ASSERT_TRUE(i3 == NULL);
}

TEST_F(AllocTest, FindFreeChunk) {
    //EXPECT_TRUE(false);
}

#define CHUNK_EQUAL(i, p, c) (p->a == c->a || p->b == c->a) && (c->b == (uint8_t *)(c->a)+chunk_size(i))

#define CHUNK_EQUAL_A(i, p, c) {ASSERT_EQ(p->a, c->a); ASSERT_EQ(c->b, (uint8_t *)(c->a)+chunk_size(i));}
#define CHUNK_EQUAL_B(i, p, c) {ASSERT_EQ(p->b, c->a); ASSERT_EQ(c->b, (uint8_t *)(c->a)+chunk_size(i));}

TEST_F(AllocTest, RequestChunkZeroLevel) {
    chunk_item *ci = request_chunk(groot, groot->k);
    ASSERT_TRUE(ci != NULL);
    ASSERT_TRUE(ci == groot->free_list);
}

TEST_F(AllocTest, RequestChunkOneLevel) {
    int newk = groot->k - 1;
    chunk_item *ci = request_chunk(groot, newk);
    ASSERT_TRUE(ci != NULL);
    ASSERT_EQ(groot->smaller->k, newk);

    chunk_item_t *chunk = groot->smaller->free_list;
    ASSERT_EQ(ci, chunk);

    ASSERT_TRUE(CHUNK_EQUAL(groot->smaller, groot->free_list, chunk));
}

TEST_F(AllocTest, RequestChunkTwoLevel) {
    int newk = groot->k - 2;
    chunk_item *ci = request_chunk(groot, newk);

    ASSERT_TRUE(ci != NULL);
    ASSERT_EQ(groot->smaller->smaller->k, newk);
    ASSERT_EQ(ci, groot->smaller->smaller->free_list);
    ASSERT_TRUE(CHUNK_EQUAL(groot->smaller->smaller, groot->smaller->free_list, groot->smaller->smaller->free_list));
}

TEST_F(AllocTest, RequestChunkBottomLevel) {
    chunk_item *ci = request_chunk(groot, 0);
    ASSERT_TRUE(ci != NULL);

    info_item_t *ii = groot;
    info_item_t *pii = ii;
    while (ii->smaller) {
        pii = ii;
        ii = ii->smaller;
    }

    // just check the last level
    ASSERT_TRUE(CHUNK_EQUAL(ii, pii->free_list, ii->free_list));
}

TEST_F(AllocTest, RequestChunkTwoLevelTwice) {
    int newk = groot->k - 2;
    chunk_item *ci1 = request_chunk(groot, newk);
    chunk_item *ci2 = request_chunk(groot, newk);
    ASSERT_EQ(ci1, ci2);

    ASSERT_EQ(1, chunk_count(groot->smaller->free_list));
    ASSERT_EQ(1, chunk_count(groot->smaller->smaller->free_list));

    // A used => same chunk
    ci_set_flag(ci1, CI_A_USED);
    ci2 = request_chunk(groot, newk);
    ASSERT_EQ(ci1, ci2);
    ASSERT_EQ(1, chunk_count(groot->smaller->smaller->free_list));

    // B used => same chunk
    ci_clear_flag(ci1, CI_A_USED);
    ci_set_flag(ci1, CI_B_USED);
    ci2 = request_chunk(groot, newk);
    ASSERT_EQ(ci1, ci2);
    ASSERT_EQ(1, chunk_count(groot->smaller->smaller->free_list));

    // A and B used => new chunk
    ci_set_flag(ci1, CI_A_USED);
    ci_set_flag(ci1, CI_B_USED);
    ci2 = request_chunk(groot, newk);
    ASSERT_NE(ci1, ci2);
    ASSERT_EQ(2, chunk_count(groot->smaller->smaller->free_list));

    info_item_t *pii = groot->smaller;
    info_item_t *ii = groot->smaller->smaller;

    ASSERT_EQ(ii->free_list, ci1);

    CHUNK_EQUAL_A(ii, pii->free_list, ci1);
    CHUNK_EQUAL_B(ii, pii->free_list, ci2);
}

chunk_item_t *chunk_discard(chunk_item_t *chunk) {
    ci_set_flag(chunk, CI_A_USED);
    ci_set_flag(chunk, CI_B_USED);
    return chunk;
}

/* allocate each block in each level
 */
TEST_F(AllocTest, RequestChunkNLevelInfinite) {
    int level = 1;
    int count = 1;
    int newk = groot->k - level;
    chunk_item_t *ci = request_chunk(groot, newk);
    info_item_t *ri = groot->smaller;
    ASSERT_EQ(ci, ri->free_list);

    for (level=2; level<10; level++) {
        newk = groot->k - level;
        chunk_item_t *ci = request_chunk(groot, newk);
        ri = ri->smaller;
        ASSERT_EQ(ci, ri->free_list);
        count = 1;
        while (ci) {
            // mark as used
            chunk_discard(ci);

            ASSERT_EQ(chunk_count(ri->free_list), count);

            ci = request_chunk(groot, newk);
            count++;
        }
        count--;
        /*
        fprintf(stderr, "%d chunks (of %d) size %dK, k = %d => max %.0f\n",
                chunk_count(ri->free_list), count, chunk_size(ri)/1024,
                newk, pow(2, level-1));
        */
        ASSERT_EQ(pow(2, level-1), count);

        // un-discard the chunks in this level
        ci = ri->free_list;
        while (ci) {
            ci->flags = 0;
            ci = ci->next;
        }
    }
}

/* merge all chunks for a specific k.
 */
TEST_F(AllocTest, MergeOneLevel) {
    int level = 2;
    
    int newk = groot->k - level;

    // first, make sure the parent A block is split
    chunk_item_t *ci = request_chunk(groot, newk);
    ASSERT_EQ(groot->free_list->flags, CI_A_SPLIT);

    dump_root(groot, 3);

    chunk_item_t *old = ci;

    // adjacent nibbles, i.e. ci's A and B
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);
    chunk_item_t *adjchunk = merge_nibbles(groot->smaller, &ci, &ci);
    ASSERT_TRUE(adjchunk != NULL);
    ASSERT_TRUE(ci == NULL);
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);

    ASSERT_EQ(groot->smaller->free_list->flags, 0);

    dump_root(groot, 3);

    ci = request_chunk(groot, newk);

    dump_root(groot, 3);

    ci_set_flag(ci, CI_A_USED);
    ci_set_flag(ci, CI_B_USED);
    chunk_item_t *ci2 = request_chunk(groot, newk);
    ASSERT_NE(ci, ci2);

    // a = used, A = free
    // a | B - A | b == can merge nibbles B and A
    ci->flags = 0;
    ci2->flags = 0;

    dump_root(groot, 3);

    int s = chunk_size(groot->smaller->smaller);
    ASSERT_EQ((uint8_t *)ci->b+s, (uint8_t *)ci2->a);

    void *cib = ci->b;
    void *ci2a = ci2->a;
    // ci's B and ci2's A
    // a B | A b
    chunk_item_t *nc = merge_nibbles(groot->smaller, &ci, &ci2);
    ASSERT_TRUE(nc != NULL);
    ASSERT_EQ(nc->a, cib);

    dump_root(groot, 3);

    // mark the rest of those two chunks as used
    // there'll be a new chunk from the parent chunk we just created,
    // with the nibble addresses of the old chunks.
    ci_set_flag(ci, CI_A_USED); 
    ci_set_flag(ci2, CI_B_USED);
    chunk_item_t *c2 = request_chunk(groot, newk);

    dump_root(groot, 3);

    //fprintf(stderr, "(nc=%p, %p %p from %p : %p) c2 %p: %p %p\n", nc, nc->a, nc->b, cib, ci2a, c2, c2->a, c2->b);
    ASSERT_EQ(c2->a, cib);
    ASSERT_EQ(c2->b, ci2a);
}

// remove chunks that no longer point to any children, i.e. all child chunk nibbles = NULL
TEST_F(InternalAllocTest, BlockCleanup) {
    SetupMemory();
    dump_root(groot, 3);
/*
/===================
K 12 = {0x8da6260  4: 0xa6533008 /      (nil)}
K 11 = {0x8da6290  c: 0xa6533008 / 0xa6d33008} {0x8da6488  4: 0xa6933008 /      (nil)}
K 10 = {0x8da62c0  1: 0xa6533008 /      (nil)} {0x8da6470  2:      (nil) / 0xa7133008} {0x8da64a0  0: 0xa6933008 / 0xa6d33008}
===================
*/
    int count_10 = chunk_count(groot->smaller->smaller->free_list);
    clean_unused_chunks(groot->smaller->smaller);
    ASSERT_EQ(count_10, chunk_count(groot->smaller->smaller->free_list));

    int count_11 = chunk_count(groot->smaller->free_list);
    clean_unused_chunks(groot->smaller);

    ASSERT_TRUE(groot->smaller->free_list->next == NULL);
    ASSERT_EQ(count_11-1, chunk_count(groot->smaller->free_list));
}

/* merge all chunks for k and k-1
 */
#if 0
TEST_F(AllocTest, MergeTwoLevel) {
    int level = 2;
    
    int newk = groot->k - level;

    // first, make sure the parent A block is split
    chunk_item_t *ci = request_chunk(groot, newk);
    ASSERT_EQ(groot->free_list->flags, CI_A_SPLIT);

    chunk_item_t *old = ci;

    // adjacent nibbles, i.e. ci's A and B
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);
    chunk_item_t *adjchunk = merge_nibbles(groot->smaller, &ci, &ci);
    ASSERT_TRUE(adjchunk != NULL);
    ASSERT_TRUE(ci == NULL);
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);

    ASSERT_EQ(groot->smaller->free_list->flags, 0);

    // for the next chunk to have another address
    void *throwaway = malloc(400);

    ci = request_chunk(groot, newk);
    //fprintf(stderr, "%s:%d ci = %p\n", __FILE__, __LINE__, ci);
    ASSERT_NE(old, ci);

    ci_set_flag(ci, CI_A_USED);
    ci_set_flag(ci, CI_B_USED);
    chunk_item_t *ci2 = request_chunk(groot, newk);
    ASSERT_NE(ci, ci2);

    // a = used, A = free
    // a | B - A | b == can merge nibbles B and A
    ci->flags = 0;
    ci2->flags = 0;

    int s = chunk_size(groot->smaller->smaller);
    ASSERT_EQ((uint8_t *)ci->b+s, (uint8_t *)ci2->a);

    void *cib = ci->b;
    // ci's B and ci2's A
    chunk_item_t *nc = merge_nibbles(groot->smaller, &ci, &ci2);
    ASSERT_TRUE(nc != NULL);
    ASSERT_EQ(nc->a, cib);
}
#endif

TEST_F(AllocTest, Destroy) {
    //EXPECT_TRUE(false);
}


TEST_F(AllocTest, UserAllocFixedSize) {
    void *ptr;
    uint32_t size = KB(512);
    void *c1a = balloc(size);
    void *c1b = balloc(size);
    ASSERT_TRUE(c1a != NULL);
    ASSERT_TRUE(c1b != NULL);
    ASSERT_EQ((uint8_t *)c1a+size, c1b);

    dump_root(groot, 6);

    void *c2a = balloc(size);
    void *c2b = balloc(size);
    ASSERT_TRUE(c2a != NULL);
    ASSERT_TRUE(c2b != NULL);
    ASSERT_EQ((uint8_t *)c1b+size, c2a);
    ASSERT_EQ((uint8_t *)c2a+size, c2b);

    // we can allocate 16*2=32 512kb blocks. Let's try it!
    for (int i=0; i<up2(heap_size)/size-2*2; i++) {
        ptr = balloc(size);
        ASSERT_TRUE(ptr != NULL);
    }

    ptr = balloc(size);
    ASSERT_TRUE(ptr == NULL);
}

// allocate one nibble for each k will allocate allocate all memory except the
// last chunk size
TEST_F(AllocTest, UserAllocOneInEach) {
    void *ptr;
    uint32_t size = up2(heap_size)>>1; // start with level 11
    void *firstptr = NULL;
    bool first = false;

    while (size >= MIN_CHUNK_SIZE) {
        ptr = balloc(size);
        ASSERT_TRUE(ptr != NULL);
        if (!first) {
            first = true;
            firstptr = ptr;
        }
        size >>= 1;
    }
    // last block starts at the start of the first 4096 block. there are room
    // for a total of two more blocks => last pointer at -2*4096 from end
    ASSERT_EQ((uint8_t *)firstptr+up2(heap_size)-MIN_CHUNK_SIZE*2, ptr);
    ptr = balloc(size);
    ASSERT_TRUE(ptr != NULL);
    ptr = balloc(size);
    ASSERT_TRUE(ptr == NULL);
}

TEST_F(AllocTest, UserAllocAll) {
    // allocate only the first three or so to make it easier to see the chunks
    // and info blocks.
    int maxsize = up2(heap_size);
    int n = maxsize/MIN_CHUNK_SIZE;
    void **pointers = (void **)malloc(n);
    info_item_t *root = groot;

    uint32_t size = pow(2, 12)*MIN_CHUNK_SIZE;
    pointers[0] = balloc(size);

    //dump_root(groot, 1);
}

TEST_F(AllocTest, UserAllocAll16384) {
    int i, k;
    int maxsize = up2(heap_size);
    int n = maxsize/MIN_CHUNK_SIZE;
    void **pointers = (void **)malloc(n);
    info_item_t *root = groot;

    uint32_t size = 16384;
    int count = maxsize/size;
    for (i=0; i<count; i++) {
        pointers[i] = balloc(size);
        ASSERT_TRUE(pointers[i] != NULL);
    }
    //dump_root(groot, 12-k+1);
    while (i >= 0)
        bfree(pointers[i--]);
}

TEST_F(AllocTest, UserAllocAll8192) {
    int i, k;
    int maxsize = up2(heap_size);
    int n = maxsize/MIN_CHUNK_SIZE;
    void **pointers = (void **)malloc(n * sizeof(void *));
    info_item_t *root = groot;

    uint32_t size = 8192;
    int count = maxsize/size;
    for (i=0; i<count; i++) {
        if (i == 1026)
            dump_root(groot, 12);
        pointers[i] = balloc(size);
        ASSERT_TRUE(pointers[i] != NULL);
    }
    //dump_root(groot, 12-k+1);
    while (i >= 0)
        bfree(pointers[i--]);
}
TEST_F(AllocTest, UserAllocAllInEach) {
    int maxsize = up2(heap_size);
    int n = maxsize/MIN_CHUNK_SIZE;
    void **pointers = (void **)malloc(n*sizeof(void *));
    info_item_t *root = groot;

    for (int k=12, i; k>=0; k--) {
        uint32_t size = pow(2, k)*MIN_CHUNK_SIZE;
        int count = maxsize/size;
        for (i=0; i<count; i++) {
            pointers[i] = balloc(size);
            ASSERT_TRUE(pointers[i] != NULL);
        }
        void *p = balloc(size);
        ASSERT_TRUE(p == NULL);
        i--;
        while (i >= 0)
            bfree(pointers[i--]);
        dump_root(groot, 12);
    }
}

TEST_F(AllocTest, UserRandomAlloc) {
    uint32_t max_size = up2(heap_size);
    int32_t bytes_left = max_size;
    uint32_t spill = 0;
    int all[4096];

    int allocs=0;
    const int threshold = MIN_CHUNK_SIZE;//MIN_CHUNK_SIZE*2-1;
    while (bytes_left >= threshold) {
        uint32_t size = rand()%(max_size>>1);
        int32_t bytes = up2(size);
        if (size < MIN_CHUNK_SIZE)
            bytes = MIN_CHUNK_SIZE;
        if (bytes_left - bytes >= 0) {
            void *p = balloc(size);
            dump_root(groot, 12);
            if (!p) {
#ifdef DEBUGLOGGING
                fprintf(stderr, "alloc failed. spill = %lu bytes (%lu kbytes)\n", spill, spill/1024);
                for (int i=0; i<allocs; i++)
                    fprintf(stderr, "%d (%d); ", all[i], up2(all[i]));
                fprintf(stderr, "\n");
#endif
            }
            else
            {
                bytes_left -= bytes;
                all[allocs] = size;
            }
            ASSERT_TRUE(p != NULL);
            allocs++;
            spill += bytes - size;
        }
    }
#ifdef DEBUGLOGGING
    fprintf(stderr, "spill = %lu bytes (%lu kbytes)\n", spill, spill/1024);
    for (int i=0; i<allocs; i++)
        fprintf(stderr, "%d (%d); ", all[i], up2(all[i]));
    fprintf(stderr, "\n");
#endif
}

TEST_F(AllocTest, UserRandomAllocMax512K) {
    uint32_t max_size = up2(heap_size);
    int32_t bytes_left = max_size;
    uint32_t spill = 0;
    int all[4096];

    int allocs=0;
    const int threshold = MIN_CHUNK_SIZE;//MIN_CHUNK_SIZE*2-1;
    while (bytes_left >= threshold) {
        // max alloc block size = 512K = 16/32
        uint32_t size = rand()%(max_size>>5);
        int32_t bytes = up2(size);
        if (size < MIN_CHUNK_SIZE)
            bytes = MIN_CHUNK_SIZE;
        if (bytes_left - bytes >= 0) {
            void *p = balloc(size);
            if (!p) {
#ifdef DEBUGLOGGING
                fprintf(stderr, "alloc failed. spill = %lu bytes (%lu kbytes)\n", spill, spill/1024);
                for (int i=0; i<allocs; i++)
                    fprintf(stderr, "%d (%d); ", all[i], up2(all[i]));
                fprintf(stderr, "\n");
#endif
            }
            else
            {
                bytes_left -= bytes;
                all[allocs] = size;
            }
            ASSERT_TRUE(p != NULL);
            allocs++;
            spill += bytes - size;
        }
    }
#ifdef DEBUGLOGGING
    fprintf(stderr, "spill = %lu bytes (%lu kbytes)\n", spill, spill/1024);
    for (int i=0; i<allocs; i++)
        fprintf(stderr, "%d (%d); ", all[i], up2(all[i]));
    fprintf(stderr, "\n");
#endif
    dump_root(groot, 12);
}

TEST_F(AllocTest, UserRandomAllocThenFree) {
    // copy'n'paste from above
    uint32_t max_size = up2(heap_size);
    int32_t bytes_left = max_size;
    uint32_t spill = 0;
    void *all[4096];

    int allocs=0;
    const int threshold = MIN_CHUNK_SIZE;
    while (bytes_left >= threshold) {
        // max allocatable block size = 16/3 = 512 kb
        uint32_t size = rand()%(max_size>>5);
        int32_t bytes = up2(size);
        if (size < MIN_CHUNK_SIZE)
            bytes = MIN_CHUNK_SIZE;
        if (bytes_left - bytes >= 0) {
            void *p = balloc(size);
            if (!p) {
                break;
            }
            else
            {
                all[allocs] = p;
                bytes_left -= bytes;
            }
            ASSERT_TRUE(p != NULL);
            allocs++;
            spill += bytes - size;
        }
    }

    // go through the list and count used nibbles
    info_item_t *root = groot;
    int total_nibbles = 0;
    while (root) {
        chunk_item_t *c = root->free_list;
        while (c) {
            // free nibble
            if (c->flags & CI_A_USED) total_nibbles++;
            if (c->flags & CI_B_USED) total_nibbles++;

            c = c->next;
        }

        root = root->smaller;
    }

    LOG("bytes left after alloc %lu\n", bytes_left);
    dump_root(groot, 12);

    // go through the list and count free nibbles
    root = groot;
    int free_nibbles = 0;
    while (root) {
        chunk_item_t *c = root->free_list;
        while (c) {
            if (ci_a_free(c)) free_nibbles++;
            if (ci_b_free(c)) free_nibbles++;

            c = c->next;
        }

        root = root->smaller;
    }

    ASSERT_EQ(free_nibbles, 0);

    // free half
    int frees = 0;
    while (frees < allocs/2) {
        int which = rand()%allocs;
        if (all[which]) {
            void *p = all[which];
            bfree(all[which]);
            all[which] = NULL;
            frees++;

            // go through the list and count free nibbles
            root = groot;
            free_nibbles = 0;
            while (root) {
                chunk_item_t *c = root->free_list;
                while (c) {
                    if (ci_a_free(c)) free_nibbles++;
                    if (ci_b_free(c)) free_nibbles++;

                    c = c->next;
                }

                root = root->smaller;
            }

            fprintf(stderr, "after free pos %d = %p, frees: %d, free nibbles %d\n", which, p, frees, free_nibbles);

            if (free_nibbles != frees)
                dump_root(groot, 12);

            ASSERT_EQ(free_nibbles, frees);

        }
    }

    // go through the list and count free nibbles
    root = groot;
    free_nibbles = 0;
    while (root) {
        chunk_item_t *c = root->free_list;
        while (c) {
            if (ci_a_free(c)) free_nibbles++;
            if (ci_b_free(c)) free_nibbles++;

            c = c->next;
        }

        root = root->smaller;
    }

    int total_nibbles_adjusted = total_nibbles % 2 ? total_nibbles-1 : total_nibbles;
    fprintf(stderr, "total nibbles %d, adjusted %d, free nibbles %d, frees = %d\n", total_nibbles, total_nibbles_adjusted, free_nibbles, frees);
    dump_root(groot, 12);

    ASSERT_EQ(frees, free_nibbles);

    LOG("after free 50%%\n");

    ASSERT_EQ(free_nibbles, total_nibbles_adjusted/2);

    // free other half
    for (int i=0; i<allocs; i++) {
        if (all[i]) {
            bfree(all[i]);
            all[i] == NULL;
        }
    }

    LOG("after free rest\n");
    dump_root(groot, 12);

    root = groot;
    while (root) {
        chunk_item_t *c = root->free_list;
        while (c) {
            ASSERT_FALSE(c->flags & CI_A_USED);
            ASSERT_FALSE(c->flags & CI_B_USED);
            
            c = c->next;
        }

        root = root->smaller;
    }
}

TEST_F(AllocTest, UserLock) {
    memory_handle_t *handle;

    handle = Balloc(8192);
    void *p = lock(handle);
    ASSERT_TRUE(p != NULL);
    bool status = unlock(handle);
    ASSERT_TRUE(status);

    p = lock(handle);
    p = lock(handle);
    ASSERT_TRUE(p == NULL);
    status = unlock(handle);
    ASSERT_TRUE(status);

    status = unlock(handle);
    ASSERT_FALSE(status);
}

TEST_F(AllocTest, UserFragmentedAllocFail) {
    const int max_allocs = 4096;
    memory_handle_t *allocs[max_allocs];

    int count = 0;
    for (count=0; count<max_allocs; count++) {
        allocs[count] = Balloc(MIN_CHUNK_SIZE); 
        ASSERT_TRUE(allocs[count] != NULL);
    }
    dump_root(groot, 12);
    for (count=0; count<max_allocs; count += 2) {
        Bfree(allocs[count]->memory);
    }
    dump_root(groot, 12);

    memory_handle_t *h = Balloc(MIN_CHUNK_SIZE*2);
    ASSERT_TRUE(h == NULL);
}

TEST_F(AllocTest, InternalLargestBlockFree) {
}

TEST_F(AllocTest, UserFragmentedAllocSuccess) {
    const int max_allocs = 4096;
    memory_handle_t *allocs[max_allocs];

    int count = 0;
    for (count=0; count<max_allocs; count++) {
        allocs[count] = Balloc(MIN_CHUNK_SIZE); 
        ASSERT_TRUE(allocs[count] != NULL);
    }
    //dump_root(groot, 12);

    for (count=1; count<max_allocs; count+=2) {
        Bfree(allocs[count]->memory);
    }

    //bcompact(1);

    memory_handle_t *h = Balloc(MIN_CHUNK_SIZE*2);
    if (h) 
        fprintf(stderr, "Test: allocated chunk of %d = %p at memory %p\n", MIN_CHUNK_SIZE*2, h->chunk, h->memory);
    ASSERT_TRUE(h != NULL && h->chunk != NULL);

    memory_handle_t *h2 = Balloc(MIN_CHUNK_SIZE*2);
    if (h2) {
        fprintf(stderr, "Test: allocated chunk that should be NULL: %p at memory %p\n", h2->chunk, h2->memory);
    }
    ASSERT_TRUE(h2 == NULL);

    bcompact(1);

    memory_handle_t *h3 = Balloc(MIN_CHUNK_SIZE*2);
    ASSERT_TRUE(h3 != NULL && h3->chunk != NULL);
}

TEST_F(AllocTest, UserFragmentedAllocHeavy) {
    const int max_allocs = 4096;
    memory_handle_t *allocs[max_allocs];

    int count = 0;
    for (count=0; count<max_allocs; count++) {
        allocs[count] = Balloc(MIN_CHUNK_SIZE); 
        ASSERT_TRUE(allocs[count] != NULL);
    }

    for (count=1; count<max_allocs; count+=2) {
        Bfree(allocs[count]->memory);
    }

    dump_root(groot, 12);

    bcompact(max_allocs>>1);

    dump_root(groot, 12);
}
#endif
