#include "Runtime2026.h"
#include "../model/PatchImage.h"
#include "AppPaths.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>

// Fail the build if model encoding and the recovered layout ever drift.
static_assert(san9::PersonConfig::Count*4 == san9::layout2026::PersonLevelsSize);
static_assert(san9::PersonConfig::FriendshipCount*6 == san9::layout2026::PersonFriendshipsSize);
static_assert(san9::CityConfig::Count*san9::CityConfig::RecordSize == san9::layout2026::CityLimitsSize);
static_assert(san9::SkillConfig::Bytes{}.size() == san9::layout2026::SkillEntriesSize);
static_assert(san9::FormationConfig::Bytes{}.size() == san9::layout2026::FormationEntriesSize);
static_assert(sizeof(WORD) == san9::layout2026::MaxExperienceSize);
static_assert(sizeof(DWORD) == san9::layout2026::CityAutomaticSize && sizeof(DWORD) == san9::layout2026::CityPendingSize);
static_assert(sizeof(BYTE) == san9::layout2026::SkillFlagsSize && sizeof(BYTE) == san9::layout2026::FormationFlagsSize);

namespace {
// The diagnostic log lives next to the executable. It is written through a single
// persistent stream: opening and closing a file per write dominated the cost of
// small writes, and an unbounded log grew to hundreds of megabytes during a
// full offline sweep. The stream is truncated once it exceeds the cap.
constexpr std::uintmax_t RuntimeLogLimit = 8u * 1024u * 1024u;

struct RuntimeLog {
    std::ofstream stream;
    std::uintmax_t written = 0;
    bool disabled = false;
};

RuntimeLog& runtimeLog() {
    static RuntimeLog log;
    return log;
}

// Detailed per-write tracing. It produces hundreds of thousands of lines and
// buries the failure records that matter, so it stays off unless the user
// explicitly asks for it. Two ways to ask, in decreasing precedence:
//   * the settings page, which turns it on for the running session
//   * command line / environment: San9Companion.exe --log-writes, SAN9_LOG_WRITES=1
bool runtimeLogTraceForced = false;

bool traceWritesRequested() {
    if (runtimeLogTraceForced) return true;
    if (GetCommandLineW()) {
        // Exact argument match: a substring search would also fire for
        // "--log-writes-off" and similar near-misses.
        std::wstring text(GetCommandLineW());
        size_t pos = 0;
        while (pos < text.size())
        {
            while (pos < text.size() && (text[pos] == L' ' || text[pos] == L'\t')) ++pos;
            const size_t start = pos;
            while (pos < text.size() && text[pos] != L' ' && text[pos] != L'\t') ++pos;
            if (pos == start) break;
            if (text.compare(start, pos - start, L"--log-writes") == 0) return true;
        }
    }
    char value[8]{};
    const DWORD length = GetEnvironmentVariableA("SAN9_LOG_WRITES", value, sizeof(value));
    return length > 0 && value[0] != '0';
}

std::string hexBytes(const BYTE* data, size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i)
        out << std::setw(2) << static_cast<unsigned>(data[i]) << ' ';
    return out.str();
}

std::string hexDword(DWORD value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}
}

void closeRuntimeLog() {
    auto& log = runtimeLog();
    if (log.stream.is_open()) log.stream.close();
    log.written = 0;
    log.disabled = false;
}

