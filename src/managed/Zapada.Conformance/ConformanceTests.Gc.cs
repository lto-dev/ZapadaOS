/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.Gc.cs
 *
 * Garbage collector conformance tests.
 *
 * These tests exercise the Zapada mark-and-sweep GC through the conformance
 * assembly's own native bridge so failures in System.Private.CoreLib do not
 * mask runtime GC issues.
 *
 *   InternalCalls.GcCollect()                -> void trigger collection
 *   InternalCalls.GcGetTotalMemory(bool)     -> long live heap bytes
 *   InternalCalls.GcGetFreeBytes()           -> int  free heap bytes
 *   InternalCalls.GcPin(object)              -> void pin object root
 *   InternalCalls.GcUnpin(object)            -> void unpin root
 *
 * Gap #5 (GC mark phase does not walk interpreter frames) impact:
 * --------------------------------------------------------------------
 * The GC mark phase currently only walks pinned roots (s_pins[]).  It does
 * NOT scan the interpreter's eval stack or frame locals.  This means any
 * managed object referenced only from a local variable will be incorrectly
 * collected when GcCollect() is called.
 *
 * Test design to work correctly under Gap #5:
 *   - Objects we intend to USE after collection MUST be pinned.
 *   - Objects we want to COLLECT should have NO references from pinned roots
 *     and no pin of their own.  They will be reclaimed.
 *   - Pinned objects must be unpinned after the test is done to avoid pin
 *     table exhaustion (limit = CLR_GC_MAX_PINS = 64).
 *
 * Conservative object payload scan:
 *   Pinned objects' payload is conservatively scanned — any 8-byte-aligned
 *   word in the payload that looks like a slab pointer keeps the pointed-to
 *   object alive.  So objects reachable from pinned objects are also alive.
 *
 * Reference: CLONES/runtime/src/mono/mono/sgen/sgen-gc.c (SGen GC)
 *            CLONES/runtime/src/coreclr/gc/ (CoreCLR GC)
 *            src/kernel/clr/gc.c (Zapada mark-and-sweep implementation)
 */


namespace Zapada.Conformance;

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Zapada.Conformance.Runtime;

/*
 * Local GC facade — wraps the conformance-native GC methods with
 * shorter names for test readability.
 */
//internal static class GC
//{
//    //internal static void Collect()         => InternalCalls.GcCollect();
//    //internal static long LiveBytes()       => InternalCalls.GcGetTotalMemory(false);
//    internal static int FreeBytes()        => InternalCalls.GcGetFreeBytes();
//}

internal static partial class ConformanceTests
{
    private sealed class FinalizableProbe
    {
        internal static int FinalizedCount;

