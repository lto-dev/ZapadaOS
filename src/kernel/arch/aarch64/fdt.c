#include <kernel/arch/aarch64/fdt.h>
#include <kernel/types.h>

/*
 * FDT structure tokens (big-endian, 4-byte aligned).
 * All fields in the FDT blob are big-endian.
 */
#define FDT_BEGIN_NODE  0x00000001u
#define FDT_END_NODE    0x00000002u
#define FDT_PROP        0x00000003u
#define FDT_NOP         0x00000004u
#define FDT_END         0x00000009u

/* FDT header layout (all uint32_t, big-endian). */
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

/* FDT property block header. */
struct fdt_prop_header {
    uint32_t len;
    uint32_t nameoff;
};

/* ------------------------------------------------------------------ */
/* Byte-swap helpers (FDT is big-endian, ARM64 is little-endian)       */
/* ------------------------------------------------------------------ */

static inline uint32_t fdt_be32(uint32_t v)
{
    return ((v & 0xFF000000u) >> 24)
         | ((v & 0x00FF0000u) >>  8)
         | ((v & 0x0000FF00u) <<  8)
         | ((v & 0x000000FFu) << 24);
}

static inline uint64_t fdt_be64(uint64_t v)
{
    return ((uint64_t)fdt_be32((uint32_t)(v >> 32)))
         | ((uint64_t)fdt_be32((uint32_t)(v & 0xFFFFFFFFu)) << 32);
}

/* ------------------------------------------------------------------ */
/* String comparison helper (no libc in freestanding environment)       */
/* ------------------------------------------------------------------ */

static int fdt_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/*
 * fdt_is_valid - returns 1 if fdt_base points to a blob with the FDT magic.
 */
int fdt_is_valid(uint64_t fdt_base)
{
    if (fdt_base == 0u) {
        return 0;
    }

    /* Require at least 4-byte alignment. */
    if (fdt_base & 3u) {
        return 0;
    }

    const struct fdt_header *hdr =
        (const struct fdt_header *)(uintptr_t)fdt_base;

    return (fdt_be32(hdr->magic) == (uint32_t)FDT_MAGIC) ? 1 : 0;
}

/*
 * fdt_get_memory - parse the FDT for the first /memory node and extract the
 * reg property (base address + size, encoded as two 64-bit big-endian cells).
 *
 * Assumes #address-cells = 2 and #size-cells = 2 (standard 64-bit layout).
 *
 * Returns 1 and fills *out_base / *out_size on success.
 * Returns 0 if no valid /memory node is found.
 */
