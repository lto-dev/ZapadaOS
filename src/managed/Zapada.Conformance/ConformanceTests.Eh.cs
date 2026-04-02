using Zapada.Conformance.Runtime;

namespace Zapada.Conformance;

internal static partial class ConformanceTests
{
    private static int s_nestedFinallyState;
    private static int s_filterProbe;

    private static int NestedFinallyLeaveValue()
    {
        s_nestedFinallyState = 0;
        InternalCalls.Write("[Conf] EH NestedFinallyLeaveValue enter\n");

        try
        {
            s_nestedFinallyState = 1;
            InternalCalls.Write("[Conf] EH NestedFinallyLeaveValue outer-try state=1\n");
            try
            {
                s_nestedFinallyState = s_nestedFinallyState * 10 + 2;
                InternalCalls.Write("[Conf] EH NestedFinallyLeaveValue inner-try state=12\n");
                return s_nestedFinallyState;
            }
            finally
            {
                s_nestedFinallyState = s_nestedFinallyState * 10 + 3;
                InternalCalls.Write("[Conf] EH NestedFinallyLeaveValue inner-finally\n");
            }
        }
        finally
        {
            s_nestedFinallyState = s_nestedFinallyState * 10 + 4;
            InternalCalls.Write("[Conf] EH NestedFinallyLeaveValue outer-finally\n");
        }
    }

    private static void TestEhNestedFinallyLeave()
    {
        InternalCalls.Write("[Conf] EH test start nested finally leave\n");
        int returned = NestedFinallyLeaveValue();
        InternalCalls.Write("[Conf] EH test done nested finally leave\n");
        if (returned == 12 && s_nestedFinallyState == 1234)
            Pass("[PASS] eh nested finally leave order");
        else
            Fail("[FAIL] eh nested finally leave order");
    }

    private static void TestEhNestedFinallyThrowCatch()
    {
        InternalCalls.Write("[Conf] EH test start nested finally throw catch\n");
        if (NestedFinallyThrowCatchValue() == 12345)
            Pass("[PASS] eh nested finally throw catch order");
        else
            Fail("[FAIL] eh nested finally throw catch order");
    }

    private static void TestEhFilterClause()
    {
        InternalCalls.Write("[Conf] EH test start filter clause\n");
        /*
         * FilterClauseValue() throws, the filter body calls FilterMatch(1)
         * which sets s_filterProbe = 0*10+1 = 1 and returns true.
         * Handler executes: result = 700 + s_filterProbe = 701.
         */
        if (FilterClauseValue() == 701)
            Pass("[PASS] eh filter clause dispatch");
        else
            Fail("[FAIL] eh filter clause dispatch");
    }

    private static void TestEhFilterRejectFallsThrough()
    {
        InternalCalls.Write("[Conf] EH test start filter reject\n");
        /*
         * FilterRejectValue() throws, the filter body calls FilterMatch(0)
         * which sets s_filterProbe = 0*10+0 = 0 and returns false.
         * Filter rejects -> falls through to plain catch handler.
         * Fallback catch: result = 900 + s_filterProbe = 900 + 0 = 900.
         */
        if (FilterRejectValue() == 900)
            Pass("[PASS] eh filter reject fallback catch");
        else
            Fail("[FAIL] eh filter reject fallback catch");
    }

    private static void TestEhCatchInsideFinallySequence()
    {
        InternalCalls.Write("[Conf] EH test start catch inside finally sequence\n");
        if (CatchInsideFinallySequence() == 1234)
            Pass("[PASS] eh catch inside finally sequence");
        else
            Fail("[FAIL] eh catch inside finally sequence");
    }

    private static void TestEhNestedCatchSelection()
    {
        InternalCalls.Write("[Conf] EH test start nested catch selection\n");
        if (NestedCatchSelectionValue() == 22)
            Pass("[PASS] eh nested catch selection");
        else
            Fail("[FAIL] eh nested catch selection");
    }

    private static int NestedFinallyThrowCatchValue()
    {
        int state = 0;
        InternalCalls.Write("[Conf] EH NestedFinallyThrowCatchValue enter\n");

        try
        {
            try
            {
                state = 1;
                InternalCalls.Write("[Conf] EH NestedFinallyThrowCatchValue outer-try state=1\n");
                try
                {
                    state = state * 10 + 2;
                    InternalCalls.Write("[Conf] EH NestedFinallyThrowCatchValue inner-try throwing\n");
                    throw new global::System.Exception();
                }
                finally
                {
                    state = state * 10 + 3;
                    InternalCalls.Write("[Conf] EH NestedFinallyThrowCatchValue inner-finally\n");
                }
            }
            finally
            {
                state = state * 10 + 4;
                InternalCalls.Write("[Conf] EH NestedFinallyThrowCatchValue outer-finally\n");
            }
        }
        catch (global::System.Exception)
        {
            state = state * 10 + 5;
            InternalCalls.Write("[Conf] EH NestedFinallyThrowCatchValue catch\n");
        }

        return state;
    }

    private static bool FilterMatch(int value)
    {
        InternalCalls.Write("[Conf] EH FilterMatch enter\n");
        s_filterProbe = s_filterProbe * 10 + value;
        return value == 1;
    }

    private static int FilterClauseValue()
    {
        int result = 0;
        s_filterProbe = 0;
        InternalCalls.Write("[Conf] EH FilterClauseValue enter\n");

        try
        {
            InternalCalls.Write("[Conf] EH FilterClauseValue throwing\n");
            throw new global::System.Exception();
        }
        catch (global::System.Exception) when (FilterMatch(1))
        {
            InternalCalls.Write("[Conf] EH FilterClauseValue handler\n");
            result = 700 + s_filterProbe;
        }

        return result;
    }

    private static int FilterRejectValue()
    {
        int result = 0;
        s_filterProbe = 0;
        InternalCalls.Write("[Conf] EH FilterRejectValue enter\n");

        try
        {
            InternalCalls.Write("[Conf] EH FilterRejectValue throwing\n");
            throw new global::System.Exception();
        }
        catch (global::System.Exception) when (FilterMatch(0))
        {
            InternalCalls.Write("[Conf] EH FilterRejectValue filtered handler\n");
            result = 800 + s_filterProbe;
        }
        catch (global::System.Exception)
        {
            InternalCalls.Write("[Conf] EH FilterRejectValue fallback catch\n");
            result = 900 + s_filterProbe;
        }

        return result;
    }

    private static int CatchInsideFinallySequence()
    {
        int state = 0;
        InternalCalls.Write("[Conf] EH CatchInsideFinallySequence enter\n");

        try
        {
            state = 1;
            throw new global::System.Exception();
        }
        catch (global::System.Exception)
        {
            state = state * 10 + 2;
        }
        finally
        {
            state = state * 10 + 3;
        }

        state = state * 10 + 4;
        return state;
    }

    private static int NestedCatchSelectionValue()
    {
        int which = 0;
        InternalCalls.Write("[Conf] EH NestedCatchSelectionValue enter\n");

        try
        {
            try
            {
                throw new global::System.Exception();
            }
            catch (global::System.ArgumentException)
            {
                which = 11;
            }
        }
        catch (global::System.Exception)
        {
            which = 22;
        }

        return which;
    }
}
