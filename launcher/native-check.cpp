#include "native-support.hpp"
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <iostream>

namespace assist {
static HANDLE start(const fs::path &application, std::wstring command)
{
    STARTUPINFOW info{sizeof(info)}; PROCESS_INFORMATION child{};
    winrt::check_bool(CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &info, &child));
    CloseHandle(child.hThread); return child.hProcess;
}
static DWORD run(const fs::path &application, std::wstring command, DWORD timeout = 30000)
{
    Handle process(start(application, std::move(command)));
    if (WaitForSingleObject(process.value, timeout) != WAIT_OBJECT_0) {
        TerminateProcess(process.value, 1); WaitForSingleObject(process.value, 5000);
        throw std::runtime_error("Test child process timed out");
    }
    DWORD result = 1; winrt::check_bool(GetExitCodeProcess(process.value, &result)); return result;
}
bool runChecks()
{
    GUID id{}; winrt::check_hresult(CoCreateGuid(&id)); wchar_t guid[40]{}; StringFromGUID2(id, guid, 40);
    const auto root = fs::temp_directory_path() / (L"obs-quick-record-native-check-" + std::wstring(guid));
    require(fs::create_directory(root), "Cannot create test directory");
    try {
        auto parse = [](const wchar_t *json, UINT modifiers, UINT key) { require(parseBinding(json) == Binding{modifiers, key}, "Shortcut parse mismatch"); };
        parse(L"{}", MOD_ALT, 'R');
        parse(LR"({"launcherModifiers":6,"launcherVirtualKey":121})", MOD_CONTROL | MOD_SHIFT, VK_F10);
        parse(LR"({"launcherModifiers":0,"launcherVirtualKey":0})", 0, 0);
        parse(LR"({"hotkey":[]})", 0, 0);
        parse(LR"({"hotkey":[{"alt":true,"key":"OBS_KEY_R"}]})", MOD_ALT, 'R');
        parse(LR"({"hotkey":[{"control":true,"key":"OBS_KEY_F24"}]})", MOD_CONTROL, VK_F24);
        parse(LR"({"unrelated":"日本語 \"{}","launcherVirtualKey":82,"launcherModifiers":1})", MOD_ALT, 'R');
        for (int digit = 0; digit < 10; ++digit) {
            auto json = L"{\"hotkey\":[{\"alt\":true,\"key\":\"OBS_KEY_" + std::to_wstring(digit) + L"\"}]}";
            require(parseBinding(json) == Binding{MOD_ALT, static_cast<UINT>('0' + digit)}, "Legacy digit mismatch");
        }
        for (auto invalid : {L"invalid", L"[]", LR"({"launcherVirtualKey":256})", LR"({"launcherVirtualKey":-1})", LR"({"launcherVirtualKey":82.5})", LR"({"launcherModifiers":16,"launcherVirtualKey":82})", LR"({"hotkey":[{"key":"OBS_KEY_999"}]})"}) {
            bool rejected = false; try { parseBinding(invalid); } catch (...) { rejected = true; }
            require(rejected, "Invalid shortcut was accepted");
        }
        const auto settings = root / L"settings.json";
        BindingCache cache; require(cache.read(settings) == Binding{}, "Missing settings default failed");
        writeFile(settings, R"({"launcherModifiers":6,"launcherVirtualKey":121})");
        require(cache.read(settings) == Binding{6,121}, "Changed binding not loaded");
        auto stamp = cache.stamp; require(cache.read(settings) == Binding{6,121} && CompareFileTime(&stamp, &cache.stamp) == 0, "Unchanged settings cache changed");
        writeFile(settings, "invalid");
        bool rejected = false; try { cache.read(settings); } catch (...) { rejected = true; }
        require(rejected, "Invalid settings were cached"); fs::remove(settings);
        require(cache.read(settings) == Binding{}, "Deleted settings not reset");

        const auto request = root / L"request.txt";
        writeRequest(request, 42); require(readFile(request) == "42\nexit-after-capture", "Initial request mismatch");
        std::atomic<bool> stop{false}; std::atomic<int> invalidReads{0};
        std::thread reader([&] {
            while (!stop) {
                Handle file(CreateFileW(request.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
                if (!file) continue;
                char data[128]{}; DWORD bytes = 0;
                if (ReadFile(file.value, data, sizeof(data), &bytes, nullptr)) {
                    std::string value(data, bytes); auto marker = value.find("\nexit-after-capture");
                    if (marker == std::string::npos || value.size() != marker + 19 || value.find_first_not_of("0123456789", 0) != marker) ++invalidReads;
                }
                SwitchToThread();
            }
        });
        try { for (DWORD pid = 43; pid < 143; ++pid) writeRequest(request, pid); }
        catch (...) { stop = true; reader.join(); throw; }
        stop = true; reader.join(); require(invalidReads == 0, "Reader saw a partial request");
        auto blocked = root / L"blocked"; fs::create_directory(blocked);
        rejected = false; try { writeRequest(blocked, 44); } catch (...) { rejected = true; }
        require(rejected, "Invalid request destination accepted");
        for (const auto &item : fs::directory_iterator(root)) require(item.path().extension() != L".tmp", "Temporary request leaked");

        auto log = root / L"launcher.log";
        writeFile(log, std::string(1024 * 1024, 'x') + "newest entry"); appendLog(log, L"rotated");
        require(fs::file_size(log.wstring() + L".1") == 256 * 1024 && readFile(log.wstring() + L".1").find("newest entry") != std::string::npos, "Old log was not bounded");
        appendLog(log, std::wstring(1000000, L'x')); require(fs::file_size(log) < 17000, "Long log message was not bounded");

        auto installed = root / L"OBSQuickRecordLauncher"; fs::create_directory(installed);
        auto exe = installed / L"obs-quick-record-launcher.exe", link = root / L"startup.lnk";
        fs::copy_file(executable(), exe); writeFile(installed / L"keep.txt", "unrelated file");
        require(safeInstalledPath(exe, root) && !safeInstalledPath(executable(), root) && !safeInstalledPath(exe, root / L"outside"), "Uninstall path safety check failed");
        setStartup(true, link, exe);
        Microsoft::WRL::ComPtr<IShellLinkW> shortcut;
        winrt::check_hresult(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shortcut)));
        Microsoft::WRL::ComPtr<IPersistFile> persisted; winrt::check_hresult(shortcut.As(&persisted));
        winrt::check_hresult(persisted->Load(link.c_str(), STGM_READ));
        wchar_t target[32768]{}, directory[32768]{};
        winrt::check_hresult(shortcut->GetPath(target, 32768, nullptr, SLGP_RAWPATH));
        winrt::check_hresult(shortcut->GetWorkingDirectory(directory, 32768));
        require(fs::path(target) == exe && fs::path(directory) == installed, "Startup link target differs");
        persisted.Reset(); shortcut.Reset(); setStartup(false, link, exe); setStartup(false, link, exe); require(!fs::exists(link), "Startup removal failed");

        auto script = root / L"remove.ps1";
        writeFile(script, "\xEF\xBB\xBF" + utf8(removalScript(exe, MAXLONG, false)));
        writeFile(exe, readFile(exe) + "changed");
        auto powershell = folder(FOLDERID_System) / L"WindowsPowerShell/v1.0/powershell.exe";
        auto command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
        require(run(powershell, command) != 0 && fs::exists(exe), "Changed executable was removed");
        writeFile(script, "\xEF\xBB\xBF" + utf8(removalScript(exe, MAXLONG, false)));
        require(run(powershell, command) == 0 && !fs::exists(exe) && fs::exists(installed / L"keep.txt"), "Safe removal failed");

        auto image = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0));
        require(image != nullptr, "Native tray icon missing"); DestroyIcon(image);
        auto report = root / L"probe.json";
        require(run(executable(), L"\"" + executable().wstring() + L"\" --benchmark 3 \"" + report.wstring() + L"\"", 15000) == 0, "Isolated native loop failed");
        auto metrics = winrt::Windows::Data::Json::JsonObject::Parse(wide(readFile(report)));
        require(metrics.GetNamedNumber(L"elapsedMs") >= 3000 && metrics.GetNamedNumber(L"timerTicks") <= 4 && metrics.GetNamedNumber(L"processScans") <= 4, "Idle loop is spinning");
        if (metrics.GetNamedBoolean(L"eventDriven")) require(metrics.GetNamedNumber(L"timerTicks") == 0, "Event-driven idle still polls");
        auto actor = root / L"obs-qr-native-qa.exe"; fs::copy_file(executable(), actor);
        auto eventsReport = root / L"events.json";
        Handle observer(start(executable(), L"\"" + executable().wstring() + L"\" --benchmark 3 \"" + eventsReport.wstring() + L"\""));
        const auto deadline = GetTickCount64() + 5000;
        while (!fs::exists(eventsReport.wstring() + L".ready") && GetTickCount64() < deadline) Sleep(10);
        require(fs::exists(eventsReport.wstring() + L".ready"), "Observer did not initialize");
        require(run(actor, L"\"" + actor.wstring() + L"\" --window-event-check") == 0, "Window-event child failed");
        require(WaitForSingleObject(observer.value, 10000) == WAIT_OBJECT_0, "Observer failed to stop"); observer.reset();
        auto events = winrt::Windows::Data::Json::JsonObject::Parse(wide(readFile(eventsReport)));
        if (events.GetNamedBoolean(L"eventDriven")) require(events.GetNamedNumber(L"processDetections") >= 1, "External process creation event was missed");
        require(run(executable(), L"\"" + executable().wstring() + L"\" --benchmark-fallback 3 \"" + report.wstring() + L"\"") == 0, "Polling fallback failed");
        metrics = winrt::Windows::Data::Json::JsonObject::Parse(wide(readFile(report)));
        require(!metrics.GetNamedBoolean(L"eventDriven") && metrics.GetNamedNumber(L"timerTicks") >= 1 && metrics.GetNamedNumber(L"processScans") >= 2, "Polling fallback did not discover processes");
        require(GetModuleHandleW(L"clr.dll") == nullptr, "Native launcher loaded the CLR");
        fs::remove_all(root); std::cout << "Native shortcut parsing, atomic requests, log bounds, startup, removal and idle loop passed\n"; return true;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; fs::remove_all(root); return false;
    }
}
}
