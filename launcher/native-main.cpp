#include "native-support.hpp"
#include <shellapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <sstream>

namespace assist {
constexpr UINT trayMessage = WM_APP + 1;
enum MenuId { Select = 101, Startup, Uninstall, Exit };
class Application {
    HWND window = nullptr;
    NOTIFYICONDATAW tray{sizeof(tray)};
    HICON image = nullptr;
    UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated"), timerInterval = 0;
    fs::path config, request, settings, report;
    BindingCache bindings;
    Binding registered;
    bool ownsKey = false, hasBinding = false, benchmark = false, forcePolling = false, discoveryQueued = false;
    const wchar_t *processName = L"obs64.exe";
    std::wstring lastError;
    Handle process;
    DWORD processId = 0, session = 0;
    HANDLE directoryChange = INVALID_HANDLE_VALUE;
    HWINEVENTHOOK obsEvents = nullptr;
    static inline Application *eventOwner = nullptr; // Out-of-context WinEvent callbacks run on this GUI thread.
    ULONGLONG nextScan = 0, pendingUntil = 0, started = GetTickCount64(), duration = 0;
    unsigned scans = 0, ticks = 0, detections = 0, hookInstalls = 0, hookRemovals = 0;
    static void CALLBACK obsWindowEvent(HWINEVENTHOOK hook, DWORD type, HWND window, LONG object, LONG child, DWORD, DWORD)
    {
        auto *owner = eventOwner;
        if (!owner || hook != owner->obsEvents || owner->processId || owner->discoveryQueued ||
            (type != EVENT_OBJECT_CREATE && type != EVENT_OBJECT_SHOW) ||
            object != OBJID_WINDOW || child != CHILDID_SELF || !window) return;
        wchar_t className[128]{};
        if (!GetClassNameW(window, className, 128) || wcsncmp(className, L"Qt", 2) != 0) return;
        DWORD pid = 0, candidateSession = 0;
        GetWindowThreadProcessId(window, &pid);
        if (!ProcessIdToSessionId(pid, &candidateSession) || candidateSession != owner->session) return;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        wchar_t path[32768]{}; DWORD length = 32768;
        if (process && QueryFullProcessImageNameW(process.value, 0, path, &length) &&
            _wcsicmp(fs::path(path).filename().c_str(), owner->processName) == 0)
            owner->discoveryQueued = PostMessageW(owner->window, WM_APP + 2, 0, 0) != FALSE;
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
    {
        auto *self = reinterpret_cast<Application *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Application *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            try { return self->message(window, message, wp, lp); }
            catch (...) { self->error(exceptionMessage()); return 0; }
        }
        return DefWindowProcW(window, message, wp, lp);
    }
    void releaseKey()
    {
        if (ownsKey && !benchmark) UnregisterHotKey(window, 1);
        if (ownsKey) log(L"hotkey released");
        ownsKey = false; hasBinding = false;
    }
    void log(const std::wstring &value)
    { try { appendLog(config / L"launcher.log", value); } catch (...) {} }
    void error(const std::wstring &value)
    {
        if (lastError == value) return;
        lastError = value; log(value);
        if (benchmark) return;
        tray.uFlags = NIF_INFO; tray.dwInfoFlags = NIIF_WARNING;
        wcsncpy_s(tray.szInfoTitle, text(L"OBS起動アシスト", L"OBS startup assistant").c_str(), _TRUNCATE);
        wcsncpy_s(tray.szInfo, value.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &tray);
    }
    void timer(UINT interval)
    {
        if (timerInterval == interval) return;
        if (timerInterval) KillTimer(window, 1);
        timerInterval = interval;
        if (interval) winrt::check_bool(SetTimer(window, 1, interval, nullptr) != 0);
    }
    bool startDiscovery()
    {
        if (forcePolling || obsEvents) return false;
        obsEvents = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, obsWindowEvent, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (obsEvents) ++hookInstalls;
        return obsEvents != nullptr;
    }
    void stopDiscovery()
    {
        auto hook = obsEvents;
        obsEvents = nullptr;
        if (hook) { UnhookWinEvent(hook); ++hookRemovals; }
    }
    bool obsRunning(bool force = false)
    {
        if (process && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) return true;
        if (process) { process.reset(); processId = 0; nextScan = 0; }
        if (!force && GetTickCount64() < nextScan) return processId != 0;
        nextScan = GetTickCount64() + 2000; processId = 0; ++scans;
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        winrt::check_bool(static_cast<bool>(snapshot));
        PROCESSENTRY32W entry{sizeof(entry)};
        for (BOOL found = Process32FirstW(snapshot.value, &entry); found; found = Process32NextW(snapshot.value, &entry)) {
            if (_wcsicmp(entry.szExeFile, processName) != 0) continue;
            DWORD candidateSession = 0;
            if (!ProcessIdToSessionId(entry.th32ProcessID, &candidateSession) || candidateSession != session) continue;
            Handle candidate(OpenProcess(SYNCHRONIZE, FALSE, entry.th32ProcessID));
            if (candidate && WaitForSingleObject(candidate.value, 0) != WAIT_TIMEOUT) continue;
            processId = entry.th32ProcessID;
            ++detections;
            process.reset(candidate.value); candidate.value = nullptr;
            return true; // Access denied still counts as running; fall back to discovery polling.
        }
        return false;
    }
    void refresh(bool force = false)
    {
        bool running = false;
        try {
            if (pendingUntil) {
                if (!fs::exists(request)) pendingUntil = 0;
                else if (GetTickCount64() > pendingUntil) {
                    fs::remove(request); pendingUntil = 0;
                    error(text(L"選択画面を開けませんでした。OBSの起動時ダイアログとプラグインを確認してください。", L"OBS did not open the selector. Check the plugin and any OBS startup dialog."));
                }
            }
            running = obsRunning(force);
            // A process handle is sufficient while OBS is alive. When it exits,
            // reinstall discovery and rescan AFTER registration to close the gap.
            if (!process && startDiscovery()) running = obsRunning(true);
            if (process) stopDiscovery();
            if (running) releaseKey();
            else {
                auto binding = bindings.read(settings);
                if (!hasBinding || !(binding == registered)) {
                    releaseKey();
                    if (binding.key && !benchmark && !RegisterHotKey(window, 1, binding.modifiers | MOD_NOREPEAT, binding.key))
                        error(text(L"キーは既に使われています。Quick Record設定で変更してください。", L"Shortcut is already in use. Change it in Quick Record settings."));
                    else {
                        registered = binding; hasBinding = true; ownsKey = binding.key != 0; lastError.clear();
                        log(L"hotkey registered " + std::to_wstring(binding.modifiers) + L":" + std::to_wstring(binding.key));
                    }
                }
            }
        } catch (...) { releaseKey(); error(exceptionMessage()); }
        // No periodic work for a known running OBS after it consumes the request.
        const bool eventDriven = running ? static_cast<bool>(process) : obsEvents && directoryChange != INVALID_HANDLE_VALUE;
        timer(pendingUntil ? 500 : eventDriven && (running || hasBinding) ? 0 : 2000);
    }
    void launch()
    {
        if (obsRunning(true)) { refresh(); return; }
        if (pendingUntil) return;
        auto obs = folder(FOLDERID_ProgramFiles) / L"obs-studio/bin/64bit/obs64.exe";
        require(fs::exists(obs), "OBS is not installed at the standard location");
        releaseKey();
        std::wstring command = L"\"" + obs.wstring() + L"\" --minimize-to-tray";
        STARTUPINFOW info{sizeof(info)}; PROCESS_INFORMATION child{};
        winrt::check_bool(CreateProcessW(obs.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, obs.parent_path().c_str(), &info, &child));
        CloseHandle(child.hThread); process.reset(child.hProcess); processId = child.dwProcessId;
        stopDiscovery();
        writeRequest(request, processId);
        pendingUntil = GetTickCount64() + 60000; timer(500);
        log(L"OBS launched; selector requested");
    }
    void addTray()
    {
        tray.hWnd = window; tray.uID = 1; tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        tray.uCallbackMessage = trayMessage; tray.hIcon = image;
        wcsncpy_s(tray.szTip, text(L"OBS起動アシスト", L"OBS startup assistant").c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_ADD, &tray); tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray);
    }
    void menu()
    {
        refresh(true);
        bool running = processId != 0;
        HMENU menu = CreatePopupMenu(); require(menu != nullptr, "Cannot create tray menu");
        const auto select = running ? text(L"OBS起動中：設定したショートカットで選択", L"OBS is running: use your recording shortcut") :
            pendingUntil ? text(L"OBSの起動を待っています…", L"Waiting for OBS to start…") : text(L"録画対象の選択を開く", L"Launch OBS selection");
        AppendMenuW(menu, MF_STRING | (running || pendingUntil ? MF_GRAYED : 0), Select, select.c_str());
        AppendMenuW(menu, MF_STRING | (fs::exists(startupPath()) ? MF_CHECKED : 0), Startup, text(L"サインイン時の自動起動を登録", L"Register automatic startup at sign-in").c_str());
        AppendMenuW(menu, MF_STRING, Uninstall, text(L"OBS起動アシストをアンインストール…", L"Uninstall OBS startup assistant…").c_str());
        AppendMenuW(menu, MF_STRING, Exit, text(L"OBS起動アシストを終了", L"Exit OBS startup assistant").c_str());
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
        UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
        if (command == Select) launch();
        if (command == Startup) setStartup(!fs::exists(startupPath()), startupPath(), executable());
        if (command == Uninstall && MessageBoxW(window, text(L"OBS起動アシストを削除しますか？\n自動起動も解除します。OBSのプラグインと設定は保持します。", L"Uninstall OBS startup assistant?\nAutomatic startup will be disabled. The OBS plugin and settings are retained.").c_str(),
            text(L"OBS起動アシスト", L"OBS startup assistant").c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            require(safeInstalledPath(executable(), folder(FOLDERID_LocalAppData)), "Run this from the installed OBS startup assistant");
            setStartup(false, startupPath(), executable()); startRemoval(executable(), GetCurrentProcessId(), true); DestroyWindow(window);
        }
        if (command == Exit) DestroyWindow(window);
    }
    void benchmarkReport()
    {
        PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};
        winrt::check_bool(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)));
        FILETIME created{}, exited{}, kernel{}, user{};
        winrt::check_bool(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user));
        auto time = [](FILETIME value) { return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
        std::ostringstream json;
        json << "{\"elapsedMs\":" << GetTickCount64() - started << ",\"cpu100ns\":" << time(kernel) + time(user)
             << ",\"workingSet\":" << memory.WorkingSetSize << ",\"privateBytes\":" << memory.PrivateUsage
             << ",\"processScans\":" << scans << ",\"timerTicks\":" << ticks << ",\"processDetections\":" << detections
             << ",\"hookInstalls\":" << hookInstalls << ",\"hookRemovals\":" << hookRemovals
             << ",\"discoveryActive\":" << (obsEvents ? "true" : "false")
             << ",\"eventDriven\":" << (process || (obsEvents && directoryChange != INVALID_HANDLE_VALUE) ? "true" : "false") << "}";
        writeFile(report, json.str());
    }
    LRESULT message(HWND target, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == taskbarCreated && !benchmark) { addTray(); return 0; }
        if (message == WM_HOTKEY && wp == 1) { launch(); return 0; }
        if (message == WM_APP + 2) { discoveryQueued = false; refresh(true); return 0; }
        if (message == trayMessage && (LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == NIN_KEYSELECT)) { menu(); return 0; }
        if (message == WM_TIMER) {
            if (benchmark && wp == 2) { benchmarkReport(); DestroyWindow(target); return 0; }
            ++ticks;
            if (benchmark && GetTickCount64() - started >= duration) { benchmarkReport(); DestroyWindow(target); }
            else refresh();
            return 0;
        }
        if (message == WM_CLOSE || (message == WM_ENDSESSION && wp)) { DestroyWindow(target); return 0; }
        if (message == WM_QUERYENDSESSION) return TRUE;
        if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(target, message, wp, lp);
    }
