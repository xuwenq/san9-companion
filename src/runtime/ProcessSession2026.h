#pragma once
#include "ProcessApi2026.h"
#include "ChangeJournal2026.h"
#include "RuntimeFailure2026.h"

enum class SessionPhase2026 { Idle, Active, Applying, RollingBack, RecoveryRequired };
enum class OperationResult2026 { None, Committed, RolledBack, RecoveryRequired, Undone, ProcessExited };
enum class ProcessState2026 { None, Alive, Exited, Unavailable, IdentityChanged };
struct ProcessIdentity2026 {
    DWORD pid = 0;
    FILETIME created{};
    HWND window = nullptr;
    bool verified = false;
};
struct RuntimeSession2026 {
    SessionPhase2026 phase = SessionPhase2026::Idle;
    OperationResult2026 result = OperationResult2026::None;
    HANDLE process = nullptr;
    ChangeJournal2026 journal;
    ProcessIdentity2026 identity;
    ProcessState2026 processState = ProcessState2026::None;
    RuntimeFailure2026 failure;
    RuntimeFailure2026 recoveryFailure;
};
// Owns process identity, resources and remote operations, independent of host layout.
class ProcessSession2026 final {
public:
    explicit ProcessSession2026(ProcessApi2026 api = {}): api_(api) {}
    ProcessSession2026(const ProcessSession2026&) = delete;
    ProcessSession2026& operator=(const ProcessSession2026&) = delete;
    const RuntimeSession2026& state() const { return state_; }
    RuntimeSession2026& state() { return state_; }
    ProcessState2026 observe();
    void require(HANDLE process);
    HANDLE acquire(DWORD access, BOOL inherit, DWORD pid, HWND window);
    void verifyConnection();
    HWND findWindow(LPCSTR className, LPCSTR title);
    bool replacementWindow(LPCSTR className);
    DWORD resolve(HWND window, LPDWORD pid);
    UINT registerMessage(LPCSTR name) { return api_.registerMessage(name); }
    UINT requireMessage(LPCSTR name);
    BOOL closeRejected(HANDLE process);
    LPVOID allocate(HANDLE process, LPVOID address, SIZE_T size, DWORD protection);
    BOOL protect(HANDLE process, LPVOID address, SIZE_T size, DWORD protection, PDWORD old);
    BOOL read(HANDLE process, LPCVOID source, LPVOID target, SIZE_T size);
    BOOL write(HANDLE process, LPVOID target, LPCVOID source, SIZE_T size);
    bool retireExited();
    // Exit code of the last observed exit. The game process itself is closed
    // by the time this is read, so the value has to be captured before that.
    DWORD lastExitCode = 0;
    bool hasExitCode = false;
    bool canRecover();
    bool restore();
    void beginOperation();
private:
    ProcessApi2026 api_;
    RuntimeSession2026 state_;
    DWORD observationError_ = 0;
    [[noreturn]] void fail(RuntimeFailure2026 failure);
};
