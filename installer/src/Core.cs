// More Save Slots installer - core logic (no UI).
// Copyright (C) 2026 MaflaTV. GPL-2.0, see LICENSE.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace MoreSaveSlotsInstaller
{
    /// <summary>A folder that holds a REPENTOGON install (zhlREPENTOGON.dll + libzhl.dll).</summary>
    public class RgonFolder
    {
        public string Path;
        public string Source;          // where it was found ("Steam", "REPENTOGON launcher", "Chosen folder")
        public string RgonVersion;     // exported __REPENTOGON_VERSION
        public string ZhlVersion;      // exported __ZHL_VERSION (the one the plugin checks)
        public string InstalledVersion; // version of an installed plugin, or null
        public bool Supported;

        public override string ToString() => Path;
    }

    public static class Core
    {
        public const string PluginFile = "zhlMoreSaveSlots.dll";
        public const string AssetFolder = "more-save-slots";
        public const string ProjectUrl = "https://github.com/maflatv/more-save-slots";
        public const string RepentogonUrl = "https://repentogon.com/install.html";
        const string GameFolderName = "The Binding of Isaac Rebirth";

        // ------------------------------------------------------------ embedded payload

        static byte[] Resource(string name)
        {
            using (var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload." + name))
            {
                if (s == null) return null;
                var ms = new MemoryStream();
                s.CopyTo(ms);
                return ms.ToArray();
            }
        }

        public static IEnumerable<string> PayloadAssets() =>
            Assembly.GetExecutingAssembly().GetManifestResourceNames()
                .Where(n => n.StartsWith("payload.assets.")).Select(n => n.Substring("payload.assets.".Length));

        static byte[] _plugin;
        public static byte[] Plugin => _plugin ?? (_plugin = Resource(PluginFile));

        /// <summary>Version of the plugin bundled in this installer.</summary>
        public static string PayloadVersion => ExportedString(Plugin, "MoreSaveSlotsVersion") ?? "?";

        /// <summary>libzhl versions the bundled plugin supports (comma separated in the DLL).</summary>
        public static string[] PayloadSupported =>
            (ExportedString(Plugin, "MoreSaveSlotsSupportedZhl") ?? "").Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries);

        // ------------------------------------------------------------ finding REPENTOGON

        public static List<RgonFolder> Detect()
        {
            var found = new List<RgonFolder>();
            void Add(string dir, string source)
            {
                if (string.IsNullOrEmpty(dir)) return;
                foreach (var d in new[] { dir, System.IO.Path.Combine(dir, "Repentogon") })
                {
                    string full;
                    try { full = ProperCase(System.IO.Path.GetFullPath(d).TrimEnd('\\')); } catch { continue; }
                    if (!IsRgonFolder(full)) continue;
                    if (found.Any(f => string.Equals(f.Path, full, StringComparison.OrdinalIgnoreCase))) continue;
                    found.Add(Inspect(full, source));
                }
            }

            foreach (var lib in SteamLibraries())
                Add(System.IO.Path.Combine(lib, "steamapps", "common", GameFolderName), "Steam");
            foreach (var exe in LauncherExecutables())
                Add(System.IO.Path.GetDirectoryName(exe), "REPENTOGON launcher");
            return found;
        }

        /// <summary>The path with the real casing of each folder on disk (Steam's libraryfolders.vdf is lowercase).</summary>
        public static string ProperCase(string path)
        {
            if (!Directory.Exists(path)) return path;
            var di = new DirectoryInfo(path);
            if (di.Parent == null) return di.FullName.ToUpperInvariant();
            string parent = ProperCase(di.Parent.FullName);
            var match = new DirectoryInfo(parent).GetDirectories(di.Name).FirstOrDefault();
            return System.IO.Path.Combine(parent, match != null ? match.Name : di.Name);
        }

        public static bool IsRgonFolder(string dir) =>
            File.Exists(System.IO.Path.Combine(dir, "zhlREPENTOGON.dll")) && File.Exists(System.IO.Path.Combine(dir, "libzhl.dll"));

        public static RgonFolder Inspect(string dir, string source)
        {
            var f = new RgonFolder { Path = dir, Source = source };
            f.ZhlVersion = ExportedStringFromFile(System.IO.Path.Combine(dir, "libzhl.dll"), "__ZHL_VERSION");
            f.RgonVersion = ExportedStringFromFile(System.IO.Path.Combine(dir, "zhlREPENTOGON.dll"), "__REPENTOGON_VERSION");
            string plugin = System.IO.Path.Combine(dir, PluginFile);
            f.InstalledVersion = File.Exists(plugin) ? (ExportedStringFromFile(plugin, "MoreSaveSlotsVersion") ?? "unknown") : null;
            f.Supported = f.ZhlVersion != null && PayloadSupported.Contains(f.ZhlVersion);
            return f;
        }

        static IEnumerable<string> SteamLibraries()
        {
            var libs = new List<string>();
            string steam = null;
            foreach (var (hive, key, value) in new[]
            {
                (Registry.CurrentUser, @"Software\Valve\Steam", "SteamPath"),
                (Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
                (Registry.LocalMachine, @"SOFTWARE\Valve\Steam", "InstallPath"),
            })
            {
                try
                {
                    using (var k = hive.OpenSubKey(key))
                    {
                        var p = k?.GetValue(value) as string;
                        if (!string.IsNullOrEmpty(p) && Directory.Exists(p)) { steam = System.IO.Path.GetFullPath(p); break; }
                    }
                }
                catch { /* no Steam or no access */ }
            }
            if (steam == null) return libs;
            libs.Add(steam);
            try
            {
                string vdf = File.ReadAllText(System.IO.Path.Combine(steam, "steamapps", "libraryfolders.vdf"));
                foreach (Match m in Regex.Matches(vdf, "\"path\"\\s*\"([^\"]+)\""))
                    libs.Add(m.Groups[1].Value.Replace("\\\\", "\\"));
            }
            catch { /* single library */ }
            return libs.Distinct(StringComparer.OrdinalIgnoreCase);
        }

        // repentogon_launcher.ini (My Games) records the Isaac executable the launcher uses.
        static IEnumerable<string> LauncherExecutables()
        {
            var docs = new[]
            {
                System.IO.Path.Combine(Environment.GetEnvironmentVariable("USERPROFILE") ?? "", "Documents"),
                Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
            };
            foreach (var d in docs.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                string ini = System.IO.Path.Combine(d, "My Games", "repentogon_launcher.ini");
                string text;
                try { text = File.ReadAllText(ini); } catch { continue; }
                var m = Regex.Match(text, @"^\s*IsaacExecutable\s*=\s*(.+?)\s*$", RegexOptions.Multiline);
                if (m.Success) yield return m.Groups[1].Value;
            }
        }

        // ------------------------------------------------------------ install / uninstall

        public static bool GameRunning() => Process.GetProcessesByName("isaac-ng").Length > 0;

        public static void Install(RgonFolder f)
        {
            if (GameRunning()) throw new InvalidOperationException("Close The Binding of Isaac before installing.");
            if (!IsRgonFolder(f.Path)) throw new InvalidOperationException("REPENTOGON was not found in " + f.Path);
            if (Plugin == null) throw new InvalidOperationException("This installer has no plugin inside (broken download).");
            File.WriteAllBytes(System.IO.Path.Combine(f.Path, PluginFile), Plugin);
            string assets = System.IO.Path.Combine(f.Path, AssetFolder);
            Directory.CreateDirectory(assets);
            foreach (var name in PayloadAssets())
                File.WriteAllBytes(System.IO.Path.Combine(assets, name), Resource("assets." + name));
        }

        /// <summary>Removes the plugin and its graphics. Save files (slots 4-9) are never touched.</summary>
        public static void Uninstall(RgonFolder f)
        {
            if (GameRunning()) throw new InvalidOperationException("Close The Binding of Isaac before uninstalling.");
            string plugin = System.IO.Path.Combine(f.Path, PluginFile);
            if (File.Exists(plugin)) File.Delete(plugin);
            string assets = System.IO.Path.Combine(f.Path, AssetFolder);
            if (Directory.Exists(assets))
            {
                foreach (var name in PayloadAssets().Concat(Directory.GetFiles(assets).Select(System.IO.Path.GetFileName)).Distinct())
                {
                    string p = System.IO.Path.Combine(assets, name);
                    if (File.Exists(p)) File.Delete(p);
                }
                if (!Directory.EnumerateFileSystemEntries(assets).Any()) Directory.Delete(assets);
            }
        }

        // ------------------------------------------------------------ PE export reader

        static string ExportedStringFromFile(string path, string symbol)
        {
            try { return ExportedString(File.ReadAllBytes(path), symbol); } catch { return null; }
        }

        /// <summary>
        /// Reads an exported `const char* symbol` from a 32-bit DLL image on disk without loading it:
        /// export table -> RVA of the pointer -> pointer value (VA at the preferred base) -> string.
        /// </summary>
        public static string ExportedString(byte[] img, string symbol)
        {
            if (img == null || img.Length < 0x40) return null;
            int pe = BitConverter.ToInt32(img, 0x3C);
            if (pe <= 0 || pe + 24 > img.Length || BitConverter.ToUInt32(img, pe) != 0x4550) return null;
            int sections = BitConverter.ToUInt16(img, pe + 6);
            int optSize = BitConverter.ToUInt16(img, pe + 20);
            int opt = pe + 24;
            if (BitConverter.ToUInt16(img, opt) != 0x10B) return null;  // PE32 only (the game is 32-bit)
            uint imageBase = BitConverter.ToUInt32(img, opt + 28);
            uint exportRva = BitConverter.ToUInt32(img, opt + 96);
            int secTable = opt + optSize;

            int Off(uint rva)
            {
                for (int i = 0; i < sections; i++)
                {
                    int s = secTable + i * 40;
                    uint va = BitConverter.ToUInt32(img, s + 12);
                    uint size = Math.Max(BitConverter.ToUInt32(img, s + 8), BitConverter.ToUInt32(img, s + 16));
                    if (rva >= va && rva < va + size) return (int)(rva - va + BitConverter.ToUInt32(img, s + 20));
                }
                return -1;
            }
            string CStr(int off)
            {
                if (off < 0) return null;
                int end = off;
                while (end < img.Length && img[end] != 0) end++;
                return Encoding.ASCII.GetString(img, off, end - off);
            }

            int exp = Off(exportRva);
            if (exportRva == 0 || exp < 0) return null;
            int count = BitConverter.ToInt32(img, exp + 24);
            int funcs = Off(BitConverter.ToUInt32(img, exp + 28));
            int names = Off(BitConverter.ToUInt32(img, exp + 32));
            int ords = Off(BitConverter.ToUInt32(img, exp + 36));
            for (int i = 0; i < count; i++)
            {
                if (CStr(Off(BitConverter.ToUInt32(img, names + i * 4))) != symbol) continue;
                int ord = BitConverter.ToUInt16(img, ords + i * 2);
                uint varRva = BitConverter.ToUInt32(img, funcs + ord * 4);
                int varOff = Off(varRva);
                if (varOff < 0) return null;
                uint va = BitConverter.ToUInt32(img, varOff);
                return CStr(Off(va - imageBase));
            }
            return null;
        }
    }
}
