#pragma once
#include <Windows.h>
#include <TlHelp32.h>

struct ProcessApi2026 {
    decltype(&::FindWindowA) findWindow = ::FindWindowA;
    decltype(&::GetWindowThreadProcessId) windowProcess = ::GetWindowThreadProcessId;
    decltype(&::OpenProcess) openProcess = ::OpenProcess;
    decltype(&::CloseHandle) closeHandle = ::CloseHandle;
    decltype(&::VirtualAllocEx) allocate = ::VirtualAllocEx;
    decltype(&::VirtualFreeEx) release = ::VirtualFreeEx;
    decltype(&::VirtualProtectEx) protect = ::VirtualProtectEx;
    decltype(&::ReadProcessMemory) read = ::ReadProcessMemory;
    decltype(&::WriteProcessMemory) write = ::WriteProcessMemory;
    decltype(&::FlushInstructionCache) flush = ::FlushInstructionCache;
    decltype(&::RegisterWindowMessageA) registerMessage = ::RegisterWindowMessageA;
    decltype(&::VirtualQueryEx) query = ::VirtualQueryEx;
    decltype(&::GetProcessId) processId = ::GetProcessId;
    decltype(&::GetProcessTimes) processTimes = ::GetProcessTimes;
    decltype(&::GetExitCodeProcess) exitCode = ::GetExitCodeProcess;
    decltype(&::WaitForSingleObject) wait = ::WaitForSingleObject;
    decltype(&::CreateToolhelp32Snapshot) snapshot = ::CreateToolhelp32Snapshot;
    decltype(&::Thread32First) threadFirst = ::Thread32First;
    decltype(&::Thread32Next) threadNext = ::Thread32Next;
    decltype(&::OpenThread) openThread = ::OpenThread;
    decltype(&::SuspendThread) suspendThread = ::SuspendThread;
    decltype(&::ResumeThread) resumeThread = ::ResumeThread;
    decltype(&::GetThreadContext) threadContext = ::GetThreadContext;
    decltype(&::Module32FirstW) moduleFirst = ::Module32FirstW;
    decltype(&::Module32NextW) moduleNext = ::Module32NextW;
};
