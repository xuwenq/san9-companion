#include "Runtime2026.h"
#include "../model/PatchImage.h"
#include <algorithm>
#include <cstring>

// Fail the build if model encoding and the recovered layout ever drift.
static_assert(san9::PersonConfig::Count*4 == san9::layout2026::PersonLevelsSize);
static_assert(san9::PersonConfig::FriendshipCount*6 == san9::layout2026::PersonFriendshipsSize);
static_assert(san9::CityConfig::Count*san9::CityConfig::RecordSize == san9::layout2026::CityLimitsSize);
static_assert(san9::SkillConfig::Bytes{}.size() == san9::layout2026::SkillEntriesSize);
static_assert(san9::FormationConfig::Bytes{}.size() == san9::layout2026::FormationEntriesSize);
static_assert(sizeof(WORD) == san9::layout2026::MaxExperienceSize);
static_assert(sizeof(DWORD) == san9::layout2026::CityAutomaticSize && sizeof(DWORD) == san9::layout2026::CityPendingSize);
static_assert(sizeof(BYTE) == san9::layout2026::SkillFlagsSize && sizeof(BYTE) == san9::layout2026::FormationFlagsSize);

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
    constexpr size_t codeSize = 51;
    const size_t totalSize = bitset.size() + sizeof(DWORD) + codeSize;
    const auto region = allocateMemory(process, nullptr, totalSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!region) return;
    const DWORD bitsetAddr = reinterpret_cast<DWORD>(region);
    const DWORD multiplierAddr = bitsetAddr + static_cast<DWORD>(bitset.size());
    const DWORD codeAddr = multiplierAddr + sizeof(DWORD);

    writePatch(reinterpret_cast<LPVOID>(bitsetAddr), bitset.data(), bitset.size());
    writePatch(reinterpret_cast<LPVOID>(multiplierAddr), &multiplier, sizeof(multiplier));

    // Growth-multiplier code block. Entry: EAX = growth probability, ESI = officer.
    // It computes officer id = (ESI - 0x1258EE0) / 0x128, checks the bitset and
    // multiplies the probability, then stores it and jumps to kakuritsu.
    std::array<BYTE, codeSize> code = {
        0x50, 0x8B, 0xC6, 0x2D, 0xE0, 0x8E, 0x58, 0x12, // push eax; mov eax,esi; sub eax,0x1258EE0
        0x33, 0xD2, 0xB9, 0x28, 0x01, 0x00, 0x00, 0xF7, 0xF1, // xor edx,edx; mov ecx,0x128; div ecx
        0xB9, 0x00, 0x00, 0x00, 0x00,                   // mov ecx, bitsetAddr
        0x80, 0x3C, 0x01, 0x00,                         // cmp byte [ecx+eax], 0
        0x74, 0x0C,                                     // je not_in_list
        0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,             // mov ecx, [multiplierAddr]
        0x58, 0x0F, 0xAF, 0xC1,                         // pop eax; imul eax,ecx
        0xEB, 0x01,                                     // jmp store
        0x58,                                           // not_in_list: pop eax
        0x89, 0x44, 0x24, 0x04,                         // store: mov [esp+4], eax
        0xFF, 0x25, 0xF0, 0xF2, 0x5F, 0x00              // jmp [0x5FF2F0]
    };
    std::memcpy(code.data() + 0x12, &bitsetAddr, 4);
    std::memcpy(code.data() + 0x1E, &multiplierAddr, 4);
    writePatch(reinterpret_cast<LPVOID>(codeAddr), code.data(), code.size());

    // Redirect the tail to the growth-multiplier code block.
    BYTE jump[6] = {0xE9, 0x00, 0x00, 0x00, 0x00, 0x90};
    const int rel32 = static_cast<int>(codeAddr - (codeBlockTail + 5));
    std::memcpy(jump + 1, &rel32, 4);
    writePatch(reinterpret_cast<LPVOID>(codeBlockTail), jump, sizeof(jump));
}

void Runtime2026::applyCultivate() {
    const auto state = reinterpret_cast<std::uintptr_t>(state_.data());
    const bool enabled = *(DWORD *)(state + san9::injection2026::EnableTrainEnabled) != 0;
    const BYTE duration = enabled
        ? static_cast<BYTE>((std::min)((std::max)(cultivate_.durationRounds, 3u), 12u))
        : static_cast<BYTE>(9);
    const BYTE activate = enabled ? 1 : 0;
    // 训练回合/培养时长 (0x14FA656) and the first 小兵 activation flag (0x6613EE).
    writePatch(reinterpret_cast<LPVOID>(0x14FA656u), &duration, 1u);
    writePatch(reinterpret_cast<LPVOID>(0x6613EEu), &activate, 1u);
}

void Runtime2026::applyProficiencyCap() {
    // Raise the game's proficiency clamp from 1000 to 65535 (SanIXPKCheat's max).
    // 0x406A70 is min(value, 1000): "cmp eax,0x3E8" and "mov eax,0x3E8".
    const DWORD cap = 0xFFFF;
    writePatch(reinterpret_cast<LPVOID>(0x406A75u), &cap, 4u);
    writePatch(reinterpret_cast<LPVOID>(0x406A80u), &cap, 4u);
}

