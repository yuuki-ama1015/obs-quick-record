using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Threading;
using System.Web.Script.Serialization;
using System.Collections.Generic;
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
    readonly NotifyIcon icon = new NotifyIcon { Icon = System.Drawing.SystemIcons.Application, Text = "OBS Quick Record Launcher", Visible = true };
    string registered = "", lastError = "";
    bool ownsKey;
    DateTime pendingUntil = DateTime.MinValue;
    string ObsPath { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "obs-studio", "bin", "64bit", "obs64.exe"); } }
    string RequestPath { get { return Path.Combine(config, "launch-request.txt"); } }

    static bool ObsRunning()
    {
        int session = Process.GetCurrentProcess().SessionId;
        foreach (Process process in Process.GetProcessesByName("obs64"))
        {
            using (process) { try { if (process.SessionId == session) return true; } catch (InvalidOperationException) {} }
        }
        return false;
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
        if (!Enum.TryParse<Keys>(name, true, out virtualKey)) throw new InvalidDataException("Open OBS once to synchronize this shortcut.");
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
        icon.ShowBalloonTip(5000, "OBS Quick Record Launcher", message, ToolTipIcon.Warning);
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
            uint[] binding = Binding(File.Exists(file) ? File.ReadAllText(file) : null);
            if (binding[0] > 15 || binding[1] > 255) throw new InvalidDataException("Invalid launcher shortcut.");
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
        if (ObsRunning() || pendingUntil != DateTime.MinValue) return;
        try
        {
            if (!File.Exists(ObsPath)) throw new FileNotFoundException("OBS is not installed at " + ObsPath);
            ReleaseKey();
            Directory.CreateDirectory(config);
            using (Process process = Process.Start(new ProcessStartInfo(ObsPath, "--minimize-to-tray") { WorkingDirectory = Path.GetDirectoryName(ObsPath), UseShellExecute = false }))
                File.WriteAllText(RequestPath, process.Id.ToString());
            pendingUntil = DateTime.UtcNow.AddSeconds(60);
            Log("OBS launched; selector requested");
        }
        catch (Exception exception) { pendingUntil = DateTime.MinValue; Error(exception.Message); }
    }
    Launcher()
    {
        window.Pressed = Launch;
        var menu = new ContextMenuStrip();
        menu.Items.Add("Launch OBS selection", null, (sender, args) => Launch());
        menu.Items.Add("Exit launcher", null, (sender, args) => ExitThread());
        icon.ContextMenuStrip = menu;
        timer.Tick += Refresh; Refresh(null, EventArgs.Empty); timer.Start();
    }
    protected override void ExitThreadCore()
    {
        timer.Stop(); ReleaseKey(); window.DestroyHandle(); icon.Visible = false; icon.Dispose(); timer.Dispose();
        base.ExitThreadCore();
    }
    [STAThread] static int Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--check")
        {
            uint[] initial = Binding(null), changed = Binding("{\"launcherVirtualKey\":121,\"launcherModifiers\":6}"), unbound = Binding("{\"hotkey\":[]}"), legacy = Binding("{\"hotkey\":[{\"alt\":true,\"key\":\"OBS_KEY_R\"}]}");
            return initial[0] == 1 && initial[1] == 82 && changed[0] == 6 && changed[1] == 121 && unbound[1] == 0 && legacy[0] == 1 && legacy[1] == 82 ? 0 : 1;
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