public:
    Application(fs::path directory, bool benchmark = false, ULONGLONG duration = 0, fs::path report = {}, bool forcePolling = false)
        : config(std::move(directory)), request(config / L"launch-request.txt"), settings(config / L"settings.json"), report(std::move(report)), benchmark(benchmark), forcePolling(forcePolling), duration(duration)
    {
        if (benchmark) processName = L"obs-qr-native-qa.exe";
        ProcessIdToSessionId(GetCurrentProcessId(), &session); fs::create_directories(config);
        WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr); type.lpfnWndProc = procedure; type.lpszClassName = L"OBSQuickRecordLauncherWindow";
        require(RegisterClassW(&type) != 0, "Cannot register launcher window");
        window = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"OBS startup assistant", 0, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, this);
        winrt::check_bool(window != nullptr);
        if (!benchmark) {
            image = static_cast<HICON>(LoadImageW(type.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
            winrt::check_bool(image != nullptr); addTray();
        }
        directoryChange = FindFirstChangeNotificationW(config.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
        eventOwner = this;
        refresh(true);
        if (benchmark) winrt::check_bool(SetTimer(window, 2, static_cast<UINT>(duration), nullptr) != 0);
    }
    ~Application()
    {
        eventOwner = nullptr;
        stopDiscovery();
        if (benchmark) KillTimer(window, 2);
        if (timerInterval) KillTimer(window, 1);
        releaseKey();
        if (!benchmark) Shell_NotifyIconW(NIM_DELETE, &tray);
        if (directoryChange != INVALID_HANDLE_VALUE) FindCloseChangeNotification(directoryChange);
        if (image) DestroyIcon(image);
        if (IsWindow(window)) DestroyWindow(window);
    }
    int run(bool launchImmediately)
    {
        if (launchImmediately) {
            try { launch(); }
            catch (...) { error(exceptionMessage()); refresh(); }
        }
        for (;;) {
            HANDLE handles[2]; DWORD count = 0, processIndex = MAXDWORD, directoryIndex = MAXDWORD;
            if (process) { processIndex = count; handles[count++] = process.value; }
            if (directoryChange != INVALID_HANDLE_VALUE) { directoryIndex = count; handles[count++] = directoryChange; }
            DWORD result = MsgWaitForMultipleObjectsEx(count, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (result == WAIT_FAILED) winrt::throw_last_error();
            if (result == WAIT_OBJECT_0 + processIndex && processIndex != MAXDWORD) { process.reset(); processId = 0; nextScan = 0; refresh(true); }
            if (result == WAIT_OBJECT_0 + directoryIndex && directoryIndex != MAXDWORD) {
                if (!FindNextChangeNotification(directoryChange)) { FindCloseChangeNotification(directoryChange); directoryChange = INVALID_HANDLE_VALUE; }
                refresh();
            }
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
    }
};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0; auto raw = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> args; for (int i = 0; i < count; ++i) args.emplace_back(raw[i]); LocalFree(raw);
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (args.size() == 2 && args[1] == L"--check") return assist::runChecks() ? 0 : 1;
        if (args.size() >= 2 && args.size() <= 4 && args[1] == L"--window-event-check") {
            const auto hold = args.size() >= 3 ? std::stoul(args[2]) : 600UL;
            assist::require(hold >= 100 && hold <= 60000, "Invalid event fixture duration");
            WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr); type.lpfnWndProc = DefWindowProcW; type.lpszClassName = L"QtQuickRecordNativeTest";
            assist::require(RegisterClassW(&type) != 0, "Cannot create event test class");
            HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"event test", 0, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, nullptr);
            winrt::check_bool(window != nullptr);
            for (int i = 0; i < 20; ++i) NotifyWinEvent(EVENT_OBJECT_SHOW, window, OBJID_WINDOW, CHILDID_SELF);
            if (args.size() == 4) assist::writeFile(args[3], "ready");
            Sleep(hold); DestroyWindow(window); return 0;
        }
        if (args.size() == 4 && (args[1] == L"--benchmark" || args[1] == L"--benchmark-fallback")) {
            GUID id{}; winrt::check_hresult(CoCreateGuid(&id)); wchar_t guid[40]{}; StringFromGUID2(id, guid, 40);
            auto directory = assist::fs::temp_directory_path() / (L"obs-quick-record-benchmark-" + std::wstring(guid));
            assist::require(assist::fs::create_directory(directory), "Cannot create benchmark directory");
            try {
                assist::writeFile(directory / L"settings.json", "{\"launcherModifiers\":1,\"launcherVirtualKey\":82}");
                auto seconds = std::stoull(args[2]); assist::require(seconds >= 1 && seconds <= 3600, "Invalid benchmark duration");
                { assist::Application app(directory, true, seconds * 1000, args[3], args[1] == L"--benchmark-fallback"); assist::writeFile(args[3] + L".ready", "ready"); app.run(false); }
                assist::fs::remove_all(directory); return 0;
            } catch (...) { assist::fs::remove_all(directory); throw; }
        }
        assist::Handle mutex(CreateMutexW(nullptr, TRUE, (L"Local\\OBSQuickRecordLauncher-" + assist::userSid()).c_str()));
        winrt::check_bool(static_cast<bool>(mutex)); if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
        assist::Application app(assist::folder(FOLDERID_RoamingAppData) / L"obs-studio/plugin_config/obs-quick-record");
        return app.run(args.size() == 2 && args[1] == L"--launch");
    } catch (...) {
        const auto error = assist::exceptionMessage();
        std::cerr << assist::utf8(error) << '\n';
        if (args.size() == 1) MessageBoxW(nullptr, error.c_str(), L"OBS startup assistant", MB_OK | MB_ICONERROR);
        return 1;
    }
}
