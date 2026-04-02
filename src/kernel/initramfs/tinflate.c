/*
 * Zapada - src/kernel/initramfs/tinflate.c
 *
 * Kernel-safe DEFLATE/gzip decompressor derived from tinf.
 */

#include <kernel/initramfs/tinflate.h>

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
    int max_sym;
} tinf_tree_t;

typedef struct {
    const uint8_t *source;
    const uint8_t *source_end;
    uint32_t tag;
    int bitcount;
    int overflow;

    uint8_t *dest_start;
    uint8_t *dest;
    uint8_t *dest_end;

    tinf_tree_t ltree;
    tinf_tree_t dtree;
} tinf_data_t;

static uint32_t read_le16(const uint8_t *p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8u);
}

static uint32_t read_le32(const uint8_t *p)
{
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8u)
         | ((uint32_t)p[2] << 16u)
         | ((uint32_t)p[3] << 24u);
}

static const uint32_t tinf_crc32tab[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

static uint32_t tinf_crc32(const void *data, uint32_t length)
{
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;

    if (length == 0u) {
        return 0u;
    }

    for (i = 0u; i < length; ++i) {
        crc ^= buf[i];
        crc = tinf_crc32tab[crc & 0x0Fu] ^ (crc >> 4u);
        crc = tinf_crc32tab[crc & 0x0Fu] ^ (crc >> 4u);
    }

    return crc ^ 0xFFFFFFFFu;
}

static void tinf_build_fixed_trees(tinf_tree_t *lt, tinf_tree_t *dt)
{
    int i;

    for (i = 0; i < 16; ++i) {
        lt->counts[i] = 0;
    }

    lt->counts[7] = 24;
    lt->counts[8] = 152;
    lt->counts[9] = 112;

    for (i = 0; i < 24; ++i) {
        lt->symbols[i] = (uint16_t)(256 + i);
    }
    for (i = 0; i < 144; ++i) {
        lt->symbols[24 + i] = (uint16_t)i;
    }
    for (i = 0; i < 8; ++i) {
        lt->symbols[24 + 144 + i] = (uint16_t)(280 + i);
    }
    for (i = 0; i < 112; ++i) {
        lt->symbols[24 + 144 + 8 + i] = (uint16_t)(144 + i);
    }

    lt->max_sym = 285;

    for (i = 0; i < 16; ++i) {
        dt->counts[i] = 0;
    }

    dt->counts[5] = 32;

    for (i = 0; i < 32; ++i) {
        dt->symbols[i] = (uint16_t)i;
    }

    dt->max_sym = 29;
}

static int tinf_build_tree(tinf_tree_t *t, const uint8_t *lengths, uint32_t num)
{
    uint16_t offs[16];
    uint32_t i;
    uint32_t num_codes;
    uint32_t available;

    for (i = 0u; i < 16u; ++i) {
        t->counts[i] = 0;
    }

    t->max_sym = -1;

    for (i = 0u; i < num; ++i) {
        if (lengths[i] > 15u) {
            return TINFLATE_INVALID_DATA;
        }

        if (lengths[i] != 0u) {
            t->max_sym = (int)i;
            t->counts[lengths[i]]++;
        }
    }

    available = 1u;
    num_codes = 0u;
    for (i = 0u; i < 16u; ++i) {
        uint32_t used = t->counts[i];

        if (used > available) {
            return TINFLATE_INVALID_DATA;
        }

        available = 2u * (available - used);
        offs[i] = (uint16_t)num_codes;
        num_codes += used;
    }

    if ((num_codes > 1u && available > 0u)
     || (num_codes == 1u && t->counts[1] != 1u)) {
        return TINFLATE_INVALID_DATA;
    }

    for (i = 0u; i < num; ++i) {
        if (lengths[i] != 0u) {
            t->symbols[offs[lengths[i]]++] = (uint16_t)i;
        }
    }

    if (num_codes == 1u) {
        t->counts[1] = 2u;
        t->symbols[1] = (uint16_t)(t->max_sym + 1);
    }

    return TINFLATE_OK;
}

static void tinf_refill(tinf_data_t *d, int num)
{
    while (d->bitcount < num) {
        if (d->source != d->source_end) {
            d->tag |= (uint32_t)(*d->source++) << d->bitcount;
        } else {
            d->overflow = 1;
        }
        d->bitcount += 8;
    }
}

static uint32_t tinf_getbits_no_refill(tinf_data_t *d, int num)
{
    uint32_t bits;

    bits = d->tag & (((uint32_t)1u << num) - 1u);
    d->tag >>= num;
    d->bitcount -= num;
    return bits;
}

static uint32_t tinf_getbits(tinf_data_t *d, int num)
{
    tinf_refill(d, num);
    return tinf_getbits_no_refill(d, num);
}

static uint32_t tinf_getbits_base(tinf_data_t *d, int num, int base)
{
    return (uint32_t)base + (num ? tinf_getbits(d, num) : 0u);
}

static int tinf_decode_symbol(tinf_data_t *d, const tinf_tree_t *t)
{
    int base = 0;
    int offs = 0;
    int len;

    for (len = 1; ; ++len) {
        offs = 2 * offs + (int)tinf_getbits(d, 1);

        if (len > 15) {
            return -1;
        }

        if (offs < (int)t->counts[len]) {
            break;
        }

        base += t->counts[len];
        offs -= t->counts[len];
    }

    return t->symbols[base + offs];
}

static int tinf_decode_trees(tinf_data_t *d, tinf_tree_t *lt, tinf_tree_t *dt)
{
    uint8_t lengths[288 + 32];
    static const uint8_t clcidx[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
        11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    uint32_t hlit;
    uint32_t hdist;
    uint32_t hclen;
    uint32_t i;
    uint32_t num;
    uint32_t length;
    int res;

    hlit = tinf_getbits_base(d, 5, 257);
    hdist = tinf_getbits_base(d, 5, 1);
    hclen = tinf_getbits_base(d, 4, 4);

    if (hlit > 286u || hdist > 30u) {
        return TINFLATE_INVALID_DATA;
    }

    for (i = 0u; i < 19u; ++i) {
        lengths[i] = 0u;
    }

    for (i = 0u; i < hclen; ++i) {
        lengths[clcidx[i]] = (uint8_t)tinf_getbits(d, 3);
    }

    res = tinf_build_tree(lt, lengths, 19u);
    if (res != TINFLATE_OK) {
        return res;
    }

    if (lt->max_sym == -1) {
        return TINFLATE_INVALID_DATA;
    }

    for (num = 0u; num < hlit + hdist; ) {
        int sym = tinf_decode_symbol(d, lt);

        if (sym < 0 || sym > lt->max_sym) {
            return TINFLATE_INVALID_DATA;
        }

        switch (sym) {
        case 16:
            if (num == 0u) {
                return TINFLATE_INVALID_DATA;
            }
            sym = lengths[num - 1u];
            length = tinf_getbits_base(d, 2, 3);
            break;
        case 17:
            sym = 0;
            length = tinf_getbits_base(d, 3, 3);
            break;
        case 18:
            sym = 0;
            length = tinf_getbits_base(d, 7, 11);
            break;
        default:
            length = 1u;
            break;
        }

        if (length > hlit + hdist - num) {
            return TINFLATE_INVALID_DATA;
        }

        while (length-- != 0u) {
            lengths[num++] = (uint8_t)sym;
        }
    }

    if (lengths[256] == 0u) {
        return TINFLATE_INVALID_DATA;
    }

    res = tinf_build_tree(lt, lengths, hlit);
    if (res != TINFLATE_OK) {
        return res;
    }

    res = tinf_build_tree(dt, lengths + hlit, hdist);
    if (res != TINFLATE_OK) {
        return res;
    }

    return TINFLATE_OK;
}

static int tinf_inflate_block_data(tinf_data_t *d, tinf_tree_t *lt, tinf_tree_t *dt)
{
    static const uint8_t length_bits[30] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
        1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
        4, 4, 4, 4, 5, 5, 5, 5, 0, 127
    };
    static const uint16_t length_base[30] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
        15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
        67, 83, 99, 115, 131, 163, 195, 227, 258, 0
    };
    static const uint8_t dist_bits[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
        4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static const uint16_t dist_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25,
        33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };

    for (;;) {
        int sym = tinf_decode_symbol(d, lt);

        if (d->overflow) {
            return TINFLATE_INVALID_DATA;
        }

        if (sym < 256) {
            if (d->dest == d->dest_end) {
                return TINFLATE_OUTPUT_OVERFLOW;
            }
            *d->dest++ = (uint8_t)sym;
        } else {
            int dist;
            int length;
            int offs;
            int i;

            if (sym == 256) {
                return TINFLATE_OK;
            }

            if (sym > lt->max_sym || (sym - 257) > 28 || dt->max_sym == -1) {
                return TINFLATE_INVALID_DATA;
            }

            sym -= 257;
            length = (int)tinf_getbits_base(d, length_bits[sym], length_base[sym]);

            dist = tinf_decode_symbol(d, dt);
            if (dist < 0 || dist > dt->max_sym || dist > 29) {
                return TINFLATE_INVALID_DATA;
            }

            offs = (int)tinf_getbits_base(d, dist_bits[dist], dist_base[dist]);

            if (offs > (int)(d->dest - d->dest_start)) {
                return TINFLATE_INVALID_DATA;
            }

            if ((int)(d->dest_end - d->dest) < length) {
                return TINFLATE_OUTPUT_OVERFLOW;
            }

            for (i = 0; i < length; ++i) {
                d->dest[i] = d->dest[i - offs];
            }

            d->dest += length;
        }
    }
}

