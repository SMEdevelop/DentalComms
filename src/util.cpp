#include "util.h"
#include <algorithm>
#include <cctype>
#include <ctime>

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n,
                        nullptr, nullptr);
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return r;
}

bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() && lower(a) == lower(b);
}

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return false;
    return lower(hay).find(lower(needle)) != std::string::npos;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::string replaceAll(std::string s, const std::string& from,
                       const std::string& to) {
    if (from.empty()) return s;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
    return s;
}

std::string exeDir() {
    static std::string cached;
    if (!cached.empty()) return cached;
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    cached = narrow(slash == std::wstring::npos ? p : p.substr(0, slash));
    return cached;
}

std::string exePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return narrow(std::wstring(buf, n));
}

std::string expandEnv(const std::string& s) {
    const std::wstring w = widen(s);
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(w.c_str(), buf, MAX_PATH * 2);
    if (n == 0 || n > MAX_PATH * 2) return s;
    return narrow(buf);
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    // already absolute?
    if (b.size() > 1 && (b[1] == ':' || b[0] == '\\' || b[0] == '/')) return b;
    char last = a[a.size() - 1];
    if (last == '\\' || last == '/') return a + b;
    return a + "\\" + b;
}

std::string appPath(const std::string& relative) {
    return joinPath(exeDir(), relative);
}

std::string relativeToApp(const std::string& absolute) {
    std::string root = lower(exeDir()) + "\\";
    std::string low = lower(absolute);
    if (low.rfind(root, 0) == 0) return absolute.substr(root.size());
    return absolute;
}

std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stripExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("\\/");
    if (dot == std::string::npos) return path;
    if (slash != std::string::npos && dot < slash) return path;
    return path.substr(0, dot);
}

bool fileExists(const std::string& path) {
    DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::string& path) {
    DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void ensureDir(const std::string& path) {
    if (path.empty() || dirExists(path)) return;
    size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos && slash > 2)
        ensureDir(path.substr(0, slash));
    CreateDirectoryW(widen(path).c_str(), nullptr);
}

bool readFile(const std::string& path, std::string& out) {
    HANDLE h = CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = TRUE;
    if (sz.QuadPart > 0)
        ok = ReadFile(h, &out[0], (DWORD)sz.QuadPart, &got, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(got);
    // tolerate a UTF-8 BOM
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
        (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) ensureDir(path.substr(0, slash));
    std::string tmp = path + ".tmp";
    HANDLE h = CreateFileW(widen(tmp).c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = TRUE;
    if (!data.empty())
        ok = WriteFile(h, data.data(), (DWORD)data.size(), &wrote, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || wrote != data.size()) { DeleteFileW(widen(tmp).c_str()); return false; }
    if (!MoveFileExW(widen(tmp).c_str(), widen(path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(widen(tmp).c_str());
        return false;
    }
    return true;
}

std::vector<std::string> listFiles(const std::string& dir,
                                   const std::string& ext) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(widen(joinPath(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = narrow(fd.cFileName);
        if (ext.empty() || (name.size() > ext.size() &&
                            iequals(name.substr(name.size() - ext.size()), ext)))
            out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

std::string computerName() {
    static std::string cached;
    static bool tried = false;
    if (tried) return cached;
    tried = true;
    wchar_t buf[256];
    DWORD n = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    if (GetComputerNameExW(ComputerNameDnsHostname, buf, &n))
        cached = narrow(std::wstring(buf, n));
    return cached;
}

std::string timeStamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute,
                  st.wSecond);
    return buf;
}

std::string lastErrorMessage(DWORD err) {
    if (!err) return "";
    LPWSTR msg = nullptr;
    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, nullptr);
    std::string out = n ? trim(narrow(std::wstring(msg, n))) : "";
    if (msg) LocalFree(msg);
    if (out.empty()) out = "error " + std::to_string((unsigned)err);
    return out;
}
