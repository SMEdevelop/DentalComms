// Small shared helpers: string conversion, paths, files.
#pragma once
#include <string>
#include <vector>
#include <windows.h>

std::wstring widen(const std::string& s);
std::string  narrow(const std::wstring& s);

std::string trim(const std::string& s);
std::string lower(const std::string& s);
bool iequals(const std::string& a, const std::string& b);
bool icontains(const std::string& hay, const std::string& needle);
std::vector<std::string> split(const std::string& s, char sep);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);

// Paths are all resolved relative to the folder holding the .exe so the app
// behaves the same however it was launched (shortcut, Explorer, console).
std::string exeDir();
std::string exePath();                        // full path of this .exe
std::string expandEnv(const std::string& s);  // "%LOCALAPPDATA%\x" -> "C:\Users\...\x"
std::string appPath(const std::string& relative);
std::string joinPath(const std::string& a, const std::string& b);
std::string baseName(const std::string& path);
std::string stripExt(const std::string& path);
std::string relativeToApp(const std::string& absolute);

bool fileExists(const std::string& path);
bool dirExists(const std::string& path);
void ensureDir(const std::string& path);
bool readFile(const std::string& path, std::string& out);
bool writeFileAtomic(const std::string& path, const std::string& data);
std::vector<std::string> listFiles(const std::string& dir, const std::string& ext);

// This computer's own network name, e.g. "RECEPTION-PC". Empty if Windows
// won't say.
std::string computerName();

std::string timeStamp();          // "14:03:22"
std::string lastErrorMessage(DWORD err = GetLastError());
