/*
 * Zapada - src/managed/Zapada.Conformance/ConformanceTests.StringBcl.cs
 *
 * BCL string method InternalCall conformance tests.
 *
 * Tests the arity-aware InternalCall dispatch for:
 *   String.StartsWith  (HASTHIS, 1-arg:  this + prefix)
 *   String.EndsWith    (HASTHIS, 1-arg:  this + suffix)
 *   String.Contains    (HASTHIS, 1-arg:  this + value)
 *   String.Compare     (static,  2-arg:  strA + strB)
 *   String.ToUpper     (HASTHIS, 0-arg:  this)
 *   String.ToLower     (HASTHIS, 0-arg:  this)
 *   String.Trim        (HASTHIS, 0-arg:  this)
 *   String.Replace     (HASTHIS, 2-arg:  this + oldValue + newValue)
 */

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    /*
     * TestStringStartsWith - System.String.StartsWith
     *
     * Verifies that the HASTHIS InternalCall dispatches correctly for the
     * 1-arg instance overload (this + prefix = 2 stack slots).
     */
    private static void TestStringStartsWith()
    {
        string s = "hello world";
        if (s.StartsWith("hello"))    Pass("[PASS] StartsWith hello-in-hello-world");    else Fail("[FAIL] StartsWith hello-in-hello-world");
        if (!s.StartsWith("world"))   Pass("[PASS] StartsWith world-not-prefix");        else Fail("[FAIL] StartsWith world-not-prefix");
        if (s.StartsWith(""))         Pass("[PASS] StartsWith empty-prefix");            else Fail("[FAIL] StartsWith empty-prefix");
        if (!s.StartsWith("HELLO"))   Pass("[PASS] StartsWith case-sensitive reject");   else Fail("[FAIL] StartsWith case-sensitive reject");
    }

    /*
     * TestStringEndsWith - System.String.EndsWith
     */
    private static void TestStringEndsWith()
    {
        string s = "hello world";
        if (s.EndsWith("world"))      Pass("[PASS] EndsWith world-suffix");              else Fail("[FAIL] EndsWith world-suffix");
        if (!s.EndsWith("hello"))     Pass("[PASS] EndsWith hello-not-suffix");          else Fail("[FAIL] EndsWith hello-not-suffix");
        if (s.EndsWith(""))           Pass("[PASS] EndsWith empty-suffix");              else Fail("[FAIL] EndsWith empty-suffix");
        if (s.EndsWith("o world"))    Pass("[PASS] EndsWith partial suffix");            else Fail("[FAIL] EndsWith partial suffix");
    }

    /*
     * TestStringContains - System.String.Contains
     */
    private static void TestStringContains()
    {
        string s = "hello world";
        if (s.Contains("lo wo"))      Pass("[PASS] Contains middle segment");            else Fail("[FAIL] Contains middle segment");
        if (s.Contains("hello"))      Pass("[PASS] Contains prefix segment");            else Fail("[FAIL] Contains prefix segment");
        if (s.Contains("world"))      Pass("[PASS] Contains suffix segment");            else Fail("[FAIL] Contains suffix segment");
        if (!s.Contains("xyz"))       Pass("[PASS] Contains absent rejects");            else Fail("[FAIL] Contains absent rejects");
        if (s.Contains(""))           Pass("[PASS] Contains empty always true");         else Fail("[FAIL] Contains empty always true");
    }

    /*
     * TestStringCompare - System.String.Compare (static, ordinal)
     *
     * Compare(a,b) < 0 when a < b, 0 when equal, > 0 when a > b.
     */
    private static void TestStringCompare()
    {
        int eq  = string.Compare("abc", "abc");
        int lt  = string.Compare("abc", "abd");
        int gt  = string.Compare("abd", "abc");

        if (eq == 0)  Pass("[PASS] Compare equal=0");        else Fail("[FAIL] Compare equal=0");
        if (lt < 0)   Pass("[PASS] Compare less<0");         else Fail("[FAIL] Compare less<0");
        if (gt > 0)   Pass("[PASS] Compare greater>0");      else Fail("[FAIL] Compare greater>0");
    }

    /*
     * TestStringToUpper - System.String.ToUpper
     */
    private static void TestStringToUpper()
    {
        string s = "hello";
        string up = s.ToUpper();
        if (up.Length == 5
            && up[0] == 'H' && up[1] == 'E' && up[2] == 'L'
            && up[3] == 'L' && up[4] == 'O')
            Pass("[PASS] ToUpper hello->HELLO");
        else
            Fail("[FAIL] ToUpper hello->HELLO");

        string mixed = "Hello World";
        string upMixed = mixed.ToUpper();
        if (upMixed.Length == 11
            && upMixed[0] == 'H' && upMixed[6] == 'W')
            Pass("[PASS] ToUpper mixed");
        else
            Fail("[FAIL] ToUpper mixed");
    }

    /*
     * TestStringToLower - System.String.ToLower
     */
    private static void TestStringToLower()
    {
        string s = "HELLO";
        string lo = s.ToLower();
        if (lo.Length == 5
            && lo[0] == 'h' && lo[1] == 'e' && lo[2] == 'l'
            && lo[3] == 'l' && lo[4] == 'o')
            Pass("[PASS] ToLower HELLO->hello");
        else
            Fail("[FAIL] ToLower HELLO->hello");

        string mixed = "Hello World";
        string loMixed = mixed.ToLower();
        if (loMixed.Length == 11
            && loMixed[0] == 'h' && loMixed[6] == 'w')
            Pass("[PASS] ToLower mixed");
        else
            Fail("[FAIL] ToLower mixed");
    }

    /*
     * TestStringTrim - System.String.Trim
     */
    private static void TestStringTrim()
    {
        string padded = "  hello  ";
        string trimmed = padded.Trim();
        if (trimmed.Length == 5
            && trimmed[0] == 'h' && trimmed[4] == 'o')
            Pass("[PASS] Trim leading+trailing spaces");
        else
            Fail("[FAIL] Trim leading+trailing spaces");

        string nopad = "hello";
        string notrimmed = nopad.Trim();
        if (notrimmed.Length == 5 && notrimmed[0] == 'h')
            Pass("[PASS] Trim no whitespace noop");
        else
            Fail("[FAIL] Trim no whitespace noop");
    }

    /*
     * TestStringReplace - System.String.Replace(string, string)
     */
    private static void TestStringReplace()
    {
        string s = "hello world";
        string r = s.Replace("world", "Zapada");
        if (r.Length == 12
            && r[6] == 'Z' && r[7] == 'a' && r[8] == 'p'
            && r[9] == 'a' && r[10] == 'd' && r[11] == 'a')
            Pass("[PASS] Replace world->Zapada");
        else
            Fail("[FAIL] Replace world->Zapada");

        string s2 = "aababc";
        string r2 = s2.Replace("ab", "X");
        if (r2.Length == 4 && r2[0] == 'a' && r2[1] == 'X' && r2[2] == 'X' && r2[3] == 'c')
            Pass("[PASS] Replace overlapping pattern");
        else
            Fail("[FAIL] Replace overlapping pattern");

        string noMatch = "hello";
        string r3 = noMatch.Replace("xyz", "!");
        if (r3.Length == 5 && r3[0] == 'h')
            Pass("[PASS] Replace no-match noop");
        else
            Fail("[FAIL] Replace no-match noop");
    }

    private static void TestUtf16BclBehaviors()
    {
        string left = "Ω";
        string right = "β";
        string joined = string.Concat(left, right);
        int compareEq = string.Compare(joined, "Ωβ");
        int compareLt = string.Compare("A", "Ω");

        if (joined.Length == 2 && joined[0] == 'Ω' && joined[1] == 'β')
            Pass("[PASS] utf16 concat preserves bmp chars");
        else
            Fail("[FAIL] utf16 concat preserves bmp chars");

        if (compareEq == 0)
            Pass("[PASS] utf16 compare equal ordinal");
        else
            Fail("[FAIL] utf16 compare equal ordinal");

        if (compareLt < 0)
            Pass("[PASS] utf16 compare ordering");
        else
            Fail("[FAIL] utf16 compare ordering");
    }

    private static void TestCoreLibStringBehaviorShapes()
    {
        string text = "AΩBΩC";
        string concat = string.Concat("AΩ", "BΩ");
        int indexOmega = text.IndexOf('Ω');
        bool containsUtf16 = text.Contains("ΩB");
        string tail = text.Substring(1);
        string slice = text.Substring(1, 3);

        if (concat.Length == 4 && concat[0] == 'A' && concat[1] == 'Ω' && concat[2] == 'B' && concat[3] == 'Ω')
            Pass("[PASS] corelib concat utf16 shape");
        else
            Fail("[FAIL] corelib concat utf16 shape");

        if (indexOmega == 1)
            Pass("[PASS] corelib IndexOf char utf16");
        else
            Fail("[FAIL] corelib IndexOf char utf16");

        if (containsUtf16)
            Pass("[PASS] corelib Contains string utf16");
        else
            Fail("[FAIL] corelib Contains string utf16");

        if (tail.Length == 4 && tail[0] == 'Ω' && tail[3] == 'C')
            Pass("[PASS] corelib Substring tail utf16");
        else
            Fail("[FAIL] corelib Substring tail utf16");

        if (slice.Length == 3 && slice[0] == 'Ω' && slice[1] == 'B' && slice[2] == 'Ω')
            Pass("[PASS] corelib Substring range utf16");
        else
            Fail("[FAIL] corelib Substring range utf16");
    }
}


