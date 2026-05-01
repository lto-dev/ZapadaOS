/*
 * Zapada - src/managed/Zapada.Minid/Minid.cs
 *
 * Minimal init supervisor (pid 1 target).
 *
 * Boot launches Minid.Run() which selects a run target and launches
 * the appropriate shell or service path. This replaces the direct
 * shell policy that was previously hardcoded in BootLoader.Run().
 *
 * Current targets:
 *   smoke  -- run the shell boot smoke sequence, then exit
 *   rc1    -- run an interactive maintenance shell (single-user)
 *   rc3    -- run the login service (multi-user, not yet implemented)
 *
 * Boot command line:
 *   --smoke  selects smoke target
 *   single   selects rc1
 *   (default) selects rc3 (falls back to rc1 until login exists)
 */
using System;
using Zapada.Storage;
using Zapada.Shell;

namespace Zapada.Minid;

public static class Minid
{
    public static int Run(bool smokeMode)
    {
        Console.Write("[Minid] init starting\n");

        string target;
        if (smokeMode)
            target = "smoke";
        else
            target = "rc1";

        Console.Write("[Minid] target: ");
        Console.Write(target);
        Console.Write("\n");
        Console.Write("[Gate] Phase-MinidInit\n");

        int rc;
        if (target == "smoke")
        {
            rc = ShellHost.RunBootSmoke();
        }
        else
        {
            Console.Write("[Minid] rc1: launching maintenance shell\n");
            rc = ShellHost.RunInteractive(-1);
        }

        Console.Write("[Minid] shell exited rc=");
        Console.Write(rc);
        Console.Write("\n");
        Console.Write("[Gate] Phase-MinidShell\n");

        return rc;
    }
}