int fdt_get_memory(uint64_t fdt_base, uint64_t *out_base, uint64_t *out_size)
{
    if (!fdt_is_valid(fdt_base) || !out_base || !out_size) {
        return 0;
    }

    const struct fdt_header *hdr =
        (const struct fdt_header *)(uintptr_t)fdt_base;

    uint32_t struct_off  = fdt_be32(hdr->off_dt_struct);
    uint32_t strings_off = fdt_be32(hdr->off_dt_strings);

    const char    *strings_blk = (const char *)(uintptr_t)(fdt_base + strings_off);
    const uint32_t *p           = (const uint32_t *)(uintptr_t)(fdt_base + struct_off);

    int  in_memory_node = 0;
    int  depth          = 0;
    int  memory_depth   = 0;

    for (;;) {
        uint32_t token = fdt_be32(*p);
        ++p;

        switch (token) {
        case FDT_BEGIN_NODE: {
            ++depth;
            /* Node name is a NUL-terminated string, padded to 4-byte align. */
            const char *name = (const char *)p;
            size_t len = 0;
            while (name[len]) { ++len; }
            /* Advance p past name (including NUL, rounded up to 4 bytes). */
            p = (const uint32_t *)(uintptr_t)
                (((uintptr_t)name + len + 1u + 3u) & ~3u);

            /*
             * The /memory node may be named "memory" or "memory@0" etc.
             * Check the base name up to the '@' character.
             */
            if (depth == 2) {
                /* Compare prefix up to '@' or NUL */
                const char *prefix = "memory";
                int match = 1;
                for (size_t i = 0; prefix[i]; ++i) {
                    if (name[i] != prefix[i]) {
                        match = 0;
                        break;
                    }
                }
                if (match && (name[6] == '\0' || name[6] == '@')) {
                    in_memory_node = 1;
                    memory_depth   = depth;
                }
            }
            break;
        }

        case FDT_END_NODE:
            if (in_memory_node && depth == memory_depth) {
                in_memory_node = 0;
            }
            --depth;
            if (depth < 0) {
                /* Malformed FDT */
                return 0;
            }
            break;

        case FDT_PROP: {
            const struct fdt_prop_header *phdr =
                (const struct fdt_prop_header *)p;
            uint32_t prop_len  = fdt_be32(phdr->len);
            uint32_t nameoff   = fdt_be32(phdr->nameoff);
            p += 2;  /* skip prop header (2 x uint32_t) */

            const char *prop_name = strings_blk + nameoff;
            const uint8_t *data   = (const uint8_t *)p;

            if (in_memory_node && fdt_strcmp(prop_name, "reg") == 0) {
                /*
                 * Expect 16 bytes: 8-byte base address + 8-byte size.
                 * (Standard 64-bit DT with #address-cells=2, #size-cells=2.)
                 */
                if (prop_len >= 16u) {
                    uint64_t base_be, size_be;
                    /* Manual 8-byte big-endian read (avoid unaligned access). */
                    uint32_t b0, b1, s0, s1;
                    const uint32_t *dw = (const uint32_t *)data;
                    b0 = fdt_be32(dw[0]);
                    b1 = fdt_be32(dw[1]);
                    s0 = fdt_be32(dw[2]);
                    s1 = fdt_be32(dw[3]);
                    base_be = ((uint64_t)b0 << 32) | b1;
                    size_be = ((uint64_t)s0 << 32) | s1;
                    *out_base = base_be;
                    *out_size = size_be;
                    return 1;
                }
            }

            /* Advance p past property data (4-byte aligned). */
            p = (const uint32_t *)(uintptr_t)
                (((uintptr_t)data + prop_len + 3u) & ~3u);
            break;
        }

        case FDT_NOP:
            /* Padding token; skip. */
            break;

        case FDT_END:
            return 0;

        default:
            /* Unknown token - FDT likely corrupt. */
            return 0;
        }
    }
}

