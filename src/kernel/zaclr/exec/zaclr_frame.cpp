#include <kernel/zaclr/exec/zaclr_frame.h>

#include <kernel/support/kernel_memory.h>
#include <kernel/zaclr/diag/zaclr_trace_events.h>
#include <kernel/zaclr/runtime/zaclr_runtime.h>
#include <kernel/zaclr/metadata/zaclr_metadata_reader.h>

extern "C" {
#include <kernel/console.h>
}

namespace
{
    static bool text_equals(const char* left, const char* right)
    {
        if (left == NULL || right == NULL)
        {
            return false;
        }

        while (*left != '\0' && *right != '\0')
        {
            if (*left != *right)
            {
                return false;
            }

            ++left;
            ++right;
        }

        return *left == *right;
    }

    static void append_text(char* buffer, size_t capacity, size_t* length, const char* text)
    {
        size_t index = 0u;

        if (buffer == NULL || capacity == 0u || length == NULL || text == NULL)
        {
            return;
        }

        while (text[index] != '\0' && (*length + 1u) < capacity)
        {
            buffer[*length] = text[index];
            ++(*length);
            ++index;
        }

        buffer[*length] = '\0';
    }

    static const char* format_trace_method_name(const struct zaclr_loaded_assembly* assembly,
                                                const struct zaclr_method_desc* method,
                                                char* buffer,
                                                size_t capacity)
    {
        size_t length = 0u;
        const struct zaclr_type_desc* owning_type;

        if (buffer == NULL || capacity == 0u)
        {
            return "<unknown>";
        }

        buffer[0] = '\0';
        if (assembly == NULL || method == NULL)
        {
            append_text(buffer, capacity, &length, "<unknown>");
            return buffer;
        }

        append_text(buffer, capacity, &length, assembly->assembly_name.text);
        append_text(buffer, capacity, &length, "::");

        owning_type = zaclr_type_map_find_by_token(&assembly->type_map, method->owning_type_token);
        if (owning_type != NULL)
        {
            if (owning_type->type_namespace.text != NULL && owning_type->type_namespace.text[0] != '\0')
            {
                append_text(buffer, capacity, &length, owning_type->type_namespace.text);
                append_text(buffer, capacity, &length, ".");
            }

            append_text(buffer, capacity, &length, owning_type->type_name.text);
            append_text(buffer, capacity, &length, ".");
        }

        append_text(buffer, capacity, &length, method->name.text);
        return buffer;
    }

    static uint16_t read_u16(const uint8_t* data)
    {
        return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    }

    static uint32_t read_u32(const uint8_t* data)
    {
        return (uint32_t)data[0]
             | ((uint32_t)data[1] << 8)
             | ((uint32_t)data[2] << 16)
             | ((uint32_t)data[3] << 24);
    }

    static struct zaclr_result decode_compressed_uint(const struct zaclr_slice* blob,
                                                      uint32_t* offset,
                                                      uint32_t* value)
    {
        uint8_t first;

        if (blob == NULL || offset == NULL || value == NULL || blob->data == NULL || *offset >= blob->size)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
        }

        first = blob->data[*offset];
        if ((first & 0x80u) == 0u)
        {
            *value = first;
            *offset += 1u;
            return zaclr_result_ok();
        }

        if ((first & 0xC0u) == 0x80u)
        {
            if ((*offset + 1u) >= blob->size)
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
            }

