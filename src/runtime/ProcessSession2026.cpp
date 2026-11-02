#include "ProcessSession2026.h"
#include <algorithm>

[[noreturn]] void ProcessSession2026::fail(RuntimeFailure2026 failure) {
    state_.failure = failure;
    throw ProcessOperationFailure2026{};
}
ProcessState2026 ProcessSession2026::observe() {
    auto state = ProcessState2026::None;
    observationError_ = 0;
    if (state_.process) {
        FILETIME created{}, exited{}, kernel{}, user{};
        const DWORD pid = api_.processId(state_.process);
        if (!pid) {
            observationError_ = GetLastError();
            state = ProcessState2026::Unavailable;
        } else if (!api_.processTimes(state_.process, &created, &exited, &kernel, &user)) {
            observationError_ = GetLastError();
            state = ProcessState2026::Unavailable;
        } else if (!state_.identity.verified || pid != state_.identity.pid
            || created.dwLowDateTime != state_.identity.created.dwLowDateTime
            || created.dwHighDateTime != state_.identity.created.dwHighDateTime)
            state = ProcessState2026::IdentityChanged;
        else {
            const DWORD wait = api_.wait(state_.process, 0);
            if (wait == WAIT_FAILED) observationError_ = GetLastError();
            state = wait == WAIT_OBJECT_0 ? ProcessState2026::Exited
                : wait == WAIT_TIMEOUT ? ProcessState2026::Alive : ProcessState2026::Unavailable;
        }
    }
    state_.processState = state;
    return state;
}
void ProcessSession2026::require(HANDLE process) {
    if (!process || process != state_.process)
        fail({RuntimeOperation2026::VerifyProcess});
    if (observe() != ProcessState2026::Alive)
        fail({RuntimeOperation2026::VerifyProcess, 0, 0, 0, observationError_});
}
HANDLE ProcessSession2026::acquire(DWORD access, BOOL inherit, DWORD pid, HWND window) {
    auto& entry = state_.journal.append({ChangeKind2026::Handle});
    entry.process = api_.openProcess(access | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, inherit, pid);
    if (!entry.process) {
        const DWORD error = GetLastError();
        state_.journal.discardLast();
        fail({RuntimeOperation2026::OpenProcess, 0, 0, 0, error});
    }
    entry.completed = true;
    state_.process = entry.process;
    state_.identity = {};
    state_.identity.pid = pid;
    state_.identity.window = window;
    return entry.process;
}
void ProcessSession2026::verifyConnection() {
    FILETIME exited{}, kernel{}, user{};
    const DWORD pid = api_.processId(state_.process);
    if (pid != state_.identity.pid)
        fail({RuntimeOperation2026::CaptureIdentity, 0, 0, 0, pid ? 0 : GetLastError()});
    if (!api_.processTimes(state_.process, &state_.identity.created, &exited, &kernel, &user))
        fail({RuntimeOperation2026::CaptureIdentity, 0, 0, 0, GetLastError()});
    state_.identity.verified = true;
    require(state_.process);
    DWORD windowPid = 0;
    const auto thread = api_.windowProcess(state_.identity.window, &windowPid);
    if (!thread || windowPid != state_.identity.pid)
        fail({RuntimeOperation2026::VerifyWindow, 0, 0, 0, thread ? 0 : GetLastError()});
}
HWND ProcessSession2026::findWindow(LPCSTR className, LPCSTR title) {
    const auto window = api_.findWindow(className, title);
    if (state_.process) {
        require(state_.process);
        DWORD pid = 0;
        if (!window) fail({RuntimeOperation2026::VerifyWindow});
        const auto thread = api_.windowProcess(window, &pid);
        if (!thread || pid != state_.identity.pid)
            fail({RuntimeOperation2026::VerifyWindow, 0, 0, 0, thread ? 0 : GetLastError()});
        state_.identity.window = window;
    }
    return window;
}
bool ProcessSession2026::replacementWindow(LPCSTR className) {
    const auto window = api_.findWindow(className, nullptr);
    DWORD pid = 0;
    return window && api_.windowProcess(window, &pid) && pid && pid != state_.identity.pid;
}
DWORD ProcessSession2026::resolve(HWND window, LPDWORD pid) {
    *pid = 0;
    const auto thread = api_.windowProcess(window, pid);
    if (!thread || !*pid)
        fail({RuntimeOperation2026::ResolveProcess, 0, 0, 0, thread ? 0 : GetLastError()});
    return thread;
}
UINT ProcessSession2026::requireMessage(LPCSTR name) {
    const auto message = registerMessage(name);
    if (!message) fail({RuntimeOperation2026::RegisterMessage, 0, 0, 0, GetLastError()});
    return message;
}
BOOL ProcessSession2026::closeRejected(HANDLE process) {
    if (state_.journal.empty() || state_.journal.last().kind != ChangeKind2026::Handle)
        fail({RuntimeOperation2026::ClosePendingChanges});
    if (!api_.closeHandle(process)) fail({RuntimeOperation2026::CloseProcess, 0, 0, 0, GetLastError()});
    state_.journal.discardLast();
    state_.process = nullptr;
    state_.identity = {};
    state_.processState = ProcessState2026::None;
    return TRUE;
}
LPVOID ProcessSession2026::allocate(HANDLE process, LPVOID address, SIZE_T size, DWORD protection) {
    require(process);
    auto& entry = state_.journal.append({ChangeKind2026::Allocation, process, nullptr, size});
    entry.address = api_.allocate(process, address, size, MEM_RESERVE | MEM_COMMIT, protection);
    if (!entry.address) {
        const DWORD error = GetLastError();
        state_.journal.discardLast();
        fail({RuntimeOperation2026::Allocate, reinterpret_cast<std::uintptr_t>(address), size, 0, error});
    }
    entry.completed = true;
    return entry.address;
}
BOOL ProcessSession2026::protect(HANDLE process, LPVOID address, SIZE_T size, DWORD protection, PDWORD old) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto page = static_cast<std::uintptr_t>(info.dwPageSize);
    auto cursor = reinterpret_cast<std::uintptr_t>(address) & ~(page - 1);
    const auto end = (reinterpret_cast<std::uintptr_t>(address) + size + page - 1) & ~(page - 1);
    bool first = true;
    while (cursor < end) {
        require(process);
        MEMORY_BASIC_INFORMATION region{};
        if (!api_.query(process, reinterpret_cast<LPCVOID>(cursor), &region, sizeof(region)))
            fail({RuntimeOperation2026::QueryProtection, cursor, sizeof(region), 0, GetLastError()});
        const auto limit = (std::min)(end, reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize);
        if (limit <= cursor || region.State != MEM_COMMIT)
            fail({RuntimeOperation2026::ValidateProtection, cursor, end-cursor});
        require(process);
        auto& entry = state_.journal.append({ChangeKind2026::Protection, process, reinterpret_cast<LPVOID>(cursor), limit-cursor});
        if (!api_.protect(process, entry.address, entry.size, protection, &entry.protection)) {
            const DWORD error = GetLastError();
            state_.journal.discardLast();
            fail({RuntimeOperation2026::Protect, cursor, limit-cursor, 0, error});
        }
        entry.completed = true;
        if (first) { *old = entry.protection; first = false; }
        cursor = limit;
    }
    return TRUE;
}
BOOL ProcessSession2026::read(HANDLE process, LPCVOID source, LPVOID target, SIZE_T size) {
    require(process);
    SIZE_T transferred = 0;
    const BOOL ok = api_.read(process, source, target, size, &transferred);
    if (!ok || transferred != size)
        fail({RuntimeOperation2026::Read, reinterpret_cast<std::uintptr_t>(source), size, transferred, ok ? 0 : GetLastError()});
    return TRUE;
}
BOOL ProcessSession2026::write(HANDLE process, LPVOID target, LPCVOID source, SIZE_T size) {
    require(process);
    Change2026 entry{ChangeKind2026::Bytes, process, target, size};
    entry.before.resize(size);
    SIZE_T read = 0;
    const BOOL readOk = api_.read(process, target, entry.before.data(), size, &read);
    if (!readOk || read != size)
        fail({RuntimeOperation2026::ReadOriginal, reinterpret_cast<std::uintptr_t>(target), size, read, readOk ? 0 : GetLastError()});
    require(process);
    // Keep preimages before writing: even FALSE or short success can mutate memory.
    auto& pending = state_.journal.append(std::move(entry));
    const BOOL written = api_.write(process, target, source, size, &pending.transferred);
    if (!written || pending.transferred != size)
        fail({RuntimeOperation2026::Write, reinterpret_cast<std::uintptr_t>(target), size, pending.transferred, written ? 0 : GetLastError()});
    require(process);
    if (!api_.flush(process, target, size))
        fail({RuntimeOperation2026::Flush, reinterpret_cast<std::uintptr_t>(target), size, pending.transferred, GetLastError()});
    pending.completed = true;
    return TRUE;
}
bool ProcessSession2026::retireExited() {
    if (observe() != ProcessState2026::Exited) return false;
    // The exit code separates a clean exit from a crash. It has to be read
    // before the handle is closed, otherwise the information is lost.
    DWORD exitCode = 0;
    if (api_.exitCode(state_.process, &exitCode)) {
        lastExitCode = exitCode;
        hasExitCode = true;
    }
    if (!api_.closeHandle(state_.process)) {
        const DWORD error = GetLastError();
        state_.phase = SessionPhase2026::RecoveryRequired;
        state_.result = OperationResult2026::RecoveryRequired;
        state_.recoveryFailure = {RuntimeOperation2026::CloseExitedProcess, 0, 0, 0, error};
        return false;
    }
    state_.journal.clear();
    state_.process = nullptr;
    state_.identity = {};
    state_.processState = ProcessState2026::None;
    state_.phase = SessionPhase2026::Idle;
    state_.result = OperationResult2026::ProcessExited;
    state_.recoveryFailure = {};
    return true;
}
bool ProcessSession2026::canRecover() {
    if (state_.process && state_.identity.verified && state_.processState != ProcessState2026::Alive) {
        state_.result = OperationResult2026::RecoveryRequired;
        state_.recoveryFailure = {RuntimeOperation2026::VerifyProcess, 0, 0, 0, observationError_};
        return false;
    }
    return true;
}
bool ProcessSession2026::restore() {
    state_.phase = SessionPhase2026::RollingBack;
    state_.recoveryFailure = {};
    while (state_.journal.size() > state_.journal.boundary()) {
        if (state_.identity.verified) {
            const auto state = observe();
            if (state == ProcessState2026::Exited) return retireExited();
            if (state != ProcessState2026::Alive) {
                state_.phase = SessionPhase2026::RecoveryRequired;
                state_.result = OperationResult2026::RecoveryRequired;
                state_.recoveryFailure = {RuntimeOperation2026::VerifyProcess, 0, 0, 0, observationError_};
                return false;
            }
        }
        const auto& entry = state_.journal.last();
        const auto target = reinterpret_cast<std::uintptr_t>(entry.address);
        bool ok = false;
        SIZE_T transferred = 0;
        DWORD ignored = 0;
        switch (entry.kind) {
        case ChangeKind2026::Bytes: {
            const BOOL written = api_.write(entry.process, entry.address, entry.before.data(), entry.size, &transferred);
            if (!written || transferred != entry.size)
                state_.recoveryFailure = {RuntimeOperation2026::RestoreBytes, target, entry.size, transferred, written ? 0 : GetLastError()};
            else if (observe() != ProcessState2026::Alive)
                state_.recoveryFailure = {RuntimeOperation2026::VerifyProcess, target, entry.size, transferred, observationError_};
            else if (!api_.flush(entry.process, entry.address, entry.size))
                state_.recoveryFailure = {RuntimeOperation2026::RestoreFlush, target, entry.size, transferred, GetLastError()};
            else ok = true;
            break;
        }
        case ChangeKind2026::Protection:
            ok = api_.protect(entry.process, entry.address, entry.size, entry.protection, &ignored) != FALSE;
            if (!ok) state_.recoveryFailure = {RuntimeOperation2026::RestoreProtection, target, entry.size, 0, GetLastError()};
            break;
        case ChangeKind2026::Allocation:
            ok = api_.release(entry.process, entry.address, 0, MEM_RELEASE) != FALSE;
            if (!ok) state_.recoveryFailure = {RuntimeOperation2026::ReleaseAllocation, target, entry.size, 0, GetLastError()};
            break;
        case ChangeKind2026::Handle:
            ok = api_.closeHandle(entry.process) != FALSE;
            if (ok) { state_.process = nullptr; state_.identity = {}; state_.processState = ProcessState2026::None; }
            else state_.recoveryFailure = {RuntimeOperation2026::CloseProcess, 0, 0, 0, GetLastError()};
            break;
        }
        if (!ok) {
            if (observe() == ProcessState2026::Exited) return retireExited();
            state_.phase = SessionPhase2026::RecoveryRequired;
            state_.result = OperationResult2026::RecoveryRequired;
            return false;
        }
        state_.journal.discardLast();
    }
    return true;
}
void ProcessSession2026::beginOperation() {
    state_.journal.begin();
    state_.failure = {};
    state_.recoveryFailure = {};
    state_.result = OperationResult2026::None;
    state_.phase = SessionPhase2026::Applying;
}
