using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Threading;
using System.Web.Script.Serialization;
using System.Collections.Generic;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

// .NET Framework and Win32 only; the plugin retains ownership while OBS is running.
sealed class Launcher : ApplicationContext
{
    sealed class HotkeyWindow : NativeWindow
    {
        public Action Pressed;
        public HotkeyWindow() { CreateHandle(new CreateParams { Parent = new IntPtr(-3) }); }
        protected override void WndProc(ref Message message)
        {
            if (message.Msg == 0x0312 && message.WParam.ToInt32() == 1) Pressed();
            base.WndProc(ref message);
        }
    }
    [DllImport("user32.dll", SetLastError = true)] static extern bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, uint key);
    [DllImport("user32.dll")] static extern bool UnregisterHotKey(IntPtr hwnd, int id);
    readonly string config = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "obs-studio", "plugin_config", "obs-quick-record");
    readonly HotkeyWindow window = new HotkeyWindow();
    readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer { Interval = 500 };
    readonly System.Drawing.Icon trayIcon = LoadTrayIcon();
    readonly NotifyIcon icon = new NotifyIcon { Text = Text("OBS起動アシスト", "OBS startup assistant") };
    static System.Drawing.Icon LoadTrayIcon()
    {
        using (var stream = typeof(Launcher).Assembly.GetManifestResourceStream("obs-startup-assistant.ico"))
        using (var image = new System.Drawing.Icon(stream, SystemInformation.SmallIconSize))
            return (System.Drawing.Icon)image.Clone();
    }
    string registered = "", lastError = "";
    bool ownsKey;
    readonly BindingCache bindingCache = new BindingCache();
    static readonly int session = CurrentSession();
    Process obsProcess;
    DateTime nextProcessScan = DateTime.MinValue;
    DateTime pendingUntil = DateTime.MinValue;
    string ObsPath { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "obs-studio", "bin", "64bit", "obs64.exe"); } }
    string RequestPath { get { return Path.Combine(config, "launch-request.txt"); } }
    static string Text(string japanese, string english) { return CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ja" ? japanese : english; }
    static string ExecutablePath { get { return Process.GetCurrentProcess().MainModule.FileName; } }
    static string StartupPath { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Startup), "OBS Quick Record Launcher.lnk"); } }

    static void SetStartup(bool enabled, string shortcutPath, string exe)
    {
        if (!enabled) { File.Delete(shortcutPath); return; }
        dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
        dynamic shortcut = null;
        try
        {
            shortcut = shell.CreateShortcut(shortcutPath);
            shortcut.TargetPath = exe;
            shortcut.WorkingDirectory = Path.GetDirectoryName(exe);
            shortcut.Save();
        }
        finally
        {
            if (shortcut != null) Marshal.FinalReleaseComObject(shortcut);
            Marshal.FinalReleaseComObject(shell);
        }
    }
    static bool SafeInstalledPath(string exe, string localRoot)
    {
        exe = Path.GetFullPath(exe);
        localRoot = Path.GetFullPath(localRoot).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        if (!exe.StartsWith(localRoot, StringComparison.OrdinalIgnoreCase) ||
            !String.Equals(Path.GetFileName(exe), "obs-quick-record-launcher.exe", StringComparison.OrdinalIgnoreCase) ||
            !String.Equals(Path.GetFileName(Path.GetDirectoryName(exe)), "OBSQuickRecordLauncher", StringComparison.OrdinalIgnoreCase)) return false;
        for (string path = exe; path.Length >= localRoot.Length; path = Path.GetDirectoryName(path))
            if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0) return false;
        return true;
    }
    static string Hash(string file)
    {
        using (var sha = SHA256.Create()) using (var stream = File.OpenRead(file))
            return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "");
    }
    static string PsQuote(string value) { return "'" + value.Replace("'", "''") + "'"; }
    static string RemovalScript(string exe, int parentId, bool showResult)
    {
        // Windows locks the running EXE; a short-lived native PowerShell helper waits for exit.
        return "$ErrorActionPreference='Stop'\n$exe=" + PsQuote(exe) + "\n$expectedHash=" + PsQuote(Hash(exe)) +
            "\n$parentId=" + parentId + "\n$showResult=" + (showResult ? "$true" : "$false") +
            "\n$success=" + PsQuote(Text("OBS起動アシストを削除しました。OBSのQuick Recordプラグインと設定は保持しています。", "OBS startup assistant removed. The OBS Quick Record plugin and settings are retained.")) +
            "\n$failure=" + PsQuote(Text("OBS起動アシストの削除に失敗しました。", "Could not remove OBS startup assistant.")) + @"
try {
    $parent=Get-Process -Id $parentId -ErrorAction SilentlyContinue
    if ($parent) {
        if ($parent.Path -ne $exe) { throw 'The process no longer matches the Launcher.' }
        if (!$parent.WaitForExit(15000)) { throw 'The Launcher did not exit.' }
    }
    $sha=[Security.Cryptography.SHA256]::Create()
    $stream=[IO.File]::OpenRead($exe)
    try { $actualHash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $sha.Dispose() }
    if ($actualHash -ne $expectedHash) { throw 'The file changed; removal canceled.' }
    [IO.File]::Delete($exe)
    $folder=Split-Path -Parent $exe
    if ((Get-ChildItem -LiteralPath $folder -Force | Measure-Object).Count -eq 0) { [IO.Directory]::Delete($folder, $false) }
    if ($showResult) { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($success, 'OBS startup assistant') }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    if ($showResult) { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($failure + [Environment]::NewLine + $_.Exception.Message, 'OBS startup assistant') }
    exit 1
} finally { Remove-Item -LiteralPath $PSCommandPath -ErrorAction SilentlyContinue }
";
    }
    static Process StartRemoval(string exe, int parentId, bool showResult)
    {
        string script = Path.Combine(Path.GetTempPath(), "obs-quick-record-remove-" + Guid.NewGuid() + ".ps1");
        File.WriteAllText(script, RemovalScript(exe, parentId, showResult), new UTF8Encoding(true));
        try
        {
            return Process.Start(new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe"),
                "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"") { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardError = !showResult });
        }
        catch { File.Delete(script); throw; }
    }
    void Uninstall()
    {
        if (MessageBox.Show(Text("OBS起動アシストをアンインストールしますか？\n自動起動を解除してOBS起動アシストを削除します。\nOBSのQuick Recordプラグインと設定は保持します。", "Uninstall OBS startup assistant?\nAutomatic startup will be disabled and the OBS startup assistant removed.\nThe OBS Quick Record plugin and settings will be retained."),
            Text("OBS起動アシスト", "OBS startup assistant"), MessageBoxButtons.YesNo, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
        try
        {
            string exe = ExecutablePath;
            string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "AppData", "Local");
            if (!SafeInstalledPath(exe, root)) throw new InvalidOperationException(Text("インストール済みのOBS起動アシストから実行してください。展開・開発用フォルダーのファイルは削除しません。", "Run this from the installed OBS startup assistant. Files in extracted or development folders will not be removed."));
            SetStartup(false, StartupPath, exe);
            using (Process helper = StartRemoval(exe, Process.GetCurrentProcess().Id, true)) { }
            ExitThread();
        }
        catch (Exception exception) { Error(exception.Message); }
    }

    static int CurrentSession() { using (var process = Process.GetCurrentProcess()) return process.SessionId; }
    bool ObsRunning(bool refresh = false)
    {
        if (obsProcess != null)
        {
            try { if (!obsProcess.HasExited) return true; }
            catch (InvalidOperationException) {} catch (System.ComponentModel.Win32Exception) {}
            obsProcess.Dispose(); obsProcess = null; nextProcessScan = DateTime.MinValue;
        }
        if (!refresh && DateTime.UtcNow < nextProcessScan) return false;
        nextProcessScan = DateTime.UtcNow.AddSeconds(1);
        foreach (Process process in Process.GetProcessesByName("obs64"))
        {
            try { if (obsProcess == null && process.SessionId == session && !process.HasExited) { obsProcess = process; continue; } }
            catch (InvalidOperationException) {}
            catch (System.ComponentModel.Win32Exception) {}
            process.Dispose();
        }
        return obsProcess != null;
    }
    sealed class BindingCache
    {
        DateTime modified;
        long length = -2;
        uint[] value;
        public uint[] Read(string path)
        {
            var file = new FileInfo(path);
            long size = file.Exists ? file.Length : -1;
            DateTime stamp = file.Exists ? file.LastWriteTimeUtc : DateTime.MinValue;
            if (value != null && size == length && stamp == modified) return value;
            var parsed = Binding(file.Exists ? File.ReadAllText(path) : null);
            if (parsed[0] > 15 || parsed[1] > 255) throw new InvalidDataException("Invalid launcher shortcut.");
            // Commit only after a successful read; atomic replacement/transient failure retries next tick.
            value = parsed; length = size; modified = stamp;
            return value;
        }
    }
    static uint ReadNumber(Dictionary<string, object> data, string name)
    {
        object value; return data.TryGetValue(name, out value) ? Convert.ToUInt32(value) : 0;
    }
    internal static uint[] Binding(string json)
    {
        if (json == null) return new uint[] { 1, 0x52 }; // Default Alt+R before the first plugin load.
        var data = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);
        if (data.ContainsKey("launcherVirtualKey")) return new uint[] { ReadNumber(data, "launcherModifiers"), ReadNumber(data, "launcherVirtualKey") };
        object raw;
        if (!data.TryGetValue("hotkey", out raw)) return new uint[] { 1, 0x52 };
        var keys = raw as System.Collections.ArrayList;
        if (keys == null || keys.Count == 0) return new uint[] { 0, 0 };
        var key = keys[0] as Dictionary<string, object>;
        string name = Convert.ToString(key["key"]).Replace("OBS_KEY_", "");
        Keys virtualKey;
        if (name.Length == 1 && name[0] >= '0' && name[0] <= '9') virtualKey = (Keys)name[0];
        else if (!Enum.TryParse<Keys>(name, true, out virtualKey) || !Enum.IsDefined(typeof(Keys), virtualKey))
            throw new InvalidDataException("Open OBS once to synchronize this shortcut.");
        uint modifiers = 0;
        foreach (var entry in new[] { "alt", "control", "shift", "command" })
        {
            object enabled;
            if (key.TryGetValue(entry, out enabled) && Convert.ToBoolean(enabled)) modifiers |= entry == "alt" ? 1U : entry == "control" ? 2U : entry == "shift" ? 4U : 8U;
        }
        return new uint[] { modifiers, (uint)virtualKey };
    }
    void ReleaseKey()
    {
        if (ownsKey) { UnregisterHotKey(window.Handle, 1); Log("hotkey released"); }
        ownsKey = false; registered = "";
    }
    void Error(string message)
    {
        if (message == lastError) return;
        lastError = message;
        icon.ShowBalloonTip(5000, Text("OBS起動アシスト", "OBS startup assistant"), message, ToolTipIcon.Warning);
        Log(message);
    }
    void Log(string message)
    {
        try { Directory.CreateDirectory(config); File.AppendAllText(Path.Combine(config, "launcher.log"), DateTime.Now.ToString("s") + " " + message + Environment.NewLine); }
        catch (IOException) {} catch (UnauthorizedAccessException) {}
    }
    void Refresh(object sender, EventArgs args)
    {
        try
        {
            if (pendingUntil != DateTime.MinValue)
            {
                if (!File.Exists(RequestPath)) pendingUntil = DateTime.MinValue;
                else if (DateTime.UtcNow > pendingUntil) { File.Delete(RequestPath); pendingUntil = DateTime.MinValue; Error("OBS did not open the selector. Check the plugin and any OBS startup dialog."); }
            }
            if (ObsRunning()) { ReleaseKey(); return; }
            string file = Path.Combine(config, "settings.json");
            uint[] binding = bindingCache.Read(file);
            string identity = binding[0] + ":" + binding[1];
            if (identity == registered) return;
            ReleaseKey();
            if (binding[1] == 0) { registered = identity; return; }
            if (!RegisterHotKey(window.Handle, 1, binding[0] | 0x4000, binding[1])) { Error("Shortcut is already in use. Change it in Quick Record settings."); return; }
            ownsKey = true; registered = identity; lastError = "";
            Log("hotkey registered " + identity);
        }
        catch (Exception exception) { ReleaseKey(); Error(exception.Message); }
    }
    void Launch()
    {
        if (ObsRunning(true)) { ReleaseKey(); return; }
        if (pendingUntil != DateTime.MinValue) return;
        try
        {
            if (!File.Exists(ObsPath)) throw new FileNotFoundException("OBS is not installed at " + ObsPath);
            ReleaseKey();
            Directory.CreateDirectory(config);
            obsProcess = Process.Start(new ProcessStartInfo(ObsPath, "--minimize-to-tray") { WorkingDirectory = Path.GetDirectoryName(ObsPath), UseShellExecute = false });
            File.WriteAllText(RequestPath, obsProcess.Id.ToString() + "\nexit-after-capture");
            pendingUntil = DateTime.UtcNow.AddSeconds(60);
            Log("OBS launched; selector requested");
        }
        catch (Exception exception) { pendingUntil = DateTime.MinValue; Error(exception.Message); }
    }
    static void UpdateSelectionItem(ToolStripMenuItem item, bool running, bool pending)
    {
        item.Enabled = !running && !pending;
        item.Text = running ? Text("OBS起動中：設定したショートカットで選択", "OBS is running: use your recording shortcut") :
            pending ? Text("OBSの起動を待っています…", "Waiting for OBS to start…") :
            Text("録画対象の選択を開く", "Launch OBS selection");
    }
    Launcher()
    {
        icon.Icon = trayIcon; icon.Visible = true;
        window.Pressed = Launch;
        var menu = new ContextMenuStrip();
        var selection = new ToolStripMenuItem();
        selection.Click += (sender, args) => Launch();
        menu.Items.Add(selection);
        var startup = new ToolStripMenuItem(Text("サインイン時の自動起動を登録", "Register automatic startup at sign-in"));
        menu.Opening += (sender, args) => {
            bool running = ObsRunning(true);
            if (running) ReleaseKey();
            UpdateSelectionItem(selection, running, pendingUntil != DateTime.MinValue);
            startup.Checked = File.Exists(StartupPath);
        };
        startup.Click += (sender, args) => {
            try { SetStartup(!File.Exists(StartupPath), StartupPath, ExecutablePath); startup.Checked = File.Exists(StartupPath); }
            catch (Exception exception) { Error(exception.Message); }
        };
        menu.Items.Add(startup);
        menu.Items.Add(Text("OBS起動アシストをアンインストール…", "Uninstall OBS startup assistant…"), null, (sender, args) => Uninstall());
        menu.Items.Add(Text("OBS起動アシストを終了", "Exit OBS startup assistant"), null, (sender, args) => ExitThread());
        icon.ContextMenuStrip = menu;
        timer.Tick += Refresh; Refresh(null, EventArgs.Empty); timer.Start();
    }
    static bool CheckManagement()
    {
        string root = Path.Combine(Path.GetTempPath(), "obs-quick-record-check-" + Guid.NewGuid());
        string folder = Path.Combine(root, "OBSQuickRecordLauncher"), exe = Path.Combine(folder, "obs-quick-record-launcher.exe"), link = Path.Combine(root, "startup.lnk");
        try
        {
            Directory.CreateDirectory(folder);
            string settingsPath = Path.Combine(root, "settings.json");
            var cache = new BindingCache();
            var before = cache.Read(settingsPath);
            if (!Object.ReferenceEquals(before, cache.Read(settingsPath))) throw new InvalidDataException("Unchanged settings were parsed again");
            File.WriteAllText(settingsPath, "{\"launcherVirtualKey\":121,\"launcherModifiers\":6}");
            if (cache.Read(settingsPath)[1] != 121) throw new InvalidDataException("Changed settings not loaded");
            File.WriteAllText(settingsPath, "invalid");
            try { cache.Read(settingsPath); throw new InvalidDataException("Invalid settings accepted"); }
            catch (ArgumentException) {}
            File.Delete(settingsPath);
            if (cache.Read(settingsPath)[1] != 82) throw new InvalidDataException("Deleted settings not reset");
            File.Copy(ExecutablePath, exe);
            File.WriteAllText(Path.Combine(folder, "keep.txt"), "unrelated file");
            if (!SafeInstalledPath(exe, root) || SafeInstalledPath(exe, Path.Combine(root, "outside")) || SafeInstalledPath(ExecutablePath, root)) throw new InvalidDataException("Path safety check failed");
            SetStartup(true, link, exe);
            if (!File.Exists(link)) throw new InvalidDataException("Startup shortcut missing");
            dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
            dynamic shortcut = shell.CreateShortcut(link);
            bool correct = shortcut.TargetPath == exe && shortcut.WorkingDirectory == folder;
            Marshal.FinalReleaseComObject(shortcut); Marshal.FinalReleaseComObject(shell);
            if (!correct) throw new InvalidDataException("Startup shortcut target or working directory differs");
            SetStartup(false, link, exe); SetStartup(false, link, exe);
            if (File.Exists(link)) throw new InvalidDataException("Startup shortcut was not removed");
            // Mismatched content must survive; then remove only the intended EXE, preserving its sibling.
            string changedScript = RemovalScript(exe, Int32.MaxValue, false);
            File.AppendAllText(exe, "changed");
            string script = Path.Combine(root, "changed.ps1"); File.WriteAllText(script, changedScript, new UTF8Encoding(true));
            using (var p = Process.Start(new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe"), "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"") { UseShellExecute = false, CreateNoWindow = true }))
                if (!p.WaitForExit(30000) || p.ExitCode == 0 || !File.Exists(exe)) throw new InvalidDataException("Changed file removal was not rejected");
            using (var p = StartRemoval(exe, Int32.MaxValue, false)) if (!p.WaitForExit(30000) || p.ExitCode != 0) throw new InvalidDataException("Removal helper failed: " + p.StandardError.ReadToEnd());
            return !File.Exists(exe) && File.Exists(Path.Combine(folder, "keep.txt"));
        }
        catch (Exception exception) { Console.Error.WriteLine(exception); return false; }
        finally
        {
            if (Path.GetFullPath(root).StartsWith(Path.GetFullPath(Path.GetTempPath()), StringComparison.OrdinalIgnoreCase) &&
                Directory.Exists(root) && (File.GetAttributes(root) & FileAttributes.ReparsePoint) == 0) Directory.Delete(root, true);
        }
    }
    protected override void ExitThreadCore()
    {
        timer.Stop(); ReleaseKey(); window.DestroyHandle(); icon.Visible = false; icon.Dispose(); trayIcon.Dispose(); timer.Dispose();
        if (obsProcess != null) obsProcess.Dispose();
        base.ExitThreadCore();
    }
    [STAThread] static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--check")
        {
            using (var item = new ToolStripMenuItem()) {
                UpdateSelectionItem(item, false, false);
                if (!item.Enabled) return 1;
                string idle = item.Text;
                UpdateSelectionItem(item, true, false);
                if (item.Enabled || item.Text == idle) return 1;
                UpdateSelectionItem(item, false, true);
                if (item.Enabled || item.Text == idle) return 1;
                UpdateSelectionItem(item, false, false);
                if (!item.Enabled || item.Text != idle) return 1;
            }
            using (var image = LoadTrayIcon()) if (image.Width <= 0 || image.Height <= 0) return 1;
            uint[] initial = Binding(null), changed = Binding("{\"launcherVirtualKey\":121,\"launcherModifiers\":6}"), unbound = Binding("{\"hotkey\":[]}"), legacy = Binding("{\"hotkey\":[{\"alt\":true,\"key\":\"OBS_KEY_R\"}]}");
            for (int digit = 0; digit <= 9; ++digit)
            {
                uint[] binding = Binding("{\"hotkey\":[{\"alt\":true,\"key\":\"OBS_KEY_" + digit + "\"}]}");
                if (binding[0] != 1 || binding[1] != 0x30 + digit) return 1;
            }
            try { Binding("{\"hotkey\":[{\"key\":\"OBS_KEY_999\"}]}"); return 1; }
            catch (InvalidDataException) {}
            return initial[0] == 1 && initial[1] == 82 && changed[0] == 6 && changed[1] == 121 && unbound[1] == 0 && legacy[0] == 1 && legacy[1] == 82 && CheckManagement() ? 0 : 1;
        }
        bool created;
        using (var mutex = new Mutex(true, "Local\\OBSQuickRecordLauncher-" + WindowsIdentity.GetCurrent().User.Value, out created))
        {
            if (!created) return 0;
            Application.EnableVisualStyles();
            var launcher = new Launcher();
            if (args.Length == 1 && args[0] == "--launch") launcher.Launch();
            Application.Run(launcher); mutex.ReleaseMutex();
        }
        return 0;
    }
}