            *value = (((uint32_t)(first & 0x3Fu)) << 8) | (uint32_t)blob->data[*offset + 1u];
            *offset += 2u;
            return zaclr_result_ok();
        }

        if ((*offset + 3u) >= blob->size)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *value = (((uint32_t)(first & 0x1Fu)) << 24)
               | ((uint32_t)blob->data[*offset + 1u] << 16)
               | ((uint32_t)blob->data[*offset + 2u] << 8)
               | (uint32_t)blob->data[*offset + 3u];
        *offset += 4u;
        return zaclr_result_ok();
    }

    static struct zaclr_result parse_local_count(const struct zaclr_loaded_assembly* assembly,
                                                 uint32_t local_sig_token,
                                                 uint16_t* out_local_count)
    {
        struct zaclr_standalonesig_row row;
        struct zaclr_slice signature_blob;
        uint32_t offset = 0u;
        uint32_t local_count = 0u;
        struct zaclr_result result;

        if (out_local_count == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_local_count = 0u;
        if (assembly == NULL || local_sig_token == 0u || (local_sig_token & 0x00FFFFFFu) == 0u)
        {
            return zaclr_result_ok();
        }

        result = zaclr_metadata_reader_get_standalonesig_row(&assembly->metadata,
                                                             local_sig_token & 0x00FFFFFFu,
                                                             &row);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = zaclr_metadata_reader_get_blob(&assembly->metadata, row.signature_blob_index, &signature_blob);
        if (result.status != ZACLR_STATUS_OK || signature_blob.size == 0u)
        {
            return result.status == ZACLR_STATUS_OK
                ? zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC)
                : result;
        }

        if (signature_blob.data[offset++] != 0x07u)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = decode_compressed_uint(&signature_blob, &offset, &local_count);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        *out_local_count = (uint16_t)local_count;
        return zaclr_result_ok();
    }

    static struct zaclr_result allocate_stack_values(uint32_t count,
                                                     struct zaclr_stack_value** out_values)
    {
        struct zaclr_stack_value* values;

        if (out_values == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_values = NULL;
        if (count == 0u)
        {
            return zaclr_result_ok();
        }

        values = (struct zaclr_stack_value*)kernel_alloc(sizeof(struct zaclr_stack_value) * count);
        if (values == NULL)
        {
            console_write("[ZACLR][frame-create] frame alloc failed\n");
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
        }

        kernel_memset(values, 0, sizeof(struct zaclr_stack_value) * count);
        *out_values = values;
        return zaclr_result_ok();
    }

    static struct zaclr_result allocate_exception_clauses(uint32_t count,
                                                          struct zaclr_exception_clause** out_clauses)
    {
        struct zaclr_exception_clause* clauses;

        if (out_clauses == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_clauses = NULL;
        if (count == 0u)
        {
            return zaclr_result_ok();
        }

        clauses = (struct zaclr_exception_clause*)kernel_alloc(sizeof(struct zaclr_exception_clause) * count);
        if (clauses == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
        }

        kernel_memset(clauses, 0, sizeof(struct zaclr_exception_clause) * count);
        *out_clauses = clauses;
        return zaclr_result_ok();
    }

    static struct zaclr_result parse_exception_clauses(const uint8_t* section,
                                                       uint32_t section_size,
                                                       struct zaclr_exception_clause* clauses,
                                                       uint16_t clause_count)
    {
        uint32_t clause_index;

        if (section == NULL || clauses == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (clause_count == 0u)
        {
            return zaclr_result_ok();
        }

        if ((section[0] & 0x40u) != 0u)
        {
            const uint32_t clause_size = 24u;
            uint32_t data_offset = 4u;

            if (section_size < data_offset + ((uint32_t)clause_count * clause_size))
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_IMAGE, ZACLR_STATUS_CATEGORY_EXEC);
            }

            for (clause_index = 0u; clause_index < clause_count; ++clause_index)
            {
                const uint8_t* clause = section + data_offset + (clause_index * clause_size);
                clauses[clause_index].flags = read_u32(clause + 0u);
                clauses[clause_index].try_offset = read_u32(clause + 4u);
                clauses[clause_index].try_length = read_u32(clause + 8u);
                clauses[clause_index].handler_offset = read_u32(clause + 12u);
                clauses[clause_index].handler_length = read_u32(clause + 16u);
                clauses[clause_index].class_token = read_u32(clause + 20u);
            }

            return zaclr_result_ok();
        }

        {
            const uint32_t clause_size = 12u;
            uint32_t data_offset = 4u;

            if (section_size < data_offset + ((uint32_t)clause_count * clause_size))
            {
                return zaclr_result_make(ZACLR_STATUS_BAD_IMAGE, ZACLR_STATUS_CATEGORY_EXEC);
            }

            for (clause_index = 0u; clause_index < clause_count; ++clause_index)
            {
                const uint8_t* clause = section + data_offset + (clause_index * clause_size);
                clauses[clause_index].flags = read_u16(clause + 0u);
                clauses[clause_index].try_offset = read_u16(clause + 2u);
                clauses[clause_index].try_length = clause[4u];
                clauses[clause_index].handler_offset = read_u16(clause + 5u);
                clauses[clause_index].handler_length = clause[7u];
                clauses[clause_index].class_token = read_u32(clause + 8u);
            }

            return zaclr_result_ok();
        }
    }

    static struct zaclr_result parse_exception_section(const struct zaclr_loaded_assembly* assembly,
                                                       const uint8_t* body,
                                                       uint32_t header_size_bytes,
                                                       uint32_t il_size,
                                                       struct zaclr_exception_clause** out_clauses,
                                                       uint16_t* out_clause_count)
    {
        const uint8_t* section;
        uint32_t section_offset;
        uint32_t section_size;
        uint32_t clause_size;
        uint32_t clause_count;
        struct zaclr_result result;

        if (assembly == NULL || body == NULL || out_clauses == NULL || out_clause_count == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *out_clauses = NULL;
        *out_clause_count = 0u;

        section_offset = header_size_bytes + il_size;
        section_offset = (section_offset + 3u) & ~3u;
        if (section_offset >= assembly->image.image.size)
        {
            return zaclr_result_ok();
        }

        section = body + section_offset;
        if ((section[0] & 0x01u) == 0u)
        {
            return zaclr_result_ok();
        }

        section_size = (uint32_t)section[1u] | ((uint32_t)section[2u] << 8) | ((uint32_t)section[3u] << 16);
        if (section_size < 4u)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_IMAGE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        clause_size = (section[0] & 0x40u) != 0u ? 24u : 12u;
        if (((section_size - 4u) % clause_size) != 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_IMAGE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        clause_count = (section_size - 4u) / clause_size;
        if (clause_count > 0xFFFFu)
        {
            return zaclr_result_make(ZACLR_STATUS_BAD_IMAGE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = allocate_exception_clauses(clause_count, out_clauses);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        result = parse_exception_clauses(section, section_size, *out_clauses, (uint16_t)clause_count);
        if (result.status != ZACLR_STATUS_OK)
        {
            kernel_free(*out_clauses);
            *out_clauses = NULL;
            return result;
        }

        *out_clause_count = (uint16_t)clause_count;
        return zaclr_result_ok();
    }

    static struct zaclr_result initialize_frame_common(struct zaclr_engine* engine,
                                                       struct zaclr_runtime* runtime,
                                                       struct zaclr_frame* parent,
                                                       const struct zaclr_loaded_assembly* assembly,
                                                       const struct zaclr_method_desc* method,
                                                       zaclr_thread_id thread_id,
                                                       zaclr_process_id process_id,
                                                       struct zaclr_frame** out_frame)
    {
        struct zaclr_frame* frame;
        char trace_name[192];
        uint32_t body_offset;
        uint32_t argument_count;
        const uint8_t* body;
        uint8_t header;
        struct zaclr_result result;

        if (engine == NULL || runtime == NULL || assembly == NULL || method == NULL || out_frame == NULL)
        {
            return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
        }

        if (method->rva == 0u)
        {
            return zaclr_result_make(ZACLR_STATUS_NOT_FOUND, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = zaclr_pe_image_rva_to_offset(&assembly->image, method->rva, &body_offset);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        body = assembly->image.image.data + body_offset;
        header = body[0];

        frame = (struct zaclr_frame*)kernel_alloc(sizeof(struct zaclr_frame));
        if (frame == NULL)
        {
            console_write("[ZACLR][frame-create] kernel_alloc frame failed method=");
            console_write(method != NULL && method->name.text != NULL ? method->name.text : "<null>");
            console_write(" size=");
            console_write_dec((uint64_t)sizeof(struct zaclr_frame));
            console_write(" free_bytes=");
            console_write_dec((uint64_t)kernel_get_free_bytes());
            console_write("\n");
            return zaclr_result_make(ZACLR_STATUS_OUT_OF_MEMORY, ZACLR_STATUS_CATEGORY_EXEC);
        }

        *frame = {};
        frame->id = engine->next_frame_id++;
        frame->runtime = runtime;
        frame->parent = parent;
        frame->assembly = assembly;
        frame->method = method;
        frame->thread_id = thread_id;
        frame->process_id = process_id;
        if (parent != NULL)
        {
            result = zaclr_generic_context_clone(&frame->generic_context, &parent->generic_context);
            if (result.status != ZACLR_STATUS_OK)
            {
                console_write("[ZACLR][frame-create] generic_context_clone failed status=");
                console_write_dec((uint64_t)result.status);
                console_write(" category=");
                console_write_dec((uint64_t)result.category);
                console_write("\n");
                kernel_free(frame);
                return result;
            }
        }
        argument_count = method->signature.parameter_count;
        if ((method->signature.calling_convention & 0x20u) != 0u)
        {
            ++argument_count;
        }
        if (argument_count > 0xFFFFu)
        {
            kernel_free(frame);
            return zaclr_result_make(ZACLR_STATUS_BAD_METADATA, ZACLR_STATUS_CATEGORY_EXEC);
        }

        frame->argument_count = (uint16_t)argument_count;

        if ((header & 0x3u) == 0x2u)
        {
            frame->il_start = body + 1u;
            frame->il_size = (uint32_t)(header >> 2);
            frame->max_stack = 8u;
            frame->local_count = 0u;
        }
        else if ((header & 0x3u) == 0x3u)
        {
            uint16_t fat_header_flags = read_u16(body);
            uint32_t header_size_dwords = (uint32_t)((fat_header_flags >> 12) & 0x0Fu);
            uint32_t header_size_bytes = header_size_dwords * 4u;
            uint32_t local_sig_token = read_u32(body + 8u);

            if (header_size_dwords < 3u)
            {
                console_write("[ZACLR][frame-create] bad fat header size method=");
                console_write(method != NULL && method->name.text != NULL ? method->name.text : "<null>");
                console_write(" fat_flags=");
                console_write_hex64((uint64_t)fat_header_flags);
                console_write(" header_dwords=");
                console_write_dec((uint64_t)header_size_dwords);
                console_write("\n");
                kernel_free(frame);
                return zaclr_result_make(ZACLR_STATUS_BAD_IMAGE, ZACLR_STATUS_CATEGORY_EXEC);
            }

            frame->max_stack = read_u16(body + 2u);
            frame->il_size = read_u32(body + 4u);
            frame->il_start = body + header_size_bytes;
        result = parse_local_count(assembly, local_sig_token, &frame->local_count);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR][frame-create] parse_local_count failed status=");
            console_write_dec((uint64_t)result.status);
            console_write(" category=");
            console_write_dec((uint64_t)result.category);
            console_write(" method=");
            console_write(method != NULL && method->name.text != NULL ? method->name.text : "<null>");
            console_write("\n");
            kernel_free(frame);
            return result;
        }

            if ((fat_header_flags & 0x0008u) != 0u)
            {
                result = parse_exception_section(assembly,
                                                 body,
                                                 header_size_bytes,
                                                 frame->il_size,
                                                 &frame->exception_clauses,
                                                 &frame->exception_clause_count);
                if (result.status != ZACLR_STATUS_OK)
                {
                    console_write("[ZACLR][frame-create] exception section failed status=");
                    console_write_dec((uint64_t)result.status);
                    console_write(" category=");
                    console_write_dec((uint64_t)result.category);
                    console_write(" method=");
                    console_write(method != NULL && method->name.text != NULL ? method->name.text : "<null>");
                    console_write("\n");
                    kernel_free(frame);
                    return result;
                }
            }
        }
        else
        {
            console_write("[ZACLR][frame-create] unrecognized header format method=");
            console_write(method != NULL && method->name.text != NULL ? method->name.text : "<null>");
            console_write(" header=");
            console_write_hex64((uint64_t)header);
            console_write("\n");
            kernel_free(frame);
            return zaclr_result_make(ZACLR_STATUS_BAD_IMAGE, ZACLR_STATUS_CATEGORY_EXEC);
        }

        result = allocate_stack_values(frame->argument_count, &frame->arguments);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR][frame-create] allocate arguments failed count=");
            console_write_dec((uint64_t)frame->argument_count);
            console_write(" status=");
            console_write_dec((uint64_t)result.status);
            console_write(" category=");
            console_write_dec((uint64_t)result.category);
            console_write("\n");
            kernel_free(frame);
            return result;
        }

        result = allocate_stack_values(frame->local_count, &frame->locals);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR][frame-create] allocate locals failed count=");
            console_write_dec((uint64_t)frame->local_count);
            console_write(" status=");
            console_write_dec((uint64_t)result.status);
            console_write(" category=");
            console_write_dec((uint64_t)result.category);
            console_write("\n");
            if (frame->arguments != NULL)
            {
                kernel_free(frame->arguments);
            }
            kernel_free(frame);
            return result;
        }

        result = zaclr_eval_stack_initialize(&frame->eval_stack, frame->max_stack);
        if (result.status != ZACLR_STATUS_OK)
        {
            console_write("[ZACLR][frame-create] eval stack init failed max_stack=");
            console_write_dec((uint64_t)frame->max_stack);
            console_write(" status=");
            console_write_dec((uint64_t)result.status);
            console_write(" category=");
            console_write_dec((uint64_t)result.category);
            console_write("\n");
            if (frame->locals != NULL)
            {
                kernel_free(frame->locals);
            }
            if (frame->arguments != NULL)
            {
                kernel_free(frame->arguments);
            }
            kernel_free(frame);
            return result;
        }

        ZACLR_TRACE_VALUE(runtime,
                          ZACLR_TRACE_CATEGORY_EXEC,
                          ZACLR_TRACE_EVENT_FRAME_PUSH,
                          format_trace_method_name(assembly, method, trace_name, sizeof(trace_name)),
                          (uint64_t)frame->id);
        ZACLR_TRACE_VALUE(runtime,
                          ZACLR_TRACE_CATEGORY_EXEC,
                          ZACLR_TRACE_EVENT_METHOD_ENTER,
                          format_trace_method_name(assembly, method, trace_name, sizeof(trace_name)),
                          (uint64_t)method->id);
        ZACLR_TRACE_VALUE(runtime,
                          ZACLR_TRACE_CATEGORY_EXEC,
                          ZACLR_TRACE_EVENT_METHOD_RVA,
                          format_trace_method_name(assembly, method, trace_name, sizeof(trace_name)),
                          (uint64_t)method->rva);
        if (frame->il_start != NULL && frame->il_size > 0u)
        {
            ZACLR_TRACE_VALUE(runtime,
                              ZACLR_TRACE_CATEGORY_EXEC,
                              ZACLR_TRACE_EVENT_OPCODE_RAW,
                              format_trace_method_name(assembly, method, trace_name, sizeof(trace_name)),
                              (uint64_t)frame->il_start[0]);
            if (method->token.raw == 0x06000A4Bu)
            {
                uint32_t dump_count = frame->il_size < 16u ? frame->il_size : 16u;
                console_write("[ZACLR][frame] Type.cctor il_bytes=");
                for (uint32_t index = 0u; index < dump_count; ++index)
                {
                    console_write_hex64((uint64_t)frame->il_start[index]);
                    console_write(index + 1u < dump_count ? " " : "");
                }
                console_write("\n");
            }
        }

        *out_frame = frame;
        return zaclr_result_ok();
    }
}

