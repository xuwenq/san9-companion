#include "RuntimeDiagnostics2026.h"
#include "Runtime2026.h"
#include "AppPaths.h"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace san9
{
namespace
{
// The log is a byte stream, so wide strings are converted to UTF-8 here.
std::string narrow(const std::wstring &value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
// A dump is only useful if the user can find it. The path is printed in the
// log line that accompanies every crash record. The file name carries the
// process id of the crashed image, so a companion dump and a game dump do
// not collide.
std::string crashDumpPath(const wchar_t *image, DWORD pid)
{
    std::wostringstream out;
    out << crashDumpFolder().wstring() << L"\\" << image << L"." << pid << L".dmp";
    return narrow(out.str());
}
std::string companionDumpPath()
{
    return crashDumpPath(L"San9Companion.exe", GetCurrentProcessId());
}
std::string gameDumpPath(DWORD pid)
{
    return crashDumpPath(L"San9PK.exe", pid);
}

const char *exitCodeMeaning(DWORD code)
{
    if (code == 0) return "normal exit";
    switch (code)
    {
    case 0xC0000005u: return "access violation (0xC0000005)";
    case 0xC000001Du: return "illegal instruction (0xC000001D)";
    case 0xC0000094u: return "integer divide by zero (0xC0000094)";
    case 0xC00000FDu: return "stack overflow (0xC00000FD)";
    case 0xC0000374u: return "heap corruption (0xC0000374)";
    case 0xC0000006u: return "in-page error (0xC0000006)";
    case 0xC000008Cu: return "floating point exception (0xC000008C)";
    case 0xC000013Au: return "integer overflow (0xC000013A)";
    case 0xC0000409u: return "stack buffer overrun (0xC0000409)";
    default: return "abnormal exit";
    }
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS *pointers)
{
    if (pointers && pointers->ExceptionRecord)
    {
        const auto code = pointers->ExceptionRecord->ExceptionCode;
        const auto address = pointers->ExceptionRecord->ExceptionAddress;
        std::ostringstream line;
        line << "CRASH unhandled exception code=0x" << std::hex << std::setw(8) << std::setfill('0') << code
             << " address=0x" << std::setw(8) << address << std::dec;
        appendRuntimeLog(line.str().c_str());
    }
    appendRuntimeLog(("CRASH dump path: " + companionDumpPath()).c_str());
    appendRuntimeLog("CRASH the dump is written by Windows Error Reporting; look in the folder above after the process closes");
    return EXCEPTION_EXECUTE_HANDLER;
}

void crashHandler()
{
    appendRuntimeLog("CRASH abnormal termination (terminate handler)");
    appendRuntimeLog(("CRASH dump path: " + companionDumpPath()).c_str());
}

bool g_handlersInstalled = false;
}

std::filesystem::path crashDumpFolder()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, _countof(buffer));
    if (length && length < _countof(buffer))
        return std::filesystem::path(buffer) / L"CrashDumps";
    return std::filesystem::path(L".") / L"CrashDumps";
}

bool enableCrashDumps(const wchar_t *executableName)
{
    if (!executableName || !*executableName) return false;
    // Windows Error Reporting writes a full dump only when the image file
    // execution option is set to 2. The value is per executable, so it does
    // not affect other programs on the machine.
    HKEY key = nullptr;
    const std::wstring subKey =
        L"Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\" +
        std::wstring(executableName);
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(),
        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return false;
    const DWORD dumpType = 2; // Full dump.
    const DWORD dumpCount = 3; // Keep the newest few.
    RegSetValueExW(key, L"DumpType", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&dumpType), sizeof(dumpType));
    RegSetValueExW(key, L"DumpCount", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&dumpCount), sizeof(dumpCount));
    RegCloseKey(key);
    return true;
}

void installCrashHandlers()
{
    if (g_handlersInstalled) return;
    g_handlersInstalled = true;
    SetUnhandledExceptionFilter(crashFilter);
    std::set_terminate(crashHandler);
}

void recordProcessExit(DWORD pid, DWORD exitCode)
{
    std::ostringstream line;
    line << "PROCESS EXIT pid=" << pid << " exitCode=0x" << std::hex << std::setw(8) << std::setfill('0') << exitCode
         << std::dec << " meaning=" << exitCodeMeaning(exitCode);
    appendRuntimeLog(line.str().c_str());
    if (exitCode != 0)
        appendRuntimeLog(("PROCESS CRASH suspected; dump path: " + gameDumpPath(pid)).c_str());
}

void recordCompanionFault(const char *reason)
{
    appendRuntimeLog((std::string("COMPANION FAULT: ") + (reason ? reason : "unknown")).c_str());
}
} // namespace san9
