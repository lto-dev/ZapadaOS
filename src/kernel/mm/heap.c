/*
 * Zapada - src/kernel/mm/heap.c
 *
 * Segmented free-list kernel heap.
 *
 * DESIGN
 * ------
 * Every allocation carries a 16-byte boundary-tag header (block_size).
 * When a block is free its first 8 user-area bytes store a singly-linked
 * "next" pointer — no separate node struct is required and no minimum
 * allocation size is imposed beyond the block alignment.
 *
 * The free list is divided into N_FAST_BUCKETS exact-size chains covering
 * payloads of 1..N_FAST_BUCKETS blocks (16..1024 bytes user data) and one
 * overflow chain for larger blocks.
 *
 *   Alloc (small, n_blocks <= N_FAST_BUCKETS):
 *     pop from g_free_buckets[n_blocks - 1]       O(1)
 *     or extend bump pointer                       O(1)
 *
 *   Alloc (large):
 *     first-fit scan of g_overflow_list            O(k) free large blocks
 *     or extend bump pointer                       O(1)
 *
 *   Free (any size):
 *     push to the appropriate bucket or overflow   O(1)
 *     reclaim bump pointer if block is at the top  O(1)
 *
 * A running g_free_bytes counter makes kheap_get_free_bytes O(1).
 *
 * MOTIVATION
 * ----------
 * The original bump-only allocator had a no-op kheap_free and was
 * deliberately designed for early-kernel "allocate-once" use.  Once the
 * ZACLR interpreter began creating and destroying thousands of frames per
 * execution the bump heap was exhausted.  The segmented free list
 * eliminates that class of failure: freed frame memory is immediately
 * recycled by the next frame push.  This mirrors the approach taken by
 * nf-interpreter's CLR_RT_EventCache (c_maxFastLists exact-size chains
 * feeding a GC-backed slow path).
 *
 * BLOCK LAYOUT
 * ------------
 *   +------------------+-----------------------------+
 *   |  header (16 B)   |  user data (aligned size)   |
 *   |  block_size      |  [next ptr when free]        |
 *   +------------------+-----------------------------+
 *
 * block_size = KHEAP_HEADER_SIZE + aligned_user_size, always a multiple
 * of KHEAP_BLOCK_ALIGN.  The minimum total block size is 32 B (16 header
 * + 16 user), which is large enough to hold a next pointer (8 B) in the
 * user area of a freed block.
 */

#include <kernel/mm/heap.h>
#include <kernel/mm/mm_defs.h>

/* ---------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------------*/

#define KHEAP_PAGE_ALIGN   4096u
#define KHEAP_BLOCK_ALIGN  16u
#define KHEAP_HEADER_SIZE  16u   /* sizeof(kheap_block_hdr_t) on all targets  */
#define KHEAP_MIN_USER     16u   /* minimum user payload (room for next ptr)  */

/*
 * N_FAST_BUCKETS — number of exact-size free-list chains.
 * Bucket i holds blocks whose user payload is exactly (i+1) * KHEAP_BLOCK_ALIGN
 * bytes, covering 16..1024 bytes.  All larger blocks go to g_overflow_list.
 */
#define N_FAST_BUCKETS     64u

/* ---------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------------*/

/*
 * kheap_block_hdr_t - 16-byte boundary tag placed before every block.
 * block_size includes the header itself and is always a multiple of
 * KHEAP_BLOCK_ALIGN.  Layout is identical on 32-bit and 64-bit targets.
 *
 *   uint32_t block_size  offset 0, size 4
 *   uint32_t _pad[3]     offset 4, size 12
 *   total                         16 bytes
 */
typedef struct kheap_block_hdr {
    uint32_t block_size;
    uint32_t _pad[3];
} kheap_block_hdr_t;

/*
 * When a block is on the free list the first 8 bytes of its user area
 * hold a pointer to the next free block in the same bucket (NULL = end).
 * We cast through a char* to avoid strict-aliasing concerns.
 */
#define FREE_NEXT_PTR(user_ptr) (*(void **)(user_ptr))

