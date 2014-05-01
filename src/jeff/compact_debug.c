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

