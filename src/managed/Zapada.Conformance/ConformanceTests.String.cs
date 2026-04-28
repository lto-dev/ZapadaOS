/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.String.cs
 *
 * String indexing, path parsing, substring, and chained equality conformance tests.
 *
 * These tests exercise:
 *   - String.Length property (ldsfld on string object header)
 *   - String indexer (string[i] -> char)
 *   - Helper-method loops with string parameters
 *   - Path-parsing loops (segment counting, extension splitting)
 *   - Substring (1-arg and 2-arg overloads)
 *   - Chained boolean comparisons (string[0] == 'X' && string[1] == 'Y' ...)
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    private static class GenericArrayHolder<T>
    {
        internal static T[] GetEmpty()
        {
            return System.Array.Empty<T>();
        }
    }

    private static class GenericStaticCounter<T>
    {
        internal static int Value;
    }

    private sealed class GenericInstanceBox<T>
    {
        internal GenericInstanceBox(T value)
        {
            _value = value;
        }

        private readonly T _value;

        internal T Get()
        {
            return _value;
        }
    }

    private static class NestedGenericOwner<T>
    {
        internal static class Helper<U>
        {
            internal static int Marker;

            internal static int BumpAndGet()
            {
                Marker = Marker + 1;
                return Marker;
            }
        }
    }

    private static char GetChar(string text, int index)
    {
        return text[index];
    }

    private static T[] GetEmptyFromGenericMethod<T>()
    {
        return System.Array.Empty<T>();
    }

    private static int CountChar(string text, char value)
    {
        int count = 0;
        int i = 0;
        while (i < text.Length)
        {
            if (text[i] == value)
            {
                count = count + 1;
            }
            i = i + 1;
        }
        return count;
    }

    private static bool IsConfDllPath(string path)
    {
        int slash = 0;
        int i = 0;
        while (i < path.Length)
        {
            if (path[i] == '/')
            {
                slash = slash + 1;
            }
            i = i + 1;
        }

        return slash == 1
            && path.Length == 9
            && path[0] == '/'
            && path[1] == 'C'
            && path[2] == 'O'
            && path[3] == 'N'
            && path[4] == 'F'
            && path[5] == '.'
            && path[6] == 'D'
            && path[7] == 'L'
            && path[8] == 'L';
    }

    private static int CountSegmentSeparators(string path)
    {
        int i = 0;
        int count = 0;
        while (i < path.Length)
        {
            if (path[i] == '/')
            {
                count = count + 1;
            }
            i = i + 1;
        }
        return count;
    }

    private static void TestStringIndexingAndBranches()
    {
        string path = "/CONF.DLL";

        if (path.Length == 9) Pass("[PASS] string length path"); else Fail("[FAIL] string length path");
        if (path[0] == '/' && path[5] == '.' && path[6] == 'D') Pass("[PASS] string indexing path"); else Fail("[FAIL] string indexing path");
        if (CountChar(path, 'L') == 2) Pass("[PASS] string helper-loop count"); else Fail("[FAIL] string helper-loop count");

        bool looksDll = path.Length == 9
                     && path[1] == 'C'
                     && path[2] == 'O'
                     && path[3] == 'N'
                     && path[4] == 'F'
                     && path[5] == '.'
                     && path[6] == 'D'
                     && path[7] == 'L'
                     && path[8] == 'L';

        if (looksDll) Pass("[PASS] branch-heavy string scan"); else Fail("[FAIL] branch-heavy string scan");
    }

    private static void TestPathParsingLoops()
    {
        Test("[TEST] Start:TestPathParsingLoops");

        string conf = "/CONF.DLL";
        string nested = "/SYS/CONF.DLL";

        if (IsConfDllPath(conf)) Pass("[PASS] path parse single-segment dll"); else Fail("[FAIL] path parse single-segment dll");
        if (!IsConfDllPath(nested)) Pass("[PASS] path parse rejects nested path"); else Fail("[FAIL] path parse rejects nested path");

        if (CountSegmentSeparators(conf) == 1) Pass("[PASS] path parse separator count root"); else Fail("[FAIL] path parse separator count root");
        if (CountSegmentSeparators(nested) == 2) Pass("[PASS] path parse separator count nested"); else Fail("[FAIL] path parse separator count nested");

        Test("[TEST] End:TestPathParsingLoops");
    }

    private static void TestStringSubstringPaths()
    {
        Test("[TEST] Start:TestStringSubstringPaths");
        /*
         * Covers the exact string slicing shapes used by PathResolver.cs.
         *
         * tail path  : path.Substring(startIdx)         — 1-arg overload
         * base path  : name.Substring(0, dotIdx)        — 2-arg overload
         * ext  path  : name.Substring(dotIdx + 1)       — 1-arg overload
         */
        string path = "/BOOT/CONF.DLL";
        Test("[TEST] S1:TestStringSubstringPaths");
        string name = path.Substring(6);          /* "CONF.DLL" */
        Test("[TEST] S2:TestStringSubstringPaths");
        string basePart = name.Substring(0, 4);   /* "CONF" */
        Test("[TEST] S3:TestStringSubstringPaths");
        string extPart = name.Substring(5);       /* "DLL" */
        Test("[TEST] S4:TestStringSubstringPaths");

        if (name.Length == 8 && name[0] == 'C' && name[4] == '.')
            Pass("[PASS] substring tail path");
        else
            Fail("[FAIL] substring tail path");

        if (basePart.Length == 4 && basePart[0] == 'C' && basePart[3] == 'F')
            Pass("[PASS] substring base path");
        else
            Fail("[FAIL] substring base path");

        if (extPart.Length == 3 && extPart[0] == 'D' && extPart[2] == 'L')
            Pass("[PASS] substring ext path");
        else
            Fail("[FAIL] substring ext path");

        Test("[TEST] End:TestStringSubstringPaths");
    }

    private static void TestChainedEqualityChecks()
    {
        string path = "/CONF.DLL";
        bool exact = path.Length == 9
                  && path[0] == '/'
                  && path[1] == 'C'
                  && path[2] == 'O'
                  && path[3] == 'N'
                  && path[4] == 'F'
                  && path[5] == '.'
                  && path[6] == 'D'
                  && path[7] == 'L'
                  && path[8] == 'L';

        bool reject = path.Length == 9
                   && path[0] == '/'
                   && path[1] == 'C'
                   && path[2] == 'O'
                   && path[3] == 'N'
                   && path[4] == 'X';

        if (exact) Pass("[PASS] chained equality exact match"); else Fail("[FAIL] chained equality exact match");
        if (!reject) Pass("[PASS] chained equality reject mismatch"); else Fail("[FAIL] chained equality reject mismatch");
    }

    private static void TestUtf16LiteralAndIndexing()
    {
        string text = "AΩB";

        if (text.Length == 3) Pass("[PASS] utf16 literal length"); else Fail("[FAIL] utf16 literal length");
        if (text[0] == 'A' && text[1] == 'Ω' && text[2] == 'B') Pass("[PASS] utf16 literal indexing"); else Fail("[FAIL] utf16 literal indexing");

        string middle = text.Substring(1, 1);
        if (middle.Length == 1 && middle[0] == 'Ω') Pass("[PASS] utf16 substring preserves bmp char"); else Fail("[FAIL] utf16 substring preserves bmp char");
    }

    private static void TestCoreLibStringContractBasics()
    {
        string empty = string.Empty;
        string one = new string('Z', 1);
        string repeated = new string('Ω', 3);
        char[] chars = new char[3];
        char[] rangeChars = new char[5];
        string fromArray;
        string fromRange;

        chars[0] = GetChar("A", 0);
        chars[1] = GetChar("Ω", 0);
        chars[2] = GetChar("B", 0);
        rangeChars[0] = GetChar("x", 0);
        rangeChars[1] = GetChar("A", 0);
        rangeChars[2] = GetChar("Ω", 0);
        rangeChars[3] = GetChar("B", 0);
        rangeChars[4] = GetChar("y", 0);

        fromArray = new string(chars);
        fromRange = new string(rangeChars, 1, 3);

        if (empty.Length == 0) Pass("[PASS] corelib string.Empty length"); else Fail("[FAIL] corelib string.Empty length");
        if (one.Length == 1) Pass("[PASS] corelib char repeat ctor single length"); else Fail("[FAIL] corelib char repeat ctor single length");
        if (one.Length > 0 && one[0] == 'Z') Pass("[PASS] corelib char repeat ctor single char"); else Fail("[FAIL] corelib char repeat ctor single char");

        if (repeated.Length == 3) Pass("[PASS] corelib char repeat ctor utf16 length"); else Fail("[FAIL] corelib char repeat ctor utf16 length");
        if (repeated.Length > 2 && repeated[0] == 'Ω' && repeated[1] == 'Ω' && repeated[2] == 'Ω') Pass("[PASS] corelib char repeat ctor utf16 chars"); else Fail("[FAIL] corelib char repeat ctor utf16 chars");

        if (fromArray.Length == 3) Pass("[PASS] corelib char array ctor utf16 length"); else Fail("[FAIL] corelib char array ctor utf16 length");
        if (fromArray.Length > 2 && fromArray[0] == 'A' && fromArray[1] == 'Ω' && fromArray[2] == 'B') Pass("[PASS] corelib char array ctor utf16 chars"); else Fail("[FAIL] corelib char array ctor utf16 chars");

        if (fromRange.Length == 3) Pass("[PASS] corelib char array range ctor utf16 length"); else Fail("[FAIL] corelib char array range ctor utf16 length");
        if (fromRange.Length > 2 && fromRange[0] == 'A' && fromRange[1] == 'Ω' && fromRange[2] == 'B') Pass("[PASS] corelib char array range ctor utf16 chars"); else Fail("[FAIL] corelib char array range ctor utf16 chars");

        if (!string.IsNullOrEmpty(fromArray)) Pass("[PASS] corelib IsNullOrEmpty nonempty"); else Fail("[FAIL] corelib IsNullOrEmpty nonempty");
        if (string.IsNullOrEmpty(empty)) Pass("[PASS] corelib IsNullOrEmpty empty"); else Fail("[FAIL] corelib IsNullOrEmpty empty");
    }

    private static void TestCoreLibGenericEmptyArrayShapes()
    {
        object[] emptyObjectsA = System.Array.Empty<object>();
        object[] emptyObjectsB = System.Array.Empty<object>();
        byte[] emptyBytesA = System.Array.Empty<byte>();
        byte[] emptyBytesB = System.Array.Empty<byte>();
        char[] emptyCharsFromMethodA = GetEmptyFromGenericMethod<char>();
        char[] emptyCharsFromMethodB = GetEmptyFromGenericMethod<char>();
        int[] emptyIntsFromTypeA = GenericArrayHolder<int>.GetEmpty();
        int[] emptyIntsFromTypeB = GenericArrayHolder<int>.GetEmpty();
        string fromEmptyChars = new string(System.Array.Empty<char>());

        if (emptyObjectsA.Length == 0) Pass("[PASS] corelib Array.Empty object length"); else Fail("[FAIL] corelib Array.Empty object length");
        if ((object)emptyObjectsA == (object)emptyObjectsB) Pass("[PASS] corelib Array.Empty object singleton"); else Fail("[FAIL] corelib Array.Empty object singleton");

        if (emptyBytesA.Length == 0) Pass("[PASS] corelib Array.Empty byte length"); else Fail("[FAIL] corelib Array.Empty byte length");
        if ((object)emptyBytesA == (object)emptyBytesB) Pass("[PASS] corelib Array.Empty byte singleton"); else Fail("[FAIL] corelib Array.Empty byte singleton");

        if (emptyCharsFromMethodA.Length == 0) Pass("[PASS] corelib Array.Empty generic method length"); else Fail("[FAIL] corelib Array.Empty generic method length");
        if ((object)emptyCharsFromMethodA == (object)emptyCharsFromMethodB) Pass("[PASS] corelib Array.Empty generic method singleton"); else Fail("[FAIL] corelib Array.Empty generic method singleton");

        if (emptyIntsFromTypeA.Length == 0) Pass("[PASS] corelib Array.Empty generic type length"); else Fail("[FAIL] corelib Array.Empty generic type length");
        if ((object)emptyIntsFromTypeA == (object)emptyIntsFromTypeB) Pass("[PASS] corelib Array.Empty generic type singleton"); else Fail("[FAIL] corelib Array.Empty generic type singleton");

        if (fromEmptyChars.Length == 0) Pass("[PASS] corelib string ctor Array.Empty char length"); else Fail("[FAIL] corelib string ctor Array.Empty char length");
        if (string.IsNullOrEmpty(fromEmptyChars)) Pass("[PASS] corelib string ctor Array.Empty char empty"); else Fail("[FAIL] corelib string ctor Array.Empty char empty");
    }

    private static void TestGenericStaticFieldIsolation()
    {
        GenericStaticCounter<int>.Value = 11;
        GenericStaticCounter<char>.Value = 22;

        if (GenericStaticCounter<int>.Value == 11) Pass("[PASS] typespec generic static field int value"); else Fail("[FAIL] typespec generic static field int value");
        if (GenericStaticCounter<char>.Value == 22) Pass("[PASS] typespec generic static field char value"); else Fail("[FAIL] typespec generic static field char value");
        if (GenericStaticCounter<int>.Value != GenericStaticCounter<char>.Value) Pass("[PASS] typespec generic static field isolation"); else Fail("[FAIL] typespec generic static field isolation");
    }

    private static void TestGenericStaticMethodOwnerResolution()
    {
        int[] intsA = GenericArrayHolder<int>.GetEmpty();
        int[] intsB = GenericArrayHolder<int>.GetEmpty();
        char[] chars = GenericArrayHolder<char>.GetEmpty();

        if (intsA.Length == 0) Pass("[PASS] typespec generic static method int length"); else Fail("[FAIL] typespec generic static method int length");
        if ((object)intsA == (object)intsB) Pass("[PASS] typespec generic static method int singleton"); else Fail("[FAIL] typespec generic static method int singleton");
        if (chars.Length == 0) Pass("[PASS] typespec generic static method char length"); else Fail("[FAIL] typespec generic static method char length");
    }

    private static void TestGenericInstanceMethodOwnerResolution()
    {
        GenericInstanceBox<int> intBox = new GenericInstanceBox<int>(7);
        GenericInstanceBox<char> charBox = new GenericInstanceBox<char>('Q');

        if (intBox.Get() == 7) Pass("[PASS] typespec generic instance method int"); else Fail("[FAIL] typespec generic instance method int");
        if (charBox.Get() == 'Q') Pass("[PASS] typespec generic instance method char"); else Fail("[FAIL] typespec generic instance method char");
    }

    private static void TestNestedGenericOwnerIdentity()
    {
        int firstIntChar = NestedGenericOwner<int>.Helper<char>.BumpAndGet();
        int secondIntChar = NestedGenericOwner<int>.Helper<char>.BumpAndGet();
        int firstCharInt = NestedGenericOwner<char>.Helper<int>.BumpAndGet();

        if (firstIntChar == 1) Pass("[PASS] typespec nested generic first int-char"); else Fail("[FAIL] typespec nested generic first int-char");
        if (secondIntChar == 2) Pass("[PASS] typespec nested generic second int-char"); else Fail("[FAIL] typespec nested generic second int-char");
        if (firstCharInt == 1) Pass("[PASS] typespec nested generic owner isolation"); else Fail("[FAIL] typespec nested generic owner isolation");
    }
}


