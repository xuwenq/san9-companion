#pragma once
#include <Windows.h>
#include "../generated/RuntimeLayout2026.h"
#include "../generated/InjectionNames2026.h"
#include <array>
#include <cstdint>
#include <type_traits>
#include <stdexcept>
#include <vector>
#include <string>
#include <functional>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cstddef>
#include <filesystem>
#include "../model/ParameterConfig.h"
#include "../model/CityConfig.h"
#include "../model/SkillConfig.h"
#include "../model/FormationConfig.h"
#include "../model/PersonConfig.h"
#include "../model/PersonSnapshot.h"
#include "GameSnapshots2026.h"
#include "PatchDescriptor2026.h"
#include "ComputerWorkTiming2026.h"
#include "ProcessSession2026.h"
#include "ImageRegion2026.h"
#include "PresentationSpeed2026.h"
#include "ThreadBarrier2026.h"
#include "DeathGradient2026.h"
#include "GrowthBoost2026.h"
#include "Cultivate2026.h"
#include "OfficerPersonalization2026.h"

// 0x409E10 dispatch; message-related names follow the host entry points,
// not an inferred game action. Values remain the recovered ABI values.
enum class ModifyReason2026 : int { TimerTick=1, GameNotification=2, GameDialogClosed=3, ParametersConfirmed=4, PresentationSpeed=5 };