int fdt_get_initrd(uint64_t fdt_base, uint64_t *out_start, uint64_t *out_end)
{
    const struct fdt_header *hdr;
    uint32_t struct_off;
    uint32_t strings_off;
    const char *strings_blk;
    const uint32_t *p;
    int in_chosen_node = 0;
    int depth = 0;
    int chosen_depth = 0;
    int have_start = 0;
    int have_end = 0;
    uint64_t initrd_start = 0u;
    uint64_t initrd_end = 0u;

    if (!fdt_is_valid(fdt_base) || !out_start || !out_end) {
        return 0;
    }

    hdr = (const struct fdt_header *)(uintptr_t)fdt_base;
    struct_off = fdt_be32(hdr->off_dt_struct);
    strings_off = fdt_be32(hdr->off_dt_strings);
    strings_blk = (const char *)(uintptr_t)(fdt_base + strings_off);
    p = (const uint32_t *)(uintptr_t)(fdt_base + struct_off);

    for (;;) {
        uint32_t token = fdt_be32(*p);
        ++p;

        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)p;
            size_t len = 0;
            while (name[len]) { ++len; }
            ++depth;
            p = (const uint32_t *)(uintptr_t)(((uintptr_t)name + len + 1u + 3u) & ~3u);

            if (depth == 2 && fdt_strcmp(name, "chosen") == 0) {
                in_chosen_node = 1;
                chosen_depth = depth;
            }
            break;
        }

        case FDT_END_NODE:
            if (in_chosen_node && depth == chosen_depth) {
                in_chosen_node = 0;
            }
            --depth;
            if (depth < 0) {
                return 0;
            }
            break;

        case FDT_PROP: {
            const struct fdt_prop_header *phdr = (const struct fdt_prop_header *)p;
            uint32_t prop_len = fdt_be32(phdr->len);
            uint32_t nameoff = fdt_be32(phdr->nameoff);
            const char *prop_name;
            const uint8_t *data;
            const uint32_t *dw;

            p += 2;
            prop_name = strings_blk + nameoff;
            data = (const uint8_t *)p;

            if (in_chosen_node && prop_len >= 4u) {
                dw = (const uint32_t *)data;

                if (fdt_strcmp(prop_name, "linux,initrd-start") == 0) {
                    if (prop_len >= 8u) {
                        initrd_start = ((uint64_t)fdt_be32(dw[0]) << 32) | (uint64_t)fdt_be32(dw[1]);
                    } else {
                        initrd_start = (uint64_t)fdt_be32(dw[0]);
                    }
                    have_start = 1;
                } else if (fdt_strcmp(prop_name, "linux,initrd-end") == 0) {
                    if (prop_len >= 8u) {
                        initrd_end = ((uint64_t)fdt_be32(dw[0]) << 32) | (uint64_t)fdt_be32(dw[1]);
                    } else {
                        initrd_end = (uint64_t)fdt_be32(dw[0]);
                    }
                    have_end = 1;
                }

                if (have_start && have_end && initrd_end > initrd_start) {
                    *out_start = initrd_start;
                    *out_end = initrd_end;
                    return 1;
                }
            }

            p = (const uint32_t *)(uintptr_t)(((uintptr_t)data + prop_len + 3u) & ~3u);
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            return 0;

        default:
            return 0;
        }
    }
}

const char *fdt_get_bootargs(uint64_t fdt_base)
{
    const struct fdt_header *hdr;
    uint32_t struct_off;
    uint32_t strings_off;
    const char *strings_blk;
    const uint32_t *p;
    int in_chosen_node = 0;
    int depth = 0;
    int chosen_depth = 0;

    if (!fdt_is_valid(fdt_base)) {
        return 0;
    }

    hdr = (const struct fdt_header *)(uintptr_t)fdt_base;
    struct_off = fdt_be32(hdr->off_dt_struct);
    strings_off = fdt_be32(hdr->off_dt_strings);
    strings_blk = (const char *)(uintptr_t)(fdt_base + strings_off);
    p = (const uint32_t *)(uintptr_t)(fdt_base + struct_off);

    for (;;) {
        uint32_t token = fdt_be32(*p);
        ++p;

        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)p;
            size_t len = 0;
            while (name[len]) { ++len; }
            ++depth;
            p = (const uint32_t *)(uintptr_t)(((uintptr_t)name + len + 1u + 3u) & ~3u);

            if (depth == 2 && fdt_strcmp(name, "chosen") == 0) {
                in_chosen_node = 1;
                chosen_depth = depth;
            }
            break;
        }

        case FDT_END_NODE:
            if (in_chosen_node && depth == chosen_depth) {
                in_chosen_node = 0;
            }
            --depth;
            if (depth < 0) {
                return 0;
            }
            break;

        case FDT_PROP: {
            const struct fdt_prop_header *phdr = (const struct fdt_prop_header *)p;
            uint32_t prop_len = fdt_be32(phdr->len);
            uint32_t nameoff = fdt_be32(phdr->nameoff);
            const char *prop_name;
            const uint8_t *data;

            p += 2;
            prop_name = strings_blk + nameoff;
            data = (const uint8_t *)p;

            if (in_chosen_node && prop_len > 0u && fdt_strcmp(prop_name, "bootargs") == 0) {
                return (const char *)data;
            }

            p = (const uint32_t *)(uintptr_t)(((uintptr_t)data + prop_len + 3u) & ~3u);
            break;
        }

        case FDT_NOP:
            break;

        case FDT_END:
            return 0;

        default:
            return 0;
        }
    }
}
