// FFXIII-2-NX Builder: one .exe with the whole builder inside (the repository as an embedded zip).
// On start it unpacks the builder into a folder you choose (once per version), then opens the builder window
// (tools\gh\builder_gui.ps1). The build itself still downloads its tools (CMake, Ninja, Python, devkitPro) as before.
//
// Command line (optional):  FFXIII-2-NX-Builder.exe [/dir <folder>] [/extract-only] [/force]
//   /dir <folder>   unpack into this folder (no folder question)
//   /extract-only   unpack, but do not open the builder window
//   /force          unpack again even when this version is already there
//
// Compiled with the .NET Framework 4 compiler (C# 5) by tools\gh\launcher\build_exe.ps1, which embeds the repository
// (git archive of HEAD) as payload.zip. The .exe is not part of the repository.
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Management;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;

[assembly: AssemblyTitle("FFXIII-2-NX Builder")]
[assembly: AssemblyProduct("FFXIII-2-NX")]
[assembly: AssemblyDescription("Builds Final Fantasy XIII-2 for the Nintendo Switch from your own game files")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]

static class Launcher
{
    const string PayloadName = "payload.zip";
    const string StampName = ".builder_version";
    const string DefaultDir = @"C:\FFXIII-2-NX";
    // the embedded tree: its commit time (yyyyMMddHHmm, sortable) and hash. A folder that already holds the same or a
    // newer tree is never overwritten by this .exe (an old copy of the .exe cannot downgrade the builder).
    const string Version = "@VERSION@";   // set by build_exe.ps1

    static string SettingsFile
    {
        get
        {
            string d = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FFXIII-2-NX-Builder");
            return Path.Combine(d, "install_folder.txt");
        }
    }