void appendRuntimeLog(const char* message, std::size_t length) {
    try {
        if (!message) return;
        auto& log = runtimeLog();
        if (log.disabled) return;
        if (!log.stream.is_open()) {
            const auto path = san9::executableFile(L"San9Companion.log");
            std::error_code error;
            const auto existing = std::filesystem::file_size(path, error);
            if (!error && existing >= RuntimeLogLimit) {
                // Start a fresh file instead of appending to an oversized one.
                std::filesystem::remove(path, error);
            }
            log.stream.open(path, std::ios::app);
            if (!log.stream) { log.disabled = true; return; }
            log.written = error ? 0u : static_cast<std::uintmax_t>(existing);
        }
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        char stamp[32];
        std::snprintf(stamp,sizeof(stamp),"%04d-%02d-%02d %02d:%02d:%02d  ",
            tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
        log.stream << stamp;
        log.stream.write(message, static_cast<std::streamsize>(length));
        log.stream.put('\n');
        log.written += length + 1;
        if (log.written >= RuntimeLogLimit) {
            // The cap is reached: stop writing instead of growing without bound.
            log.stream.close();
            log.written = 0;
            log.disabled = true;
        }
    } catch (...) {
    }
}

void appendRuntimeLog(const char* message) {
    appendRuntimeLog(message, message ? std::strlen(message) : 0u);
    try {
        auto& log = runtimeLog();
        if (log.stream.is_open()) log.stream.flush();
    } catch (...) {
    }
}

void appendRuntimeLogTrace(const char* message, std::size_t length) {
    if (!traceWritesRequested()) return;
    appendRuntimeLog(message, length);
}

void setRuntimeLogTrace(bool enabled) {
    runtimeLogTraceForced = enabled;
    appendRuntimeLog(enabled ? "detailed write tracing enabled"
                             : "detailed write tracing disabled");
}

void Runtime2026::restoreSessionBaseline() {
    speedAllocation_=savedSpeedAllocation_=0;
    appliedSpeed_=savedAppliedSpeed_=1;
    ++configurationEpoch_;
    // Keep user inputs; discard game addresses, version-derived data and event state.
    const auto inputs = state_;
    std::array<BYTE, 450> inputData{};
    dataOut(0x42D948, inputData.data(), 400);
    dataOut(0x42DAD8, inputData.data()+400, 50);
    state_ = initialState_;
    data_ = initialData_;
    for (const auto& field : san9::ParameterConfig::fields())
        copyIn(field.hostOffset, inputs.data()+field.hostOffset, sizeof(std::int32_t));
    for (const auto& span : san9::layout2026::LifecyclePreserveSpans)
        copyIn(span.offset,inputs.data()+span.offset,span.size);
    for (const auto& span : san9::layout2026::ConfigurationSpans)
        copyIn(span.offset,inputs.data()+span.offset,span.size);
    dataIn(0x42D948, inputData.data(), 400);
    dataIn(0x42DAD8, inputData.data()+400, 50);
    for (const auto& span : san9::layout2026::LifecycleResetSpans)
        std::fill_n(state_.data()+span.offset,span.size,BYTE{0});
    if (field<DWORD>(san9::layout2026::CityAutomatic)) field<DWORD>(san9::layout2026::CityPending)=1;
    if (field<BYTE>(san9::layout2026::SkillFlags)&1) field<BYTE>(san9::layout2026::SkillFlags)|=2;
    if (field<BYTE>(san9::layout2026::FormationFlags)&1) field<BYTE>(san9::layout2026::FormationFlags)|=2;
}
bool Runtime2026::retireExitedProcess() {
    if (!resumeSpeedThreads()) return false;
    if (!process_.retireExited()) {
        if (process_.state().recoveryFailure.operation == RuntimeOperation2026::CloseExitedProcess)
            setStatus(RuntimeStatusKind2026::ExitCleanupFailed);
        return false;
    }
    restoreSessionBaseline();
    undoPending_ = false;
    setStatus(RuntimeStatusKind2026::ProcessExited);
    return true;
}
HANDLE Runtime2026::openProcess(DWORD access, BOOL inherit, DWORD pid) {
    const HANDLE handle = process_.acquire(access, inherit, pid, field<HWND>(san9::layout2026::TargetWindow));
    initialState_ = savedState_; initialData_ = savedData_;
    process_.verifyConnection();
    return handle;
}
HWND Runtime2026::findGameWindow(LPCSTR className, LPCSTR title) {
    const auto window = process_.findWindow(className, title);
    if (process_.state().process) field<HWND>(san9::layout2026::TargetWindow) = window;
    return window;
}
DWORD Runtime2026::resolveProcess(HWND window, LPDWORD pid) { return process_.resolve(window, pid); }
UINT Runtime2026::registerRuntimeMessage(LPCSTR name) { return process_.requireMessage(name); }
BOOL Runtime2026::closeProcess(HANDLE process) { return process_.closeRejected(process); }
LPVOID Runtime2026::allocateMemory(HANDLE process, LPVOID address, SIZE_T size, DWORD, DWORD protection) {
    return process_.allocate(process, address, size, protection);
}
BOOL Runtime2026::protectMemory(HANDLE process, LPVOID address, SIZE_T size, DWORD protection, PDWORD old) {
    return process_.protect(process, address, size, protection, old);
}
BOOL Runtime2026::writeBytes(HANDLE process, LPVOID target, LPCVOID source, SIZE_T size) {
    return process_.write(process, target, source, size);
}
bool Runtime2026::reverseChanges() {
    if (!process_.restore()) {
        const auto operation = process_.state().recoveryFailure.operation;
        setStatus(operation == RuntimeOperation2026::VerifyProcess ? RuntimeStatusKind2026::IdentityUnavailable
            : operation == RuntimeOperation2026::CloseExitedProcess ? RuntimeStatusKind2026::ExitCleanupFailed
            : RuntimeStatusKind2026::RecoveryRequired);
        return false;
    }
    if (process_.state().result == OperationResult2026::ProcessExited) {
        restoreSessionBaseline();
        undoPending_ = false;
        setStatus(RuntimeStatusKind2026::ProcessExited);
    }
    return true;
}
bool Runtime2026::retryRollback() {
    if (process_.state().recoveryFailure.operation==RuntimeOperation2026::ResumeThreads && !resumeSpeedThreads()) return false;
    if (process_.state().process && observeProcess() == ProcessState2026::Exited) return retireExitedProcess();
    if (process_.state().phase != SessionPhase2026::RecoveryRequired) return true;
    if (!process_.canRecover()) {
        setStatus(RuntimeStatusKind2026::IdentityUnavailable);
        return false;
    }
    if (!pauseSpeedThreads()) { resumeSpeedThreads(); return false; }
    const bool restored=reverseChanges();
    if (!resumeSpeedThreads() || !restored) return false;
    if (process_.state().result == OperationResult2026::ProcessExited) return true;
    if (undoPending_) restoreSessionBaseline();
    else {
        state_ = savedState_; data_ = savedData_;
        speedAllocation_=savedSpeedAllocation_; appliedSpeed_=savedAppliedSpeed_;
    }
    process_.state().phase = process_.state().process ? SessionPhase2026::Active : SessionPhase2026::Idle;
    process_.state().result = undoPending_ ? OperationResult2026::Undone : OperationResult2026::RolledBack;
    undoPending_ = false;
    setStatus(RuntimeStatusKind2026::Restored);
    return true;
}
void Runtime2026::modify(ModifyReason2026 reason) {
    if (process_.state().process) {
        if (observeProcess() == ProcessState2026::Exited) { retireExitedProcess(); return; }
        if (process_.state().phase == SessionPhase2026::Active && process_.state().processState == ProcessState2026::Alive) {
            if (process_.replacementWindow(pointer<char>(0x42EDD4))) {
                undo(); // Finish the old session before ever opening the replacement.
                return;
            }
        }
    }
    const bool updateExtension = hasGameSession() && reason==ModifyReason2026::ParametersConfirmed;
    runOperation([&] {
        modifyBranches(reason);
        applyPresentationSpeed();
        // The recovered patch site is in the initial-connection branch only.
        // Live confirmation must update it in the same rollback transaction.
        if (updateExtension && process_.state().process) {
            const BYTE instruction = skipDurabilitySiegeSelection_ ? 0x33 : 0x85;
            writeMemory(process_.state().process, 0x4B87EBu, &instruction, 1, nullptr);
        }
    });
}
bool Runtime2026::readGameMemory(DWORD address, void* buffer, SIZE_T size) {
    if (process_.state().process && observeProcess() == ProcessState2026::Exited) { retireExitedProcess(); return false; }
    if (!hasGameSession()) return false;
    try { return readMemory(process_.state().process, address, buffer, size, nullptr) != FALSE; }
    catch (const ProcessOperationFailure2026&) {
        if (observeProcess() == ProcessState2026::Exited) retireExitedProcess();
        return false;
    }
}
void Runtime2026::applyComputerWorkTiming() {
    // RE-SAN9PK-0174: metadata bytes 0/1 are initial delay/calendar period.
    // Advanced difficulty halves the delay in the game's shared eligibility gate.
    const std::array<BYTE, 2> reward = computerWorkTiming_.improveRewards
        ? std::array<BYTE, 2>{60, 0} : std::array<BYTE, 2>{90, 30};
    const BYTE attackDelay = computerWorkTiming_.skipAttackStartDelay ? 0 : 120;
    writePatch(0x6574F0u, reward.data(), reward.size()); // Work code 15.
    writePatch(0x6574C0u, &attackDelay, sizeof(attackDelay)); // Work code 9 only.
}

void Runtime2026::applyDeathGradient() {
    // RE-SAN9PK death gradient: when "高能武将可阵亡" is enabled, the original
    // single-stat chance (min(30, 80 - [officer+0x50])) is replaced by a MAX(四维)
    // gradient (min(lowRate, threshold - MAX)). MAX is already in EAX at the
    // patch sites, so only the three bytes below need to change.
    const auto state = reinterpret_cast<std::uintptr_t>(state_.data());
    const bool enabled = *(DWORD *)(state + san9::injection2026::AllPersonDeadEnabled) != 0;
    const DWORD threshold = deathGradient_.threshold > 120u ? 120u : deathGradient_.threshold;
    const DWORD lowRate = deathGradient_.lowRate > 100u ? 100u : deathGradient_.lowRate;
    // 0x502667: "mov esi, [esi+0x50]" -> "mov esi, eax; nop" (use MAX instead of the stat).
    const BYTE loadMax[3] = {0x8B, 0xF0, 0x90};
    const BYTE loadStat[3] = {0x8B, 0x76, 0x50};
    const BYTE slopeThreshold = static_cast<BYTE>(threshold);
    const BYTE slopeOriginal = 0x50;
    const BYTE capRate = static_cast<BYTE>(lowRate);
    const BYTE capOriginal = 0x1E;
    if (enabled) {
        writePatch(reinterpret_cast<LPVOID>(0x502667u), loadMax, sizeof(loadMax));
        writePatch(reinterpret_cast<LPVOID>(0x50266Du), &slopeThreshold, 1u);
        writePatch(reinterpret_cast<LPVOID>(0x502677u), &capRate, 1u);
    } else {
        writePatch(reinterpret_cast<LPVOID>(0x502667u), loadStat, sizeof(loadStat));
        writePatch(reinterpret_cast<LPVOID>(0x50266Du), &slopeOriginal, 1u);
        writePatch(reinterpret_cast<LPVOID>(0x502677u), &capOriginal, 1u);
    }
}
void Runtime2026::applyGrowthBoost() {
    const auto process = field<HANDLE>(san9::layout2026::ProcessHandle);
    if (!process) return;

    // Parse the comma-separated officer list into a bitset (0..799).
    std::array<BYTE, 800> bitset{};
    bool any = false;
    if (!growthBoost_.officerList.empty()) {
        size_t pos = 0;
        while (pos < growthBoost_.officerList.size()) {
            const auto comma = growthBoost_.officerList.find(L',', pos);
            const auto token = growthBoost_.officerList.substr(
                pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos);
            unsigned id = 0;
            bool valid = !token.empty();
            for (wchar_t c : token) {
                if (c < L'0' || c > L'9') { valid = false; break; }
                id = id * 10 + static_cast<unsigned>(c - L'0');
                if (id >= 800) { valid = false; break; }
            }
            if (valid) {
                bitset[id] = 1;
                any = true;
            }
            if (comma == std::wstring::npos) break;
            pos = comma + 1;
        }
    }

    // The tail of codeDynamicAddProficiency: "mov [esp+4],eax; jmp normal"
    // (the preceding "inc eax" is a conditional part of the original probability
    // and must stay untouched).
    const DWORD codeBlockTail = reinterpret_cast<DWORD>(global<LPVOID>(0x42F27C)) + 0x40;

    if (!any || growthBoost_.multiplier <= 1) {
        const BYTE original[6] = {0x89, 0x44, 0x24, 0x04, 0xEB, 0xBC};
        writePatch(reinterpret_cast<LPVOID>(codeBlockTail), original, sizeof(original));
        return;
    }

    const DWORD multiplier = growthBoost_.multiplier;
    constexpr size_t codeSize = 40;
    const size_t totalSize = bitset.size() + codeSize;
    const auto region = allocateMemory(process, nullptr, totalSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!region) return;
    const DWORD bitsetAddr = reinterpret_cast<DWORD>(region);
    const DWORD codeAddr = bitsetAddr + static_cast<DWORD>(bitset.size());

    writePatch(reinterpret_cast<LPVOID>(bitsetAddr), bitset.data(), bitset.size());

    // Growth-multiplier code block. Entry: EAX = growth probability, ESI = officer
    // (CPersonData). Officer id = word[esi+4] (the person-table index; bounds < 0x320).
    // If the officer is in the bitset, multiply the probability, then write it to
    // [esp+4] (the argument slot of the original call) and tail-jump into the API.
    std::array<BYTE, codeSize> code = {
        0x89, 0xC1,                   // mov ecx, eax
        0x0F, 0xB7, 0x46, 0x04,       // movzx eax, word [esi+4]
        0x3D, 0x20, 0x03, 0x00, 0x00, // cmp eax, 0x320
        0x73, 0x11,                   // jae skip
        0xBA, 0x00, 0x00, 0x00, 0x00, // mov edx, bitsetAddr
        0x80, 0x3C, 0x02, 0x00,       // cmp byte [edx+eax], 0
        0x74, 0x06,                   // je skip
        0x69, 0xC9, 0x00, 0x00, 0x00, 0x00, // imul ecx, ecx, multiplier
        0x89, 0x4C, 0x24, 0x04,       // skip: mov [esp+4], ecx
        0xFF, 0x25, 0xF0, 0xF2, 0x5F, 0x00 // jmp [0x5FF2F0]
    };
    std::memcpy(code.data() + 0x0E, &bitsetAddr, 4);
    std::memcpy(code.data() + 0x1A, &multiplier, 4);
    writePatch(reinterpret_cast<LPVOID>(codeAddr), code.data(), code.size());

    // Redirect the tail to the growth-multiplier code block.
    BYTE jump[6] = {0xE9, 0x00, 0x00, 0x00, 0x00, 0x90};
    const int rel32 = static_cast<int>(codeAddr - (codeBlockTail + 5));
    std::memcpy(jump + 1, &rel32, 4);
    writePatch(reinterpret_cast<LPVOID>(codeBlockTail), jump, sizeof(jump));

    {
        size_t officers = 0;
        for (auto v : bitset) if (v) ++officers;
        std::ostringstream line;
        line << "growth boost installed: officers=" << officers
             << " multiplier=" << multiplier
             << " codeAddr=0x" << std::hex << std::setw(8) << std::setfill('0') << codeAddr;
        appendRuntimeLog(line.str().c_str());
    }
}

void Runtime2026::applyCultivate() {
    const auto state = reinterpret_cast<std::uintptr_t>(state_.data());
    const bool enabled = *(DWORD *)(state + san9::injection2026::EnableTrainEnabled) != 0;
    const BYTE duration = enabled
        ? static_cast<BYTE>((std::min)((std::max)(cultivate_.durationRounds, 3u), 12u))
        : static_cast<BYTE>(9);
    // 培养时长(旬). The address below is an unverified guess, not a reverse
    // engineered one: it lives in the .data BSS region, has zero references
    // inside the PE and no pointer in .data aims at it.
    //   ⚠️ 未验证地址：PE 内零引用、无指针指向、位于 .data BSS；
    //     若实机再崩，先删这行
    writePatch(reinterpret_cast<LPVOID>(0x14FA656u), &duration, 1u);
    // This function no longer writes a 小兵 activation flag.  The former write
    // to 0x6613EE was the root cause of crash #4: 0x6613EE is byte index 2 of
    // the id dword at 0x6613EC (value 0x000003E8, id 1000), so writing 1 there
    // turned the id into 0x000103E8, the lookup at 0x5B7050 then failed and
    // getflag at 0x5B72F0 dereferenced a null entry.
    // 强制小兵培养 is implemented by RuntimeInjection2026.cpp:1170-1172, which
    // writes 0x40CAEF / 0x40BF25 = 0x32 / 0 exactly like the original tool.
}

void Runtime2026::applyProficiencyCap() {
    const auto process = field<HANDLE>(san9::layout2026::ProcessHandle);
    if (!process) return;

    // 熟练度上限函数位于分配区内偏移 0x2190（原工具固定基址 0xA0000+0x2190）。
    const DWORD capAddr = field<DWORD>(san9::layout2026::AllocationBase) + 0x2190u;

    // 每武将每系熟练度上限 (uint16[800][9]) 扁平化为字节。
    std::array<std::uint8_t, 800 * 9 * 2> capFlat{};
    bool any = false;
    for (size_t o = 0; o < 800; ++o)
        for (size_t s = 0; s < 9; ++s) {
            const std::uint16_t v = personalization_.proficiencyCap[o][s];
            capFlat[(o * 9 + s) * 2] = static_cast<std::uint8_t>(v);
            capFlat[(o * 9 + s) * 2 + 1] = static_cast<std::uint8_t>(v >> 8);
            if (v != 0 && v != OfficerPersonalization2026::DefaultCap) any = true;
        }
    if (!any) {
        // 无自定义上限：写回原游戏逻辑（含重定位后的熟练度表访问），避免 uploadInjectionPayloads 晚于本函数覆盖。
        writePatch(reinterpret_cast<LPVOID>(capAddr), &global<DWORD>(0x4256B8), 0x5Du);
        return;
    }

    // 替换熟练度上限逻辑：保留原计算，按 (武将ID, 系) 查表覆盖上限。
    static const BYTE code[93] = {
        0x99,0xB9,0xFA,0x00,0x00,0x00,0xF7,0xF1,0x83,0xE8,0x04,0xF7,0xD8,0x85,0xC0,0x7F,0x04,0x33,0xC0,0xB0,0x01,0x3C,0x04,0x7E,0x02,0xB0,0x04,0x0F,0xAF,0x44,0x24,0x08,0x99,0xB1,0x03,0xF7,0xF1,0x89,0x44,0x24,0x08,
        0x0F,0xB7,0x46,0x04,0x3D,0x20,0x03,0x00,0x00,0x7D,0x17,0x6B,0xC0,0x09,0x03,0xC7,0x0F,0xB7,0x04,0x45,0x00,0x00,0x00,0x00,0x66,0x85,0xC0,0x74,0x05,0x66,0x89,0x44,0x24,0x0C,
        0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
        0xFF,0x25,0x0C,0xF3,0x5F,0x00};

    const size_t capSize = capFlat.size();
    const auto region = allocateMemory(process, nullptr, capSize, MEM_COMMIT, PAGE_READWRITE);
    if (!region) return;
    const DWORD capArr = reinterpret_cast<DWORD>(region);
    writePatch(reinterpret_cast<LPVOID>(capArr), capFlat.data(), capFlat.size());

    BYTE bytes[sizeof(code)];
    std::memcpy(bytes, code, sizeof(code));
    std::memcpy(bytes + 0x3D, &capArr, 4);  // movzx eax, word [capArr + eax*2] 的 disp32
    writePatch(reinterpret_cast<LPVOID>(capAddr), bytes, sizeof(bytes));
}

void Runtime2026::applyOfficerPersonalization() {
    const auto process = field<HANDLE>(san9::layout2026::ProcessHandle);
    if (!process) return;

    // 提升兵法发动机会把握概率：hook 0x4FEB90（`mov esi,0xA`，esi 此时仍是单位对象）。
    // stub 先保存单位到 callee-saved ebx，重放 `mov esi,0xA; call [0x5FF2F0]; add esp,0xC`，
    // 再沿单位 +0x30 → CPersonData → word[+4] 取武将索引查 activationBoost；命中则 eax=1。
    // 返回 0x4FEB9E（原 `test eax,eax` 之后的清栈分支）。
    const BYTE boostOriginal[5] = {0xBE, 0x0A, 0x00, 0x00, 0x00}; // mov esi,0xA
    const BYTE boostCode[56] = {
        0x8B, 0xDE,                   // mov ebx, esi
        0xBE, 0x0A, 0x00, 0x00, 0x00, // mov esi, 0xA
        0xFF, 0x15, 0xF0, 0xF2, 0x5F, 0x00, // call [0x5FF2F0]
        0x83, 0xC4, 0x0C,             // add esp, 0xC
        0x8B, 0x5B, 0x30,             // mov ebx, [ebx+0x30]
        0x85, 0xDB,                   // test ebx, ebx
        0x74, 0x1C,                   // je done
        0x0F, 0xB7, 0x5B, 0x04,       // movzx ebx, word [ebx+4]
        0x81, 0xFB, 0x20, 0x03, 0x00, 0x00, // cmp ebx, 0x320
        0x73, 0x10,                   // jae done
        0xB9, 0x00, 0x00, 0x00, 0x00, // mov ecx, activationArray
        0x80, 0x3C, 0x19, 0x00,       // cmp byte [ecx+ebx], 0
        0x74, 0x05,                   // je done
        0xB8, 0x01, 0x00, 0x00, 0x00, // mov eax, 1
        0xE9, 0x00, 0x00, 0x00, 0x00  // jmp 0x4FEB9E
    };
    installPerOfficerHook(process, personalization_.activationBoost, 0x4FEB90u,
                          boostOriginal, sizeof(boostOriginal), 0x4FEB9Eu,
                          boostCode, sizeof(boostCode), 0x24,
                          "activationBoost");

    // 兵法威力一定加倍(出动画)：hook 0x50603B（`add esp,0xC; test eax,eax`）。
    // stub 沿 [esi+0x80] → [+0x88] 链表逐节点取 CPersonData → word[+4] 查 criticalHit；
    // 命中则 eax=1 并保留 esi（战斗单位），返回 0x506040。
    const BYTE criticalOriginal[5] = {0x83, 0xC4, 0x0C, 0x85, 0xC0}; // add esp,0xC; test eax,eax
    const BYTE criticalCode[84] = {
        0x83, 0xC4, 0x0C,             // add esp, 0xC
        0x50,                         // push eax
        0x53,                         // push ebx
        0x57,                         // push edi
        0x8B, 0x86, 0x80, 0x00, 0x00, 0x00, // mov eax, [esi+0x80]
        0x85, 0xC0,                   // test eax, eax
        0x74, 0x3A,                   // je notfound
        0x8B, 0x80, 0x88, 0x00, 0x00, 0x00, // mov eax, [eax+0x88]
        0x89, 0xC7,                   // mov edi, eax
        0x85, 0xFF,                   // loop: test edi, edi
        0x74, 0x2E,                   // je notfound
        0x8B, 0x47, 0x08,             // mov eax, [edi+8]
        0x0F, 0xB7, 0x40, 0x04,       // movzx eax, word [eax+4]
        0x3D, 0x20, 0x03, 0x00, 0x00, // cmp eax, 0x320
        0x73, 0x1C,                   // jae next
        0xBB, 0x00, 0x00, 0x00, 0x00, // mov ebx, criticalArray
        0x80, 0x3C, 0x03, 0x00,       // cmp byte [ebx+eax], 0
        0x74, 0x11,                   // je next
        0xB8, 0x01, 0x00, 0x00, 0x00, // mov eax, 1
        0x83, 0xC4, 0x04,             // found: add esp,4
        0x5F,                         // pop edi
        0x5B,                         // pop ebx
        0x85, 0xC0,                   // test eax, eax
        0xE9, 0x00, 0x00, 0x00, 0x00, // jmp 0x506040
        0x8B, 0x3F,                   // next: mov edi, [edi]
        0xEB, 0xCE,                   // jmp loop
        0x58,                         // notfound: pop eax
        0x5F,                         // pop edi
        0x5B,                         // pop ebx
        0x85, 0xC0,                   // test eax, eax
        0xE9, 0x00, 0x00, 0x00, 0x00  // jmp 0x506040
    };
    installPerOfficerHook(process, personalization_.criticalHit, 0x50603Bu,
                          criticalOriginal, sizeof(criticalOriginal), 0x506040u,
                          criticalCode, sizeof(criticalCode), 0x2B,
                          "criticalHit", 0x42);

    // 发动间隔(天, per-skill)：hook 0x4FF08B（`idiv ecx`）。esi=CPersonData → word[+4]
    // 取武将索引、[ebx+0x34] 取技能ID；intervalFlat 用 0xFF 标记"未自定义"。
    // 命中则把配置天数直接写 eax（跳过 idiv），否则跑原版 idiv。
    // 两条路径尾部都重放 `mov ecx,esi; push eax` 并跳回 0x4FF090 让原 call 0x4FB760 执行。
    bool intervalChanged = false;
    for (size_t o = 0; o < 800 && !intervalChanged; ++o)
        for (size_t s = 0; s < 41; ++s)
            if (personalization_.interval[o][s] != OfficerPersonalization2026::defaultInterval[s]) {
                intervalChanged = true;
                break;
            }
    const BYTE intervalOriginal[5] = {0xF7, 0xF9, 0x8B, 0xCE, 0x50}; // idiv ecx; mov ecx,esi; push eax
    const LPVOID intervalHook = reinterpret_cast<LPVOID>(0x4FF08Bu);
    if (!intervalChanged) {
        writePatch(intervalHook, intervalOriginal, sizeof(intervalOriginal));
        appendRuntimeLog("personalization interval: no skill changed, hook restored at 0x004FF08B");
    } else {
        std::array<std::uint8_t, 800 * 41> intervalFlat{};
        for (size_t o = 0; o < 800; ++o)
            for (size_t s = 0; s < 41; ++s) {
                const std::uint8_t value = personalization_.interval[o][s];
                intervalFlat[o * 41 + s] =
                    value == OfficerPersonalization2026::defaultInterval[s] ? 0xFFu : value;
            }
        const BYTE intervalCode[77] = {
            0x53,                         // push ebx
            0x56,                         // push esi
            0x50,                         // push eax
            0x51,                         // push ecx
            0x8B, 0x43, 0x34,             // mov eax, [ebx+0x34]
            0x83, 0xE0, 0x3F,             // and eax, 0x3F
            0x83, 0xF8, 0x29,             // cmp eax, 0x29
            0x73, 0x2F,                   // jae not_custom
            0x0F, 0xB7, 0x4E, 0x04,       // movzx ecx, word [esi+4]
            0x81, 0xF9, 0x20, 0x03, 0x00, 0x00, // cmp ecx, 0x320
            0x73, 0x23,                   // jae not_custom
            0x6B, 0xC9, 0x29,             // imul ecx, ecx, 0x29
            0x03, 0xC1,                   // add eax, ecx
            0xB9, 0x00, 0x00, 0x00, 0x00, // mov ecx, intervalArray
            0x80, 0x3C, 0x01, 0xFF,       // cmp byte [ecx+eax], 0xFF
            0x74, 0x13,                   // je not_custom
            0x0F, 0xB6, 0x0C, 0x01,       // movzx ecx, byte [ecx+eax]
            0x8B, 0xC1,                   // mov eax, ecx
            0x83, 0xC4, 0x08,             // add esp, 8
            0x5E,                         // pop esi
            0x5B,                         // pop ebx
            0x8B, 0xCE,                   // mov ecx, esi
            0x50,                         // push eax
            0xE9, 0x00, 0x00, 0x00, 0x00, // jmp 0x4FF090
            0x59,                         // not_custom: pop ecx
            0x58,                         // pop eax
            0x99,                         // cdq
            0xF7, 0xF9,                   // idiv ecx
            0x5E,                         // pop esi
            0x5B,                         // pop ebx
            0x8B, 0xCE,                   // mov ecx, esi
            0x50,                         // push eax
            0xE9, 0x00, 0x00, 0x00, 0x00  // jmp 0x4FF090
        };
        const size_t total = intervalFlat.size() + sizeof(intervalCode);
        const auto region = allocateMemory(process, nullptr, total, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (region) {
            const DWORD arr = reinterpret_cast<DWORD>(region);
            const DWORD codeAddr = arr + static_cast<DWORD>(intervalFlat.size());
            writePatch(reinterpret_cast<LPVOID>(arr), intervalFlat.data(), intervalFlat.size());
            BYTE code[sizeof(intervalCode)];
            std::memcpy(code, intervalCode, sizeof(intervalCode));
            std::memcpy(code + 0x21, &arr, 4);           // mov ecx, intervalArray imm32
            const int jmpRel = static_cast<int>(0x4FF090u - (codeAddr + 0x39 + 5));
            std::memcpy(code + 0x3A, &jmpRel, 4);        // jmp 0x4FF090 (E9@0x39, rel32@0x3A)
            const int jmpRelNc = static_cast<int>(0x4FF090u - (codeAddr + 0x48 + 5));
            std::memcpy(code + 0x49, &jmpRelNc, 4);      // not_custom: jmp 0x4FF090 (E9@0x48, rel32@0x49)
            writePatch(reinterpret_cast<LPVOID>(codeAddr), code, sizeof(code));
            BYTE jump[5] = {0xE9, 0x00, 0x00, 0x00, 0x00};
            const int rel = static_cast<int>(codeAddr - (0x4FF08Bu + 5));
            std::memcpy(jump + 1, &rel, 4);
            writePatch(intervalHook, jump, sizeof(jump));
            size_t changed = 0;
            for (size_t o = 0; o < 800; ++o)
                for (size_t s = 0; s < 41; ++s)
                    if (personalization_.interval[o][s] != OfficerPersonalization2026::defaultInterval[s]) ++changed;
            std::ostringstream line;
            line << "personalization interval: entries=" << changed
                 << " hook=0x004FF08B codeAddr=0x" << std::hex << std::setw(8) << std::setfill('0') << codeAddr
                 << " arr=0x" << std::setw(8) << arr
                 << " codeSize=" << std::dec << sizeof(intervalCode)
                 << " jmpRel=0x" << std::hex << std::setw(8) << static_cast<unsigned>(jmpRel)
                 << " jmpRelNc=0x" << std::setw(8) << static_cast<unsigned>(jmpRelNc)
                 << " jump@hook=" << hexBytes(jump, sizeof(jump))
                 << " code=" << hexBytes(code, sizeof(code))
                 << " officerIndex=word[esi+4]";
            appendRuntimeLog(line.str().c_str());
        }
    }
}

void Runtime2026::installPerOfficerHook(HANDLE process, const std::array<std::uint8_t, 800>& values,
                                        DWORD hookAddress, const BYTE* original, size_t originalSize,
                                        DWORD returnAddress,
                                        const BYTE* code, size_t codeSize, size_t arrayAddrOffset,
                                        const char* feature, size_t extraJumpRelOffset) {
    bool any = false;
    size_t officers = 0;
    for (auto v : values) if (v > 0) { any = true; ++officers; }

    const LPVOID hook = reinterpret_cast<LPVOID>(hookAddress);
    if (!any) {
        writePatch(hook, original, originalSize);
        {
            std::ostringstream line;
            line << "personalization " << feature << ": no officer selected, hook restored at 0x"
                 << std::hex << std::setw(8) << std::setfill('0') << hookAddress
                 << " original=" << hexBytes(original, originalSize);
            appendRuntimeLog(line.str().c_str());
        }
        return;
    }

    const size_t total = values.size() + codeSize;
    const auto region = allocateMemory(process, nullptr, total, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!region) {
        appendRuntimeLog((std::string("personalization ") + feature + ": allocation failed").c_str());
        return;
    }
    const DWORD arr = reinterpret_cast<DWORD>(region);
    const DWORD codeAddr = arr + static_cast<DWORD>(values.size());

    writePatch(reinterpret_cast<LPVOID>(arr), values.data(), values.size());

    std::vector<BYTE> codeBytes(code, code + codeSize);
    std::memcpy(codeBytes.data() + arrayAddrOffset, &arr, 4);
    // 尾部 `E9 rel32` 跳回特性自定的继续执行点（activation/critical 在 stub 内
    // 已重放被替换指令后的逻辑，这里直接跳回 returnAddress）。
    const int jumpRel = static_cast<int>(returnAddress - (codeAddr + static_cast<DWORD>(codeSize)));
    std::memcpy(codeBytes.data() + codeSize - 4, &jumpRel, 4);
    // critical 代码块在 found 分支另有一处 `E9 rel32`（E9@0x41, rel32@0x42），
    // 同样跳回 returnAddress，需单独重定位（否则留在 0 会跳到自身后 5 字节）。
    if (extraJumpRelOffset != SIZE_MAX) {
        const int extraRel = static_cast<int>(returnAddress - (codeAddr + static_cast<DWORD>(extraJumpRelOffset + 4)));
        std::memcpy(codeBytes.data() + extraJumpRelOffset, &extraRel, 4);
    }
    writePatch(reinterpret_cast<LPVOID>(codeAddr), codeBytes.data(), codeBytes.size());

    // 覆盖恰好 originalSize 字节（activation/critical 均为 5 字节 `E9 rel32`），
    // 不会吞掉后续的 `add esp,0xC` 等原指令（B4）。
    std::vector<BYTE> jump(originalSize, 0x90);
    jump[0] = 0xE9;
    const int rel = static_cast<int>(codeAddr - (hookAddress + 5));
    std::memcpy(jump.data() + 1, &rel, 4);
    writePatch(hook, jump.data(), jump.size());

    {
        std::ostringstream line;
        line << "personalization " << feature << ": officers=" << officers
             << " hook=0x" << std::hex << std::setw(8) << std::setfill('0') << hookAddress
             << " codeAddr=0x" << std::setw(8) << codeAddr
             << " arr=0x" << std::setw(8) << arr
             << " codeSize=" << std::dec << codeSize
             << " arrayAddrOffset=0x" << std::hex << arrayAddrOffset
             << " return=0x" << std::setw(8) << returnAddress
             << " jumpRel=0x" << std::setw(8) << static_cast<unsigned>(jumpRel)
             << " jump@hook=" << hexBytes(jump.data(), jump.size())
             << " code=" << hexBytes(codeBytes.data(), codeBytes.size());
        appendRuntimeLog(line.str().c_str());
    }
}

bool Runtime2026::togglePlayerForce(unsigned index) {
    if (index >= 50 || process_.state().phase != SessionPhase2026::Active) return false;
    return runOperation([&] {
        findGameWindow(pointer<char>(0x42EDD4), nullptr);
        const DWORD address = 0x1253C38 + index * 0xD4 + 0x34;
        BYTE flag = 0;
        readMemory(process_.state().process, address, &flag, 1, nullptr);
        flag ^= 1;
        writeMemory(process_.state().process, address, &flag, 1, nullptr);
    });
}
bool Runtime2026::runOperation(const std::function<void()>& operation) {
    if (process_.state().phase == SessionPhase2026::Applying || process_.state().phase == SessionPhase2026::RollingBack)
        return false;
    if (process_.state().phase == SessionPhase2026::RecoveryRequired) {
        setStatus(RuntimeStatusKind2026::ModificationBlocked);
        return false;
    }
    savedState_ = state_;
    savedData_ = data_;
    savedSpeedAllocation_=speedAllocation_; savedAppliedSpeed_=appliedSpeed_;
    process_.beginOperation();
    try {
        if (process_.state().process) requireProcess(process_.state().process);
        operation();
        if (process_.state().process) requireProcess(process_.state().process);
        if (!resumeSpeedThreads()) return false;
        if (process_.state().journal.boundary() == 0 && !process_.state().journal.empty()) {
            initialState_ = savedState_; initialData_ = savedData_;
        }
        // Keep operation history intact until this succeeds. Any exception still
        // rolls back the complete operation to the preceding committed baseline.
        process_.state().journal.commit();
        process_.state().phase = process_.state().process ? SessionPhase2026::Active : SessionPhase2026::Idle;
        process_.state().result = OperationResult2026::Committed;
    } catch (const ProcessOperationFailure2026&) {
        {
            std::ostringstream line;
            line << "modify failed op=" << static_cast<int>(process_.state().failure.operation)
                 << " target=0x" << std::hex << std::setw(8) << std::setfill('0') << process_.state().failure.target
                 << std::dec << " requested=" << process_.state().failure.requested
                 << " transferred=" << process_.state().failure.transferred
                 << " error=" << process_.state().failure.systemError;
            appendRuntimeLog(line.str().c_str());
        }
        process_.state().phase = SessionPhase2026::RecoveryRequired;
        retryRollback();
        if (process_.state().result == OperationResult2026::RolledBack)
            setStatus(RuntimeStatusKind2026::ModificationRolledBack);
    } catch (...) {
        process_.state().failure = {RuntimeOperation2026::RuntimeException};
        appendRuntimeLog("modify exception (unknown), rolled back");
        process_.state().phase = SessionPhase2026::RecoveryRequired;
        retryRollback();
        if (process_.state().result == OperationResult2026::RolledBack)
            setStatus(RuntimeStatusKind2026::ExceptionRolledBack);
    }
    if (!resumeSpeedThreads()) return false;
    return process_.state().result == OperationResult2026::Committed;
}
bool Runtime2026::undo() {
    if (process_.state().process && observeProcess() == ProcessState2026::Exited) return retireExitedProcess();
    if (!retryRollback()) return false;
    if (process_.state().journal.empty()) { process_.state().result = OperationResult2026::Undone; return true; }
    savedState_ = initialState_; savedData_ = initialData_;
    process_.state().journal.restoreAll();
    undoPending_ = true;
    process_.state().phase = SessionPhase2026::RecoveryRequired;
    return retryRollback();
}

static_assert(sizeof(void*) == 4, "2026 runtime requires Win32");

Runtime2026::Runtime2026(ProcessApi2026 api): process_(api), threadBarrier_(api) {
    static constexpr BYTE seed[] = {
#include "../generated/companion_data.inc"
    };
    std::copy(std::begin(seed), std::end(seed), data_.begin());
#include "../generated/StateConstants.inc"
    constexpr DWORD offsets[] = {7620,3700,4720,4724,4828,4832,5212};
    constexpr DWORD values[] = {0xA31B0,0xA4000,0xA5662,0xA5630,0xA6E00,0xA3CC0,0xA1AE0};
    for (size_t i=0; i<std::size(offsets); ++i) field<DWORD>(offsets[i]) = values[i];
    for (size_t i=0;i<san9::PersonConfig::Count;++i) field<DWORD>(san9::layout2026::PersonLevels+4*i)=0x3FEAA;
    field<DWORD>(san9::injection2026::PersonFriendshipAddress)=0x610740;
    copyIn(san9::layout2026::PersonFriendships, pointer<BYTE>(0x42DB28), 58*6);
    std::fill_n(pointer<BYTE>(0x42DC84), 58*6, BYTE{0});
    std::fill_n(state_.data()+san9::injection2026::InitialCityMaxSoldier,50,BYTE{50});
    std::fill_n(state_.data()+san9::injection2026::InitialCityMaxPeopleHeart,50,BYTE{10});
    for (DWORD i=0;i<50;++i) global<BYTE>(0x42DAD8+i) += 2;
    const auto limits = san9::CityConfig{}.encodeLimits();
    copyIn(san9::layout2026::CityLimits, limits.data(), limits.size());
    setSkillConfig(san9::SkillConfig{});
    setFormationConfig(san9::FormationConfig{});
    setParameters(san9::ParameterConfig{});
}
void Runtime2026::setCallbackWindow(HWND owner) {
    field<HWND>(san9::layout2026::OwnerWindow)=owner;
    field<UINT>(san9::layout2026::MessageId)=process_.registerMessage("San9PKHard");
    field<DWORD>(san9::layout2026::CallbackReturnStub)=0xC3C3C3C3;
}
bool Runtime2026::isGameMessage(UINT message) const {
    UINT registered = 0;
    copyOut(san9::layout2026::MessageId, &registered, sizeof(registered));
    return registered != 0 && message == registered && hasGameSession();
}
bool Runtime2026::handleGameMessage(UINT message, WPARAM action, LPARAM argument) {
    if (!isGameMessage(message)) return false;
    auto& sequence = field<DWORD>(san9::layout2026::EventSequence);
    if (!sequence) {
        if (action == 1 && argument) modify(ModifyReason2026::GameNotification);
        else if (action == 2 && argument == 1020) sequence = 1;
    } else if (sequence == 1 && action == 2) {
        if (argument == 0) { modify(ModifyReason2026::GameDialogClosed); sequence = 0; }
        else if (argument == 1003) sequence = 0;
    }
    return true;
}
ConfigurationState2026 Runtime2026::configurationState() const {
    DWORD automatic = 0, pending = 0;
    BYTE skill = 0, formation = 0;
    copyOut(san9::layout2026::CityAutomatic, &automatic, sizeof(automatic));
    copyOut(san9::layout2026::CityPending, &pending, sizeof(pending));
    copyOut(san9::layout2026::SkillFlags, &skill, sizeof(skill));
    copyOut(san9::layout2026::FormationFlags, &formation, sizeof(formation));
    return {automatic != 0, pending != 0, (skill & 1) != 0, (skill & 2) != 0,
        (formation & 1) != 0, (formation & 2) != 0};
}
void Runtime2026::setConfigurationState(const ConfigurationState2026& state) {
    field<DWORD>(san9::layout2026::CityAutomatic) = state.cityAutomatic ? 1 : 0;
    field<DWORD>(san9::layout2026::CityPending) = state.cityPending ? 1 : 0;
    field<BYTE>(san9::layout2026::SkillFlags) = BYTE((state.skillLoaded ? 1 : 0) | (state.skillPending ? 2 : 0));
    field<BYTE>(san9::layout2026::FormationFlags) = BYTE((state.formationLoaded ? 1 : 0) | (state.formationPending ? 2 : 0));
}
void Runtime2026::copyIn(size_t offset, const void* source, size_t size) {
    auto destination=stateBytes(offset,size);
    validateImagePointer(reinterpret_cast<std::uintptr_t>(source),size);
    if (size) std::memcpy(destination,source,size);
}
void Runtime2026::copyOut(size_t offset, void* destination, size_t size) const {
    auto source=stateBytes(offset,size);
    validateImagePointer(reinterpret_cast<std::uintptr_t>(destination),size);
    if (size) std::memcpy(destination,source,size);
}
void Runtime2026::dataIn(DWORD address, const void* source, size_t size) {
    auto destination=dataBytes(address,size);
    validateImagePointer(reinterpret_cast<std::uintptr_t>(source),size);
    if (size) std::memcpy(destination,source,size);
}
void Runtime2026::dataOut(DWORD address, void* destination, size_t size) {
    auto source=dataBytes(address,size);
    validateImagePointer(reinterpret_cast<std::uintptr_t>(destination),size);
    if (size) std::memcpy(destination,source,size);
}
void Runtime2026::validateImagePointer(std::uintptr_t address, size_t size) const {
    const auto dataOrigin=reinterpret_cast<std::uintptr_t>(data_.data());
    const auto stateOrigin=reinterpret_cast<std::uintptr_t>(state_.data());
    if (address >= dataOrigin && address-dataOrigin <= data_.size())
        nativeDataBytes(address,size);
    else if (address >= stateOrigin && address-stateOrigin < state_.size())
        stateBytes(address-stateOrigin,size);
}
void Runtime2026::captureGameBytes(std::uintptr_t source, std::uintptr_t destination, SIZE_T size) {
    auto bytes=dataBytes(destination,size);
    readMemory(field<HANDLE>(san9::layout2026::ProcessHandle),source,bytes,size,nullptr);
}
void Runtime2026::setParameters(const san9::ParameterConfig& parameters) {
    if (parameters.validate().error != san9::ParameterError::none)
        throw std::invalid_argument("Invalid parameter configuration");
    for (const auto& f:san9::ParameterConfig::fields()) field<std::int32_t>(f.hostOffset)=parameters.get(f.controlId);
}
san9::PersonConfig Runtime2026::personConfig() const {
    san9::PersonConfig config;san9::PersonConfig::LevelBytes levels{};san9::PersonConfig::FriendshipBytes friendships{};
    copyOut(san9::layout2026::PersonLevels,levels.data(),levels.size());copyOut(san9::layout2026::PersonFriendships,friendships.data(),friendships.size());
    config.decode(levels,friendships);return config;
}
void Runtime2026::setPersonConfig(const san9::PersonConfig& config) {
    const auto levels=config.encodeLevels();const auto friendships=config.encodeFriendships();
    copyIn(san9::layout2026::PersonLevels,levels.data(),levels.size());copyIn(san9::layout2026::PersonFriendships,friendships.data(),friendships.size());
}
san9::FormationConfig Runtime2026::formationConfig() const {
    san9::FormationConfig config;san9::FormationConfig::Bytes bytes{};
    copyOut(san9::layout2026::FormationEntries,bytes.data(),bytes.size());config.decode(bytes);return config;
}
void Runtime2026::setFormationConfig(const san9::FormationConfig& config) {
    const auto bytes=config.encode();copyIn(san9::layout2026::FormationEntries,bytes.data(),bytes.size());
}
san9::SkillConfig Runtime2026::skillConfig() const {
    san9::SkillConfig config;san9::SkillConfig::Bytes bytes{};
    copyOut(san9::layout2026::SkillEntries,bytes.data(),bytes.size());config.decode(bytes);return config;
}
void Runtime2026::setSkillConfig(const san9::SkillConfig& config) {
    const auto bytes=config.encode();copyIn(san9::layout2026::SkillEntries,bytes.data(),bytes.size());
}
san9::CityConfig Runtime2026::cityConfig() const {
    san9::CityConfig config;
    std::array<BYTE, san9::CityConfig::Count * san9::CityConfig::RecordSize> limits{};
    copyOut(san9::layout2026::CityLimits,limits.data(),limits.size());
    config.decodeLimits(limits);
    std::memcpy(config.relations.data(),dataBytes(0x42D948,400),400);
    std::memcpy(config.attackDays.data(),dataBytes(0x42DAD8,50),50);
    return config;
}
void Runtime2026::setCityConfig(const san9::CityConfig& config) {
    const auto limits=config.encodeLimits();
    copyIn(san9::layout2026::CityLimits,limits.data(),limits.size());
    dataIn(0x42D948,config.relations.data(),400);
    dataIn(0x42DAD8,config.attackDays.data(),50);
}
bool Runtime2026::rebase() {
    static constexpr san9::Relocation relocations[] = {
#include "../generated/relocations.inc"
    };
    const DWORD base = field<DWORD>(san9::layout2026::AllocationBase);
    const DWORD delta = base-global<DWORD>(0x420050);
    if (!delta) return false;
    for (const auto& r:relocations) {
        DWORD value;
        BYTE* destination = r.objectField ? stateBytes(r.location,sizeof(value)) : dataBytes(r.location,sizeof(value));
        std::memcpy(&value,destination,4);
        value += delta;
        std::memcpy(destination,&value,4);
    }
    return true; // Caller updates the base slot, as in 0x409E10.
}
