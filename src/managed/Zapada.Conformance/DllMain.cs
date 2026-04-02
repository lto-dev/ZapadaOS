/*
 * Zapada - src/managed/Zapada.Conformance/DllMain.cs
 *
 * Entry point for CONF.DLL.
 * Runs IL conformance tests adapted from the .NET runtime test suite and
 * emits [Gate] Phase3.2-Conf when all assertions pass.
 */

using Zapada.Conformance.Runtime;

namespace Zapada.Conformance;

public static class DllMain
{
    public static int Initialize()
    {
        int result = ConformanceTests.Run();
        if (result != 0)
        {
            InternalCalls.Write("[Boot] Conformance FAILED\n");
            return -1;
        }
        InternalCalls.Write("[Boot] Conformance OK\n");
        InternalCalls.Write("[Gate] Phase3.2-Conf\n");
        InternalCalls.Write("[Gate] Phase-CrossAsmField\n");
        InternalCalls.Write("[Gate] Phase-CrossAsmConf\n");
        InternalCalls.Write("[Gate] Phase-CctorWiden\n");
        return 0;
    }
}


