#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <winrt/Windows.Data.Json.h>

namespace assist {
namespace fs = std::filesystem;
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE value = nullptr) : value(value) {}
    ~Handle() { reset(); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    void reset(HANDLE next = nullptr) { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = next; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
void require(bool condition, const char *message);
std::wstring text(const wchar_t *ja, const wchar_t *en);
fs::path folder(REFKNOWNFOLDERID id);
fs::path executable();
fs::path startupPath();
std::wstring userSid();
std::string utf8(const std::wstring &value);
std::wstring wide(const std::string &value);
std::string readFile(const fs::path &path, size_t maximum = 2 * 1024 * 1024);
void writeFile(const fs::path &path, const std::string &value);
void writeRequest(const fs::path &path, DWORD pid);
void appendLog(const fs::path &path, const std::wstring &message);
void setStartup(bool enabled, const fs::path &shortcut, const fs::path &exe);
bool safeInstalledPath(const fs::path &exe, const fs::path &root);
std::wstring removalScript(const fs::path &exe, DWORD parentId, bool showResult);
void startRemoval(const fs::path &exe, DWORD parentId, bool showResult);
struct Binding {
    UINT modifiers = MOD_ALT, key = 'R';
    bool operator==(const Binding &other) const { return modifiers == other.modifiers && key == other.key; }
};
Binding parseBinding(const std::wstring &json);
struct BindingCache {
    FILETIME stamp{};
    ULONGLONG size = 0;
    bool cached = false, existed = false;
    Binding value;
    Binding read(const fs::path &path);
};
bool runChecks();
}