/*
 * Zero n_blocks * KHEAP_BLOCK_ALIGN bytes of the user area pointed to by
 * user_ptr.  Called before returning any recycled block from the free list
 * to restore the zeroed-page invariant that the bump allocator provided
 * incidentally (fresh pages in QEMU are always zero, but recycled blocks
 * contain the stale embedded next pointer written by bucket_push).
 *
 * Operates on pointer-sized words for efficiency.  Because KHEAP_BLOCK_ALIGN
 * is a multiple of sizeof(uintptr_t) the division is always exact.
 */
static void block_zero_user(void *user_ptr, uint32_t n_blocks)
{
    uintptr_t *p      = (uintptr_t *)user_ptr;
    uint32_t   n_words = (n_blocks * KHEAP_BLOCK_ALIGN) / (uint32_t)sizeof(uintptr_t);
    uint32_t   i;

    for (i = 0u; i < n_words; ++i) {
        p[i] = 0u;
    }
}

/* ---------------------------------------------------------------------------
 * Allocator state
 * ---------------------------------------------------------------------------*/

static uintptr_t g_heap_alloc_start;  /* first block header; 0 until first alloc */
static uintptr_t g_heap_current;      /* bump pointer (end of last carved block)  */
static uintptr_t g_heap_end;          /* one byte past the last usable byte       */
static uintptr_t g_heap_min_start;    /* floor from kheap_reserve_until pre-init  */
static size_t    g_heap_size = EARLY_HEAP_SIZE;
static size_t    g_free_bytes;        /* running total of free user-visible bytes */

static void *g_free_buckets[N_FAST_BUCKETS]; /* index = (n_blocks - 1)           */
static void *g_overflow_list;                 /* large blocks (> N_FAST_BUCKETS)  */

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------------*/

static uintptr_t heap_align_up(uintptr_t v, uintptr_t a)
{
    return (v + a - 1u) & ~(a - 1u);
}

/* Return the number of KHEAP_BLOCK_ALIGN-sized blocks needed for user_size
 * bytes of user payload (always at least 1). */
static uint32_t blocks_for_size(size_t user_size)
{
    size_t aligned = heap_align_up((uintptr_t)user_size, KHEAP_BLOCK_ALIGN);
    if (aligned < KHEAP_MIN_USER) {
        aligned = KHEAP_MIN_USER;
    }
    return (uint32_t)(aligned / KHEAP_BLOCK_ALIGN);
}

/* Push a free block (identified by its user pointer) into the appropriate
 * bucket or the overflow list, and update g_free_bytes. */
static void bucket_push(void *user_ptr, uint32_t n_blocks)
{
    uint32_t user_bytes = n_blocks * KHEAP_BLOCK_ALIGN;

    if (n_blocks > 0u && n_blocks <= N_FAST_BUCKETS) {
        uint32_t idx = n_blocks - 1u;
        FREE_NEXT_PTR(user_ptr) = g_free_buckets[idx];
        g_free_buckets[idx]     = user_ptr;
    } else {
        FREE_NEXT_PTR(user_ptr) = g_overflow_list;
        g_overflow_list         = user_ptr;
    }

    g_free_bytes += (size_t)user_bytes;
}

/* Pop an exact-size block from the fast bucket.  Returns the user pointer
 * or NULL if the bucket is empty. */
static void *bucket_pop_fast(uint32_t n_blocks)
{
    uint32_t idx;
    void    *node;

    if (n_blocks == 0u || n_blocks > N_FAST_BUCKETS) {
        return NULL;
    }

    idx  = n_blocks - 1u;
    node = g_free_buckets[idx];
    if (node == NULL) {
        return NULL;
    }

    g_free_buckets[idx] = FREE_NEXT_PTR(node);
    g_free_bytes       -= (size_t)(n_blocks * KHEAP_BLOCK_ALIGN);
    return node;
}

/* Pop a block from the overflow list that is >= n_blocks.  Splits the tail
 * if the found block is significantly larger (> 25 % overhead threshold).
 * Returns the user pointer or NULL if nothing fits. */
