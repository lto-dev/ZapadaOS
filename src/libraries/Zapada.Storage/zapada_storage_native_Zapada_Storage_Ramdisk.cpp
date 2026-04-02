/*
 * Zapada - src/libraries/Zapada.Storage/zapada_storage_native_Zapada_Storage_Ramdisk.cpp
 */

#include "ZapadaStorage.h"

extern "C" void console_write(const char *str);
extern "C" uint32_t ramdisk_file_count(void);
extern "C" ramdisk_status_t ramdisk_get_file(uint32_t index, ramdisk_file_t *file);

namespace
{
    static int32_t zapada_storage_ramdisk_lookup_index(const char *filename)
    {
        uint32_t count = ramdisk_file_count();
        uint32_t i;

        if (filename == NULL)
        {
            return -1;
        }

        for (i = 0u; i < count; i++)
        {
            ramdisk_file_t file;
            const char *a;
            const char *b;

            if (ramdisk_get_file(i, &file) != RAMDISK_OK)
            {
                continue;
            }

            a = file.filename;
            b = filename;

            while (*a != '\0' && *b != '\0' && *a == *b)
            {
                a++;
                b++;
            }

            if (*a == '\0' && *b == '\0')
            {
                return (int32_t)i;
            }
        }

        return -1;
    }
}

HRESULT Library_zapada_storage_native_Zapada_Storage_Ramdisk::FileCount___STATIC__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    stack.SetResult_I4((int32_t)ramdisk_file_count());
    NANOCLR_NOCLEANUP_NOLABEL();
}

HRESULT Library_zapada_storage_native_Zapada_Storage_Ramdisk::Lookup___STATIC__I4__STRING(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();
    const char *filename = stack.Arg0().RecoverString();
    stack.SetResult_I4(zapada_storage_ramdisk_lookup_index(filename));
    NANOCLR_NOCLEANUP_NOLABEL();
}

HRESULT Library_zapada_storage_native_Zapada_Storage_Ramdisk::Read___STATIC__I4__I4__SZARRAY_U1__I4__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();

    int32_t fileIndex = stack.Arg0().NumericByRefConst().s4;
    CLR_RT_HeapBlock_Array *buffer = stack.Arg1().DereferenceArray();
    int32_t offset = stack.Arg2().NumericByRefConst().s4;
    int32_t count = stack.Arg3().NumericByRefConst().s4;
    ramdisk_file_t file;
    uint8_t *dst;
    uint32_t available;
    uint32_t toCopy;
    uint32_t i;

    if (fileIndex < 0 || buffer == NULL || offset < 0 || count < 0)
    {
        stack.SetResult_I4(-1);
        NANOCLR_NOCLEANUP_NOLABEL();
    }

    if (ramdisk_get_file((uint32_t)fileIndex, &file) != RAMDISK_OK)
    {
        stack.SetResult_I4(-1);
        NANOCLR_NOCLEANUP_NOLABEL();
    }

    if ((uint32_t)offset + (uint32_t)count > buffer->m_numOfElements)
    {
        stack.SetResult_I4(-1);
        NANOCLR_NOCLEANUP_NOLABEL();
    }

    dst = (uint8_t *)buffer->GetFirstElement();
    available = file.size;
    toCopy = (uint32_t)count < available ? (uint32_t)count : available;

    for (i = 0u; i < toCopy; i++)
    {
        dst[(uint32_t)offset + i] = file.data[i];
    }

    stack.SetResult_I4((int32_t)toCopy);

    NANOCLR_NOCLEANUP_NOLABEL();
}

HRESULT Library_zapada_storage_native_Zapada_Storage_Ramdisk::GetFileName___STATIC__STRING__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();

    int32_t fileIndex = stack.Arg0().NumericByRefConst().s4;
    ramdisk_file_t file;

    if (fileIndex < 0 || ramdisk_get_file((uint32_t)fileIndex, &file) != RAMDISK_OK)
    {
        stack.SetResult_Object(NULL);
        NANOCLR_NOCLEANUP_NOLABEL();
    }

    NANOCLR_SET_AND_LEAVE(stack.SetResult_String(file.filename));

    NANOCLR_CLEANUP();
    stack.SetResult_Object(NULL);
    NANOCLR_CLEANUP_END();
}

HRESULT Library_zapada_storage_native_Zapada_Storage_Ramdisk::GetFileSize___STATIC__I4__I4(CLR_RT_StackFrame &stack)
{
    NANOCLR_HEADER();

    int32_t fileIndex = stack.Arg0().NumericByRefConst().s4;
    ramdisk_file_t file;

    if (fileIndex < 0 || ramdisk_get_file((uint32_t)fileIndex, &file) != RAMDISK_OK)
    {
        stack.SetResult_I4(0);
        NANOCLR_NOCLEANUP_NOLABEL();
    }

    stack.SetResult_I4((int32_t)file.size);
    NANOCLR_NOCLEANUP_NOLABEL();
}