void Runtime2026::applyOfficerPersonalization() {
    const auto process = field<HANDLE>(san9::layout2026::ProcessHandle);
    if (!process) return;

    // 发动率：hook 0x4FEB95 的 kakuritsu，按武将覆盖发动概率。
    const BYTE rateOriginal[6] = {0xFF, 0x15, 0xF0, 0xF2, 0x5F, 0x00};
    const BYTE rateCode[42] = {
        0x50, 0x8B, 0xC6, 0x2D, 0xE0, 0x8E, 0x58, 0x12,
        0x33, 0xD2, 0xB9, 0x28, 0x01, 0x00, 0x00, 0xF7, 0xF1,
        0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0xB6, 0x0C, 0x01, 0x85, 0xC9, 0x74, 0x04, 0x89, 0x4C, 0x24, 0x04,
        0x58, 0xFF, 0x25, 0xF0, 0xF2, 0x5F, 0x00
    };
    installPerOfficerHook(process, personalization_.activationPercent, 0x4FEB95u,
                          rateOriginal, sizeof(rateOriginal), rateCode, sizeof(rateCode), 0x12);

    // 发动间隔：hook 0x4FF08D（idiv 之后），按武将覆盖间隔。
    const BYTE intervalOriginal[8] = {0x8B, 0xCE, 0x50, 0xE8, 0xCB, 0xC6, 0xFF, 0xFF};
    const BYTE intervalCode[48] = {
        0x50, 0x8B, 0xC3, 0x2D, 0xE0, 0x8E, 0x58, 0x12,
        0x33, 0xD2, 0xB9, 0x28, 0x01, 0x00, 0x00, 0xF7, 0xF1,
        0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0xB6, 0x0C, 0x01, 0x85, 0xC9, 0x74, 0x03, 0x89, 0x0C, 0x24,
        0x58, 0x8B, 0xCE, 0x50, 0xE8, 0x35, 0xB7, 0x4F, 0x00,
        0xE9, 0x65, 0xF0, 0x4F, 0x00
    };
    installPerOfficerHook(process, personalization_.interval, 0x4FF08Du,
                          intervalOriginal, sizeof(intervalOriginal), intervalCode, sizeof(intervalCode), 0x12);

    // 威力加倍(出动画)：hook 0x506035 的 10% 会心 kakuritsu，命中则把概率改为 100。
    const BYTE criticalOriginal[6] = {0xFF, 0x15, 0xF0, 0xF2, 0x5F, 0x00};
    const BYTE criticalCode[44] = {
        0x50, 0x8B, 0xC6, 0x2D, 0xE0, 0x8E, 0x58, 0x12,
        0x33, 0xD2, 0xB9, 0x28, 0x01, 0x00, 0x00, 0xF7, 0xF1,
        0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x3C, 0x01, 0x00, 0x74, 0x08, 0xC7, 0x44, 0x24, 0x04, 0x64, 0x00, 0x00, 0x00,
        0x58, 0xFF, 0x25, 0xF0, 0xF2, 0x5F, 0x00
    };
    installPerOfficerHook(process, personalization_.criticalHit, 0x506035u,
                          criticalOriginal, sizeof(criticalOriginal), criticalCode, sizeof(criticalCode), 0x12);
}

void Runtime2026::installPerOfficerHook(HANDLE process, const std::array<std::uint8_t, 800>& values,
                                        DWORD hookAddress, const BYTE* original, size_t originalSize,
                                        const BYTE* code, size_t codeSize, size_t arrayAddrOffset) {
    bool any = false;
    for (auto v : values) if (v > 0) { any = true; break; }

    const LPVOID hook = reinterpret_cast<LPVOID>(hookAddress);
    if (!any) {
        writePatch(hook, original, originalSize);
        return;
    }

    const size_t total = values.size() + codeSize;
    const auto region = allocateMemory(process, nullptr, total, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!region) return;
    const DWORD arr = reinterpret_cast<DWORD>(region);
    const DWORD codeAddr = arr + static_cast<DWORD>(values.size());

    writePatch(reinterpret_cast<LPVOID>(arr), values.data(), values.size());

    std::vector<BYTE> codeBytes(code, code + codeSize);
    std::memcpy(codeBytes.data() + arrayAddrOffset, &arr, 4);
    writePatch(reinterpret_cast<LPVOID>(codeAddr), codeBytes.data(), codeBytes.size());

    std::vector<BYTE> jump(originalSize, 0x90);
    jump[0] = 0xE9;
    const int rel = static_cast<int>(codeAddr - (hookAddress + 5));
    std::memcpy(jump.data() + 1, &rel, 4);
    writePatch(hook, jump.data(), jump.size());
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
        process_.state().phase = SessionPhase2026::RecoveryRequired;
        retryRollback();
        if (process_.state().result == OperationResult2026::RolledBack)
            setStatus(RuntimeStatusKind2026::ModificationRolledBack);
    } catch (...) {
        process_.state().failure = {RuntimeOperation2026::RuntimeException};
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
    field<WORD>(san9::layout2026::MaxExperience)=1500;
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
    WORD maximum=0;copyOut(san9::layout2026::MaxExperience,&maximum,sizeof(maximum));config.decode(levels,friendships,maximum);return config;
}
void Runtime2026::setPersonConfig(const san9::PersonConfig& config) {
    const auto levels=config.encodeLevels();const auto friendships=config.encodeFriendships();
    copyIn(san9::layout2026::PersonLevels,levels.data(),levels.size());copyIn(san9::layout2026::PersonFriendships,friendships.data(),friendships.size());
    field<WORD>(san9::layout2026::MaxExperience)=config.maxExperience;
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