static void *bucket_pop_overflow(uint32_t n_blocks)
{
    void    **prev_ptr;
    void     *cur;
    void     *best        = NULL;
    void    **best_prev   = NULL;
    uint32_t  best_blocks = 0u;
    kheap_block_hdr_t *hdr;
    uint32_t  cur_blocks;

    /* Best-fit search (accept up to 25 % waste, prefer exact match). */
    prev_ptr = &g_overflow_list;
    cur      = g_overflow_list;

    while (cur != NULL) {
        hdr        = (kheap_block_hdr_t *)((uintptr_t)cur - KHEAP_HEADER_SIZE);
        cur_blocks = (hdr->block_size - KHEAP_HEADER_SIZE) / KHEAP_BLOCK_ALIGN;

        if (cur_blocks == n_blocks) {
            best      = cur;
            best_prev = prev_ptr;
            best_blocks = cur_blocks;
            break; /* exact match: stop immediately */
        }

        if (cur_blocks > n_blocks) {
            /* Accept if within 25 % overhead or smaller than best so far. */
            if (best == NULL || cur_blocks < best_blocks) {
                if (cur_blocks <= (n_blocks * 20u) / 16u) {
                    best        = cur;
                    best_prev   = prev_ptr;
                    best_blocks = cur_blocks;
                }
            }
        }

        prev_ptr = (void **)cur;
        cur      = FREE_NEXT_PTR(cur);
    }

    if (best == NULL) {
        return NULL;
    }

    /* Unlink from overflow list. */
    *best_prev = FREE_NEXT_PTR(best);
    g_free_bytes -= (size_t)(best_blocks * KHEAP_BLOCK_ALIGN);

    /* Split if the block is large enough to hold the carved allocation AND a
     * valid tail block (tail header + at least one user block).
     *
     * Block layout (all slots are KHEAP_BLOCK_ALIGN == KHEAP_HEADER_SIZE bytes):
     *
     *   [carved_hdr][carved_user * n_blocks][tail_hdr][tail_user * tail_blocks]
     *
     * The tail header consumes exactly one KHEAP_BLOCK_ALIGN slot inside the
     * original user region.  Therefore:
     *   tail_blocks = best_blocks - n_blocks - 1   (one slot taken by tail_hdr)
     *
     * The split is only valid when tail_blocks >= 1, i.e. best_blocks >= n_blocks + 2.
     * When best_blocks == n_blocks + 1 there is only room for the tail header with
     * no user data; skip the split and return the block one slot larger than needed.
     */
    if (best_blocks >= n_blocks + 2u) {
        uint32_t           tail_blocks   = best_blocks - n_blocks - 1u;
        uintptr_t          tail_hdr_addr = (uintptr_t)best
                                           + (uintptr_t)n_blocks * KHEAP_BLOCK_ALIGN;
        kheap_block_hdr_t *tail_hdr = (kheap_block_hdr_t *)tail_hdr_addr;
        void              *tail_usr = (void *)(tail_hdr_addr + KHEAP_HEADER_SIZE);

        tail_hdr->block_size = KHEAP_HEADER_SIZE + tail_blocks * KHEAP_BLOCK_ALIGN;
        tail_hdr->_pad[0]    = 0u;
        tail_hdr->_pad[1]    = 0u;
        tail_hdr->_pad[2]    = 0u;

        /* Update the carved block's header to reflect the trimmed size. */
        hdr = (kheap_block_hdr_t *)((uintptr_t)best - KHEAP_HEADER_SIZE);
        hdr->block_size = KHEAP_HEADER_SIZE + n_blocks * KHEAP_BLOCK_ALIGN;

        /* Return the tail to the pool. */
        bucket_push(tail_usr, tail_blocks);
    }

    return best;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

void kheap_set_size(size_t heap_size)
{
    if (g_heap_current != 0u || heap_size == 0u) {
        return;
    }
    g_heap_size = heap_size;
}

void kheap_init(void)
{
    uintptr_t start;

    start = heap_align_up((uintptr_t)&kernel_end, KHEAP_PAGE_ALIGN);
    if (g_heap_min_start > start) {
        start = heap_align_up(g_heap_min_start, KHEAP_PAGE_ALIGN);
    }

    g_heap_alloc_start = 0u;
    g_heap_current     = start;
    g_heap_end         = start + (uintptr_t)g_heap_size;
    g_free_bytes       = 0u;
}

void kheap_reserve_until(uintptr_t min_start)
{
    uintptr_t aligned;

    if (g_heap_current == 0u) {
        if (min_start > g_heap_min_start) {
            g_heap_min_start = min_start;
        }
        return;
    }

    /*
     * Only valid before the first allocation; after g_heap_alloc_start is
     * set, the block list begins there and advancing g_heap_current would
     * create a gap with no header, corrupting iteration.
     */
    if (g_heap_alloc_start != 0u) {
        return;
    }

    if (min_start <= g_heap_current) {
        return;
    }

    aligned = heap_align_up(min_start, KHEAP_BLOCK_ALIGN);
    if (aligned > g_heap_end) {
        g_heap_current = g_heap_end;
        return;
    }

    g_heap_current = aligned;
}

void *kheap_alloc(size_t size)
{
    uint32_t   n_blocks;
    uint32_t   total_size;
    void      *user_ptr;
    uintptr_t  block_start;
    uintptr_t  block_end;
    kheap_block_hdr_t *hdr;

    if (size == 0u || g_heap_current == 0u) {
        return NULL;
    }

    n_blocks   = blocks_for_size(size);
    total_size = KHEAP_HEADER_SIZE + n_blocks * KHEAP_BLOCK_ALIGN;

    /* Pin alloc_start on the first call (after all reserve calls). */
    if (g_heap_alloc_start == 0u) {
        g_heap_alloc_start = heap_align_up(g_heap_current, KHEAP_BLOCK_ALIGN);
        g_heap_current     = g_heap_alloc_start;
    }

    /* Fast path: exact-size bucket. */
    user_ptr = bucket_pop_fast(n_blocks);
    if (user_ptr != NULL) {
        /*
         * Clear the stale embedded next pointer (and any other stale data) so
         * that callers which expect zero-initialised memory from the allocator
         * are not surprised.  The bump path returns memory from freshly-zero'd
         * pages, so only the recycled paths need this treatment.
         */
        block_zero_user(user_ptr, n_blocks);
        return user_ptr;
    }

    /* Slow path: overflow list (large or fragmented). */
    if (n_blocks > N_FAST_BUCKETS) {
        user_ptr = bucket_pop_overflow(n_blocks);
        if (user_ptr != NULL) {
            block_zero_user(user_ptr, n_blocks);
            return user_ptr;
        }
    }

    /* Fallback: carve a new block from the bump pointer. */
    block_start = heap_align_up(g_heap_current, KHEAP_BLOCK_ALIGN);
    block_end   = block_start + (uintptr_t)total_size;

    if (block_end > g_heap_end || block_end < block_start) {
        return NULL; /* out of heap space */
    }

    hdr             = (kheap_block_hdr_t *)block_start;
    hdr->block_size = total_size;
    hdr->_pad[0]    = 0u;
    hdr->_pad[1]    = 0u;
    hdr->_pad[2]    = 0u;
    g_heap_current  = block_end;

    return (void *)(block_start + KHEAP_HEADER_SIZE);
}

void kheap_free(void *ptr)
{
    kheap_block_hdr_t *hdr;
    uint32_t           n_blocks;

    if (ptr == NULL || g_heap_alloc_start == 0u) {
        return;
    }

    hdr = (kheap_block_hdr_t *)((uintptr_t)ptr - KHEAP_HEADER_SIZE);

    /* Sanity: header must lie within the allocated region. */
    if ((uintptr_t)hdr < g_heap_alloc_start || (uintptr_t)hdr >= g_heap_current) {
        return;
    }

    n_blocks = (hdr->block_size - KHEAP_HEADER_SIZE) / KHEAP_BLOCK_ALIGN;

    /*
     * Top-of-heap reclaim: if this block is the last one carved from the
     * bump pointer, simply wind the pointer back.  This keeps the bump
     * region compact for LIFO-pattern callers (e.g., interpreter frames).
     */
    if ((uintptr_t)hdr + (uintptr_t)hdr->block_size >= g_heap_current) {
        g_heap_current = (uintptr_t)hdr;
        return;
    }

    /* Otherwise push into the appropriate bucket for later reuse. */
    bucket_push(ptr, n_blocks);
}

size_t kheap_get_free_bytes(void)
{
    size_t bump_space;

    if (g_heap_end <= g_heap_current) {
        return g_free_bytes;
    }

    bump_space = (size_t)(g_heap_end - g_heap_current);
    return g_free_bytes + bump_space;
}