        ~FinalizableProbe()
        {
            FinalizedCount = FinalizedCount + 1;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void AllocateFinalizableProbe(bool suppressFinalize, bool reregister)
    {
        FinalizableProbe probe = new FinalizableProbe();
        if (suppressFinalize)
        {
            GC.SuppressFinalize(probe);
        }

        if (reregister)
        {
            GC.ReRegisterForFinalize(probe);
        }
    }

    /*
     * TestGcBasic
     *
     * Verifies that:
     * 1. GcLiveBytes() + GcFreeBytes() is approximately the slab size (positive)
     * 2. Allocating objects increases live bytes / decreases free bytes.
     * 3. Collecting reclaims unpinned objects (free bytes increase).
     * 4. Pinned objects survive collection unchanged.
     *
     * The test uses ExTypeA (defined in ConformanceTests.ExceptionType.cs) for
     * allocations since it has a Code int field usable as a sentinel value.
     */
    internal static void TestGcBasic()
    {
        /*
         * Test 1: heap consistency check.
         * live + free should equal the total slab size (1 MiB = 1,048,576 bytes).
         * After many allocations, the numbers should still sum correctly.
         */
        int live0  = (int)InternalCalls.GcGetTotalMemory(false); ;
        int free0  = InternalCalls.GcGetFreeBytes();
        int total0 = live0 + free0;

        /* Total should be > 0 and < 2 MiB (sanity check) */
        if (total0 > 0 && total0 < 2 * 1024 * 1024)
            Pass("[PASS] GC heap total bytes is sane");
        else
            Fail("[FAIL] GC heap total bytes is sane");

        /*
         * Test 2: allocation decreases free bytes.
         * Allocate one ExTypeA object and check free bytes decreased.
         */
        int free_before = InternalCalls.GcGetFreeBytes();
        ExTypeA obj1 = new ExTypeA();
        obj1.Code = 0xAA;
        InternalCalls.GcPin(obj1);   /* pin so it survives any implicit collection */

        int free_after_alloc = InternalCalls.GcGetFreeBytes();

        if (free_after_alloc < free_before)
            Pass("[PASS] GC alloc decreases free bytes");
        else
            Fail("[FAIL] GC alloc decreases free bytes");

        /*
         * Test 3: unpinned objects are reclaimed.
         * Allocate a second object WITHOUT pinning.  Collect.
         * Free bytes should increase (the second object is reclaimed).
         */
        int free_before_collect = InternalCalls.GcGetFreeBytes();

        {
            /* Scope: after this block, obj2 is only referenced from the
             * interpreter frame (local variable), which the GC does not scan.
             * So after GcCollect(), obj2's memory is reclaimed. */
            ExTypeA obj2 = new ExTypeA();
            obj2.Code = 0xBB;
            /* NOT pinned — will be reclaimed */
        }

        InternalCalls.GcCollect();   /* reclaim obj2 */

        int free_after_collect = InternalCalls.GcGetFreeBytes();

        if (free_after_collect > free_before_collect)
            Pass("[PASS] GC collect reclaims unpinned objects");
        else
            Fail("[FAIL] GC collect reclaims unpinned objects");

        /*
         * Test 4: pinned object survives collection.
         * obj1 was pinned before the collect.  Verify it is still valid.
         */
        if (obj1 != null && obj1.Code == 0xAA)
            Pass("[PASS] GC pinned object survives collection");
        else
            Fail("[FAIL] GC pinned object survives collection");

        /* Unpin obj1 to release the pin table slot. */
        InternalCalls.GcUnpin(obj1);

        /*
         * Test 5: second collection after unpin.
         * After unpinning obj1, another collection should reclaim it.
         * (obj1 local still holds the pointer value, but the GC doesn't see it)
         */
        InternalCalls.GcCollect();

        int free_after_second_collect = InternalCalls.GcGetFreeBytes();

        /* free bytes should be back to approximately where they were before
         * the first allocation (all objects reclaimed). */
        if (free_after_second_collect >= free_before)
            Pass("[PASS] GC second collect after unpin reclaims object");
        else
            Fail("[FAIL] GC second collect after unpin reclaims object");

        /*
         * Test 6: multiple collection cycles.
         * Verify the GC can run multiple cycles without corruption.
         */
        {
            int i;
            for (i = 0; i < 5; i++) {
                InternalCalls.GcCollect();
            }
        }
        Pass("[PASS] GC multiple collection cycles stable");

        /*
         * Test 7: heap size consistency after all collections.
         * live + free should still sum to the same total as at the start.
         */
        int live_end  = (int)InternalCalls.GcGetTotalMemory(false);
        int free_end  = InternalCalls.GcGetFreeBytes();
        int total_end = live_end + free_end;

        if (total_end == total0)
            Pass("[PASS] GC heap total bytes consistent after cycles");
        else
            Fail("[FAIL] GC heap total bytes consistent after cycles");
    }

    internal static void TestGcHandleBasics()
    {
        object value = new ExTypeA();
        GCHandle handle = GCHandle.Alloc(value, GCHandleType.Normal);
 
        if (handle.IsAllocated && handle.Target == value)
            Pass("[PASS] GCHandle normal alloc/target");
        else
            Fail("[FAIL] GCHandle normal alloc/target");
 
        object replacement = new ExTypeA();
        handle.Target = replacement;
        if (handle.Target == replacement)
            Pass("[PASS] GCHandle target set");
        else
            Fail("[FAIL] GCHandle target set");
 
        object previous = handle.Target!;
        object replacement2 = new ExTypeA();
        handle.Target = replacement2;
        if (previous == replacement && handle.Target == replacement2)
            Pass("[PASS] GCHandle repeated target exchange");
        else
            Fail("[FAIL] GCHandle repeated target exchange");
 
        System.Type typeA1 = typeof(ExTypeA);
        System.Type typeA2 = typeof(ExTypeA);
        GCHandle typeHandle = GCHandle.Alloc(typeA1, GCHandleType.Normal);
        if (typeHandle.IsAllocated && object.ReferenceEquals(typeHandle.Target, typeA1) && object.ReferenceEquals(typeA2, typeA1))
            Pass("[PASS] GCHandle works with RuntimeType handles");
        else
            Fail("[FAIL] GCHandle works with RuntimeType handles");
 
        typeHandle.Target = typeA2;
        if (object.ReferenceEquals(typeHandle.Target, typeA2))
            Pass("[PASS] GCHandle retargets same RuntimeType cache object");
        else
            Fail("[FAIL] GCHandle retargets same RuntimeType cache object");
 
        typeHandle.Free();
        handle.Free();
        Pass("[PASS] GCHandle free");
    }
internal static void TestWeakHandleBasic()
    {
        ExTypeA value = new ExTypeA();
        WeakGCHandle<ExTypeA> weak = new WeakGCHandle<ExTypeA>(value, false);

        if (weak.TryGetTarget(out ExTypeA? targetBeforeCollect) && targetBeforeCollect == value)
            Pass("[PASS] WeakGCHandle target visible while alive");
        else
            Fail("[FAIL] WeakGCHandle target visible while alive");

        targetBeforeCollect = null;
        value = null!;
        InternalCalls.GcCollect();

        if (!weak.TryGetTarget(out ExTypeA? targetAfterCollect) || targetAfterCollect == null)
            Pass("[PASS] WeakGCHandle clears after collect");
        else
            Fail("[FAIL] WeakGCHandle clears after collect");

        weak.Dispose();
    }

    internal static void TestFinalizationBasics()
    {
        FinalizableProbe.FinalizedCount = 0;
        AllocateFinalizableProbe(false, false);
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        if (FinalizableProbe.FinalizedCount > 0)
            Pass("[PASS] finalizer runs for unreachable object");
        else
            Fail("[FAIL] finalizer runs for unreachable object");
    }

    internal static void TestSuppressFinalize()
    {
        FinalizableProbe.FinalizedCount = 0;
        AllocateFinalizableProbe(true, false);
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        if (FinalizableProbe.FinalizedCount == 0)
            Pass("[PASS] suppress finalize prevents finalizer run");
        else
            Fail("[FAIL] suppress finalize prevents finalizer run");
    }

    internal static void TestReRegisterForFinalize()
    {
        FinalizableProbe.FinalizedCount = 0;
        AllocateFinalizableProbe(true, true);
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();

        if (FinalizableProbe.FinalizedCount > 0)
            Pass("[PASS] reregister for finalize restores finalizer run");
        else
            Fail("[FAIL] reregister for finalize restores finalizer run");
    }
}