static int tinf_inflate_uncompressed_block(tinf_data_t *d)
{
    uint32_t length;
    uint32_t invlength;

    if ((uint32_t)(d->source_end - d->source) < 4u) {
        return TINFLATE_INVALID_DATA;
    }

    length = read_le16(d->source);
    invlength = read_le16(d->source + 2);

    if (length != (~invlength & 0x0000FFFFu)) {
        return TINFLATE_INVALID_DATA;
    }

    d->source += 4;

    if ((uint32_t)(d->source_end - d->source) < length) {
        return TINFLATE_INVALID_DATA;
    }

    if ((uint32_t)(d->dest_end - d->dest) < length) {
        return TINFLATE_OUTPUT_OVERFLOW;
    }

    while (length-- != 0u) {
        *d->dest++ = *d->source++;
    }

    d->tag = 0u;
    d->bitcount = 0;
    return TINFLATE_OK;
}

static int tinf_uncompress(void *dest, uint32_t *dest_len, const void *source, uint32_t source_len)
{
    tinf_data_t d;
    uint32_t bfinal;
    uint32_t btype;
    int res;

    if (dest == 0 || dest_len == 0 || source == 0) {
        return TINFLATE_INVALID_DATA;
    }

    d.source = (const uint8_t *)source;
    d.source_end = d.source + source_len;
    d.tag = 0u;
    d.bitcount = 0;
    d.overflow = 0;
    d.dest_start = (uint8_t *)dest;
    d.dest = (uint8_t *)dest;
    d.dest_end = d.dest_start + *dest_len;

    do {
        bfinal = tinf_getbits(&d, 1);
        btype = tinf_getbits(&d, 2);

        switch (btype) {
        case 0u:
            d.bitcount = 0;
            d.tag = 0u;
            res = tinf_inflate_uncompressed_block(&d);
            break;
        case 1u:
            tinf_build_fixed_trees(&d.ltree, &d.dtree);
            res = tinf_inflate_block_data(&d, &d.ltree, &d.dtree);
            break;
        case 2u:
            res = tinf_decode_trees(&d, &d.ltree, &d.dtree);
            if (res == TINFLATE_OK) {
                res = tinf_inflate_block_data(&d, &d.ltree, &d.dtree);
            }
            break;
        default:
            return TINFLATE_INVALID_DATA;
        }

        if (res != TINFLATE_OK) {
            return res;
        }
    } while (bfinal == 0u);

    *dest_len = (uint32_t)(d.dest - d.dest_start);
    return TINFLATE_OK;
}