enum class RuntimeStatusKind2026 {
    None, Connected, WaitingForGame, UnsupportedVersion, ExitCleanupFailed, ProcessExited, IdentityUnavailable,
    RecoveryRequired, Restored, ModificationBlocked, ModificationRolledBack, ExceptionRolledBack
};
struct RuntimeStatus2026 {
    RuntimeStatusKind2026 kind = RuntimeStatusKind2026::None;
    DWORD errorCount = 0;
    RuntimeFailure2026 failure;
    RuntimeFailure2026 recoveryFailure;
    unsigned activityDots = 0;
};
struct ConfigurationState2026 {
    bool cityAutomatic = false;
    bool cityPending = false;
    bool skillLoaded = false;
    bool skillPending = false;
    bool formationLoaded = false;
    bool formationPending = false;
};
struct RuntimeTestAccess2026;
// 追加诊断日志到可执行文件旁的 San9Companion.log；失败静默，绝不干扰注入流程。
// 日志使用持久流并受总字节上限约束，避免高频写入拖慢注入事务。
void appendRuntimeLog(const char* message);
void appendRuntimeLog(const char* message, std::size_t length);
// 高频逐次事件（如每次内存写入）。默认不记录，设置 SAN9_LOG_WRITES=1 才开启。
void appendRuntimeLogTrace(const char* message, std::size_t length);
// 运行时开关：开启/关闭逐次写入追踪。设置页与命令行/环境变量共用这一条路径。
void setRuntimeLogTrace(bool enabled);
// 关闭并释放日志文件句柄；程序退出或测试结束时调用。
void closeRuntimeLog();
// 把宽字符串按 UTF-8 写入日志流，日志是字节流，不能直接流出 wstring。
inline void runtimeLogStream(std::ostringstream& line, const std::wstring& value) {
    if (value.empty()) return;
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return;
    std::string text(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), text.data(), size, nullptr, nullptr);
    line << text;
}
inline void runtimeLogStream(std::ostringstream& line, const std::filesystem::path& value) {
    runtimeLogStream(line, value.wstring());
}
template<class T>
void runtimeLogStream(std::ostringstream& line, const T& value) {
    line << value;
}
// 格式化为一行并写入日志。调用点用流式写法拼装，避免重复样板代码。
template<class... Args>
void appendRuntimeLogFormat(const Args&... args) {
    std::ostringstream line;
    (runtimeLogStream(line, args), ...);
    appendRuntimeLog(line.str().c_str());
}
// 把一个地址格式化成固定宽度的十六进制，便于在日志里对齐比较。
std::string runtimeLogAddress(DWORD address);
// Offset-addressed x86 state recovered from the 2026 binary, independent of MFC.
// Only this translation layer exposes offsets; dialogs exchange typed values.
class Runtime2026 final {
public:
    explicit Runtime2026(ProcessApi2026 api = {});
    Runtime2026(const Runtime2026&) = delete;
    Runtime2026& operator=(const Runtime2026&) = delete;
    // The endpoint is the game callback ABI, never a GUI control to update.
    void setCallbackWindow(HWND endpoint);
    bool isGameMessage(UINT message) const;
    bool handleGameMessage(UINT message, WPARAM action, LPARAM argument);
    RuntimeStatus2026 status() const {
        auto value = status_;
        value.failure = process_.state().failure;
        value.recoveryFailure = process_.state().recoveryFailure;
        return value;
    }
    ConfigurationState2026 configurationState() const;
    void setConfigurationState(const ConfigurationState2026& state);
    void modify(ModifyReason2026 reason);
    bool togglePlayerForce(unsigned index);
    bool undo();
    bool retryRollback();
    bool hasGameSession() const {
        return process_.state().phase == SessionPhase2026::Active && process_.state().process != nullptr
            && process_.state().processState == ProcessState2026::Alive;
    }
    bool readPersonSnapshot(san9::PersonSnapshot& snapshot);
    bool readCitySnapshot(CitySnapshot2026& snapshot);
    bool readSkillSnapshot(SkillSnapshot2026& snapshot);
    bool readFormationSnapshot(FormationSnapshot2026& snapshot);
    bool readPlayerForces(std::vector<PlayerForce2026>& forces);
    const RuntimeSession2026& session() const { return process_.state(); }
    // Changes only when an exited or restored session resets its host projection.
    std::uint64_t configurationEpoch() const { return configurationEpoch_; }
    void setSkipDurabilitySiegeSelection(bool enabled) { skipDurabilitySiegeSelection_ = enabled; }
    void setComputerWorkTiming(ComputerWorkTiming2026 value) { computerWorkTiming_ = value; }
    void setDeathGradient(DeathGradient2026 value) { deathGradient_ = value; }
    void setGrowthBoost(GrowthBoost2026 value) { growthBoost_ = value; }
    void setCultivate(Cultivate2026 value) { cultivate_ = value; }
    void setPersonalization(OfficerPersonalization2026 value) { personalization_ = value; }
    void setPresentationSpeed(unsigned multiplier);
    void setParameters(const san9::ParameterConfig& parameters);
    san9::PersonConfig personConfig() const;
    void setPersonConfig(const san9::PersonConfig& config);
    san9::FormationConfig formationConfig() const;
    void setFormationConfig(const san9::FormationConfig& config);
    san9::SkillConfig skillConfig() const;
    void setSkillConfig(const san9::SkillConfig& config);
    san9::CityConfig cityConfig() const;
    void setCityConfig(const san9::CityConfig& config);
private:
    friend struct RuntimeTestAccess2026;
    template<class T> T& field(size_t offset) {
        return *reinterpret_cast<T*>(stateBytes(offset,sizeof(T)));
    }
    void copyIn(size_t offset, const void* source, size_t size);
    void copyOut(size_t offset, void* destination, size_t size) const;
    void dataIn(DWORD address, const void* source, size_t size);
    void dataOut(DWORD address, void* destination, size_t size);
    bool readGameMemory(DWORD address, void* buffer, SIZE_T size);
    bool rebase();
    BYTE* stateBytes(size_t offset, size_t size) {
        return ImageRegion2026<BYTE>(state_.data(),state_.size(),0).at(offset,size);
    }
    const BYTE* stateBytes(size_t offset, size_t size) const {
        return ImageRegion2026<const BYTE>(state_.data(),state_.size(),0).at(offset,size);
    }
    BYTE* dataBytes(std::uintptr_t address, size_t size) {
        return ImageRegion2026<BYTE>(data_.data(),data_.size(),0x41F000).at(address,size);
    }
    const BYTE* dataBytes(std::uintptr_t address, size_t size) const {
        return ImageRegion2026<const BYTE>(data_.data(),data_.size(),0x41F000).at(address,size);
    }
    const BYTE* nativeDataBytes(std::uintptr_t address, size_t size) const {
        return ImageRegion2026<const BYTE>(data_.data(),data_.size(),reinterpret_cast<std::uintptr_t>(data_.data())).at(address,size);
    }
    void validateImagePointer(std::uintptr_t address, size_t size) const;
    void captureGameBytes(std::uintptr_t source, std::uintptr_t destination, SIZE_T size);
    ProcessSession2026 process_;
    ThreadBarrier2026 threadBarrier_;
    DWORD speedAllocation_ = 0, savedSpeedAllocation_ = 0;
    unsigned speedMultiplier_ = 1, appliedSpeed_ = 1, savedAppliedSpeed_ = 1;
    void applyPresentationSpeed();
    bool pauseSpeedThreads(bool installing = false);
    bool resumeSpeedThreads();
    alignas(4) std::array<BYTE, san9::layout2026::StateSize> state_{};
    alignas(4) std::array<BYTE, 0x11000> data_{};
    RuntimeStatus2026 status_;
    decltype(state_) savedState_{}, initialState_{};
    decltype(data_) savedData_{}, initialData_{};
    bool undoPending_ = false;
    std::uint64_t configurationEpoch_ = 0;
    bool skipDurabilitySiegeSelection_ = true; // Preserve the companion default.
    ComputerWorkTiming2026 computerWorkTiming_;
    DeathGradient2026 deathGradient_;
    GrowthBoost2026 growthBoost_;
    Cultivate2026 cultivate_;
    OfficerPersonalization2026 personalization_;
    void applyComputerWorkTiming();
    void applyDeathGradient();
    void applyGrowthBoost();
    void applyCultivate();
    void applyProficiencyCap();
    void applyOfficerPersonalization();
    // Writes one log line per configured officer, so the log alone proves
    // which personalization values were actually applied.
    void logPersonalizationSummary();
    void installPerOfficerHook(HANDLE process, const std::array<std::uint8_t, 800>& values,
                               DWORD hookAddress, const BYTE* original, size_t originalSize,
                               DWORD returnAddress,
                               const BYTE* code, size_t codeSize, size_t arrayAddrOffset,
                               const char* feature, size_t extraJumpRelOffset = SIZE_MAX);
    void modifyBranches(ModifyReason2026 reason);
    bool connectGame(HWND window);
    void animateConnectedStatus();
    void installCoreHooks();
    void installNotificationBridge();
    void uploadInjectionPayloads();
    void installGameplayPatches();
    void applyParameterPatches();
    void writeDescriptor(DWORD address, PatchSource2026 source);
    void writeDescriptorRange(DWORD first, size_t count, PatchSource2026 source);
    void writeInlineRange(DWORD first, size_t count);
    void initializeHookDescriptors();
    template<class A, class B> void writePatch(A target, B source, SIZE_T size) {
        writeMemory(field<HANDLE>(san9::layout2026::ProcessHandle), target, source, size, nullptr);
    }
    void applyGameConfiguration(ModifyReason2026 reason);
    // Writes the values the game actually receives for the combined options,
    // so the log can be compared with the observed behaviour.
    void logParameterResults(std::uintptr_t state);    void applyCityConfiguration(ModifyReason2026 reason);
    void applySkillConfiguration();
    void applyFormationConfiguration(ModifyReason2026 reason);
    void writeGameValue(DWORD target, DWORD value, SIZE_T size = sizeof(DWORD));
    bool checkVersion();
    BOOL updateZone(int enabled);
    HWND findGameWindow(LPCSTR className, LPCSTR title);
    DWORD resolveProcess(HWND window, LPDWORD pid);
    UINT registerRuntimeMessage(LPCSTR name);
    HANDLE openProcess(DWORD access, BOOL inherit, DWORD pid);
    BOOL closeProcess(HANDLE process);
    LPVOID allocateMemory(HANDLE process, LPVOID address, SIZE_T size, DWORD type, DWORD protection);
    BOOL protectMemory(HANDLE process, LPVOID address, SIZE_T size, DWORD protection, PDWORD old);
    BOOL writeBytes(HANDLE process, LPVOID target, LPCVOID source, SIZE_T size);
    bool reverseChanges();
    bool runOperation(const std::function<void()>& operation);
    ProcessState2026 observeProcess() { return process_.observe(); }
    void requireProcess(HANDLE process) { process_.require(process); }
    bool retireExitedProcess();
    void restoreSessionBaseline();
    template<class T> T* pointer(DWORD address) {
        return reinterpret_cast<T*>(dataBytes(address,sizeof(T)));
    }
    template<class T> T& global(DWORD address) { return *pointer<T>(address); }
    void setStatus(RuntimeStatusKind2026 kind);
    // Every status change is recorded. The user needs a trail that shows
    // whether a modification was applied, not only the failures.
    void setStatus(RuntimeStatusKind2026 kind, const char* reason);
    template<class T> static std::uintptr_t addressOf(T value) {
        if constexpr (std::is_pointer_v<T>) return reinterpret_cast<std::uintptr_t>(value);
        else return static_cast<std::uintptr_t>(value);
    }
    template<class T> const BYTE* sourceVA(T value, size_t size) const {
        return dataBytes(addressOf(value),size);
    }
    template<class A, class B> BOOL writeMemory(HANDLE process, A target, B source, SIZE_T size, SIZE_T*) {
        validateImagePointer(addressOf(source),size);
        {
            // Hot path: format without allocating, so the journal transaction is not
            // slowed down by per-write diagnostics.
            char line[80];
            const auto address = static_cast<unsigned long long>(addressOf(target));
            const int length = std::snprintf(line,sizeof(line),
                "write target=0x%08llx size=%llu",
                address,
                static_cast<unsigned long long>(size));
            if (length > 0) appendRuntimeLogTrace(line, static_cast<std::size_t>(length));
        }
        return writeBytes(process, reinterpret_cast<LPVOID>(addressOf(target)),
            reinterpret_cast<LPCVOID>(addressOf(source)), size);
    }
    template<class A, class B> BOOL readMemory(HANDLE process, A source, B target, SIZE_T size, SIZE_T*) {
        validateImagePointer(addressOf(target),size);
        return process_.read(process, reinterpret_cast<LPCVOID>(addressOf(source)),
            reinterpret_cast<LPVOID>(addressOf(target)), size);
    }
};
