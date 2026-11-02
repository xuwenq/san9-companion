#pragma once
#include <Windows.h>
#include <cstdint>
#include <filesystem>

// Diagnostics shared by the runtime and the host application.
//
// The companion writes one line per meaningful event to San9Companion.log so
// the user can prove that a modification was actually applied. Logging must
// never disturb the injection transaction, so every entry point swallows its
// own failures.
namespace san9
{
// Folder Windows Error Reporting writes full process dumps to.
std::filesystem::path crashDumpFolder();
// Registers an executable for full WER dumps under the current user. The
// registry write needs no administrator rights. Returns false on failure.
bool enableCrashDumps(const wchar_t *executableName);
// Installs the unhandled exception and terminate handlers. Safe to call twice.
void installCrashHandlers();
// Records the exit of the game process. The exit code separates a clean exit
// from a crash: 0 is normal, 0xC0000005 is an access violation.
void recordProcessExit(DWORD pid, DWORD exitCode);
// Records an abnormal termination the host detected on its own.
void recordCompanionFault(const char *reason);
}
