#include "native-support.hpp"
#include <bcrypt.h>
#include <sddl.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace assist {
using namespace winrt::Windows::Data::Json;
using Microsoft::WRL::ComPtr;
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
std::wstring exceptionMessage()
{
    try { throw; }
    catch (const winrt::hresult_error &error) { return std::wstring(error.message()); }
    catch (const std::exception &error) { return wide(error.what()); }
    catch (...) { return L"Unexpected startup assistant error"; }
}
std::wstring text(const wchar_t *ja, const wchar_t *en)
{ return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE ? ja : en; }
fs::path folder(REFKNOWNFOLDERID id)
{
    PWSTR value = nullptr;
    winrt::check_hresult(SHGetKnownFolderPath(id, 0, nullptr, &value));
    fs::path result(value); CoTaskMemFree(value); return result;
}
fs::path executable()
{
    std::wstring value(32768, L'\0');
    DWORD count = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    require(count && count < value.size(), "Cannot read executable path");
    value.resize(count); return value;
}
fs::path startupPath() { return folder(FOLDERID_Startup) / L"OBS Quick Record Launcher.lnk"; }
std::wstring userSid()
{
    Handle token; winrt::check_bool(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value));
    DWORD bytes = 0; GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    winrt::check_bool(GetTokenInformation(token.value, TokenUser, buffer.data(), bytes, &bytes));
    PWSTR sid = nullptr;
    winrt::check_bool(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid));
    std::wstring result(sid); LocalFree(sid); return result;
}
std::string utf8(const std::wstring &value)
{
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    winrt::check_bool(size);
    std::string result(size, '\0');
    winrt::check_bool(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr));
    return result;
}
std::wstring wide(const std::string &value)
{
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    winrt::check_bool(size);
    std::wstring result(size, L'\0');
    winrt::check_bool(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size));
    return result;
}
std::string readFile(const fs::path &path, size_t maximum)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(input.good(), "Cannot open file");
    const auto length = input.tellg();
    require(length >= 0 && static_cast<unsigned long long>(length) <= maximum, "File is too large");
    std::string result(static_cast<size_t>(length), '\0'); input.seekg(0);
    if (!result.empty()) input.read(result.data(), static_cast<std::streamsize>(result.size()));
    require(input.good(), "Cannot read file"); return result;
}
void writeFile(const fs::path &path, const std::string &value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size())); output.close();
    require(output.good(), "Cannot write file");
}
static std::wstring uniqueName()
{
    GUID id{}; winrt::check_hresult(CoCreateGuid(&id));
    wchar_t value[40]{}; StringFromGUID2(id, value, 40); return value;
}
void writeRequest(const fs::path &path, DWORD pid)
{
    auto temporary = path; temporary += L"." + uniqueName() + L".tmp";
    try {
        writeFile(temporary, std::to_string(pid) + "\nexit-after-capture");
        winrt::check_bool(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING));
    } catch (...) { DeleteFileW(temporary.c_str()); throw; }
}
void appendLog(const fs::path &path, const std::wstring &message)
{
    constexpr size_t limit = 256 * 1024;
    if (fs::exists(path) && fs::file_size(path) >= limit) {
        std::ifstream input(path, std::ios::binary); input.seekg(-static_cast<std::streamoff>(limit), std::ios::end);
        std::string tail(limit, '\0'); input.read(tail.data(), static_cast<std::streamsize>(tail.size()));
        require(input.good(), "Cannot rotate log"); input.close();
        writeFile(path.wstring() + L".1", tail); fs::remove(path);
    }
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t stamp[32]{}; swprintf_s(stamp, L"%04u-%02u-%02uT%02u:%02u:%02u ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    output << utf8(stamp + message.substr(0, 4096)) << '\n';
    require(output.good(), "Cannot append log");
}
void setStartup(bool enabled, const fs::path &shortcut, const fs::path &exe)
{
    if (!enabled) { fs::remove(shortcut); return; }
    ComPtr<IShellLinkW> link;
    winrt::check_hresult(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)));
    winrt::check_hresult(link->SetPath(exe.c_str()));
    winrt::check_hresult(link->SetWorkingDirectory(exe.parent_path().c_str()));
    ComPtr<IPersistFile> file; winrt::check_hresult(link.As(&file));
    winrt::check_hresult(file->Save(shortcut.c_str(), TRUE));
}
static bool equal(const std::wstring &a, const std::wstring &b)
{ return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL; }
bool safeInstalledPath(const fs::path &exe, const fs::path &root)
{
    auto absolute = fs::absolute(exe).lexically_normal(), base = fs::absolute(root).lexically_normal();
    if (!equal(absolute.filename().wstring(), L"obs-quick-record-launcher.exe") ||
        !equal(absolute.parent_path().filename().wstring(), L"OBSQuickRecordLauncher")) return false;
    for (auto path = absolute; path != path.root_path(); path = path.parent_path()) {
        if (equal(path.wstring(), base.wstring())) return true;
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    }
    return false;
}
static std::wstring hashFile(const fs::path &path)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "Cannot open SHA-256");
    try {
        require(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0, "Cannot create SHA-256");
        std::ifstream input(path, std::ios::binary); require(input.good(), "Cannot hash file");
        BYTE buffer[32768];
        while (input.read(reinterpret_cast<char *>(buffer), sizeof(buffer)) || input.gcount())
            require(BCryptHashData(hash, buffer, static_cast<ULONG>(input.gcount()), 0) >= 0, "Cannot hash bytes");
        require(input.eof(), "Cannot finish reading hash input");
        BYTE digest[32]; require(BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0, "Cannot finish SHA-256");
        std::wstring result; for (BYTE byte : digest) { result += L"0123456789ABCDEF"[byte >> 4]; result += L"0123456789ABCDEF"[byte & 15]; }
        BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); return result;
    } catch (...) { if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); throw; }
}
static std::wstring quotePs(std::wstring value)
{
    size_t offset = 0; while ((offset = value.find(L"'", offset)) != std::wstring::npos) { value.insert(offset, L"'"); offset += 2; }
    return L"'" + value + L"'";
}
std::wstring removalScript(const fs::path &exe, DWORD parentId, bool showResult)
{
    return L"$ErrorActionPreference='Stop'\n$exe=" + quotePs(exe.wstring()) + L"\n$expectedHash=" + quotePs(hashFile(exe)) +
        L"\n$parentId=" + std::to_wstring(parentId) + L"\n$showResult=" + (showResult ? L"$true" : L"$false") +
        L"\n$success=" + quotePs(text(L"OBS起動アシストを削除しました。プラグインと設定は保持しています。", L"OBS startup assistant removed. The plugin and settings are retained.")) +
        L"\n$failure=" + quotePs(text(L"OBS起動アシストの削除に失敗しました。", L"Could not remove OBS startup assistant.")) + LR"(
try {
    $parent=Get-Process -Id $parentId -ErrorAction SilentlyContinue
    if ($parent) {
        if ($parent.Path -ne $exe) { throw 'The process no longer matches the Launcher.' }
        if (!$parent.WaitForExit(15000)) { throw 'The Launcher did not exit.' }
    }
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $expectedHash) { throw 'The file changed; removal canceled.' }
    [IO.File]::Delete($exe)
    $folder=Split-Path -Parent $exe
    if ((Get-ChildItem -LiteralPath $folder -Force | Measure-Object).Count -eq 0) { [IO.Directory]::Delete($folder, $false) }
    if ($showResult) { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($success, 'OBS startup assistant') }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    if ($showResult) { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($failure + [Environment]::NewLine + $_.Exception.Message, 'OBS startup assistant') }
    exit 1
} finally { Remove-Item -LiteralPath $PSCommandPath -ErrorAction SilentlyContinue }
)";
}
void startRemoval(const fs::path &exe, DWORD parentId, bool showResult)
{
    auto script = fs::temp_directory_path() / (L"obs-quick-record-remove-" + uniqueName() + L".ps1");
    writeFile(script, "\xEF\xBB\xBF" + utf8(removalScript(exe, parentId, showResult)));
    std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
    auto powershell = folder(FOLDERID_System) / L"WindowsPowerShell/v1.0/powershell.exe";
    STARTUPINFOW info{sizeof(info)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &info, &process)) {
        DeleteFileW(script.c_str()); winrt::throw_last_error();
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
static UINT number(const JsonObject &json, const wchar_t *name)
{
    if (!json.HasKey(name)) return 0;
    double value = json.GetNamedNumber(name);
    require(std::isfinite(value) && value >= 0 && value <= std::numeric_limits<UINT>::max() && std::floor(value) == value, "Invalid shortcut number");
    return static_cast<UINT>(value);
}
Binding parseBinding(const std::wstring &json)
{
    auto object = JsonObject::Parse(json);
    Binding result;
    if (object.HasKey(L"launcherVirtualKey")) result = {number(object, L"launcherModifiers"), number(object, L"launcherVirtualKey")};
    else if (object.HasKey(L"hotkey")) {
        auto keys = object.GetNamedArray(L"hotkey");
        if (!keys.Size()) return {0, 0};
        auto key = keys.GetObjectAt(0);
        std::wstring name(key.GetNamedString(L"key"));
        require(name.rfind(L"OBS_KEY_", 0) == 0, "Invalid OBS shortcut"); name.erase(0, 8);
        result = {0, 0};
        if (name.size() == 1 && ((name[0] >= L'A' && name[0] <= L'Z') || (name[0] >= L'0' && name[0] <= L'9'))) result.key = name[0];
        else if (name.size() == 7 && _wcsnicmp(name.c_str(), L"NUMPAD", 6) == 0 && name[6] >= L'0' && name[6] <= L'9') result.key = VK_NUMPAD0 + name[6] - L'0';
        else if (name.size() >= 2 && name[0] == L'F' && name.size() <= 3 && name.find_first_not_of(L"0123456789", 1) == std::wstring::npos) {
            auto index = std::stoul(name.substr(1)); if (index >= 1 && index <= 24) result.key = VK_F1 + static_cast<UINT>(index) - 1;
        } else {
            static const std::pair<const wchar_t *, UINT> names[] = {{L"SPACE",VK_SPACE},{L"RETURN",VK_RETURN},{L"ENTER",VK_RETURN},{L"ESCAPE",VK_ESCAPE},{L"TAB",VK_TAB},{L"BACK",VK_BACK},{L"HOME",VK_HOME},{L"END",VK_END},{L"LEFT",VK_LEFT},{L"RIGHT",VK_RIGHT},{L"UP",VK_UP},{L"DOWN",VK_DOWN},{L"INSERT",VK_INSERT},{L"DELETE",VK_DELETE},{L"PAGEUP",VK_PRIOR},{L"PAGEDOWN",VK_NEXT},{L"PAUSE",VK_PAUSE}};
            for (const auto &entry : names) if (equal(name, entry.first)) result.key = entry.second;
            // Names previously accepted by the managed Keys enum. Current OBS
            // exports the actual virtual key; these preserve older configuration.
            static const std::pair<const wchar_t *, UINT> legacy[] = {
                {L"BACKSPACE",VK_BACK},{L"PRIOR",VK_PRIOR},{L"NEXT",VK_NEXT},{L"CAPITAL",VK_CAPITAL},
                {L"CAPSLOCK",VK_CAPITAL},{L"NUMLOCK",VK_NUMLOCK},{L"SCROLL",VK_SCROLL},
                {L"MULTIPLY",VK_MULTIPLY},{L"ADD",VK_ADD},{L"SEPARATOR",VK_SEPARATOR},
                {L"SUBTRACT",VK_SUBTRACT},{L"DECIMAL",VK_DECIMAL},{L"DIVIDE",VK_DIVIDE},
                {L"PRINTSCREEN",VK_SNAPSHOT},{L"SNAPSHOT",VK_SNAPSHOT},{L"APPS",VK_APPS},
                {L"OEMSEMICOLON",VK_OEM_1},{L"OEMPLUS",VK_OEM_PLUS},{L"OEMCOMMA",VK_OEM_COMMA},
                {L"OEMMINUS",VK_OEM_MINUS},{L"OEMPERIOD",VK_OEM_PERIOD},{L"OEMQUESTION",VK_OEM_2},
                {L"OEMTILDE",VK_OEM_3},{L"OEMOPENBRACKETS",VK_OEM_4},{L"OEMPIPE",VK_OEM_5},
                {L"OEMCLOSEBRACKETS",VK_OEM_6},{L"OEMQUOTES",VK_OEM_7},{L"OEMBACKSLASH",VK_OEM_102},
                {L"VOLUMEMUTE",VK_VOLUME_MUTE},{L"VOLUMEDOWN",VK_VOLUME_DOWN},{L"VOLUMEUP",VK_VOLUME_UP},
                {L"MEDIANEXTTRACK",VK_MEDIA_NEXT_TRACK},{L"MEDIAPREVIOUSTRACK",VK_MEDIA_PREV_TRACK},
                {L"MEDIASTOP",VK_MEDIA_STOP},{L"MEDIAPLAYPAUSE",VK_MEDIA_PLAY_PAUSE}
            };
            for (const auto &entry : legacy) if (equal(name, entry.first)) result.key = entry.second;
        }
        require(result.key != 0, "Open OBS once to synchronize this shortcut");
        const std::pair<const wchar_t *, UINT> modifiers[] = {{L"alt",MOD_ALT},{L"control",MOD_CONTROL},{L"shift",MOD_SHIFT},{L"command",MOD_WIN}};
        for (const auto &entry : modifiers) if (key.HasKey(entry.first) && key.GetNamedBoolean(entry.first)) result.modifiers |= entry.second;
    }
    require(result.modifiers <= 15 && result.key <= 255, "Invalid launcher shortcut"); return result;
}
Binding BindingCache::read(const fs::path &path)
{
    WIN32_FILE_ATTRIBUTE_DATA info{};
    bool exists = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) != FALSE;
    if (!exists && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) winrt::throw_last_error();
    ULONGLONG length = (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (cached && existed == exists && length == size && CompareFileTime(&info.ftLastWriteTime, &stamp) == 0) return value;
    Binding parsed;
    if (exists) {
        auto bytes = readFile(path);
        if (bytes.rfind("\xFF\xFE", 0) == 0 || bytes.rfind("\xFE\xFF", 0) == 0) {
            require(bytes.size() % 2 == 0, "Invalid UTF-16 settings");
            const bool little = static_cast<unsigned char>(bytes[0]) == 0xFF;
            std::wstring json;
            for (size_t offset = 2; offset < bytes.size(); offset += 2) {
                auto a = static_cast<unsigned char>(bytes[offset]), b = static_cast<unsigned char>(bytes[offset + 1]);
                json += static_cast<wchar_t>(little ? a | (b << 8) : (a << 8) | b);
            }
            parsed = parseBinding(json);
        } else {
            if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);
            parsed = parseBinding(wide(bytes));
        }
    }
    value = parsed; cached = true; existed = exists; size = length; stamp = info.ftLastWriteTime; return value;
}
}
