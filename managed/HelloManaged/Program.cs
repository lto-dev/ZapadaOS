/*
 * Zapada - managed/HelloManaged/Program.cs
 *
 * Phase 2A + Phase 3A managed entry point.
 *
 * This assembly is compiled to standard .NET IL and embedded in the Zapada
 * kernel binary.  The Zapada PE loader reads the ECMA-335 metadata to locate
 * the entry-point method body (Program.Main), and the CIL interpreter
 * executes it.
 *
 * Phase 2A opcodes exercised:
 *   ldstr (0x72), call (0x28), ret (0x2A)
 *   ldc.i4.N (0x16-0x1E)
 *   ldloc.N/stloc.N (0x06-0x0D)
 *   ldarg.N (0x02-0x05)
 *   add (0x58)
 *   blt.s (0x32), br.s (0x2B)
 *
 * Phase 3A opcodes exercised:
 *   newobj (0x73), ldfld (0x7B), stfld (0x7D)   -- object model
 *   newarr (0x8D), ldelem.u1 (0x91), stelem.i1 (0x9C), ldlen (0x8E)  -- arrays
 *   box (0x8C), unbox.any (0xA5)                 -- boxing
 *   throw (0x7A), leave.s (0xDE)                 -- exception handling
 *
 * InternalCalls exercised:
 *   Console.Write(string)    -- write null-terminated string to console
 *   Console.WriteInt(int)    -- write signed 32-bit integer to console
 *   Console.WriteHex(int)    -- write low byte as 2-digit hex (Phase 3A)
 */

using System;
using System.Runtime.CompilerServices;

/*
 * TestPoint - a simple class with two int fields.
 * Used to test the object model opcodes: newobj, ldfld, stfld.
 */
internal class TestPoint
{
    /* Instance fields exercising stfld / ldfld */
    public int X;
    public int Y;
}

/*
 * KernelException - a user-defined exception type.
 * Used to test the throw / catch exception handling opcodes.
 * Inherits from System.Exception so that C# allows it to be thrown.
 * The base-class constructor call (System.Exception..ctor) is handled
 * as a stub no-op by the interpreter's cross-assembly resolution.
 */
internal class KernelException : System.Exception { }

/*
 * Program - entry point class.
 */
internal class Program
{
    /* ------------------------------------------------------------------ */
    /* Phase 2A helpers                                                     */
    /* ------------------------------------------------------------------ */

    /*
     * Compute - managed helper exercising ldarg, add, ret.
     */
    [MethodImpl(MethodImplOptions.NoInlining)]
    static int Compute(int a, int b)
    {
        return a + b;
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3A: Step 1.1 — object model test                              */
    /* ------------------------------------------------------------------ */

    /*
     * TestObjectFields
     *
     * Exercises: newobj, stfld, ldfld, call.
     *
     * Gate output: "[Phase3A] field X=42\n"
     */
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void TestObjectFields()
    {
        TestPoint pt = new TestPoint();
        pt.X = 42;
        pt.Y = 99;
        Console.Write("[Phase3A] field X=");
        Console.Write(pt.X);
        Console.Write("\n");
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3A: Step 1.2 — array test                                     */
    /* ------------------------------------------------------------------ */

    /*
     * TestArray
     *
     * Exercises: newarr (byte[]), stelem.i1, ldelem.u1, ldlen.
     *
     * Gate output: "[Phase3A] arr[0]=0xAB\n"
     *
     * WriteHex prints the low byte as 2 upper-case hex digits; the "0x"
     * prefix is written explicitly to keep the gate string deterministic.
     */
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void TestArray()
    {
        byte[] buf = new byte[16];
        buf[0] = 0xAB;
        Console.Write("[Phase3A] arr[0]=0x");
        Console.Write(buf[0].ToString("X2"));
        Console.Write("\n");
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3A: Step 1.3 — ldstr gate string                              */
    /* ------------------------------------------------------------------ */

    /*
     * TestLdstr
     *
     * Exercises: ldstr — the string constant "boot" must be retrieved from
     * the #US heap and printed.  This verifies ldstr decoding and the
     * null-terminated ASCII conversion in the interpreter.
     *
     * Gate output: "[Phase3A] ldstr=boot\n"
     */
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void TestLdstr()
    {
        Console.Write("[Phase3A] ldstr=boot\n");
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3A: Step 1.4 — exception handling test                        */
    /* ------------------------------------------------------------------ */

    /*
     * TestException
     *
     * Exercises: newobj, throw, leave.s, catch handler dispatch.
     *
     * The throw opcode searches the method's exception handler table for
     * a catch clause whose try range covers the throw offset.  The catch
     * body prints "[Phase3A] caught" and then leaves.
     *
     * Gate output: "[Phase3A] caught\n"
     */
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void TestException()
    {
        try
        {
            throw new KernelException();
        }
        catch (KernelException)
        {
            Console.Write("[Phase3A] caught\n");
        }
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3A: Step 1.5 — box / unbox test                               */
    /* ------------------------------------------------------------------ */

    /*
     * TestBoxUnbox
     *
     * Exercises: box (int32 -> object), unbox.any (object -> int32).
     *
     * The interpreter allocates a boxed object on the heap, stores the
     * int32 value 77 into it, then unboxes it back to int32 and prints.
     *
     * Gate output: "[Phase3A] unbox=77\n"
     */
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void TestBoxUnbox()
    {
        int n = 77;
        object boxed = (object)n;
        int unboxed = (int)boxed;
        Console.Write("[Phase3A] unbox=");
        Console.Write(unboxed);
        Console.Write("\n");
    }

    /* ------------------------------------------------------------------ */
    /* Main - entry point                                                    */
    /* ------------------------------------------------------------------ */

    /*
     * Main - Phase 2A + Phase 3A entry point.
     *
     * Phase 2A section preserved intact; Phase 3A tests appended.
     * The Phase 2A gate checks must continue to pass.
     */
    static void Main()
    {
        /* --- Phase 2A: managed call test -------------------------------- */
        Console.Write("Phase 2A: managed call test\n");
        int result = Compute(3, 7);
        Console.Write(result);
        Console.Write("\n");

        /* --- Phase 2A: loop test ---------------------------------------- */
        Console.Write("Phase 2A: loop test\n");
        int i = 0;
        while (i < 5)
        {
            Console.Write(i);
            Console.Write("\n");
            i = i + 1;
        }

        /* --- completion marker (Phase 2A) ------------------------------- */
        Console.Write("Phase 2A complete.\n");

        /* --- Phase 3A: object model ------------------------------------- */
        TestObjectFields();

        /* --- Phase 3A: array ------------------------------------------- */
        TestArray();

        /* --- Phase 3A: string literal ----------------------------------- */
        TestLdstr();

        /* --- Phase 3A: exception handling ------------------------------- */
        TestException();

        /* --- Phase 3A: box / unbox -------------------------------------- */
        TestBoxUnbox();

        /* --- Phase 3A: completion marker -------------------------------- */
        Console.Write("[Phase3A] complete\n");
    }
}