extern "C" struct zaclr_result zaclr_frame_create_root(struct zaclr_engine* engine,
                                                        struct zaclr_runtime* runtime,
                                                        struct zaclr_launch_state* launch_state,
                                                        struct zaclr_frame** out_frame)
{
    struct zaclr_result result;

    if (launch_state == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    result = initialize_frame_common(engine,
                                     runtime,
                                     NULL,
                                     launch_state->assembly,
                                     launch_state->entry_method,
                                     launch_state->thread.id,
                                     launch_state->process.id,
                                     out_frame);
    if (result.status == ZACLR_STATUS_OK && out_frame != NULL && *out_frame != NULL)
    {
        launch_state->thread.current_frame = (*out_frame)->id;
        launch_state->thread.state = ZACLR_THREAD_STATE_RUNNING;
    }

    return result;
}

extern "C" struct zaclr_result zaclr_frame_create_child(struct zaclr_engine* engine,
                                                          struct zaclr_runtime* runtime,
                                                          struct zaclr_frame* parent,
                                                         const struct zaclr_loaded_assembly* assembly,
                                                         const struct zaclr_method_desc* method,
                                                         struct zaclr_frame** out_frame)
{
    return initialize_frame_common(engine,
                                    runtime,
                                    parent,
                                    assembly,
                                    method,
                                    parent != NULL ? parent->thread_id : runtime->boot_launch.thread.id,
                                    parent != NULL ? parent->process_id : runtime->boot_launch.process.id,
                                     out_frame);
}

extern "C" struct zaclr_result zaclr_frame_bind_arguments(struct zaclr_frame* frame,
                                                             struct zaclr_eval_stack* caller_stack)
{
    uint32_t argument_index;

