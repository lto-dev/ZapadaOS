/*
 * Zapada - src/boot/multiboot2.h
 *
 * Minimal Multiboot2 structure definitions required to read boot information
 * passed by GRUB2. Based on the Multiboot2 specification.
 *
 * Only the tags used during Stage 1.1 bring-up are defined here. Additional
 * tags can be added as needed in later stages.
 */

#ifndef ZAPADA_MULTIBOOT2_H
#define ZAPADA_MULTIBOOT2_H

#include <kernel/types.h>

/* Magic value passed in EAX by a Multiboot2-compliant bootloader. */
#define MULTIBOOT2_BOOTLOADER_MAGIC  0x36D76289u

/* Tag types */
#define MB2_TAG_END              0
#define MB2_TAG_CMDLINE          1
#define MB2_TAG_BOOT_LOADER_NAME 2
#define MB2_TAG_MODULE           3
#define MB2_TAG_MEMORY_MAP       6
#define MB2_TAG_FRAMEBUFFER      8

/* Memory map entry types */
#define MB2_MEMORY_AVAILABLE     1
#define MB2_MEMORY_RESERVED      2
#define MB2_MEMORY_ACPI          3
#define MB2_MEMORY_NVS           4
#define MB2_MEMORY_BADRAM        5

/* --------------------------------------------------------------------------
 * Fixed-size header at the start of the Multiboot2 information structure.
 * -------------------------------------------------------------------------- */
typedef struct mb2_info {
    uint32_t total_size;
    uint32_t reserved;
    /* Followed by a sequence of tags. */
} __attribute__((packed)) mb2_info_t;

/* --------------------------------------------------------------------------
 * Common tag header. Every tag begins with these two fields.
 * -------------------------------------------------------------------------- */
typedef struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

/* --------------------------------------------------------------------------
 * Memory map tag (type 6).
 * -------------------------------------------------------------------------- */
typedef struct mb2_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

typedef struct mb2_tag_mmap {
    uint32_t type;          /* MB2_TAG_MEMORY_MAP */
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* Followed by (size - 16) / entry_size entries of type mb2_mmap_entry_t. */
} __attribute__((packed)) mb2_tag_mmap_t;

/* --------------------------------------------------------------------------
 * Command line tag (type 1).
 * -------------------------------------------------------------------------- */
typedef struct mb2_tag_cmdline {
    uint32_t type;          /* MB2_TAG_CMDLINE */
    uint32_t size;
    char     string[0];     /* Null-terminated command line string. */
} __attribute__((packed)) mb2_tag_cmdline_t;

/* --------------------------------------------------------------------------
 * Boot loader name tag (type 2).
 * -------------------------------------------------------------------------- */
typedef struct mb2_tag_loader_name {
    uint32_t type;          /* MB2_TAG_BOOT_LOADER_NAME */
    uint32_t size;
    char     string[0];     /* Null-terminated loader name string. */
} __attribute__((packed)) mb2_tag_loader_name_t;

/* --------------------------------------------------------------------------
 * Module tag (type 3).
 * Represents a boot module (e.g., initramfs image) loaded by GRUB.
 * -------------------------------------------------------------------------- */
typedef struct mb2_tag_module {
    uint32_t type;          /* MB2_TAG_MODULE */
    uint32_t size;
    uint32_t mod_start;     /* Start address of module in memory. */
    uint32_t mod_end;       /* End address of module in memory (one past last byte). */
    char     string[0];     /* Null-terminated command line or module name. */
} __attribute__((packed)) mb2_tag_module_t;

typedef struct mb2_tag_framebuffer_common {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
} __attribute__((packed)) mb2_tag_framebuffer_common_t;

typedef struct mb2_tag_framebuffer_rgb {
    mb2_tag_framebuffer_common_t common;
    uint8_t framebuffer_red_field_position;
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_field_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_field_position;
    uint8_t framebuffer_blue_mask_size;
} __attribute__((packed)) mb2_tag_framebuffer_rgb_t;

/*
 * mb2_find_tag - Walk the Multiboot2 tag list and return the first tag
 *                matching the given type, or NULL if not found.
 *
 * Parameters:
 *   info - Pointer to the Multiboot2 information structure.
 *   type - Tag type to search for (one of MB2_TAG_* above).
 */
static inline mb2_tag_t *mb2_find_tag(mb2_info_t *info, uint32_t type)
{
    /*
     * Tags begin immediately after the 8-byte fixed header and are 8-byte
     * aligned. Iteration stops at the end tag (type 0) or when we reach the
     * end of the reported total_size.
     */
    uintptr_t   addr = (uintptr_t)info + sizeof(mb2_info_t);
    uintptr_t   end  = (uintptr_t)info + info->total_size;

    while (addr < end) {
        mb2_tag_t *tag = (mb2_tag_t *)addr;

        if (tag->type == MB2_TAG_END) {
            break;
        }

        if (tag->type == type) {
            return tag;
        }

        /* Each tag is padded to an 8-byte boundary. */
        addr += (tag->size + 7) & ~7u;
    }

    return 0;
}

/*
 * mb2_iterate_modules - Walk through all module tags in the Multiboot2 info.
 *                        Calls callback for each module tag found.
 *
 * Parameters:
 *   info     - Pointer to the Multiboot2 information structure.
 *   callback - Function to call for each module. Return non-zero to stop iteration.
 *
 * Returns the last non-zero callback result, or 0 if all modules were processed.
 */
static inline int mb2_iterate_modules(mb2_info_t *info,
                                      int (*callback)(mb2_tag_module_t *module, void *arg),
                                      void *arg)
{
    uintptr_t   addr = (uintptr_t)info + sizeof(mb2_info_t);
    uintptr_t   end  = (uintptr_t)info + info->total_size;
    int         result = 0;

    while (addr < end) {
        mb2_tag_t *tag = (mb2_tag_t *)addr;

        if (tag->type == MB2_TAG_END) {
            break;
        }

        if (tag->type == MB2_TAG_MODULE) {
            result = callback((mb2_tag_module_t *)tag, arg);
            if (result != 0) {
                return result;
            }
        }

        /* Each tag is padded to an 8-byte boundary. */
        addr += (tag->size + 7) & ~7u;
    }

    return result;
}

#endif /* ZAPADA_MULTIBOOT2_H */


