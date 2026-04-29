#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>

#include <kernel/support/kernel_memory.h>

extern "C" {
#include <kernel/console.h>
}

namespace {

static uint16_t read_u16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static struct zaclr_result bad_metadata(void)
{
    return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_METADATA);
}

static struct zaclr_result invalid_argument(void)
{
    return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_METADATA);
}

static bool strings_equal(const char* lhs, const char* rhs)
{
    size_t index;

    if (lhs == NULL || rhs == NULL) {
        return lhs == rhs;
    }

    for (index = 0u;; ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }

        if (lhs[index] == '\0') {
            return true;
        }
    }
}

static uint32_t read_heap_index(const struct zaclr_metadata_reader* reader,
                                const uint8_t** p,
                                uint8_t heap_bit)
{
    uint32_t value;

    if ((reader->heap_sizes & heap_bit) != 0u) {
        value = read_u32(*p);
        *p += 4u;
    } else {
        value = read_u16(*p);
        *p += 2u;
    }

    return value;
}

static uint32_t table_index_size(const struct zaclr_metadata_reader* reader, uint32_t table)
{
    if (table >= ZACLR_METADATA_MAX_TABLES) {
        return 2u;
    }

    return reader->row_counts[table] > 0xFFFFu ? 4u : 2u;
}

static uint32_t max_rows_for_tables(const struct zaclr_metadata_reader* reader,
                                    const uint8_t* tables,
                                    uint32_t table_count)
{
    uint32_t max_rows = 0u;
    uint32_t index;

    for (index = 0u; index < table_count; ++index) {
        uint8_t table = tables[index];
        if (table < ZACLR_METADATA_MAX_TABLES && reader->row_counts[table] > max_rows) {
            max_rows = reader->row_counts[table];
        }
    }

    return max_rows;
}

static uint32_t coded_index_size(const struct zaclr_metadata_reader* reader,
                                 const uint8_t* tables,
                                 uint32_t table_count,
                                 uint32_t tag_bits)
{
    uint32_t max_rows = max_rows_for_tables(reader, tables, table_count);
    return max_rows >= (1u << (16u - tag_bits)) ? 4u : 2u;
}

