// More Save Slots installer - entry point.
// Copyright (C) 2026 MaflaTV. GPL-2.0, see LICENSE.

using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace MoreSaveSlotsInstaller
{
    static class Program
    {
        [DllImport("kernel32.dll")] static extern bool AttachConsole(int pid);

        [STAThread]
        static int Main(string[] args)
        {
            if (args.Length > 0) return Cli(args);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
            return 0;
        }

        // Command line (also used for automated tests):
        //   --list                 detected REPENTOGON folders
        //   --status <folder>      REPENTOGON / plugin versions of one folder
        //   --install <folder>     install or update the plugin in that folder
        //   --uninstall <folder>   remove the plugin from that folder (saves are never touched)
        static int Cli(string[] args)
        {
            AttachConsole(-1);
            Console.WriteLine();
            Console.WriteLine($"More Save Slots installer - plugin {Core.PayloadVersion} (supports libzhl {string.Join(", ", Core.PayloadSupported)})");
            try
            {
                switch (args[0])
                {
                    case "--list":
                        foreach (var f in Core.Detect()) Print(f);
                        return 0;
                    case "--status":
                    case "--install":
                    case "--uninstall":
                        if (args.Length < 2) throw new ArgumentException("Missing folder.");
                        string dir = System.IO.Path.GetFullPath(args[1]).TrimEnd('\\');
                        if (!Core.IsRgonFolder(dir)) throw new InvalidOperationException("REPENTOGON was not found in " + dir);
                        var folder = Core.Inspect(dir, "Command line");
                        if (args[0] == "--install")
                        {
                            if (!folder.Supported)
                                throw new InvalidOperationException($"REPENTOGON {folder.ZhlVersion} is not supported by this release.");
                            Core.Install(folder);
                            Console.WriteLine("Installed.");
                        }
                        else if (args[0] == "--uninstall")
                        {
                            Core.Uninstall(folder);
                            Console.WriteLine("Uninstalled.");
                        }
                        Print(Core.Inspect(dir, "Command line"));
                        return 0;
                    default:
                        throw new ArgumentException("Unknown option " + args[0]);
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Error: " + e.Message);
                return 1;
            }
        }

        static void Print(RgonFolder f) =>
            Console.WriteLine($"{f.Path} | {f.Source} | REPENTOGON {f.RgonVersion ?? "?"} (libzhl {f.ZhlVersion ?? "?"}) | " +
                $"{(f.Supported ? "supported" : "NOT supported")} | plugin {f.InstalledVersion ?? "not installed"}");
    }
}