    [STAThread]
    static int Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        string dir = null;
        bool extractOnly = false, force = false;
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i].ToLowerInvariant();
            if ((a == "/dir" || a == "-dir") && i + 1 < args.Length) dir = args[++i];
            else if (a == "/extract-only" || a == "-extract-only") extractOnly = true;
            else if (a == "/force" || a == "-force") force = true;
        }

        try
        {
            if (dir == null)
            {
                string remembered = ReadRemembered();
                if (remembered != null && IsCurrent(remembered) && !force) dir = remembered;
                else
                {
                    using (SetupForm f = new SetupForm(remembered ?? DefaultDir))
                    {
                        if (f.ShowDialog() != DialogResult.OK) return 1;
                        dir = f.Folder;
                    }
                }
            }
            dir = Path.GetFullPath(dir);

            if (force || !IsCurrent(dir))
            {
                if (BuildRunning(dir))
                {
                    MessageBox.Show("The builder window is open (or a build is running) in\n" + dir + "\n\nThis .exe is a newer version of the " +
                        "builder and updates that folder first. Let a running build finish, close the builder window, then start this .exe again.", "FFXIII-2-NX Builder", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return 5;
                }
                using (ExtractForm f = new ExtractForm(dir))
                {
                    f.ShowDialog();
                    if (f.Error is OperationCanceledException) return 1;
                    if (f.Error != null)
                    {
                        MessageBox.Show("The builder could not be unpacked to\n" + dir + "\n\n" + f.Error.Message,
                            "FFXIII-2-NX Builder", MessageBoxButtons.OK, MessageBoxIcon.Error);
                        return 2;
                    }
                }
            }
            Remember(dir);
            if (extractOnly) return 0;
            return OpenBuilder(dir) ? 0 : 3;
        }
        catch (Exception e)
        {
            MessageBox.Show(e.Message, "FFXIII-2-NX Builder", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 4;
        }
    }

    // A build_switch.ps1 (or the builder window) running from this folder: unpacking now would change its files.
    static bool BuildRunning(string dir)
    {
        try
        {
            string d = Path.GetFullPath(dir).TrimEnd('\\') + "\\tools\\gh\\";
            using (ManagementObjectSearcher q = new ManagementObjectSearcher(
                "SELECT CommandLine FROM Win32_Process WHERE Name='powershell.exe' OR Name='pwsh.exe'"))
            {
                foreach (ManagementObject p in q.Get())
                {
                    string c = p["CommandLine"] as string;
                    if (c != null && c.IndexOf(d, StringComparison.OrdinalIgnoreCase) >= 0) return true;
                }
            }
        }
        catch { }
        return false;
    }

    static bool IsCurrent(string dir)
    {
        try
        {
            string stamp = Path.Combine(dir, StampName);
            if (!File.Exists(stamp)) return false;
            if (!File.Exists(Path.Combine(dir, @"tools\gh\builder_gui.ps1"))) return false;
            return StampNumber(File.ReadAllText(stamp)) >= StampNumber(Version);
        }
        catch { return false; }
    }

    // The commit time of a stamp; 0 for the first builds' stamps (the hash only), which are all older.
    static long StampNumber(string stamp)
    {
        string first = stamp.Trim().Split(' ')[0];
        long n;
        if (first.Length == 12 && long.TryParse(first, out n)) return n;
        return 0;
    }

    static string ReadRemembered()
    {
        try
        {
            if (!File.Exists(SettingsFile)) return null;
            string s = File.ReadAllText(SettingsFile).Trim();
            return s.Length > 0 ? s : null;
        }
        catch { return null; }
    }

    static void Remember(string dir)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile));
            File.WriteAllText(SettingsFile, dir);
        }
        catch { }
    }

    static bool OpenBuilder(string dir)
    {
        string gui = Path.Combine(dir, @"tools\gh\builder_gui.ps1");
        string ps = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\powershell.exe");
        if (!File.Exists(ps)) ps = "powershell.exe";
        ProcessStartInfo si = new ProcessStartInfo(ps,
            "-NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + gui + "\"");
        si.WorkingDirectory = dir;
        si.UseShellExecute = false;
        si.CreateNoWindow = true;
        try
        {
            Process.Start(si);
            return true;
        }
        catch (Exception e)
        {
            MessageBox.Show("Windows PowerShell could not be started: " + e.Message + "\n\nOpen the builder with\n" +
                Path.Combine(dir, "FFXIII-2-NX Builder.bat"), "FFXIII-2-NX Builder", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
    }

    public static Stream OpenPayload()
    {
        Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(PayloadName);
        if (s == null) throw new InvalidOperationException("this .exe has no builder inside (resource " + PayloadName + " missing)");
        return s;
    }

    // Unpacks every file of the payload into dir. Files that are not in the payload (your SD folder, the builder's
    // settings, downloaded tools) are left alone.
    public static void Extract(string dir, Action<int, int, string> progress, Func<bool> cancelled)
    {
        Directory.CreateDirectory(dir);
        string root = Path.GetFullPath(dir).TrimEnd('\\') + "\\";
        using (Stream s = OpenPayload())
        using (ZipArchive zip = new ZipArchive(s, ZipArchiveMode.Read))
        {
            int total = zip.Entries.Count, n = 0;
            byte[] buf = new byte[1 << 20];
            foreach (ZipArchiveEntry e in zip.Entries)
            {
                if (cancelled()) throw new OperationCanceledException("cancelled");
                n++;
                string rel = e.FullName.Replace('/', '\\');
                string target = Path.GetFullPath(Path.Combine(root, rel));
                if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) continue;
                if (rel.EndsWith("\\"))
                {
                    Directory.CreateDirectory(target);
                }
                else
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    if (File.Exists(target)) File.SetAttributes(target, FileAttributes.Normal);
                    using (Stream src = e.Open())
                    using (FileStream dst = new FileStream(target, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16))
                    {
                        int r;
                        while ((r = src.Read(buf, 0, buf.Length)) > 0) dst.Write(buf, 0, r);
                    }
                    try { File.SetLastWriteTime(target, e.LastWriteTime.DateTime); } catch { }
                }
                if ((n & 63) == 0 || n == total) progress(n, total, rel);
            }
        }
        File.WriteAllText(Path.Combine(root, StampName), Version);
    }

    public static Icon AppIcon()
    {
        try { return Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { return null; }
    }

    public static long PayloadBytes()
    {
        long sum = 0;
        using (Stream s = OpenPayload())
        using (ZipArchive zip = new ZipArchive(s, ZipArchiveMode.Read))
        {
            foreach (ZipArchiveEntry e in zip.Entries) sum += e.Length;
        }
        return sum;
    }
}

class SetupForm : Form
{
    TextBox box;
    Label space;
    public string Folder { get { return box.Text.Trim(); } }

