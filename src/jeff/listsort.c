//#include <stdio.h>

/*
 * Demonstration code for sorting a linked list.
 * 
 * The algorithm used is Mergesort, because that works really well
 * on linked lists, without requiring the O(N) extra space it needs
 * when you do it on arrays.
 * 
 * This code can handle singly and doubly linked lists, and
 * circular and linear lists too. For any serious application,
 * you'll probably want to remove the conditionals on `is_circular'
 * and `is_double' to adapt the code to your own purpose. 
 * 
 */

/*
 * This file is copyright 2001 Simon Tatham.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compact_internal.h"


uintptr_t rm_header__cmp(void *a, void *b) {

    rm_header_t *x = (rm_header_t *)a;
    rm_header_t *y = (rm_header_t *)b;

#if 0 // TODO: FUTURE WORK
    // Does not work, don't know why.

    // merge blocks next to each other.
    if (x->flags == HEADER_FREE_BLOCK && y->flags == HEADER_FREE_BLOCK) {
        if ((unsigned char *)x->memory + x->size == y->memory) {
            y->memory = NULL;
            x->size += y->size;
            fprintf(stderr, "* sort: x->y: merging %p and %p\n", x, y);
        } else if ((unsigned char *)y->memory + y->size == x->memory) {
            x->memory = NULL;
            y->size += x->size;
            fprintf(stderr, "* sort: y->x: merging %p and %p\n", y, x);
        }
    }
#endif

    // Treat NULL as infinitely large for it to sink to the bottom.

    uintptr_t X = x->memory == NULL ? UINTPTR_MAX : (uintptr_t)x->memory;
    uintptr_t Y = y->memory == NULL ? UINTPTR_MAX : (uintptr_t)y->memory;

    if (X > Y)
        return 1;
    else
        return 0;
}


//element *listsort(element *list, int is_circular, int is_double) {
rm_header_t *rm_header__sort(rm_header_t *list, bool is_circular, bool is_double, compare_cb cmp) {
    rm_header_t *p, *q, *e, *tail, *oldhead;
    int insize, nmerges, psize, qsize, i;

    (void)is_double;

    /*
     * Silly special case: if `list' was passed in as NULL, return
     * NULL immediately.
     */
    if (!list)
        return NULL;

    insize = 1;

    while (1) {
        p = list;
        oldhead = list;		       /* only used for circular linkage */
        list = NULL;
        tail = NULL;

        nmerges = 0;  /* count number of merges we do in this pass */

        while (p) {
            nmerges++;  /* there exists a merge to be done */
            /* step `insize' places along from p */
            q = p;
            psize = 0;
            for (i = 0; i < insize; i++) {
                psize++;
                if (is_circular)
                    q = (q->next == oldhead ? NULL : q->next);
                else
                    q = q->next;
                if (!q) break;
            }

            /* if q hasn't fallen off end, we have two lists to merge */
            qsize = insize;

            /* now we have two lists; merge them */
            while (psize > 0 || (qsize > 0 && q)) {

                /* decide whether next element of merge comes from p or q */
                if (psize == 0) {
                    /* p is empty; e must come from q. */
                    e = q; q = q->next; qsize--;
                    if (is_circular && q == oldhead) q = NULL;
                } else if (qsize == 0 || !q) {
                    /* q is empty; e must come from p. */
                    e = p; p = p->next; psize--;
                    if (is_circular && p == oldhead) p = NULL;
                } else if (cmp(p,q) <= 0) {
                    /* First element of p is lower (or same);
                     * e must come from p. */
                    e = p; p = p->next; psize--;
                    if (is_circular && p == oldhead) p = NULL;
                } else {
                    /* First element of q is lower; e must come from q. */
                    e = q; q = q->next; qsize--;
                    if (is_circular && q == oldhead) q = NULL;
                }

                /* add the next element to the merged list */
                if (tail) {
                    tail->next = e;
                } else {
                    list = e;
                }
#if 0
                if (is_double) {
                    /* Maintain reverse pointers in a doubly linked list. */
                    e->prev = tail;
                }
#endif
                tail = e;
            }

            /* now p has stepped `insize' places along, and q has too */
            p = q;
        }
        if (is_circular) {
            tail->next = list;
#if 0
            if (is_double)
                list->prev = tail;
#endif
        } else
            tail->next = NULL;

        /* If we have done only one merge, we're finished. */
        if (nmerges <= 1)   /* allow for nmerges==0, the empty list case */
            return list;

        /* Otherwise repeat, merging lists twice the size */
        insize *= 2;
    }

    // make compiler happy
    return NULL;
}