static uint32_t table_row_size(const struct zaclr_metadata_reader* reader, uint32_t table)
{
    static const uint8_t resolution_scope_tables[] = { 0x00u, 0x1Au, 0x23u, 0x01u };
    static const uint8_t typedef_or_ref_tables[] = { 0x02u, 0x01u, 0x1Bu };
    static const uint8_t has_constant_tables[] = { 0x04u, 0x08u, 0x17u };
    static const uint8_t has_custom_attribute_tables[] = {
        0x06u, 0x04u, 0x01u, 0x02u, 0x08u, 0x09u, 0x0Au, 0x00u,
        0x0Eu, 0x17u, 0x14u, 0x11u, 0x1Au, 0x1Bu, 0x20u, 0x23u,
        0x26u, 0x27u, 0x2Au, 0x2Cu, 0x2Bu
    };
    static const uint8_t custom_attribute_type_tables[] = { 0x06u, 0x0Au };
    static const uint8_t has_field_marshal_tables[] = { 0x04u, 0x08u };
    static const uint8_t has_decl_security_tables[] = { 0x02u, 0x06u, 0x20u };
    static const uint8_t member_ref_parent_tables[] = { 0x02u, 0x01u, 0x1Au, 0x06u, 0x1Bu };
    static const uint8_t has_semantics_tables[] = { 0x14u, 0x17u };
    static const uint8_t method_def_or_ref_tables[] = { 0x06u, 0x0Au };
    static const uint8_t member_forwarded_tables[] = { 0x04u, 0x06u };
    static const uint8_t implementation_tables[] = { 0x26u, 0x27u, 0x23u };
    static const uint8_t type_or_method_def_tables[] = { 0x02u, 0x06u };
    const uint32_t str_w = (reader->heap_sizes & 0x01u) != 0u ? 4u : 2u;
    const uint32_t guid_w = (reader->heap_sizes & 0x02u) != 0u ? 4u : 2u;
    const uint32_t blob_w = (reader->heap_sizes & 0x04u) != 0u ? 4u : 2u;

    switch (table) {
        case 0x00u: return 2u + str_w + guid_w + guid_w + guid_w;
        case 0x01u: return coded_index_size(reader, resolution_scope_tables, 4u, 2u) + str_w + str_w;
        case 0x02u: return 4u + str_w + str_w + coded_index_size(reader, typedef_or_ref_tables, 3u, 2u)
                           + table_index_size(reader, 0x04u) + table_index_size(reader, 0x06u);
        case 0x03u: return table_index_size(reader, 0x04u);
        case 0x04u: return 2u + str_w + blob_w;
        case 0x05u: return table_index_size(reader, 0x06u);
        case 0x06u: return 4u + 2u + 2u + str_w + blob_w + table_index_size(reader, 0x08u);
        case 0x07u: return table_index_size(reader, 0x08u);
        case 0x08u: return 2u + 2u + str_w;
        case 0x09u: return table_index_size(reader, 0x02u) + coded_index_size(reader, typedef_or_ref_tables, 3u, 2u);
        case 0x0Au: return coded_index_size(reader, member_ref_parent_tables, 5u, 3u) + str_w + blob_w;
        case 0x0Bu: return 2u + coded_index_size(reader, has_constant_tables, 3u, 2u) + blob_w;
        case 0x0Cu: return coded_index_size(reader, has_custom_attribute_tables, 21u, 5u)
                           + coded_index_size(reader, custom_attribute_type_tables, 2u, 3u) + blob_w;
        case 0x0Du: return coded_index_size(reader, has_field_marshal_tables, 2u, 1u) + blob_w;
        case 0x0Eu: return 2u + coded_index_size(reader, has_decl_security_tables, 3u, 2u) + blob_w;
        case 0x0Fu: return 2u + 4u + table_index_size(reader, 0x02u);
        case 0x10u: return 4u + table_index_size(reader, 0x04u);
        case 0x11u: return blob_w;
        case 0x12u: return table_index_size(reader, 0x02u) + table_index_size(reader, 0x14u);
        case 0x13u: return table_index_size(reader, 0x14u);
        case 0x14u: return 2u + str_w + coded_index_size(reader, typedef_or_ref_tables, 3u, 2u);
        case 0x15u: return table_index_size(reader, 0x02u) + table_index_size(reader, 0x17u);
        case 0x16u: return table_index_size(reader, 0x17u);
        case 0x17u: return 2u + str_w + blob_w;
        case 0x18u: return 2u + table_index_size(reader, 0x06u) + coded_index_size(reader, has_semantics_tables, 2u, 1u);
        case 0x19u: return table_index_size(reader, 0x02u) + coded_index_size(reader, method_def_or_ref_tables, 2u, 1u)
                           + coded_index_size(reader, method_def_or_ref_tables, 2u, 1u);
        case 0x1Au: return str_w;
        case 0x1Bu: return blob_w;
        case 0x1Cu: return 2u + coded_index_size(reader, member_forwarded_tables, 2u, 1u) + str_w + table_index_size(reader, 0x1Au);
        case 0x1Du: return 4u + table_index_size(reader, 0x04u);
        case 0x1Eu: return 4u + table_index_size(reader, 0x14u);
        case 0x20u: return 4u + 2u + 2u + 2u + 2u + 4u + blob_w + str_w + str_w;
        case 0x21u: return 4u;
        case 0x22u: return 4u + 4u + str_w + blob_w;
        case 0x23u: return 2u + 2u + 2u + 2u + 4u + blob_w + str_w + str_w + blob_w;
        case 0x24u: return 4u + table_index_size(reader, 0x26u) + table_index_size(reader, 0x23u);
        case 0x25u: return 4u + table_index_size(reader, 0x23u) + table_index_size(reader, 0x23u);
        case 0x26u: return 4u + str_w + blob_w;
        case 0x27u: return 4u + 4u + str_w + str_w + coded_index_size(reader, implementation_tables, 3u, 2u);
        case 0x28u: return 4u + 4u + str_w + coded_index_size(reader, implementation_tables, 3u, 2u);
        case 0x29u: return table_index_size(reader, 0x02u) + table_index_size(reader, 0x02u);
        case 0x2Au: return 2u + 2u + coded_index_size(reader, type_or_method_def_tables, 2u, 1u) + str_w;
        case 0x2Bu: return coded_index_size(reader, method_def_or_ref_tables, 2u, 1u) + blob_w;
        case 0x2Cu: return table_index_size(reader, 0x2Au) + coded_index_size(reader, typedef_or_ref_tables, 3u, 2u);
        default: return 0u;
    }
}

