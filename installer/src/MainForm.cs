// More Save Slots installer - window.
// Copyright (C) 2026 MaflaTV. GPL-2.0, see LICENSE.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace MoreSaveSlotsInstaller
{
    public class MainForm : Form
    {
        readonly ListView list = new ListView();
        readonly Label status = new Label();
        readonly Button install = new Button();
        readonly Button uninstall = new Button();
        readonly Timer timer = new Timer { Interval = 1500 };
        readonly List<RgonFolder> extra = new List<RgonFolder>();
        bool gameWasRunning;

        public MainForm()
        {
            Text = $"More Save Slots {Core.PayloadVersion} - Installer";
            Font = new Font("Segoe UI", 9f);
            ClientSize = new Size(900, 470);
            MinimumSize = new Size(640, 420);
            StartPosition = FormStartPosition.CenterScreen;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

            var title = new Label
            {
                Text = "More Save Slots",
                Font = new Font("Segoe UI Semibold", 15f),
                AutoSize = true,
                Location = new Point(14, 10),
            };
            var intro = new Label
            {
                Text =
                    "Adds up to 9 save files (3 pages of 3) to the file select menu of The Binding of Isaac: Repentance+.\r\n" +
                    $"Requires REPENTOGON {string.Join(" / ", Core.PayloadSupported)}. Files 4-9 are stored on this PC only (never in Steam Cloud).\r\n" +
                    "The first time the game starts with the mod, a backup of your saves is made.",
                Location = new Point(16, 46),
                Size = new Size(870, 52),
                Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
            };

            list.View = View.Details;
            list.CheckBoxes = true;
            list.FullRowSelect = true;
            list.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            list.Location = new Point(16, 104);
            list.Size = new Size(868, 200);
            list.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            list.ShowItemToolTips = true;
            list.Columns.Add("REPENTOGON folder", 430);
            list.Columns.Add("Found by", 145);
            list.Columns.Add("REPENTOGON", 90);
            list.Columns.Add("More Save Slots", 175);
            list.ItemChecked += (s, e) => UpdateButtons();

            var add = new Button { Text = "Add folder…", Size = new Size(110, 28), Location = new Point(16, 312), Anchor = AnchorStyles.Bottom | AnchorStyles.Left };
            add.Click += (s, e) => AddFolder();
            var refresh = new Button { Text = "Refresh", Size = new Size(90, 28), Location = new Point(132, 312), Anchor = AnchorStyles.Bottom | AnchorStyles.Left };
            refresh.Click += (s, e) => Reload();

            status.Location = new Point(16, 350);
            status.Size = new Size(868, 48);
            status.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;

            install.Text = "Install / Update";
            install.Size = new Size(130, 32);
            install.Location = new Point(16, 422);
            install.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
            install.Click += (s, e) => Run(true);
            uninstall.Text = "Uninstall";
            uninstall.Size = new Size(100, 32);
            uninstall.Location = new Point(152, 422);
            uninstall.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
            uninstall.Click += (s, e) => Run(false);

            var rgonLink = new LinkLabel { Text = "Get REPENTOGON", AutoSize = true, Location = new Point(610, 430), Anchor = AnchorStyles.Bottom | AnchorStyles.Right };
            rgonLink.LinkClicked += (s, e) => Open(Core.RepentogonUrl);
            var projectLink = new LinkLabel { Text = "Project page", AutoSize = true, Location = new Point(720, 430), Anchor = AnchorStyles.Bottom | AnchorStyles.Right };
            projectLink.LinkClicked += (s, e) => Open(Core.ProjectUrl);
            var close = new Button { Text = "Close", Size = new Size(70, 32), Location = new Point(814, 422), Anchor = AnchorStyles.Bottom | AnchorStyles.Right };
            close.Click += (s, e) => Close();

            Controls.AddRange(new Control[] { title, intro, list, add, refresh, status, install, uninstall, rgonLink, projectLink, close });

            timer.Tick += (s, e) =>
            {
                bool running = Core.GameRunning();
                if (running != gameWasRunning) UpdateButtons();
            };
            timer.Start();
            Reload();
        }

        void Reload()
        {
            var folders = Core.Detect();
            foreach (var e in extra)
                if (!folders.Any(f => string.Equals(f.Path, e.Path, StringComparison.OrdinalIgnoreCase)))
                    folders.Add(Core.Inspect(e.Path, e.Source));

            list.BeginUpdate();
            list.Items.Clear();
            foreach (var f in folders)
            {
                string state;
                if (!f.Supported) state = "REPENTOGON not supported";
                else if (f.InstalledVersion == null) state = "Not installed";
                else if (f.InstalledVersion == Core.PayloadVersion) state = "Installed (" + f.InstalledVersion + ")";
                else state = "Installed (" + f.InstalledVersion + ") - update";
                var item = new ListViewItem(new[] { ShortPath(f.Path), f.Source, f.RgonVersion ?? f.ZhlVersion ?? "?", state }) { Tag = f, ToolTipText = f.Path };
                if (!f.Supported) item.ForeColor = Color.Gray;
                item.Checked = f.Supported;
                list.Items.Add(item);
            }
            list.EndUpdate();
            UpdateButtons();
        }

        // "C:\…\The Binding of Isaac Rebirth\Repentogon": keeps the end of long paths visible.
        static string ShortPath(string path)
        {
            if (path.Length <= 60) return path;
            var parts = path.Split('\\');
            if (parts.Length <= 4) return path;
            return parts[0] + "\\…\\" + string.Join("\\", parts.Skip(parts.Length - 3));
        }

        IEnumerable<RgonFolder> Checked() => list.CheckedItems.Cast<ListViewItem>().Select(i => (RgonFolder)i.Tag);

        void UpdateButtons()
        {
            gameWasRunning = Core.GameRunning();
            var chosen = Checked().ToList();
            install.Enabled = !gameWasRunning && chosen.Any(f => f.Supported);
            uninstall.Enabled = !gameWasRunning && chosen.Any(f => f.InstalledVersion != null);
            if (gameWasRunning)
            {
                status.ForeColor = Color.Firebrick;
                status.Text = "The Binding of Isaac is running. Close it to install or uninstall.";
            }
            else if (list.Items.Count == 0)
            {
                status.ForeColor = Color.Firebrick;
                status.Text = "REPENTOGON was not found. Install it first (Get REPENTOGON), or use \"Add folder…\" to choose the folder that contains zhlREPENTOGON.dll.";
            }
            else
            {
                status.ForeColor = SystemColors.ControlText;
                status.Text = "Tick the REPENTOGON folders to install to. If your game has more than one (for example the game folder and its \"Repentogon\" subfolder), install to all of them.\r\n" +
                              "Uninstalling removes only the mod: files 4-9 stay on your PC and come back if you install again.";
            }
        }

        void AddFolder()
        {
            using (var dlg = new FolderBrowserDialog { Description = "Choose the folder that contains zhlREPENTOGON.dll" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                string dir = dlg.SelectedPath.TrimEnd('\\');
                if (!Core.IsRgonFolder(dir))
                {
                    MessageBox.Show(this, "REPENTOGON was not found in that folder (zhlREPENTOGON.dll and libzhl.dll are needed).",
                        Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                extra.Add(new RgonFolder { Path = dir, Source = "Chosen folder" });
                Reload();
            }
        }

        void Run(bool doInstall)
        {
            var targets = Checked().Where(f => doInstall ? f.Supported : f.InstalledVersion != null).ToList();
            if (targets.Count == 0) return;
            var done = new List<string>();
            var failed = new List<string>();
            foreach (var f in targets)
            {
                try
                {
                    if (doInstall) Core.Install(f); else Core.Uninstall(f);
                    done.Add(f.Path);
                }
                catch (Exception e)
                {
                    failed.Add(f.Path + ": " + e.Message);
                }
            }
            string msg = (doInstall ? "Installed in:\r\n" : "Uninstalled from:\r\n") + string.Join("\r\n", done);
            if (failed.Count > 0) msg += "\r\n\r\nFailed:\r\n" + string.Join("\r\n", failed);
            if (doInstall && done.Count > 0)
                msg += "\r\n\r\nStart the game with REPENTOGON. In the file select menu, press right on file 3 to go to the next page.";
            MessageBox.Show(this, msg, Text, MessageBoxButtons.OK, failed.Count > 0 ? MessageBoxIcon.Warning : MessageBoxIcon.Information);
            Reload();
        }

        static void Open(string url)
        {
            try { Process.Start(url); } catch { }
        }
    }
}