static int tinf_gzip_uncompress(void *dest, uint32_t *dest_len, const void *source, uint32_t source_len)
{
    const uint8_t *src = (const uint8_t *)source;
    uint8_t *dst = (uint8_t *)dest;
    const uint8_t *start;
    uint32_t dlen;
    uint32_t crc32;
    int res;
    uint8_t flg;

    if (source_len < 18u) {
        return TINFLATE_INVALID_DATA;
    }

    if (src[0] != 0x1Fu || src[1] != 0x8Bu) {
        return TINFLATE_INVALID_DATA;
    }

    if (src[2] != 8u) {
        return TINFLATE_INVALID_DATA;
    }

    flg = src[3];
    if (flg & 0xE0u) {
        return TINFLATE_INVALID_DATA;
    }

    start = src + 10;

    if (flg & 4u) {
        uint32_t xlen = read_le16(start);
        if (xlen > source_len - 12u) {
            return TINFLATE_INVALID_DATA;
        }
        start += xlen + 2u;
    }

    if (flg & 8u) {
        do {
            if ((uint32_t)(start - src) >= source_len) {
                return TINFLATE_INVALID_DATA;
            }
        } while (*start++ != 0u);
    }

    if (flg & 16u) {
        do {
            if ((uint32_t)(start - src) >= source_len) {
                return TINFLATE_INVALID_DATA;
            }
        } while (*start++ != 0u);
    }

    if (flg & 2u) {
        uint32_t hcrc;
        if ((uint32_t)(start - src) > source_len - 2u) {
            return TINFLATE_INVALID_DATA;
        }
        hcrc = read_le16(start);
        if (hcrc != (tinf_crc32(src, (uint32_t)(start - src)) & 0x0000FFFFu)) {
            return TINFLATE_INVALID_DATA;
        }
        start += 2;
    }

    dlen = read_le32(&src[source_len - 4u]);
    crc32 = read_le32(&src[source_len - 8u]);

    if (dlen > *dest_len) {
        return TINFLATE_OUTPUT_OVERFLOW;
    }

    if ((uint32_t)((src + source_len) - start) < 8u) {
        return TINFLATE_INVALID_DATA;
    }

    res = tinf_uncompress(dst, dest_len, start, (uint32_t)((src + source_len) - start - 8));
    if (res != TINFLATE_OK) {
        return res;
    }

    if (*dest_len != dlen) {
        return TINFLATE_INVALID_DATA;
    }

    if (crc32 != tinf_crc32(dst, dlen)) {
        return TINFLATE_INVALID_DATA;
    }

    return TINFLATE_OK;
}

tinflate_status_t tinflate_decompress(
    const uint8_t *src,
    uint32_t src_len,
    uint8_t *dst,
    uint32_t dst_len,
    uint32_t *out_len)
{
    int res;

    if (src == 0 || dst == 0 || out_len == 0) {
        return TINFLATE_INVALID_DATA;
    }

    *out_len = dst_len;

    if (src_len >= 2u && src[0] == 0x1Fu && src[1] == 0x8Bu) {
        res = tinf_gzip_uncompress(dst, out_len, src, src_len);
    } else {
        res = tinf_uncompress(dst, out_len, src, src_len);
    }

    return res;
}