    if (frame == NULL || caller_stack == NULL)
    {
        return zaclr_result_make(ZACLR_STATUS_INVALID_ARGUMENT, ZACLR_STATUS_CATEGORY_EXEC);
    }

    if (false && frame->method != NULL
        && frame->method->name.text != NULL
        && frame->assembly != NULL
        && frame->assembly->assembly_name.text != NULL
        && text_equals(frame->assembly->assembly_name.text, "System.Private.CoreLib")
        && text_equals(frame->method->name.text, "Equals"))
    {
        console_write("[ZACLR][bind-equals] owner_token=");
        console_write_hex64((uint64_t)frame->method->owning_type_token.raw);
        console_write(" method_token=");
        console_write_hex64((uint64_t)frame->method->token.raw);
        console_write(" callconv=");
        console_write_hex64((uint64_t)frame->method->signature.calling_convention);
        console_write(" param_count=");
        console_write_dec((uint64_t)frame->method->signature.parameter_count);
        console_write(" method_flags=");
        console_write_hex64((uint64_t)frame->method->method_flags);
        console_write(" frame_arg_count=");
        console_write_dec((uint64_t)frame->argument_count);
        console_write(" caller_depth=");
        console_write_dec((uint64_t)caller_stack->depth);
        console_write("\n");
    }