static struct zaclr_result parse_tilde_stream(struct zaclr_metadata_reader* reader)
{
    const uint8_t* ts;
    const uint8_t* row_counts_ptr;
    const uint8_t* table_ptr;
    uint64_t valid_mask;
    uint32_t table;

    ts = reader->tilde_stream.data;
    if (ts == NULL || reader->tilde_stream.size < 24u) {
        return bad_metadata();
    }

    reader->heap_sizes = ts[6];
    valid_mask = (uint64_t)read_u32(ts + 8u) | ((uint64_t)read_u32(ts + 12u) << 32);
    reader->valid_mask = valid_mask;

    row_counts_ptr = ts + 24u;
    for (table = 0u; table < ZACLR_METADATA_MAX_TABLES; ++table) {
        if ((valid_mask & (1ULL << table)) != 0u) {
            if ((size_t)(row_counts_ptr - ts) + 4u > reader->tilde_stream.size) {
                return bad_metadata();
            }

            reader->row_counts[table] = read_u32(row_counts_ptr);
            row_counts_ptr += 4u;
        }
    }

    table_ptr = row_counts_ptr;
    for (table = 0u; table < ZACLR_METADATA_MAX_TABLES; ++table) {
        uint32_t row_size;
        uint32_t bytes_required;

        if ((valid_mask & (1ULL << table)) == 0u) {
            continue;
        }

        row_size = table_row_size(reader, table);
        if (row_size == 0u && reader->row_counts[table] != 0u) {
            return bad_metadata();
        }

        bytes_required = row_size * reader->row_counts[table];
        if ((size_t)(table_ptr - ts) + bytes_required > reader->tilde_stream.size) {
            return bad_metadata();
        }

        reader->tables[table].rows = table_ptr;
        reader->tables[table].row_size = row_size;
        reader->tables[table].row_count = reader->row_counts[table];
        table_ptr += bytes_required;
    }

    if (reader->row_counts[0x20u] > 0u) {
        struct zaclr_assembly_row row;
        struct zaclr_result result = zaclr_metadata_reader_get_assembly_row(reader, 1u, &row);
        if (result.status != ZACLR_STATUS_OK) {
            return result;
        }

        reader->assembly_name_index = row.name_index;
    }

    return zaclr_result_ok();
}

}