    public SetupForm(string initial)
    {
        Text = "FFXIII-2-NX Builder";
        Icon = Launcher.AppIcon();
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(560, 250);
        Font = new Font("Segoe UI", 9f);

        Label title = new Label();
        title.Text = "Final Fantasy XIII-2 for the Nintendo Switch";
        title.Font = new Font("Segoe UI", 12f, FontStyle.Bold);
        title.SetBounds(16, 12, 528, 26);
        Controls.Add(title);

        Label info = new Label();
        info.Text = "The builder is unpacked into this folder (about 0.6 GB). The build works there too and puts the " +
            "finished SD card folder in its SD\\ subfolder. Choose a drive with about 20 GB free.";
        info.SetBounds(16, 44, 528, 48);
        Controls.Add(info);

        box = new TextBox();
        box.Text = initial;
        box.SetBounds(16, 100, 440, 24);
        box.TextChanged += delegate { UpdateSpace(); };
        Controls.Add(box);

        Button browse = new Button();
        browse.Text = "Browse...";
        browse.SetBounds(464, 99, 80, 26);
        browse.Click += delegate
        {
            using (FolderBrowserDialog d = new FolderBrowserDialog())
            {
                d.Description = "Folder for the FFXIII-2-NX builder (a new subfolder FFXIII-2-NX is made in it)";
                d.ShowNewFolderButton = true;
                if (d.ShowDialog(this) == DialogResult.OK)
                {
                    string p = d.SelectedPath;
                    if (!Path.GetFileName(p.TrimEnd('\\')).Equals("FFXIII-2-NX", StringComparison.OrdinalIgnoreCase))
                        p = Path.Combine(p, "FFXIII-2-NX");
                    box.Text = p;
                }
            }
        };
        Controls.Add(browse);

        space = new Label();
        space.SetBounds(16, 130, 528, 20);
        Controls.Add(space);

        Label note = new Label();
        note.Text = "Your game files are only read, never changed. The build downloads its tools (CMake, Python, devkitPro).";
        note.ForeColor = SystemColors.GrayText;
        note.SetBounds(16, 154, 528, 36);
        Controls.Add(note);

        Button ok = new Button();
        ok.Text = "Unpack and open";
        ok.SetBounds(312, 206, 140, 30);
        ok.Click += delegate
        {
            string f = Folder;
            if (f.Length < 3 || !Path.IsPathRooted(f))
            {
                MessageBox.Show(this, "Please enter a full folder path, for example " + @"C:\FFXIII-2-NX", Text);
                return;
            }
            try { Path.GetFullPath(f); }
            catch (Exception e)
            {
                MessageBox.Show(this, "This is not a valid folder: " + e.Message, Text);
                return;
            }
            DialogResult = DialogResult.OK;
            Close();
        };
        Controls.Add(ok);
        AcceptButton = ok;

        Button cancel = new Button();
        cancel.Text = "Cancel";
        cancel.SetBounds(460, 206, 84, 30);
        cancel.DialogResult = DialogResult.Cancel;
        Controls.Add(cancel);
        CancelButton = cancel;

        UpdateSpace();
    }

    void UpdateSpace()
    {
        try
        {
            string r = Path.GetPathRoot(Path.GetFullPath(Folder));
            DriveInfo d = new DriveInfo(r);
            double gb = d.AvailableFreeSpace / 1073741824.0;
            space.Text = "Free on " + r + ": " + gb.ToString("0.0") + " GB" + (gb < 20 ? "  (less than the 20 GB the build needs)" : "");
            space.ForeColor = gb < 20 ? Color.Firebrick : SystemColors.ControlText;
        }
        catch
        {
            space.Text = "";
        }
    }
}

class ExtractForm : Form
{
    readonly string dir;
    ProgressBar bar;
    Label status;
    volatile bool cancel;
    public Exception Error;

    public ExtractForm(string dir)
    {
        this.dir = dir;
        Text = "FFXIII-2-NX Builder: unpacking";
        Icon = Launcher.AppIcon();
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        ControlBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(520, 130);
        Font = new Font("Segoe UI", 9f);

        Label l = new Label();
        l.Text = "Unpacking the builder to " + dir;
        l.SetBounds(16, 12, 488, 20);
        Controls.Add(l);

        bar = new ProgressBar();
        bar.SetBounds(16, 38, 488, 22);
        Controls.Add(bar);

        status = new Label();
        status.SetBounds(16, 66, 488, 20);
        status.AutoEllipsis = true;
        Controls.Add(status);

        Button c = new Button();
        c.Text = "Cancel";
        c.SetBounds(420, 92, 84, 28);
        c.Click += delegate { cancel = true; c.Enabled = false; };
        Controls.Add(c);

        Shown += delegate
        {
            Thread t = new Thread(Work);
            t.IsBackground = true;
            t.Start();
        };
    }

    void Work()
    {
        try
        {
            Launcher.Extract(dir, delegate(int n, int total, string name)
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    bar.Maximum = total;
                    bar.Value = Math.Min(n, total);
                    status.Text = n + " / " + total + "   " + name;
                });
            }, delegate { return cancel; });
        }
        catch (Exception e)
        {
            Error = e;
        }
        BeginInvoke((MethodInvoker)delegate { Close(); });
    }
}