    if (caller_stack->depth < frame->argument_count)
    {
        return zaclr_result_make(ZACLR_STATUS_DISPATCH_ERROR, ZACLR_STATUS_CATEGORY_EXEC);
    }

    for (argument_index = frame->argument_count; argument_index > 0u; --argument_index)
    {
        struct zaclr_result result = zaclr_eval_stack_pop(caller_stack, &frame->arguments[argument_index - 1u]);
        if (result.status != ZACLR_STATUS_OK)
        {
            return result;
        }

        if (frame->arguments[argument_index - 1u].kind == ZACLR_STACK_VALUE_LOCAL_ADDRESS)
        {
            uint32_t local_index = (uint32_t)frame->arguments[argument_index - 1u].data.raw;
            if (frame->parent != NULL && local_index < frame->parent->local_count)
            {
                frame->arguments[argument_index - 1u].data.raw = (uintptr_t)&frame->parent->locals[local_index];
            }
        }
    }

    return zaclr_result_ok();
}

extern "C" void zaclr_frame_destroy(struct zaclr_frame* frame)
{
        if (frame != NULL)
    {
        uint32_t index;

        zaclr_eval_stack_destroy(&frame->eval_stack);
        if (frame->exception_clauses != NULL)
        {
            kernel_free(frame->exception_clauses);
        }
        zaclr_generic_context_reset(&frame->generic_context);
        if (frame->locals != NULL)
        {
            for (index = 0u; index < frame->local_count; ++index)
            {
                zaclr_stack_value_reset(&frame->locals[index]);
            }

            kernel_free(frame->locals);
        }
        if (frame->arguments != NULL)
        {
            for (index = 0u; index < frame->argument_count; ++index)
            {
                zaclr_stack_value_reset(&frame->arguments[index]);
            }

            kernel_free(frame->arguments);
        }
        kernel_free(frame);
    }
}

extern "C" uint32_t zaclr_frame_flags(const struct zaclr_frame* frame)
{
    return frame != NULL ? frame->flags : 0u;
}