extern "C" struct zaclr_result zaclr_metadata_reader_initialize(struct zaclr_metadata_reader* reader,
                                                                 const struct zaclr_slice* metadata_root)
{
    const uint8_t* data;
    uint32_t version_length;
    uint32_t stream_count;
    uint32_t stream_index;
    const uint8_t* stream_ptr;

    if (reader == NULL || metadata_root == NULL || metadata_root->data == NULL || metadata_root->size < 20u)
    {
        return invalid_argument();
    }

    *reader = {};
    reader->metadata_root = *metadata_root;
    data = metadata_root->data;

    if (read_u32(data) != 0x424A5342u) {
        return bad_metadata();
    }

    version_length = read_u32(data + 12u);
    if ((16u + version_length + 4u) > metadata_root->size) {
        return bad_metadata();
    }

    stream_count = read_u16(data + 16u + version_length + 2u);
    stream_ptr = data + 16u + version_length + 4u;

    for (stream_index = 0u; stream_index < stream_count; ++stream_index) {
        const char* stream_name;
        size_t name_length;
        uint32_t offset;
        uint32_t size;
        size_t padded_name_bytes;

        if ((size_t)(stream_ptr - data) + 8u > metadata_root->size) {
            return bad_metadata();
        }

        offset = read_u32(stream_ptr);
        size = read_u32(stream_ptr + 4u);
        stream_name = (const char*)(stream_ptr + 8u);
        name_length = kernel_strlen(stream_name);
        padded_name_bytes = (name_length + 1u + 3u) & ~3u;

        if ((size_t)(stream_ptr - data) + 8u + padded_name_bytes > metadata_root->size) {
            return bad_metadata();
        }

        if ((size_t)offset + (size_t)size > metadata_root->size) {
            return bad_metadata();
        }

        if (strings_equal(stream_name, "#Strings")) {
            reader->strings_heap.data = data + offset;
            reader->strings_heap.size = size;
        } else if (strings_equal(stream_name, "#Blob")) {
            reader->blob_heap.data = data + offset;
            reader->blob_heap.size = size;
        } else if (strings_equal(stream_name, "#US")) {
            reader->user_string_heap.data = data + offset;
            reader->user_string_heap.size = size;
        } else if (strings_equal(stream_name, "#~") || strings_equal(stream_name, "#-")) {
            reader->tilde_stream.data = data + offset;
            reader->tilde_stream.size = size;
        }

        stream_ptr += 8u + padded_name_bytes;
    }

    if (reader->tilde_stream.data == NULL) {
        return bad_metadata();
    }

    return parse_tilde_stream(reader);
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_string(const struct zaclr_metadata_reader* reader,
                                                                 uint32_t index,
                                                                 struct zaclr_name_view* value)
{
    const char* text;
    size_t length;

    if (reader == NULL || value == NULL) {
        return invalid_argument();
    }

    if (index >= reader->strings_heap.size) {
        return bad_metadata();
    }

    text = (const char*)(reader->strings_heap.data + index);
    length = kernel_strlen(text);
    if ((size_t)index + length >= reader->strings_heap.size) {
        return bad_metadata();
    }

    value->text = text;
    value->length = length;
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_blob(const struct zaclr_metadata_reader* reader,
                                                               uint32_t index,
                                                               struct zaclr_slice* value)
{
    uint8_t header;
    uint32_t length;
    uint32_t payload_offset;

    if (reader == NULL || value == NULL) {
        return invalid_argument();
    }

    if (reader->blob_heap.data == NULL || index >= reader->blob_heap.size) {
        return bad_metadata();
    }

    header = reader->blob_heap.data[index];
    if ((header & 0x80u) == 0u) {
        length = header;
        payload_offset = index + 1u;
    } else if ((header & 0xC0u) == 0x80u) {
        if (index + 1u >= reader->blob_heap.size) {
            return bad_metadata();
        }

        length = (((uint32_t)(header & 0x3Fu)) << 8) | (uint32_t)reader->blob_heap.data[index + 1u];
        payload_offset = index + 2u;
    } else {
        if (index + 3u >= reader->blob_heap.size) {
            return bad_metadata();
        }

        length = (((uint32_t)(header & 0x1Fu)) << 24)
               | ((uint32_t)reader->blob_heap.data[index + 1u] << 16)
               | ((uint32_t)reader->blob_heap.data[index + 2u] << 8)
               | (uint32_t)reader->blob_heap.data[index + 3u];
        payload_offset = index + 4u;
    }

    if ((size_t)payload_offset + length > reader->blob_heap.size) {
        return bad_metadata();
    }

    value->data = reader->blob_heap.data + payload_offset;
    value->size = length;
    return zaclr_result_ok();
}

extern "C" uint32_t zaclr_metadata_reader_get_row_count(const struct zaclr_metadata_reader* reader,
                                                         uint32_t table_id)
{
    if (reader == NULL || table_id >= ZACLR_METADATA_MAX_TABLES) {
        return 0u;
    }

    return reader->row_counts[table_id];
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_assembly_row(const struct zaclr_metadata_reader* reader,
                                                                        uint32_t row_1based,
                                                                        struct zaclr_assembly_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x20u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    (void)read_u32(p); p += 4u;
    row->major_version = read_u16(p); p += 2u;
    row->minor_version = read_u16(p); p += 2u;
    row->build_number = read_u16(p); p += 2u;
    row->revision_number = read_u16(p); p += 2u;
    row->flags = read_u32(p); p += 4u;
    row->public_key_blob_index = read_heap_index(reader, &p, 0x04u);
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->culture_index = read_heap_index(reader, &p, 0x01u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_module_row(const struct zaclr_metadata_reader* reader,
                                                                      uint32_t row_1based,
                                                                      struct zaclr_module_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x00u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->generation = read_u16(p); p += 2u;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->mvid_index = read_heap_index(reader, &p, 0x02u);
    row->enc_id_index = read_heap_index(reader, &p, 0x02u);
    row->enc_base_id_index = read_heap_index(reader, &p, 0x02u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_typedef_row(const struct zaclr_metadata_reader* reader,
                                                                      uint32_t row_1based,
                                                                      struct zaclr_typedef_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t typedef_or_ref_tables[] = { 0x02u, 0x01u, 0x1Bu };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x02u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->flags = read_u32(p); p += 4u;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->namespace_index = read_heap_index(reader, &p, 0x01u);
    if (coded_index_size(reader, typedef_or_ref_tables, 3u, 2u) == 4u) {
        row->extends = read_u32(p); p += 4u;
    } else {
        row->extends = read_u16(p); p += 2u;
    }
    row->field_list = table_index_size(reader, 0x04u) == 4u ? read_u32(p) : read_u16(p); p += table_index_size(reader, 0x04u);
    row->method_list = table_index_size(reader, 0x06u) == 4u ? read_u32(p) : read_u16(p);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_field_row(const struct zaclr_metadata_reader* reader,
                                                                    uint32_t row_1based,
                                                                    struct zaclr_field_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x04u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->flags = read_u16(p); p += 2u;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->signature_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_methoddef_row(const struct zaclr_metadata_reader* reader,
                                                                        uint32_t row_1based,
                                                                        struct zaclr_methoddef_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x06u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->rva = read_u32(p); p += 4u;
    row->impl_flags = read_u16(p); p += 2u;
    row->flags = read_u16(p); p += 2u;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->signature_blob_index = read_heap_index(reader, &p, 0x04u);
    row->param_list = table_index_size(reader, 0x08u) == 4u ? read_u32(p) : read_u16(p);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_param_row(const struct zaclr_metadata_reader* reader,
                                                                    uint32_t row_1based,
                                                                    struct zaclr_param_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x08u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->flags = read_u16(p); p += 2u;
    row->sequence = read_u16(p); p += 2u;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_memberref_row(const struct zaclr_metadata_reader* reader,
                                                                        uint32_t row_1based,
                                                                        struct zaclr_memberref_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t member_ref_parent_tables[] = { 0x02u, 0x01u, 0x1Au, 0x06u, 0x1Bu };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x0Au];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->class_coded_index = coded_index_size(reader, member_ref_parent_tables, 5u, 3u) == 4u ? read_u32(p) : read_u16(p);
    p += coded_index_size(reader, member_ref_parent_tables, 5u, 3u);
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->signature_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_constant_row(const struct zaclr_metadata_reader* reader,
                                                                       uint32_t row_1based,
                                                                       struct zaclr_constant_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t has_constant_tables[] = { 0x04u, 0x08u, 0x17u };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x0Bu];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->type = read_u16(p); p += 2u;
    row->parent_coded_index = coded_index_size(reader, has_constant_tables, 3u, 2u) == 4u ? read_u32(p) : read_u16(p);
    p += coded_index_size(reader, has_constant_tables, 3u, 2u);
    row->value_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_customattribute_row(const struct zaclr_metadata_reader* reader,
                                                                              uint32_t row_1based,
                                                                              struct zaclr_customattribute_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t has_custom_attribute_tables[] = {
        0x06u, 0x04u, 0x01u, 0x02u, 0x08u, 0x09u, 0x0Au, 0x00u,
        0x0Eu, 0x17u, 0x14u, 0x11u, 0x1Au, 0x1Bu, 0x20u, 0x23u,
        0x26u, 0x27u, 0x2Au, 0x2Cu, 0x2Bu
    };
    static const uint8_t custom_attribute_type_tables[] = { 0x06u, 0x0Au };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x0Cu];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->parent_coded_index = coded_index_size(reader, has_custom_attribute_tables, 21u, 5u) == 4u ? read_u32(p) : read_u16(p);
    p += coded_index_size(reader, has_custom_attribute_tables, 21u, 5u);
    row->type_coded_index = coded_index_size(reader, custom_attribute_type_tables, 2u, 3u) == 4u ? read_u32(p) : read_u16(p);
    p += coded_index_size(reader, custom_attribute_type_tables, 2u, 3u);
    row->value_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_fieldmarshal_row(const struct zaclr_metadata_reader* reader,
                                                                           uint32_t row_1based,
                                                                           struct zaclr_fieldmarshal_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t has_field_marshal_tables[] = { 0x04u, 0x08u };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x0Du];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->parent_coded_index = coded_index_size(reader, has_field_marshal_tables, 2u, 1u) == 4u ? read_u32(p) : read_u16(p);
    p += coded_index_size(reader, has_field_marshal_tables, 2u, 1u);
    row->native_type_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_classlayout_row(const struct zaclr_metadata_reader* reader,
                                                                          uint32_t row_1based,
                                                                          struct zaclr_classlayout_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x0Fu];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->packing_size = read_u16(p); p += 2u;
    row->class_size = read_u32(p); p += 4u;
    row->parent = table_index_size(reader, 0x02u) == 4u ? read_u32(p) : read_u16(p);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_fieldlayout_row(const struct zaclr_metadata_reader* reader,
                                                                          uint32_t row_1based,
                                                                          struct zaclr_fieldlayout_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x10u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->offset = read_u32(p); p += 4u;
    row->field = table_index_size(reader, 0x04u) == 4u ? read_u32(p) : read_u16(p);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_event_row(const struct zaclr_metadata_reader* reader,
                                                                    uint32_t row_1based,
                                                                    struct zaclr_event_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t typedef_or_ref_tables[] = { 0x02u, 0x01u, 0x1Bu };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x14u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->flags = read_u16(p); p += 2u;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->event_type_coded_index = coded_index_size(reader, typedef_or_ref_tables, 3u, 2u) == 4u ? read_u32(p) : read_u16(p);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_property_row(const struct zaclr_metadata_reader* reader,
                                                                       uint32_t row_1based,
                                                                       struct zaclr_property_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x17u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->flags = read_u16(p); p += 2u;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->type_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_genericparam_row(const struct zaclr_metadata_reader* reader,
                                                                           uint32_t row_1based,
                                                                           struct zaclr_genericparam_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t type_or_method_def_tables[] = { 0x02u, 0x06u };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x2Au];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->number = read_u16(p); p += 2u;
    row->flags = read_u16(p); p += 2u;
    row->owner_coded_index = coded_index_size(reader, type_or_method_def_tables, 2u, 1u) == 4u ? read_u32(p) : read_u16(p);
    p += coded_index_size(reader, type_or_method_def_tables, 2u, 1u);
    row->name_index = read_heap_index(reader, &p, 0x01u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_assemblyref_row(const struct zaclr_metadata_reader* reader,
                                                                            uint32_t row_1based,
                                                                            struct zaclr_assemblyref_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x23u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->major_version = read_u16(p); p += 2u;
    row->minor_version = read_u16(p); p += 2u;
    row->build_number = read_u16(p); p += 2u;
    row->revision_number = read_u16(p); p += 2u;
    row->flags = read_u32(p); p += 4u;
    row->public_key_or_token_blob_index = read_heap_index(reader, &p, 0x04u);
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->culture_index = read_heap_index(reader, &p, 0x01u);
    row->hash_value_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_exportedtype_row(const struct zaclr_metadata_reader* reader,
                                                                            uint32_t row_1based,
                                                                            struct zaclr_exportedtype_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t implementation_tables[] = { 0x26u, 0x27u, 0x23u };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x27u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->flags = read_u32(p); p += 4u;
    row->type_def_id = read_u32(p); p += 4u;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    row->namespace_index = read_heap_index(reader, &p, 0x01u);
    row->implementation_coded_index = coded_index_size(reader, implementation_tables, 3u, 2u) == 4u ? read_u32(p) : read_u16(p);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_standalonesig_row(const struct zaclr_metadata_reader* reader,
                                                                             uint32_t row_1based,
                                                                             struct zaclr_standalonesig_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x11u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->signature_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_moduleref_row(const struct zaclr_metadata_reader* reader,
                                                                         uint32_t row_1based,
                                                                         struct zaclr_moduleref_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x1Au];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->name_index = read_heap_index(reader, &p, 0x01u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_implmap_row(const struct zaclr_metadata_reader* reader,
                                                                       uint32_t row_1based,
                                                                       struct zaclr_implmap_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t member_forwarded_tables[] = { 0x04u, 0x06u };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x1Cu];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->flags = read_u16(p); p += 2u;
    if (coded_index_size(reader, member_forwarded_tables, 2u, 1u) == 4u) {
        row->member_forwarded_coded_index = read_u32(p); p += 4u;
    } else {
        row->member_forwarded_coded_index = read_u16(p); p += 2u;
    }
    row->import_name_index = read_heap_index(reader, &p, 0x01u);
    row->import_scope = table_index_size(reader, 0x1Au) == 4u ? read_u32(p) : read_u16(p);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_methodspec_row(const struct zaclr_metadata_reader* reader,
                                                                          uint32_t row_1based,
                                                                          struct zaclr_methodspec_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;
    static const uint8_t method_def_or_ref_tables[] = { 0x06u, 0x0Au };

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x2Bu];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    if (coded_index_size(reader, method_def_or_ref_tables, 2u, 1u) == 4u) {
        row->method_coded_index = read_u32(p); p += 4u;
    } else {
        row->method_coded_index = read_u16(p); p += 2u;
    }
    row->instantiation_blob_index = read_heap_index(reader, &p, 0x04u);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_nestedclass_row(const struct zaclr_metadata_reader* reader,
                                                                          uint32_t row_1based,
                                                                          struct zaclr_nestedclass_row* row)
{
    const struct zaclr_metadata_table_view* table;
    const uint8_t* p;

    if (reader == NULL || row == NULL) {
        return invalid_argument();
    }

    table = &reader->tables[0x29u];
    if (row_1based == 0u || row_1based > table->row_count || table->rows == NULL) {
        return bad_metadata();
    }

    p = table->rows + (row_1based - 1u) * table->row_size;
    row->nested_class = table_index_size(reader, 0x02u) == 4u ? read_u32(p) : read_u16(p);
    p += table_index_size(reader, 0x02u);
    row->enclosing_class = table_index_size(reader, 0x02u) == 4u ? read_u32(p) : read_u16(p);
    return zaclr_result_ok();
}

extern "C" struct zaclr_result zaclr_metadata_reader_get_assembly_name(const struct zaclr_metadata_reader* reader,
                                                                         struct zaclr_name_view* assembly_name)
{
    if (reader == NULL || assembly_name == NULL) {
        return invalid_argument();
    }

    if (reader->assembly_name_index == 0u) {
        assembly_name->text = "";
        assembly_name->length = 0u;
        return zaclr_result_ok();
    }

    return zaclr_metadata_reader_get_string(reader, reader->assembly_name_index, assembly_name);
}
