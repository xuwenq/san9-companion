#include "Runtime2026.h"
#include <sstream>
#include <iomanip>

using _DWORD = DWORD;
using _WORD = WORD;
using _BYTE = BYTE;
static_assert(sizeof(void*) == 4, "2026 injection requires Win32");
#include "../generated/Symbols.inc"

static const BYTE unk_41CD60[] = {
#include "../generated/OfficialLoyalty.inc"
};

// 原工具 unk_42C894 代码块（writePatch 到 allocationBase+0x9A50）在游戏启动早期即触发，
// 其逻辑 `mov esi,[esi+0x298]; 查找 id=0x45a; mov [eax+4],[0x661140]` 会：
//   1. 查找失败(eax=0)时写空指针 → 0x9A73 崩溃；
//   2. 查找成功时把游戏对象 id 字段(+4)污染成 [0x661140] 的未初始化值 → 连锁查找失败 → 0x5B6754 崩溃。
// 该功能非本项目四类武将个性化，属原工具(SanIXPKCheat)遗留代码块，直接禁用。
//
// ★ 禁用桩必须保留原块的**返回值契约**：原块入口 eax = 调用方传入的对象指针，
//   出口是 `mov eax,ebp; ret 4`，即把入口 eax 原样返回给调用方。
//   调用点 0x51FA13（descriptor 0x42C930 写入 `call 0xA9A50`）依赖该返回值：
//     0x51FA13: call 0xA9A50
//     0x51FA18: xor edx,edx ; je 0x51FA3B
//     0x51FA3B: mov ecx,eax
//     0x51FA3D: call 0x45C8A0      ← 以 ecx(=eax) 为 this，eax==0 则读 [0x28] 崩溃
//   2026-10-31 实机 dump 证实：早先使用的 `xor eax,eax; ret 4` 桩把 eax 抹成 0，
//   直接制造崩溃 #3（异常地址 0x45C8A0，读取地址 0x28，返回地址 0x51FA42）。
//   故改用 `ret 4`：原样返回入口 eax（eax 是寄存器传参，调用方期望拿回的就是它），
//   既不执行任何污染逻辑，又保持返回值与栈平衡语义。
static const BYTE unk_42C894_disabled[] = {
    0xC2, 0x04, 0x00,  // ret 4  (eax 原样返回；栈上弹出 1 个 stdcall 参数)
};
static_assert(sizeof(unk_42C894_disabled) == 3, "disabled stub size");

// The reason name appears in the log so a reader can tell which trigger
// produced a given patch application.
static const char* modifyReasonName(ModifyReason2026 reason) {
  switch (reason) {
  case ModifyReason2026::TimerTick: return "TimerTick";
  case ModifyReason2026::GameNotification: return "GameNotification";
  case ModifyReason2026::GameDialogClosed: return "GameDialogClosed";
  case ModifyReason2026::ParametersConfirmed: return "ParametersConfirmed";
  case ModifyReason2026::PresentationSpeed: return "PresentationSpeed";
  }
  return "Unknown";
}

// Evidence: 0x409E10. All commands execute inside the caller's journal transaction.
void Runtime2026::modifyBranches(ModifyReason2026 reason)
{
  appendRuntimeLogFormat("BRANCH reason=", modifyReasonName(reason));
  const HWND window = findGameWindow(ClassName, nullptr);
  if (!window)
  {
    field<HANDLE>(san9::layout2026::ProcessHandle) = nullptr;
    field<HWND>(san9::layout2026::TargetWindow) = nullptr;
    field<DWORD>(san9::layout2026::ConnectTicks) = 0;
    setStatus(RuntimeStatusKind2026::WaitingForGame, "no game window found");
    return;
  }
  if (field<HWND>(san9::layout2026::TargetWindow) != window)
  {
    appendRuntimeLog("BRANCH initial connection to a new game window");
    if (!connectGame(window)) return;
    appendRuntimeLog("BRANCH installing core hooks");
    installCoreHooks();
    appendRuntimeLog("BRANCH installing notification bridge");
    installNotificationBridge();
    appendRuntimeLog("BRANCH uploading injection payloads");
    uploadInjectionPayloads();
    appendRuntimeLog("BRANCH installing gameplay patches");
    installGameplayPatches();
    appendRuntimeLog("BRANCH applying parameter patches");
    applyParameterPatches();
    appendRuntimeLog("BRANCH applying game configuration");
    applyGameConfiguration(reason);
    appendRuntimeLog("BRANCH initial connection finished");
    animateConnectedStatus();
    return;
  }
  switch (reason)
  {
  case ModifyReason2026::ParametersConfirmed:
    appendRuntimeLog("BRANCH parameters confirmed: reapplying parameter patches and game configuration");
    applyParameterPatches();
    applyGameConfiguration(reason);
    break;
  case ModifyReason2026::GameNotification:
  case ModifyReason2026::GameDialogClosed:
    appendRuntimeLog("BRANCH game event: reapplying game configuration");
    applyGameConfiguration(reason);
    break;
  case ModifyReason2026::TimerTick:
  case ModifyReason2026::PresentationSpeed:
    break;
  default:
    return;
  }
  animateConnectedStatus();
}

bool Runtime2026::connectGame(HWND window)
{
  const int ticks = static_cast<int>(++field<DWORD>(san9::layout2026::ConnectTicks));
  if (ticks <= 5) return false;
  field<DWORD>(san9::layout2026::ConnectTicks) = 0;
  field<DWORD>(san9::layout2026::StatusTicks) = 0;
  field<HWND>(san9::layout2026::TargetWindow) = window;
  resolveProcess(window, &field<DWORD>(san9::layout2026::ProcessId));
  const HANDLE process = openProcess(0x38u, false, field<DWORD>(san9::layout2026::ProcessId));
  field<HANDLE>(san9::layout2026::ProcessHandle) = process;
  if (!checkVersion())
  {
    field<HWND>(san9::layout2026::TargetWindow) = nullptr;
    closeProcess(process);
    field<HANDLE>(san9::layout2026::ProcessHandle) = nullptr;
    setStatus(RuntimeStatusKind2026::UnsupportedVersion, "the game build does not match the recovered patch set");
    appendRuntimeLogFormat("CONNECT rejected: unsupported game version, pid=", field<DWORD>(san9::layout2026::ProcessId));
    return false;
  }
  DWORD oldProtection = 0;
  const DWORD pageSize = field<DWORD>(san9::injection2026::PageSize);
  protectMemory(process, field<LPVOID>(7448), pageSize, PAGE_EXECUTE_READWRITE, &oldProtection);
  protectMemory(process, field<LPVOID>(7452), pageSize, PAGE_READWRITE, &oldProtection);
  protectMemory(process, field<LPVOID>(7456), pageSize, PAGE_READWRITE, &oldProtection);
  protectMemory(process, field<LPVOID>(7468), pageSize, PAGE_EXECUTE_READWRITE, &oldProtection);
  const auto allocation = allocateMemory(process, nullptr,
      pageSize * field<DWORD>(san9::injection2026::AllocPageNum), MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  field<DWORD>(san9::layout2026::AllocationBase) = reinterpret_cast<DWORD>(allocation);
  if (rebase()) lpAddress = allocation;
  {
    std::ostringstream line;
    line << "connected allocationBase=0x" << std::hex << std::setw(8) << std::setfill('0')
         << reinterpret_cast<DWORD>(allocation) << std::dec << " pages="
         << field<DWORD>(san9::injection2026::AllocPageNum);
    appendRuntimeLog(line.str().c_str());
  }
  field<DWORD>(san9::layout2026::EventSequence) = 0;
  return true;
}

void Runtime2026::animateConnectedStatus()
{
  const int ticks = static_cast<int>(++field<DWORD>(san9::layout2026::StatusTicks));
  if (ticks < 1) return;
  setStatus(RuntimeStatusKind2026::Connected);
  status_.errorCount = field<DWORD>(san9::layout2026::ErrorCount);
  status_.activityDots = unsigned(ticks - 1);
  if (ticks == 6) field<DWORD>(san9::layout2026::StatusTicks) = 0;
}

void Runtime2026::installCoreHooks()
{
  const auto state = reinterpret_cast<std::uintptr_t>(state_.data());
  char *patchCodeCursorAt41F930;
  int patchIndexAt41F920;
  int patchIndexAt42B890;
  int patchIndexAt42B908;
  int *patchCodeCursorAt42B990;
  int patchIndexAt42B970;
  int patchIndexAt42B9D0;
  writeDescriptorRange(0x42F618u, 4, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F718u, 3, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F690u, 4, PatchSource2026::HostPointer);
  patchCodeCursorAt41F930 = (char *)&unk_41F930;
  patchIndexAt41F920 = 0;
  do
  {
    writePatch((LPVOID)dword_41F920[patchIndexAt41F920], patchCodeCursorAt41F930, dword_41F928[patchIndexAt41F920]);
    patchCodeCursorAt41F930 += 16;
    ++patchIndexAt41F920;
  }
  while ( (int)patchCodeCursorAt41F930 < (int)byte_41F950 );

  writeDescriptorRange(0x42F740u, 3, PatchSource2026::HostPointer);
  writePatch(lpBaseAddress, &codeDefenceHorseWimble, 0xFu);

  writeDescriptorRange(0x42F6E8u, 2, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F7D0u, 2, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F580u, 2, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F6C0u, 3, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F608u, 1, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F768u, 3, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F700u, 2, PatchSource2026::HostPointer);
  writePatch(dword_421640, &codeWisdomAntiBlackArt, 0xEu);

  writeDescriptorRange(0x42F598u, 5, PatchSource2026::HostPointer);
  writePatch(dword_420510, &codeLowerBuildExpense, 9u);

  writeDescriptorRange(0x42F660u, 2, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F7B8u, 2, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F5D8u, 1, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F5E8u, 1, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F790u, 3, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F5F8u, 1, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F7E8u, 2, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F678u, 2, PatchSource2026::HostPointer);

  writeDescriptorRange(0x42F648u, 2, PatchSource2026::HostPointer);
  writePatch(dword_4215D4, &codeIntelligenceKillerCoef, 8u);
  writePatch(dword_421608, &codeIntelligenceKillerCoef2, 9u);
  writePatch(dword_422598, &codeTreasureValue, 9u);
  writePatch(dword_423888, &codeOfficialLoyaty, 0xBu);
  writePatch(dword_4244C0, &codeBuildRealGoal, 0xFu);
  writePatch(dword_422D60, &codeSetForceGoal, 0xBu);
  writePatch(dword_425A10, &codeGetCityMaxPeopleHeart, 0xFu);
  writePatch(dword_42F0D0, &unk_4298B8, 0x36u);
  writePatch(dword_42F0CC, &unk_429918, 0x14u);
  writePatch(dword_420014, &codeTestDarkFlag, 0xDu);
  for ( patchIndexAt42B890 = 0; patchIndexAt42B890 < 4; ++patchIndexAt42B890 )
  {
    writePatch((LPVOID)dword_42B890[patchIndexAt42B890], (char *)&unk_42B8C0 + dword_42B8B0[patchIndexAt42B890], dword_42B8A0[patchIndexAt42B890]);
  }
  for ( patchIndexAt42B908 = 0; patchIndexAt42B908 < 4; ++patchIndexAt42B908 )
  {
    writePatch((LPVOID)dword_42B908[patchIndexAt42B908], (char *)&unk_42B938 + dword_42B928[patchIndexAt42B908], dword_42B918[patchIndexAt42B908]);
  }
  patchCodeCursorAt42B990 = (int *)&unk_42B990;
  patchIndexAt42B970 = 0;
  do
  {
    writePatch((LPVOID)dword_42B970[patchIndexAt42B970], patchCodeCursorAt42B990, dword_42B980[patchIndexAt42B970]);
    patchCodeCursorAt42B990 += 4;
    ++patchIndexAt42B970;
  }
  while ( (int)patchCodeCursorAt42B990 < (int)dword_42B9D0 );
  writePatch(dword_42F56C, &unk_4201BC, 0x20u);
  writePatch(dword_42F568, &unk_4201DC, 0x20u);
  writePatch(dword_42F3DC, &unk_423D8C, 0x18u);
  writePatch(*(LPVOID *)(state + 7620), &unk_41CD60, 0x4Du);
  writePatch(*(LPVOID *)(state + 5216), &unk_42DDE0, 0xCu);
  writePatch(dword_420008, &btMapKillerPower, 8u);
  writePatch(dword_42F578, &codeChaseFlag, 0x39u);
  writePatch(dword_42F564, &codeDispatchPerson, 0x86u);
  writePatch(dword_42F560, &unk_4202B0, 0xF4u);
  writePatch(dword_42F530, &codeBuildingTotalSoldier, 0x8Fu);
  writePatch(dword_42F514, &unk_421230, 0x2D4u);
  writePatch(dword_42F50C, &codeEnableCommerce, 0x6Au);
  writePatch(dword_42F508, &codeCheckAbuse, 0x52u);
  writePatch(dword_42F4F8, &codeSaveTroopExpense, 0x2Fu);
  writePatch(dword_42F4E8, &codeEnableCultivate, 0x85u);
  writePatch(dword_42F4D8, &codeCommandAbility, 0x3Fu);
  writePatch(dword_42F4CC, &codeJiaXuBack, 0x26u);
  writePatch(dword_42F4E4, &codeMoveIdlePerson, 0x1Au);
  writePatch(dword_42F4DC, &codeCommandIntAbility, 0x1Eu);
  writePatch(dword_42F104, &codeCaptiveMoney2, 0x26u);
  writePatch(dword_42F4B4, &codeCaptiveMoney, 0xDu);
  writePatch(dword_42F4A4, &codeInviteNewPerson, 0x20u);
  writePatch(dword_42F4A0, &codeUpdateKillerDamage, 0x2Fu);
  writePatch(dword_42F490, &codeSaveHarbor, 0x31u);
  writePatch(dword_42F48C, &codeGotoNpcCity, 0x66u);
  writePatch(dword_42F480, &codeFightToKiller, 0x17u);
  writePatch(dword_42F46C, &codePreBuildingSoldier, 0x5Cu);
  writePatch(dword_42F458, &codeNearNullCity, 0x5Du);
  writePatch(dword_42F450, &codeAllieAttack, 0x28u);
  writePatch(dword_42F43C, &unk_423110, 0x66u);
  writePatch(dword_42F3D8, &codeDecideAttackOk, 0x20u);
  writePatch(dword_42F40C, &unk_4237A8, 0x18u);
  writePatch(dword_42F3EC, &codeOperationPerDay, 0xA6u);
  writePatch(dword_42F3AC, &codeLeagueSucceed, 0x4Fu);
  writePatch(dword_42F3A4, &unk_424420, 0x1Du);
  writePatch(dword_42F3B4, &codeGoThroughNearBuilding, 0xC3u);
  writePatch(dword_42F3C8, &codeSelectWaterWisdom, 0x2Fu);
  writePatch(dword_42F464, &unk_422BB0, 0x3Cu);
  writePatch(dword_42F378, &unk_424870, 0xBAu);
  writePatch(dword_42F3F4, &codeLoadNearMaxSoldier, 0x10u);
  writePatch(dword_42F3F0, &codeLoadNearMaxSoldier2, 0x10u);
  writePatch(dword_42F424, &codeTestRegardedBuilding, 0x81u);
  writePatch(dword_42F38C, &codeKillerPowerToDamage, 0x24u);
  writePatch(dword_42F340, &codeGetBuildingProp, 0x28u);
  writePatch(dword_42F330, &unk_4252F8, 0x28u);
  writePatch(dword_42F2C0, &codeGetTransportSoldier, 0x50u);
  writePatch(dword_42F350, &codePhaseErrorAlienate, 0x34u);
  writePatch(dword_42F298, &codeMaxPeopleHeartPush, 0x26u);
  writePatch(dword_42F294, &codeMaxPeopleHeartMov, 0x12u);
  writePatch(dword_42F274, &codeAttackTroopSoldier2, 0xBDu);
  writePatch(dword_42F264, &codePreBuildingSoldier2, 0x118u);
  writePatch(dword_42F254, &codeSecondKillerInterval, 0x2Eu);
  writePatch(dword_42F244, &codeKillerArrayCoef, 0x82u);
  writePatch(dword_42F238, &codeDisplayTroopCondition, 0x6Au);
  writePatch(dword_42F224, &codeTroopAtSmallMap, 0x16u);
  writePatch(dword_42F1D4, &unk_4277A8, 0xB5u);
  writePatch(dword_42F1C8, &codeAllStrongForce, 0x3Au);
  writePatch(dword_42F1B8, &codeKillerDriveProp, 0x7Au);
  writePatch(dword_42F354, &codeDirectBigAnimation, 0x16u);
  writePatch(dword_42F19C, &codeUpdateCaptiveProp, 0x1Fu);
  writePatch(dword_42F190, &codeRobFood, 0x6Au);
  writePatch(dword_42F220, &codeGetTroopViewState, 0x1Bu);
  writePatch(dword_42F208, &codeHideDutyPersonNum, 0x3Eu);
  writePatch(dword_42F1F0, &codeHideCityMorale, 0x18u);
  writePatch(dword_42F1E4, &codeCounselorReportProp, 0x63u);
  writePatch(dword_42F17C, &codeBuildArriveDay, 0xA9u);
  writePatch(dword_42F570, &codeRetreatSoldierTroop, 0x14u);
  writePatch(dword_42F154, &codeHarvest, 0x72u);
  writePatch(dword_42F130, &codeAutoProcessTask, 0x26u);
  writePatch(dword_42F120, &codeKillSubPrestige, 0x2Eu);
  writePatch(dword_42F110, &codeAbilityKillerProp, 0x4Cu);
  writePatch(dword_42F100, &codeAddWaterKiller, 0x35u);
  writePatch(dword_42F0FC, &codeEnableWaterKiller, 0x10u);
  writePatch(dword_42F0F8, &unk_429220, 0x9Au);
  writePatch(dword_42F0F4, &codeInvestment, 0x62u);
  writePatch(dword_42F0F0, &codeHeartAttackDamage1, 0x28u);
  writePatch(dword_42F0EC, &codeHeartAttackDamage2, 0x13u);
  writePatch(dword_42F0E8, &unk_429410, 0x67u);
  writePatch(dword_42F0E4, &unk_4294F0, 0xF4u);
  writePatch(dword_42F0E0, &unk_429610, 0x5Au);
  writePatch(dword_42F0DC, &unk_429690, 0x70u);
  writePatch(dword_42F0D8, &unk_429728, 0x48u);
  writePatch(dword_42F0D4, &unk_429798, 0xF4u);
  writePatch(dword_42F070, &unk_42B3F0, 0xECu);
  writePatch(dword_42F06C, &unk_42B5C8, 0x98u);
  writePatch(dword_42B700, sourceVA(lpBuffer, nSize), nSize);
  writePatch(dword_42F068, &unk_42B718, 0x4Fu);
  writePatch(dword_42F064, aQhld, 0x2Cu);
  writePatch(dword_42F060, &unk_42B7BC, 0x40u);
  writePatch(dword_42F05C, &unk_42B850, 0x17u);
  for ( patchIndexAt42B9D0 = 0; patchIndexAt42B9D0 < 6; ++patchIndexAt42B9D0 )
  {
    writePatch((LPVOID)dword_42B9D0[patchIndexAt42B9D0], (char *)&unk_42BA18 + dword_42BA00[patchIndexAt42B9D0], dword_42B9E8[patchIndexAt42B9D0]);
  }
  writePatch(dword_42F058, &unk_42BA48, 0x358u);
  writePatch(dword_42F054, &unk_42BDE0, 0x1A0u);
  writePatch(dword_42F050, &unk_42BFA8, 0xA0u);
  writePatch(dword_42F04C, &unk_42C070, 0xC4u);
  writePatch(dword_42F048, &unk_42C4F0, 0x4Eu);
  writePatch(dword_42F044, &unk_42C568, 0x2Eu);
  writePatch(dword_42F040, &unk_42C5C0, 0x2Au);
  writePatch(off_42C7C8, &unk_42C7CC, 8u);
  writePatch(dword_42F03C, dword_42C7D4, 0xBEu);
  writePatch(dword_42F038, &unk_42C894_disabled, sizeof(unk_42C894_disabled));
  writePatch(dword_42F034, &unk_42C9D0, 0x20u);
  writePatch(dword_42F030, &unk_42CA18, 0x2Eu);
  writePatch(dword_42F02C, &unk_42CC90, 0x72u);
  writePatch(dword_42F024, &unk_42CE68, 0x6Eu);
  writePatch(dword_42F020, &unk_42CFF0, 0x9Eu);
  writePatch(dword_42F01C, aHX, 0x84u);
  writePatch(dword_42F018, &unk_42D568, 0xAEu);
  writePatch(dword_42F0A8, &unk_42A1B8, 0x24u);
  writePatch(dword_42F0A4, &unk_42A1DC, 0x34u);
  writePatch(dword_42F0A0, &unk_42A210, 0xECu);
  writePatch(dword_42F09C, &unk_42A2FC, 0x112u);
  writePatch(dword_42F098, &unk_42A410, 0x13Eu);
  writePatch(dword_42F094, &unk_42A578, 0x32u);
  writePatch(dword_42F090, &unk_42A5D8, 0x18Cu);
  writePatch(dword_42F08C, &unk_42A798, 0x6C0u);
  writePatch(dword_42F088, &unk_42AE58, 0x98u);
  writePatch(dword_42F084, &unk_42AEF0, 0x9Cu);
  writePatch(dword_42F080, &unk_42AFB8, 0xFCu);
  writePatch(dword_42F07C, asc_42B0B4, 0xEAu);
  writePatch(dword_42F078, &unk_42B1C8, 0x9Eu);
  writePatch(dword_42F074, &unk_42B2B8, 0xBCu);
  writePatch(dword_42F0C8, sub_429958, 0x80u);
  writePatch(dword_42F0C4, &unk_4299D8, 0xBAu);
  writePatch(dword_42F0C0, &unk_429D18, 0x88u);
  writePatch(dword_42F0BC, &unk_429DC8, 0x14u);
  writePatch(dword_42F0B8, &unk_429DDC, 0x88u);
  writePatch(dword_42F0B4, &unk_42A048, 0x1Au);
  writePatch(dword_42F0B0, &unk_42A098, 0x6Eu);
  writePatch(dword_42F0AC, &unk_42A158, 0x34u);
  writePatch(dword_42F55C, &codeWisdomPerson, 0x49u);
  writePatch(dword_42F540, &unk_4209C0, 0x59u);
  writePatch(dword_42F534, &codeForceTroopList, 0x40u);
  writePatch(dword_42F524, &codeRelayDispatchOk, 0x27u);
  writePatch(dword_42F51C, &codeBattleDisableTask, 0x72u);
  writePatch(dword_42F504, &codeBuildingBackup, 0x6Au);
  writePatch(dword_42F4F4, &unk_421B88, 0x2Eu);
  writePatch(dword_42F4E0, &codeCheckOfficerMoney, 0xA7u);
  writePatch(dword_42F4C4, &codeDegradePerson, 0x84u);
  writePatch(dword_42F4B0, &codeSearchRange, 0x78u);
  writePatch(dword_42F498, &codeBuildingKillerPerson, 0x6Eu);
  writePatch(dword_42F47C, &codeDraftMinMoney, 0x10u);
  writePatch(dword_42F468, &codeGoOnAttack, 0x4Du);
  writePatch(dword_42F454, &codeGetPlayerCity, 0xCDu);
  writePatch(dword_42F444, &codeAttackSourceCity, 0x24u);
  writePatch(dword_42F438, &codeAddCityRelation, 0x5Cu);
  writePatch(dword_42F428, &codeRetryAttackedBuilding, 0x2Bu);
  writePatch(dword_42F404, &codeLeftCleverKiller, 0x35u);
  writePatch(dword_42F3E8, &codeSelectAttackArray, 0x1A2u);
  writePatch(dword_42F3D4, &codeSetNearAttackFlag, 0x54u);
  writePatch(dword_42F3A0, &codeDecideBuild, 0x29u);
  writePatch(dword_42F3D0, &codeAdjustAttackSoldier, 0xCu);
  writePatch(dword_42F3C0, &codeAntiAttackAntitrap, 0x29u);
  writePatch(dword_42F398, &codeSelectTrainPerson, 0x23u);
  writePatch(dword_42F390, &codeRefuseChange, 0x29u);
  writePatch(dword_42F374, &unk_424958, 0xD0u);
  writePatch(dword_42F430, &codeTestAttackGoalForce, 0x7Fu);
  writePatch(dword_42F420, &codeRegardedBuildingOnAttack, 0x3Eu);
  writePatch(dword_42F368, &codeLinkageKillerToRecord, 0x16u);
  writePatch(dword_42F360, &codeUpdateSeigePower, 0x30u);
  writePatch(dword_42F34C, &codeForceFightTroop, 0x2Au);
  writePatch(dword_42F33C, &codeGetForceDangerCoef, 0x5Bu);
  writePatch(dword_42F380, &codeStopTaskBug, 0x16u);
  writePatch(dword_42F2BC, &codeAutoSaveDocname, 0x15u);
  // codePersonExperience 改由 applyProficiencyCap 按需写原版/上限版，避免此处晚于 installCoreHooks 覆盖掉上限逻辑。
  writePatch(dword_42F2A0, &codeIntelligenceOfficerPara, 0x2Fu);
  writePatch(dword_42F290, &codeMaxPeopleHeartBasic, 0x17u);
  writePatch(dword_42F27C, &codeDynamicAddProficiency, 0x46u);
  writePatch(dword_42F270, &codePersonCharNum, 0x8Cu);
  writePatch(dword_42F260, &unk_426220, 0xBDu);
  writePatch(dword_42F250, &unk_426540, 0x100u);
  writePatch(dword_42F54C, &codeDeleteBigPerson, 0x4Fu);
  writePatch(dword_42F230, &codeComparePersonName, 0x35u);
  writePatch(dword_42F1E0, &codeBlcokProp, 0x1Eu);
  writePatch(dword_42F0BC, &unk_429DC8, 0x14u);
  writePatch(dword_42F1B4, &codeClearTroopCurGoal, 0x19u);
  writePatch(dword_42F1A4, &codeStrengthKillerBigAnimaiton, 0x21u);
  writePatch(dword_42F198, aHX_0, 0xBCu);
  writePatch(dword_42F1CC, &codeKillerToBuilding, 0x1Du);
  writePatch(dword_42F21C, &codeJourneyGoalTroop, 0x1Du);
  writePatch(dword_42F204, &codeHideInfoMenu, 0x2Du);
  writePatch(dword_42F1F8, &codeHideDownStone, 0x1Au);
  writePatch(dword_42F1EC, &codeHideCityIncome, 0x17u);
  writePatch(dword_42F188, &codeChaosPowerCoef, 0x40u);
  writePatch(dword_42F170, &codeAttackPlayerBuild, 0xA4u);
  writePatch(dword_42F150, &unk_4287C8, 0x58u);
  writePatch(dword_42F144, &unk_428954, 0x60u);
  writePatch(dword_42F140, &unk_4289E0, 0x22u);
  writePatch(dword_42F13C, &unk_428A30, 0x98u);
  writePatch(dword_42F12C, &codeGetLeagueGoal, 0xBCu);
  writePatch(dword_42F11C, &codeTrainBasicAbility, 0x37u);
  // Reverse the basic-ability coefficient: "sub" -> "add" so a larger
  // coefficient strengthens the trained officer instead of weakening it.
  const BYTE addOpcode = 0x00;
  writePatch(reinterpret_cast<char *>(dword_42F11C) + 0x1D, &addOpcode, 1u);
  writePatch(reinterpret_cast<char *>(dword_42F11C) + 0x2E, &addOpcode, 1u);
  writePatch(dword_42F10C, &unk_429050, 0x2Du);
}

void Runtime2026::installNotificationBridge()
{
  const auto state = reinterpret_cast<std::uintptr_t>(state_.data());
  UINT messageId;
  char *patchCodeCursorAt41F848;
  int patchIndexAt41F818;
  if ( *(_DWORD *)(state + san9::layout2026::MessageId) == 0 )
  {
    messageId = registerRuntimeMessage(String);
    *(_DWORD *)(state + san9::layout2026::MessageId) = messageId;
  }
  writePatch((LPVOID)0x43F7E4, stateBytes(san9::layout2026::OwnerWindow, 0xCu), 0xCu);
  patchCodeCursorAt41F848 = (char *)&unk_41F848;
  patchIndexAt41F818 = 0;
  do
  {
    writePatch((LPVOID)dword_41F818[patchIndexAt41F818], patchCodeCursorAt41F848, dword_41F830[patchIndexAt41F818]);
    patchCodeCursorAt41F848 += 16;
    ++patchIndexAt41F818;
  }
  while ( (int)patchCodeCursorAt41F848 < (int)byte_41F8A8 );
}

void Runtime2026::uploadInjectionPayloads()
{
  int *patchCodeCursorAt425870;
  int patchIndexAt425840;
  writePatch(dword_42F574, &codeCheckTroopPos, 0x9Bu);
  writePatch(dword_42F0B4, &unk_42A048, 0x1Au);
  writePatch(dword_42F520, &codeFastRelay, 0x8Bu);
  writePatch(dword_42F550, &unk_4206A8, 0x1A1u);
  writePatch(dword_42F548, &codeAntiAttackMinDefence2, 0xC7u);
  writePatch(dword_42F500, &unk_421914, 0x13Eu);
  writePatch(dword_42F4F0, &codeMaxAntiTroop, 0x47u);
  writePatch(dword_42F4D0, &codeUndelCommandPerson, 0x19u);
  writePatch(dword_42F4BC, &codeArrayImage, 0x23u);
  writePatch(dword_42F4B8, &codeTroopExperience, 0x3Cu);
  writePatch(dword_42F4AC, &codeSearchCondition, 0x51u);
  writePatch(dword_42F494, &codeNotSureCaptive, 0x13u);
  writePatch(dword_42F488, &codeNewForceSoldier, 0x1Au);
  writePatch(dword_42F474, &codeSafeArriveDay, 0x14u);
  writePatch(dword_42F478, &codeAttackMinMoney, 0x71u);
  writePatch(dword_42F44C, &unk_422EE0, 0x18u);
  writePatch(dword_422FD0, &codeCircleAttackBuilding, 0xFu);
  writePatch(dword_42F510, &codeCheckGoOnAttack, 0xA2u);
  writePatch(dword_42F414, &unk_423570, 0x1AEu);
  writePatch(dword_42F400, &codeAddOfficialLoyaty, 0x26u);
  writePatch(dword_42F3E4, &codeAdjustKiller, 0x55u);
  writePatch(dword_42F3B8, &unk_424060, 0x6Bu);
  writePatch(dword_42F408, &codeBeingBuild, 0x13u);
  writePatch(dword_42F3BC, &codeAntiAttackWisdom, 0x60u);
  writePatch(dword_42F3C4, &codeSelectAttackWisdom, 0x31u);
  writePatch(dword_42F384, &codeDraftFailBug, 0x3Bu);
  writePatch(dword_42F3A8, &unk_424310, 0x12u);
  writePatch(dword_42F554, &codeCheckMinDefence, 0x23u);
  writePatch(dword_42F41C, &codeRegardedBuildingOnAttack2, 0x4Eu);
  writePatch(dword_42F370, &codeCheckPersonKillerLevel, 0x40u);
  writePatch(dword_42F388, &codeLinkageKillerToDialog, 0x1Du);
  writePatch(dword_42F35C, &codeGetArrayToWall, 0x21u);
  writePatch(dword_42F348, &codeCheckTransportTask, 0x3Fu);
  writePatch(dword_42F338, &unk_4251F4, 0x50u);
  writePatch(dword_42F2C8, &unk_4253FC, 0x34u);
  writePatch(dword_42F2B8, &codeMultiAlienate, 0x16u);
  writePatch(dword_42F2AC, &codeGetOfficerType, 0x20u);
  patchCodeCursorAt425870 = (int *)&unk_425870;
  patchIndexAt425840 = 0;
  do
  {
    writePatch((LPVOID)dword_425840[patchIndexAt425840], patchCodeCursorAt425870, dword_425858[patchIndexAt425840]);
    patchCodeCursorAt425870 += 4;
    ++patchIndexAt425840;
  }
  while ( (int)patchCodeCursorAt425870 < (int)dword_4258D0 );
  writePatch(dword_42F28C, &codeMaxSoldierPush, 0x14u);
  writePatch(dword_42F288, &unk_425C28, 0x18u);
  writePatch(dword_42F284, &unk_425C80, 0x12u);
  writePatch(dword_42F26C, &codeAttackRatioDoor, 0x35u);
  writePatch(dword_42F25C, &codeTroopRetreat, 0x1Cu);
  writePatch(dword_42F24C, &unk_42670C, 0x1Eu);
  writePatch(dword_42F240, &codeGetArrayAttackRange, 0x30u);
  writePatch(dword_42F22C, &codeCheckPlayerViewRange, 0x9Bu);
  writePatch(dword_42F1DC, &codeAdjustAttackArray, 0x274u);
  writePatch(dword_42F1C4, &codeCheckLongRangeArray, 0x24u);
  writePatch(dword_42F1B0, &codeMoveGuardTroop, 0xBCu);
  writePatch(dword_42F49C, &codeSoldierCommandKiller, 0x59u);
  writePatch(dword_42F194, &codeYearTableInfo, 0x17u);
  writePatch(dword_42F1BC, &codeWaterKillerProp, 0x4Eu);
  writePatch(dword_42F214, &codeFalseGoalTroop, 0x1Au);
  writePatch(dword_42F200, &codeDamageNumberDown, 0x1Au);
  writePatch(dword_42F1F4, &codeHideSpecialState, 0x17u);
  writePatch(dword_42F18C, &codeRetreatBuildingCondition, 0x10u);
  writePatch(dword_42F234, &codeTestPlayerViewRange, 0x75u);
  writePatch(dword_42F184, &codeClearSecondKillerPerson, 0xDu);
  writePatch(dword_42F178, &codeJudgeAttackBuild, 0x2Fu);
  writePatch(dword_42F15C, &codeMaxBusiness, 0x47u);
  writePatch(dword_42F14C, &codeGetForceMilitaryPop, 0x28u);
  writePatch(dword_42F138, &codePhaseErrorInvite, 0xAu);
  writePatch(dword_42F128, &codeFightSubIncome, 0x64u);
  writePatch(dword_42F118, &codeCheckDraftFood, 0x4Cu);
  writePatch(dword_42F108, &codeGetMaxCaptive, 0x17u);
  writePatch(dword_42F558, &codeAttackTroopCondition, 0x78u);
  writePatch(dword_42F538, &unk_420A80, 0x64u);
  writePatch(dword_42F52C, &codeRelayFailedDispatch, 0x146u);
  writePatch(dword_42F53C, &unk_420A48, 0x36u);
  writePatch(dword_42F518, &codeCheckAttackTroop, 0x6Bu);
  writePatch(dword_42F4FC, &codeAdjustPersonPos, 0x85u);
  writePatch(dword_42F4EC, &codeBuildingAntiAttackTroop, 0x34u);
  writePatch(dword_42F544, &unk_420990, 0x2Eu);
  writePatch(dword_42F4D4, &codeJiaXuOfficer, 0x54u);
  writePatch(dword_42F4C8, &codeIntelligenceAbility, 0x1Eu);
  writePatch(dword_42F4C0, &codeStoneStop, 0x7Du);
  writePatch(dword_42F4A8, &codeSearchInvite, 0x29u);
  writePatch(dword_42F484, &codeSubAntiTrick, 0x26u);
  writePatch(dword_42F470, &codeGossipToPlayer, 0x13u);
  writePatch(dword_42F45C, &codeFilterBuildingSoldier, 0x19u);
  writePatch(dword_42F448, &unk_422F20, 0x38u);
  writePatch(dword_42F440, &unk_423010, 0xD4u);
  writePatch(dword_42F42C, &codeReserveAttackedBuilding, 0x1Cu);
  writePatch(dword_42F028, &unk_42CD8C, 0x86u);
  writePatch(dword_42F410, &unk_423748, 0x36u);
  writePatch(dword_42F3FC, &unk_423910, 0x68u);
  writePatch(dword_42F3E0, &unk_423D60, 0x2Au);
  writePatch(dword_42F3B0, &unk_424258, 0x14u);
  writePatch(dword_42F39C, &codeMaxBuildMoney, 0x31u);
  writePatch(dword_42F3CC, &codeCheckWisdomAntitrap, 0x1Eu);
  writePatch(dword_42F394, &unk_4245B0, 0x20u);
  writePatch(dword_42F37C, &codeDraftMinFood, 0x13u);
  writePatch(dword_42F3F8, &unk_4239B8, 0x16u);
  writePatch(dword_42F460, &codePreBuildingSoldier3, 0xA2u);
  writePatch(dword_42F434, &codeTestNotNullCity, 0x29u);
  writePatch(dword_42F418, &codeRegardedBuildingOnAttack3, 0x34u);
  writePatch(dword_42F36C, &codeJudgeKillerLinkage, 0x25u);
  writePatch(dword_42F364, &codeMainForceDispatch, 0x55u);
  writePatch(dword_42F358, &codeGetArrayToGarrison, 0x21u);
  writePatch(dword_42F344, &unk_424DF0, 0x356u);
  writePatch(dword_42F334, &unk_425270, 0x60u);
  writePatch(dword_42F2C4, &codeCheckTransportBlock, 0xACu);
  writePatch(dword_42F2B4, &codeTryInviteCaptive, 0x68u);
  writePatch(dword_42F2A8, &codeCommandOfficerPara, 0x2Bu);
  writePatch(dword_42F29C, &codeMaxPeopleHeartCmp, 0x27u);
  writePatch(dword_42F2A4, &codeDeleteMaxOfficer, 0x12u);
  writePatch(dword_42F280, &codeMaxSoldierCmp, 0x11u);
  writePatch(dword_42F278, &codeAttackTroopSoldier, 0xEAu);
  writePatch(dword_42F268, &codeTroopAtGoodPlace, 0x3Fu);
  writePatch(dword_42F258, &codeSecondKiller, 0x198u);
  writePatch(dword_42F248, &codeGetAntiAttackGarrison, 0x30Bu);
  writePatch(dword_42F23C, &unk_426B40, 0xE0u);
  writePatch(dword_42F228, &codePersonSmallDialog, 0x10u);
  writePatch(dword_42F1D8, &codeGetArrayKillerPower, 0x6Cu);
  writePatch(dword_42F1D0, &codeChaosLinkageFlag, 0x17u);
  writePatch(dword_42F1C0, &codeChangeTroopCommand, 0x1Cu);
  writePatch(dword_42F1A8, &codeUseBaramArray, 0x58u);
  writePatch(dword_42F1A0, &codeWeakenLongRangeKiller, 0x48u);
  writePatch(dword_42F1AC, &codeMonthTableInfo, 0x22u);
  writePatch(dword_42F218, &codeGetInfoDistance, 0x6Cu);
  writePatch(dword_42F210, &codeDisplayBuildingCondition, 0xB8u);
  writePatch(dword_42F1FC, &codeGetPlayerForce, 0x1Bu);
  writePatch(dword_42F20C, &codeGetBuildingViewState, 0x1Cu);
  writePatch(dword_42F1E8, &codeAllocFreeTroop, 0x39u);
  writePatch(dword_42F180, &codeSkipArriveDay, 0x1C9u);
  writePatch(dword_42F174, &codeGetBuildSoldier, 0xA4u);
  writePatch(dword_42F158, &codeIncome, 0x6Bu);
  writePatch(dword_42F148, &codeGetForceStrength, 0x32u);
  writePatch(dword_42F134, &codePlayerAutoProcess, 0x33u);
  writePatch(dword_42F124, &codeLeaderInspire, 0xD1u);
  writePatch(dword_42F114, &codeRetreatChangeArray, 0x1Bu);
}

void Runtime2026::installGameplayPatches()
{
  const auto state = reinterpret_cast<std::uintptr_t>(state_.data());
  LPVOID *patchDescriptorCursorAt422918;
  int patchIndexAt4258D0;
  int patchIndexAt428000;
  int patchIndexAt42C134;
  int patchIndexAt42CA78;
  int patchIndexAt42CADC;
  int patchIndexAt42CB74;
  const DWORD initializationValue = 1;
  writeDescriptorRange(0x42CF68u, 7, PatchSource2026::CompanionAddress);

  writeDescriptorRange(0x41FA58u, 6, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41F960u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FB68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422390u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422368u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FBA8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FBD0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FC10u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FC50u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FC90u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FD18u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FDC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FE18u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FE70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FF00u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421518u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421700u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421D20u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FF98u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41FFF0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4200A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4200C8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420690u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420190u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4204F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420400u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420428u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420450u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420478u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4204A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420298u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4205A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420530u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420640u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420AF8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420860u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420A30u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420B68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420BD0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420C88u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424BB0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420CF8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420E68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420EB8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420F70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420FE0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4210D8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421168u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421180u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421080u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4215F0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421628u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421668u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421740u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421868u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421890u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421A68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421B18u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421B70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421BC8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421C38u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422B48u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421E90u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421EE8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421F88u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422008u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422050u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422078u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422118u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4220A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4220C8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422158u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422208u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421D68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421DC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422300u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4222B0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421F28u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429120u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4223C8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422468u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4224E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422538u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422580u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4225B8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422610u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422708u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4227A8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422880u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4211A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4211B8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4211D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4211E8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421200u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421218u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422838u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4228D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4228F8u, PatchSource2026::CompanionAddress);
  patchDescriptorCursorAt422918 = (LPVOID *)&unk_422918;
  do
  {
    writePatch(*patchDescriptorCursorAt422918, &unk_422910, 4u);
    ++patchDescriptorCursorAt422918;
  }
  while ( (int)patchDescriptorCursorAt422918 < (int)&dword_422948 );
  writeDescriptor(0x422958u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4229D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422A70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422AC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422CC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422D48u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422E78u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422F08u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422F68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422EC8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422FB8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422FF8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4230F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423190u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B5B0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423218u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423338u, PatchSource2026::CompanionAddress);

  writeDescriptorRange(0x42CE38u, 2, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423DD8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423390u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423730u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423790u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4237D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423870u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4238A8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4238F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4239A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423CF0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423E58u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4240E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424280u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4242F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424450u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4244A8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423810u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424108u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4244E8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424548u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4241F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424218u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423E90u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423F08u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423FC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424048u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424598u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423F68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4245E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424638u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424738u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424858u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424940u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424A38u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424338u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4239E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423A18u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423A50u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x422D80u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423270u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423480u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4234F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423558u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424AA0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424AF0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424688u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424B30u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4246D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424C30u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424C80u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424CC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424D70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425590u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424DD8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425158u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425258u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4252E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425328u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424778u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4247A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4247C8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4247F0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424818u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425440u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425518u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4255D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425610u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425728u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425798u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4257D8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425828u, PatchSource2026::CompanionAddress);
  for ( patchIndexAt4258D0 = 0; patchIndexAt4258D0 < 2; ++patchIndexAt4258D0 )
  {
    writePatch((LPVOID)dword_4258D0[patchIndexAt4258D0], (char *)&unk_425900 + dword_4258E0[patchIndexAt4258D0], dword_4258D8[patchIndexAt4258D0]);
  }
  writeDescriptor(0x425980u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4259A8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4259D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4259F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425A38u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425A88u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425AB0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425AD8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425B00u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425B40u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425B68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425BA8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425BE8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425C10u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425C48u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425C68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425CA0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425CC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425D00u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425D70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425E88u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x425F70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426028u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426088u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4260F0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426310u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426528u, PatchSource2026::CompanionAddress);

  writeDescriptorRange(0x426690u, 5, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426740u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426B28u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426CA0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426E28u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426E68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426FD0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4274B0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427890u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4278B8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4278E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427990u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429900u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427B18u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427B40u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429940u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427B88u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427C70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427D68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427DF8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427EE0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427F20u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427CC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427FB8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427928u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426ED0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426FA8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4270B0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427138u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427160u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x420038u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x426EF8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4271E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427228u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427288u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4272C8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427FE8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427308u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427348u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427370u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4273D8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x427468u, PatchSource2026::CompanionAddress);
  for ( patchIndexAt428000 = 0; patchIndexAt428000 < 5; ++patchIndexAt428000 )
  {
    writePatch((LPVOID)dword_428000[patchIndexAt428000], (char *)&unk_42803C + dword_428028[patchIndexAt428000], dword_428014[patchIndexAt428000]);
  }
  writeDescriptor(0x428118u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428150u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428348u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428420u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428478u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428658u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428678u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428710u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4287B0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428830u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428850u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428870u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428890u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4288B8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4288E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4289C8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428A18u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428CA8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428D38u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428D58u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428D78u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x423B20u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428ED0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x428BC0u, PatchSource2026::CompanionAddress);
  writePatch(off_428F30, sourceVA(off_428F38, dword_428F34), dword_428F34);
  writeDescriptor(0x428FC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429090u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4290D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4291B0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4292D0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429360u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4293B8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4293F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429488u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4294B0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4294D8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4295F8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429678u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429710u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429780u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4298A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B500u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B528u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B540u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B568u, PatchSource2026::CompanionAddress);

  writeDescriptorRange(0x42B690u, 3, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B778u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B810u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B838u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42B878u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42BDC8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42BF90u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42C058u, PatchSource2026::CompanionAddress);
  for ( patchIndexAt42C134 = 0; patchIndexAt42C134 < 6; ++patchIndexAt42C134 )
  {
    writePatch((LPVOID)dword_42C134[patchIndexAt42C134], (char *)&unk_42C17C + dword_42C164[patchIndexAt42C134], dword_42C14C[patchIndexAt42C134]);
  }
  writeDescriptor(0x42C550u, PatchSource2026::CompanionAddress);
  writePatch(off_42C5A8, sourceVA(off_42C5B0, dword_42C5AC), dword_42C5AC);
  writeDescriptor(0x42C600u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42C930u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42C9B8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42CA00u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42CA60u, PatchSource2026::CompanionAddress);
  for ( patchIndexAt42CA78 = 0; patchIndexAt42CA78 < 3; ++patchIndexAt42CA78 )
  {
    writePatch((LPVOID)dword_42CA78[patchIndexAt42CA78], (char *)&unk_42CA9C + dword_42CA90[patchIndexAt42CA78], dword_42CA84[patchIndexAt42CA78]);
  }
  for ( patchIndexAt42CADC = 0; patchIndexAt42CADC < 4; ++patchIndexAt42CADC )
  {
    writePatch((LPVOID)dword_42CADC[patchIndexAt42CADC], (char *)&unk_42CB0C + dword_42CAFC[patchIndexAt42CADC], dword_42CAEC[patchIndexAt42CADC]);
  }
  for ( patchIndexAt42CB74 = 0; patchIndexAt42CB74 < 4; ++patchIndexAt42CB74 )
  {
    writePatch((LPVOID)dword_42CB74[patchIndexAt42CB74], (char *)&unk_42CBA4 + dword_42CB94[patchIndexAt42CB74], dword_42CB84[patchIndexAt42CB74]);
  }
  writeDescriptor(0x42CC78u, PatchSource2026::CompanionAddress);

  writeDescriptorRange(0x42CD40u, 3, PatchSource2026::CompanionAddress);

  writeDescriptorRange(0x42D0C0u, 3, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42D1A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42D1C8u, PatchSource2026::CompanionAddress);

  writeDescriptorRange(0x42D338u, 23, PatchSource2026::CompanionAddress);

  writeDescriptorRange(0x42D7B8u, 17, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42A080u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42A118u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42A140u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42A1A0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429E78u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429EA0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429EC8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429EF0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429F18u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429F40u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429F68u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429F90u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429FB8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429FE0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42A008u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42A030u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429AA8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429AD0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429AF8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429B20u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429B48u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429B70u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429B98u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429BC0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429BE8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429C10u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429C38u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429C60u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429C88u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429CB0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429CD8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429D00u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x429DB0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41F8E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41F908u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x41F8B8u, PatchSource2026::CompanionAddress);
  writePatch(*(LPVOID *)(state + 6440), &initializationValue, 1u);
  writeDescriptor(0x42A560u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x42A5C0u, PatchSource2026::CompanionAddress);
  writePatch(off_42AFA0, sourceVA(off_42AFA8, dword_42AFA4), dword_42AFA4);
  writeDescriptor(0x42B1B0u, PatchSource2026::CompanionAddress);
  writePatch(off_42B278, sourceVA(off_42B280, dword_42B27C), dword_42B27C);
  writePatch(off_42B2A0, sourceVA(off_42B2A8, dword_42B2A4), dword_42B2A4);
  writePatch(off_42B388, sourceVA(off_42B390, dword_42B38C), dword_42B38C);
  writePatch(off_42B3B0, sourceVA(off_42B3B8, dword_42B3B4), dword_42B3B4);
  writePatch(off_42B3D8, sourceVA(off_42B3E0, dword_42B3DC), dword_42B3DC);
  writeDescriptor(0x41F988u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x421788u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4217B8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4217E8u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424240u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424360u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x4243E0u, PatchSource2026::CompanionAddress);
  writeDescriptor(0x424408u, PatchSource2026::CompanionAddress);
  captureGameBytes(addressOf(dword_4253B4), addressOf(off_4253BC), dword_4253B8);
  writeDescriptor(0x4253A8u, PatchSource2026::CompanionAddress);
  writePatch(dword_4253C0, &codeGetPersonFriendship, 0x38u);
  writeDescriptor(0x427188u, PatchSource2026::CompanionAddress);
  writePatch(*(LPVOID *)(state + san9::injection2026::BurnExperienceAddr), stateBytes(san9::injection2026::BurnExperienceCode, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::RobExperienceAddr), stateBytes(san9::injection2026::RobExperienceCode, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::ReinforceExperienceAddr), stateBytes(san9::injection2026::ReinforceExperienceCode, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::TrainExperienceAddr), stateBytes(san9::injection2026::TrainExperienceCode, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::RepairExperienceAddr), stateBytes(san9::injection2026::RepairExperienceCode, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::CultivateMinPoliticsAddr), stateBytes(san9::injection2026::CultivateMinPolitics, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::DispatchArrayAddr), stateBytes(san9::injection2026::DispatchArray, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::DispatchShipAddr), stateBytes(san9::injection2026::DispatchShip, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::ChaosDayAddr), stateBytes(san9::injection2026::ChaosDayCoef, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::LowerTrickAddr), stateBytes(san9::injection2026::LowerTrickCoef, 1u), 1u);
  writePatch(*(LPVOID *)(state + 6480), stateBytes(6484, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::LowerTrapAddr), stateBytes(san9::injection2026::LowerTrickCoef, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AlienateIntelligenceAddr), stateBytes(san9::injection2026::AlienateIntelligenceCode, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::GossipIntelligenceAddr), stateBytes(san9::injection2026::GossipIntelligenceCode, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::InspireIntelligenceAddr), stateBytes(san9::injection2026::InspireIntelligenceCode, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::CheatIntelligenceAddr), stateBytes(san9::injection2026::CheatIntelligenceCode, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::DisorderIntelligenceAddr), stateBytes(san9::injection2026::DisorderIntelligenceCode, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::BuildExperienceAddr), stateBytes(san9::injection2026::BuildExperienceCode, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AntiAttackMinSoldierdAddr), stateBytes(san9::injection2026::AntiAttackMinSoldier, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AntiAttackBarbarismAddr), stateBytes(san9::injection2026::AntiAttackBarbarism, 1u), 1u);
  writePatch(*(LPVOID *)(state + 6504), stateBytes(6508, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AlienateForceNameAddr), stateBytes(san9::injection2026::AlienateForceName, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AntiMinTroopSoldierAddr), stateBytes(san9::injection2026::AntiMinTroopSoldier, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::SaveBaseAddr), stateBytes(san9::injection2026::SaveBase, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::OfficerBasicCommandAddr), stateBytes(san9::injection2026::OfficerBasicCommand, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::OfficerBasicIntAddr), stateBytes(san9::injection2026::OfficerBasicInt, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::SearchMinPoliticsAddr), stateBytes(san9::injection2026::SearchMinPolitics, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::FootAntiElephantAddr), stateBytes(san9::injection2026::FootAntiElephant, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::InspireMaxVigourAddr), stateBytes(san9::injection2026::InspireMaxVigour, 1u), 1u);
  writePatch(*(LPVOID *)(state + 6740), stateBytes(6744, 1u), 1u);
  writePatch(*(LPVOID *)(state + 6752), stateBytes(san9::injection2026::GossipSubLoyalty, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::GoalNullCityAddr), stateBytes(san9::injection2026::GoalNullCity, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::NullBulidingDangerAddr), stateBytes(san9::injection2026::NullBulidingDanger, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AvoidNearCityAddr), stateBytes(san9::injection2026::AvoidNearCity, 2u), 2u);
  writePatch(*(LPVOID *)(state + san9::injection2026::RetreatMinVigourAddr), stateBytes(san9::injection2026::RetreatMinVigour, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::SelectCleverKillerAddr), stateBytes(san9::injection2026::SelectCleverKiller, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::NeglectChongCheAddr), (LPCVOID)(state + (skipDurabilitySiegeSelection_ ? 6828 : 6832)), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::SameForceTroopAddr), stateBytes(san9::injection2026::SameForceTroop, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::SameForceTroopAddr2), stateBytes(san9::injection2026::SameForceTroop, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AttackIgnoreAntitrapAddr), stateBytes(san9::injection2026::AttackIgnoreAntitrap, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::HomeForceDoorAddr), stateBytes(san9::injection2026::HomeForceDoor, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AvoidInduceMaxDayAddr), stateBytes(san9::injection2026::AvoidInduceMaxDay, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::GetTroopGoalBugAddr), stateBytes(san9::injection2026::GetTroopGoalBug, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::GoalToSourceDamageAddr), stateBytes(san9::injection2026::GoalToSourceDamage, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::BadBrothersArrayEndAddr), stateBytes(san9::injection2026::BadBrothersArrayEnd, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::TroopFromBuildingAddr), stateBytes(san9::injection2026::TroopFromBuilding, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::FalseAntiTroopAddr), stateBytes(san9::injection2026::ExternalAntiTroop, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::DisorderAntiTroopAddr), stateBytes(san9::injection2026::InternalAntiTroop, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::DeclareAttackTroopAddr), stateBytes(san9::injection2026::DeclareAttackTroop, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::InviteSubLoyatyAddr), stateBytes(san9::injection2026::InviteSubLoyaty, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::CommandOfficerEndFlagAddr), stateBytes(san9::injection2026::CommandOfficerEndFlag, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::IntelligenceOfficerEndFlagAddr), stateBytes(san9::injection2026::IntelligenceOfficerEndFlag, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::ShowCelueKillerAddr), stateBytes(san9::injection2026::ShowCelueKiller, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::LvbuAttackDoorAddr), stateBytes(san9::injection2026::LvbuAttackDoor, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::EnableSecondKillerAddr), stateBytes(san9::injection2026::EnableSecondKiller, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::TroopGoalBuildingAddr), stateBytes(san9::injection2026::TroopGoalBuilding, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::NotGetPersonQuotaAddr), stateBytes(san9::injection2026::NotGetPersonQuota, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::YearTableInvalidFlagAddr), stateBytes(san9::injection2026::YearTableInvalidFlag, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::TransportGoalInfoAddr), stateBytes(san9::injection2026::TransportGoalInfo, 4u), 4u);
  writePatch(*(LPVOID *)(state + san9::injection2026::CounselorMinCandidateAddr), stateBytes(san9::injection2026::CounselorMinCandidate, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::NoarrayToBuildingAddr), stateBytes(san9::injection2026::NoarrayToBuilding, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::NoarrayToBuildingAddr2), stateBytes(san9::injection2026::NoarrayToBuilding, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::SearchNoResultAddr), stateBytes(san9::injection2026::SearchNoResult, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::AttackBuildForceAddr), stateBytes(san9::injection2026::AttackBuildForce, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::BuildMaxArriveDayAddr), stateBytes(san9::injection2026::BuildMaxArriveDay, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::GameEndYearAddr), stateBytes(san9::injection2026::GameEndYear, 4u), 4u);
  writePatch((LPVOID)(*(_DWORD *)(state + san9::injection2026::BuildMinBuildingAddr) - 32), stateBytes(san9::injection2026::BuildMinBuilding, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::BuildMinBuildingAddr), stateBytes(san9::injection2026::BuildMinBuilding, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::UpdateGoalForceAddr), stateBytes(san9::injection2026::UpdateGoalForce, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::UpdateGoalPropAddr), stateBytes(san9::injection2026::UpdateGoalProp, 1u), 1u);

  writeInlineRange(0x42C250u, 42);

  // Work code 15's timing at 0x42C788 is now owned by the optional parameter phase.
  writeInlineRange(0x42C618u, 23);
  writeInlineRange(0x42C798u, 3);
  writePatch(*(LPVOID *)(state + 7480), stateBytes(7484, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7492), stateBytes(7504, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7496), stateBytes(7504, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7500), stateBytes(7512, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7520), stateBytes(7524, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7532), stateBytes(7536, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7544), stateBytes(7548, 4u), 4u);
  writePatch(*(LPVOID *)(state + 7556), stateBytes(7560, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7580), stateBytes(7584, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7592), stateBytes(7596, 4u), 4u);
  writePatch(*(LPVOID *)(state + 7604), stateBytes(7608, 4u), 4u);
}

void Runtime2026::applyParameterPatches()
{
  const auto state = reinterpret_cast<std::uintptr_t>(state_.data());
  int selectedSoldierCommandKillerIndexBias;
  int selectedNpcAllieAttackIndexBias;
  int selectedDisableBurnIndexBias;
  int selectedEnableRelayDispatchIndexBias;
  int selectedEnableCityPopulationCoefIndexBias;
  DWORD patchValue = 0;
  writePatch(*(LPVOID *)(state + 3700), stateBytes(san9::layout2026::PersonLevels, 0xC80u), 0xC80u);
  writePatch(*(LPVOID *)(state + san9::injection2026::PersonFriendshipAddress), stateBytes(san9::layout2026::PersonFriendships, 0x2B8u), 0x2B8u);
  byte_42F164 = *(_BYTE *)(state + san9::injection2026::MoneyCut1);
  byte_42F165 = *(_BYTE *)(state + san9::injection2026::MoneyCut2);
  byte_42F166 = *(_BYTE *)(state + san9::injection2026::MoneyCut3);
  byte_42F167 = *(_BYTE *)(state + san9::injection2026::FoodCut1);
  byte_42F168 = *(_BYTE *)(state + san9::injection2026::FoodCut2);
  byte_42F169 = *(_BYTE *)(state + san9::injection2026::FoodCut3);
  byte_42F16A = *(_BYTE *)(state + san9::injection2026::DraftCut1);
  byte_42F16B = *(_BYTE *)(state + san9::injection2026::DraftCut2);
  byte_42F16C = *(_BYTE *)(state + san9::injection2026::DraftCut3);
  writePatch(dword_42F160, &byte_42F164, 9u);
  patchValue = *(_DWORD *)(state + san9::injection2026::YearDifference);
  writePatch((char *)dword_42F394 + 2, &patchValue, 4u);
  writePatch((char *)dword_42F194 + 12, &patchValue, 4u);
  patchValue = *(_DWORD *)(state + san9::injection2026::KillerPowerCoef);
  writePatch((char *)dword_42F4A0 + 29, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::OldSlashDeadCoef) + 4 * *(_DWORD *)(state + san9::injection2026::SlashDeadCoef);
  writePatch(*(LPVOID *)(state + san9::injection2026::SlashDeadCoefAddr), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::SecondKillerEnabled) == 0;
  writeDescriptor(0x4264D0u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::NoOfficialSalary);
  if ( patchValue != 0 )
    writePatch(*(LPVOID *)(state + 7616), &patchValue, 4u);
  patchValue = 40 - 2 * *(_DWORD *)(state + san9::injection2026::BlockPropCoef);
  writePatch((char *)dword_42F1E0 + 8, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::NpcAttackDisorder);
  if ( patchValue != 0 )
    patchValue = 10 - patchValue;
  writePatch((LPVOID)0x43F4CC, &patchValue, 1u);
  writePatch((LPVOID)0x43F4CF, stateBytes(san9::injection2026::NatureAttractCoef, 1u), 1u);
  writePatch(*(LPVOID *)(state + 7360), stateBytes(san9::injection2026::OnePersonKillerProp, 1u), 1u);
  writePatch((LPVOID)(*(_DWORD *)(state + 7360) + 8), stateBytes(san9::injection2026::TwoPersonKillerProp, 1u), 1u);
  writePatch((LPVOID)(*(_DWORD *)(state + 7360) + 16), stateBytes(san9::injection2026::ThreePersonKillerProp, 1u), 1u);
  writePatch((LPVOID)(*(_DWORD *)(state + 7360) + 24), stateBytes(san9::injection2026::FourPersonKillerProp, 1u), 1u);
  writePatch((LPVOID)(*(_DWORD *)(state + 7360) + 32), stateBytes(san9::injection2026::FivePersonKillerProp, 1u), 1u);
  writePatch((LPVOID)0x43FF19, stateBytes(san9::injection2026::AntiPlayerLeagueCoef, 1u), 1u);
  patchValue = 4 - (*(_DWORD *)(state + san9::injection2026::AntiPlayerLeagueCoef) >> 2);
  writePatch(*(LPVOID *)(state + san9::injection2026::CheckLeagueProbabilityAddr), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::LeagueTimeAddr), stateBytes(san9::injection2026::LeagueTime, 1u), 1u);
  writePatch((LPVOID)0x43FF1A, stateBytes(san9::injection2026::FightSubIncomeCoef, 1u), 1u);
  writePatch((LPVOID)0x43FF1D, stateBytes(san9::injection2026::MaxCaptiveCoef, 1u), 1u);
  writePatch((LPVOID)0x43F3D1, stateBytes(san9::injection2026::BasicAbilityCoef, 1u), 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::SexCoefAddr), stateBytes(san9::injection2026::SexCoef, 1u), 1u);
  patchValue = (unsigned __int8)*(_DWORD *)(state + san9::injection2026::TroopViewRange)
               + (((unsigned __int8)*(_DWORD *)(state + san9::injection2026::BuildingViewRange)
                 + (((unsigned __int8)*(_DWORD *)(state + san9::injection2026::CityViewRange) + ((*(_DWORD *)(state + san9::injection2026::SoldierDisplayError) & 0x7F) << 8)) << 8)) << 8);
  writePatch((LPVOID)0x43F887, &patchValue, 4u);
  writePatch((LPVOID)0x43F88F, stateBytes(san9::injection2026::CounselorReportCoef, 1u), 1u);
  writePatch(*(LPVOID *)(state + 4828), &dword_42D948, 0x190u);
  writePatch(*(LPVOID *)(state + 4832), &byte_42DAD8, 0x32u);
  patchValue = *(_DWORD *)(state + san9::injection2026::EnableTrainEnabled) != 0 ? 0x32 : 0;
  writePatch(*(LPVOID *)(state + 6224), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + 6228), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::AllPersonLiveEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::AllPersonLive);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldAllPersonLive);
  writePatch(*(LPVOID *)(state + san9::injection2026::AllPersonLiveAddr), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::DynamicAddProficiencyEnabled) != 0 ? 47513 : 10219;
  writePatch(dword_42F2B0, &patchValue, 2u);
  patchValue = *(_DWORD *)(state + san9::injection2026::DisableAlienateEnabled) != 0 ? 22 : 10;
  writePatch((LPVOID)(reinterpret_cast<std::uintptr_t>(dword_41FFA4) + 24), &patchValue, 1u);
  selectedSoldierCommandKillerIndexBias = -(*(_DWORD *)(state + san9::injection2026::SoldierCommandKillerEnabled) != 0);
  selectedSoldierCommandKillerIndexBias = (selectedSoldierCommandKillerIndexBias & ~0xFF) | ((selectedSoldierCommandKillerIndexBias & 0xFE) & 0xFF);
  patchValue = selectedSoldierCommandKillerIndexBias + 204;
  writePatch((char *)dword_42F49C + 88, &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::AllPersonDeadEnabled) != 0 )
    patchValue = deathGradient_.threshold > 120u ? 120u : deathGradient_.threshold;
  else
    patchValue = *(_DWORD *)(state + 6612);
  writePatch(*(LPVOID *)(state + 6596), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + 6600), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + 6604), &patchValue, 1u);
  applyDeathGradient();
  applyGrowthBoost();
  applyCultivate();
  applyProficiencyCap();
  applyOfficerPersonalization();
  patchValue = *(_DWORD *)(state + san9::injection2026::FriendToAlienateEnabled) == 0;
  writeDescriptor(0x424D20u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  if ( *(_DWORD *)(state + san9::injection2026::FriendToAlienateEnabled) != 0 )
    patchValue = *(_DWORD *)(state + 7056);
  else
    patchValue = *(_DWORD *)(state + 7060);
  writePatch(*(LPVOID *)(state + 7052), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::FriendToAlienateEnabled) != 0;
  writeDescriptor(0x4256A0u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::FriendToAlienateEnabled) == 0;
  writeDescriptor(0x428B10u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::UpdateWaterKillerPropEnabled) == 0;
  writeDescriptor(0x427A70u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::AbilityKillerPropEnabled) == 0;
  writeDescriptor(0x429038u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  if ( *(_DWORD *)(state + san9::injection2026::SubWoundSoldierEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::TroopWound);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldTroopWound);
  writePatch(*(LPVOID *)(state + san9::injection2026::TroopWoundAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::SubWoundSoldierEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::BuildingWound);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldBuildingWound);
  writePatch(*(LPVOID *)(state + san9::injection2026::BuildingWoundAddr), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::ChangeTroopCommandEnabled) == 0;
  writeDescriptor(0x4279F8u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::NpcAllieAttackEnabled) != 0 ? 5 : 0;
  writePatch((char *)dword_42F454 + 26, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::NpcAllieAttackEnabled) != 0;
  writePatch((char *)dword_42F450 + 9, &patchValue, 1u);
  selectedNpcAllieAttackIndexBias = -(*(_DWORD *)(state + san9::injection2026::NpcAllieAttackEnabled) != 0);
  selectedNpcAllieAttackIndexBias = (selectedNpcAllieAttackIndexBias & ~0xFF) | ((selectedNpcAllieAttackIndexBias & 0x8A) & 0xFF);
  patchValue = selectedNpcAllieAttackIndexBias + 235;
  writePatch((char *)dword_42F3B8 + 28, &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::DisableNpcPlotForceEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::DisableNpcPlotForce);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldDisableNpcPlotForce);
  writePatch(*(LPVOID *)(state + san9::injection2026::DisableNpcPlotForceAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::BurnFoodEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::BurnFoodProbability);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldBurnFoodProbability);
  writePatch(*(LPVOID *)(state + san9::injection2026::BurnFoodProbabilityAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::BurnFoodEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::BurnFoodEffect);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldBurnFoodEffect);
  writePatch(*(LPVOID *)(state + san9::injection2026::BurnFoodEffectAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::BurnFoodEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::BurnBuildingEffect);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldBurnBuildingEffect);
  writePatch(*(LPVOID *)(state + san9::injection2026::BurnBuildingEffectAddr), &patchValue, 1u);
  selectedDisableBurnIndexBias = -(*(_DWORD *)(state + san9::injection2026::DisableBurnEnabled) != 0);
  selectedDisableBurnIndexBias = (selectedDisableBurnIndexBias & ~0xFF) | ((selectedDisableBurnIndexBias & 0xAE) & 0xFF);
  patchValue = selectedDisableBurnIndexBias + 133;
  writePatch(*(LPVOID *)(state + 6924), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::MunicipalDraftEnabled) != 0 ? 0 : 3;
  writePatch((char *)dword_42F364 + 41, &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::DoubleBuildingDefenseEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::BuildingDefenseCoef);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldBuildingDefenseCoef);
  writePatch(*(LPVOID *)(state + san9::injection2026::BuildingDefenseCoefAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::DisableSearchEventEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::EventMeetHuaTuo);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldEventMeetHuaTuo);
  writePatch(*(LPVOID *)(state + san9::injection2026::EventMeetHuaTuoAddr), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::SubRobFoodEnabled) != 0;
  writePatch((LPVOID)0x43FF1C, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::DynamicAddAbilityEnabled) != 0 ? 6 : 0;
  writePatch((char *)dword_42F27C + 1, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::DispatchSoldierToPlayerEnabled) != 0 ? 116 : 235;
  writePatch((char *)dword_42F33C + 45, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::SubCounsellorJudgeEnabled) != 0 ? 3 : 1;
  writePatch((LPVOID)(dword_41FD34 + 3), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::SubCounsellorJudgeEnabled) != 0 ? 9 : 5;
  writePatch((char *)dword_41FDC0 + 29, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::AddNpcMoneyFoodEnabled) != 0;
  writePatch((LPVOID)0x43FF16, &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::SearchMaxAbilityEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::MaxBasicAbility);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldMaxBasicAbility);
  writePatch(*(LPVOID *)(state + san9::injection2026::MaxSearchAbilityAddr), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::MaxBasicAbilityAddr), &patchValue, 1u);
  writePatch((LPVOID)(*(_DWORD *)(state + san9::injection2026::MaxBasicAbilityAddr) + 48), &patchValue, 1u);
  writePatch((LPVOID)(*(_DWORD *)(state + san9::injection2026::MaxBasicAbilityAddr) + 96), &patchValue, 1u);
  writePatch((LPVOID)(*(_DWORD *)(state + san9::injection2026::MaxBasicAbilityAddr) + 144), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::NotAddDraftEnabled) != 0 ? 3051 : 1795;
  writePatch((char *)dword_42F140 + 14, &patchValue, 2u);
  patchValue = *(_DWORD *)(state + san9::injection2026::AlikeKillerLinkageEnabled) != 0 ? 0xE : 0;
  writePatch((char *)dword_42F36C + 1, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::EnableBaramArrayEnabled) != 0 ? 86 : 195;
  writePatch(dword_42F1A8, &patchValue, 1u);
  selectedEnableRelayDispatchIndexBias = -(*(_DWORD *)(state + san9::injection2026::EnableRelayDispatchEnabled) != 0);
  selectedEnableRelayDispatchIndexBias = (selectedEnableRelayDispatchIndexBias & ~0xFF) | ((selectedEnableRelayDispatchIndexBias & 0x8A) & 0xFF);
  patchValue = selectedEnableRelayDispatchIndexBias + 235;
  writePatch((char *)dword_42F52C + 20, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::DisableNpcPlotForceEnabled) != 0;
  writePatch((LPVOID)0x43F4CD, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::WeakenLongRangeKillerEnabled) != 0 ? 86 : 195;
  writePatch(dword_42F1A0, &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::MunicipalForceDraftEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::MunicipalForceDraft);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldMunicipalForceDraft);
  writePatch(*(LPVOID *)(state + 6876), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::MunicipalForceDraftEnabled) != 0 )
    patchValue = *(_DWORD *)(state + 7440);
  else
    patchValue = *(_DWORD *)(state + 7444);
  writePatch(*(LPVOID *)(state + 7436), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::MinDurabilityZeroEnabled) != 0;
  writeDescriptor(0x41F9E0u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::FightKillerPropEnabled) == 0;
  writeDescriptor(0x422998u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::NotSureCaptiveEnabled) == 0;
  writeDescriptor(0x422748u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::GooseKillerLinkageEnabled) == 0;
  writeDescriptor(0x4285E8u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  if ( *(_DWORD *)(state + san9::injection2026::GooseKillerLinkageEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::BlockArrayNum);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldBlockArrayNum);
  writePatch(*(LPVOID *)(state + san9::injection2026::BlockArrayNumAddr), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::NullCitySubMunicipalEnabled) != 0;
  writePatch((LPVOID)0x43F4CE, &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::NpcSearchTreasureEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::NpcSearchTreasure);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldNpcSearchTreasure);
  writePatch(*(LPVOID *)(state + san9::injection2026::NpcSearchTreasureAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::NpcSearchTreasureEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::NpcDisplayTreasure);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldNpcDisplayTreasure);
  writePatch(*(LPVOID *)(state + san9::injection2026::NpcDisplayTreasureAddr), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::SoldierSubKillerEnabled) != 0 ? 61943 : 747;
  writePatch((char *)dword_42F49C + 33, &patchValue, 2u);
  patchValue = *(_DWORD *)(state + san9::injection2026::NpcAutoDraftEnabled) == 0;
  writePatch((LPVOID)dword_428AD8[3 * patchValue], sourceVA((&off_428AE0)[3 * patchValue], dword_428ADC[3 * patchValue]), dword_428ADC[3 * patchValue]);
  patchValue = *(_DWORD *)(state + san9::injection2026::PlayerAutoProcessEnabled) == 0;
  writeDescriptor(0x428B70u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::LowDurabilityChaosEnabled) != 0;
  writePatch((LPVOID)0x43FF1B, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::LeaderInspireEnabled) == 0;
  writeDescriptor(0x428E78u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  if ( *(_DWORD *)(state + san9::injection2026::DisableKillPersonEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::KillPersonProp);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldKillPersonProp);
  writePatch(*(LPVOID *)(state + san9::injection2026::KillPersonPropAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::KillAddProficiencyEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::BattleExperienceCode);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::BattleExperienceOldCode);
  writePatch(*(LPVOID *)(state + san9::injection2026::BattleExperienceAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::KillAddProficiencyEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::WaterExperienceCode);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::WaterExperienceOldCode);
  writePatch(*(LPVOID *)(state + san9::injection2026::WaterExperienceAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::DisableMovePersonEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::EnableGoMenu);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldEnableGoMenu);
  writePatch(*(LPVOID *)(state + san9::injection2026::EnableGoMenuAddr), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::EnableComeMenuAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::GeneralDeadEnabled) != 0 )
    patchValue = *(_DWORD *)(state + 6628);
  else
    patchValue = *(_DWORD *)(state + 6632);
  writePatch(*(LPVOID *)(state + 6616), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + 6620), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + 6624), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::BarbarianNotDisappearEnabled) != 0 )
    patchValue = *(_DWORD *)(state + 7412);
  else
    patchValue = *(_DWORD *)(state + 7416);
  writePatch(*(LPVOID *)(state + 7400), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + 7404), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + 7408), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::AddKillerJianyuEnabled) == 0;
  writeDescriptor(0x429178u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  patchValue = *(_DWORD *)(state + san9::injection2026::AddKillerJianyuEnabled) != 0 ? 1 : 5;
  writePatch((char *)dword_42F0FC + 9, &patchValue, 1u);
  patchValue = 6 - (*(_DWORD *)(state + san9::injection2026::DisableKillerJianlanEnabled) != 0);
  writePatch((char *)dword_42F0FC + 4, &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::DisableKillerJianlanEnabled) == 0;
  writeDescriptor(0x429208u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  if ( *(_DWORD *)(state + san9::injection2026::NotDisplayExchangeEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::NotDisplayExchange);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldNotDisplayExchange);
  writePatch(*(LPVOID *)(state + san9::injection2026::NotDisplayExchangeAddr1), &patchValue, 1u);
  writePatch(*(LPVOID *)(state + san9::injection2026::NotDisplayExchangeAddr2), &patchValue, 1u);
  selectedEnableCityPopulationCoefIndexBias = -(*(_DWORD *)(state + san9::injection2026::EnableCityPopulationCoefEnabled) != 0);
  selectedEnableCityPopulationCoefIndexBias = (selectedEnableCityPopulationCoefIndexBias & ~0xFF) | ((selectedEnableCityPopulationCoefIndexBias & 0x4C) & 0xFF);
  patchValue = selectedEnableCityPopulationCoefIndexBias + 195;
  writePatch((char *)dword_42F150 + 6, &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::Parameter1164) != 0 )
    patchValue = *(_DWORD *)(state + 7572);
  else
    patchValue = *(_DWORD *)(state + 7576);
  writePatch(*(LPVOID *)(state + 7568), &patchValue, 2u);
  patchValue = *(_DWORD *)(state + san9::injection2026::Parameter1165) != 0 ? 30089 : 59011;
  writePatch((char *)dword_42B5B0 + 8, &patchValue, 2u);
  patchValue = *(_DWORD *)(state + san9::injection2026::WarFogEnabled) != 0 ? 53811 : 5867;
  writePatch(dword_42F1FC, &patchValue, 2u);
  if ( *(_DWORD *)(state + san9::injection2026::WarFogEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::DisableBurnExplorer);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldDisableBurnExplorer);
  writePatch(*(LPVOID *)(state + san9::injection2026::DisableBurnExplorerAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::WarFogEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::DisableAttackExplorer);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldDisableAttackExplorer);
  writePatch(*(LPVOID *)(state + san9::injection2026::DisableAttackExplorerAddr), &patchValue, 1u);
  if ( *(_DWORD *)(state + san9::injection2026::WarFogEnabled) != 0 )
    patchValue = *(_DWORD *)(state + san9::injection2026::MaskPersonWhere);
  else
    patchValue = *(_DWORD *)(state + san9::injection2026::OldMaskPersonWhere);
  writePatch(*(LPVOID *)(state + san9::injection2026::MaskPersonWhereAddr), &patchValue, 1u);
  patchValue = *(_DWORD *)(state + san9::injection2026::WarFogEnabled) != 0;
  writeDescriptor(0x42A780u + 12 * (patchValue), PatchSource2026::CompanionAddress);
  applyComputerWorkTiming();
  logParameterResults(state);
}

void Runtime2026::logParameterResults(std::uintptr_t state)
{
  // These are the values the game actually receives, not the values the user
  // typed. Several options are combined with the original value, so only the
  // combined number can be compared with the game behaviour.
  const auto oldSlashDead = *(_DWORD *)(state + san9::injection2026::OldSlashDeadCoef);
  const auto slashDead = *(_DWORD *)(state + san9::injection2026::SlashDeadCoef);
  const auto blockProp = *(_DWORD *)(state + san9::injection2026::BlockPropCoef);
  const auto antiLeague = *(_DWORD *)(state + san9::injection2026::AntiPlayerLeagueCoef);
  const auto npcDisorder = *(_DWORD *)(state + san9::injection2026::NpcAttackDisorder);
  const auto killerPower = *(_DWORD *)(state + san9::injection2026::KillerPowerCoef);
  appendRuntimeLogFormat("PARAM RESULT singleSlashDeathCoef input=", slashDead,
      " original=", oldSlashDead, " written=", oldSlashDead + 4 * slashDead);
  appendRuntimeLogFormat("PARAM RESULT blockPropCoef input=", blockProp, " written=", 40 - 2 * blockProp);
  appendRuntimeLogFormat("PARAM RESULT antiLeagueCoef input=", antiLeague,
      " probabilityCheck=", 4 - (antiLeague >> 2));
  appendRuntimeLogFormat("PARAM RESULT npcAttackDisorder input=", npcDisorder,
      " written=", npcDisorder != 0 ? 10 - npcDisorder : 0);
  appendRuntimeLogFormat("PARAM RESULT killerPowerCoef input=", killerPower);
  appendRuntimeLogFormat("PARAM RESULT deathGradientThreshold written=",
      *(_DWORD *)(state + 6596));
}

void Runtime2026::writeDescriptor(DWORD address, PatchSource2026 source)
{
  PatchDescriptor2026 descriptor{};
  dataOut(address, &descriptor, sizeof(descriptor));
  if (!descriptor.length) throw std::out_of_range("empty patch descriptor");
  const BYTE *bytes = source == PatchSource2026::CompanionAddress
      ? sourceVA(descriptor.source,descriptor.length) : nativeDataBytes(descriptor.source,descriptor.length);
  writePatch(descriptor.target, bytes, descriptor.length);
}

void Runtime2026::writeDescriptorRange(DWORD first, size_t count, PatchSource2026 source)
{
  dataBytes(first,imageRecordBytes2026(count,sizeof(PatchDescriptor2026)));
  for (size_t index = 0; index < count; ++index)
    writeDescriptor(first + DWORD(index * sizeof(PatchDescriptor2026)), source);
}

void Runtime2026::writeInlineRange(DWORD first, size_t count)
{
  dataBytes(first,imageRecordBytes2026(count,sizeof(InlinePatch2026)));
  for (size_t index = 0; index < count; ++index)
  {
    InlinePatch2026 descriptor{};
    dataOut(first + DWORD(index * sizeof(InlinePatch2026)), &descriptor, sizeof(descriptor));
    if (!descriptor.length || descriptor.length > descriptor.bytes.size()) throw std::out_of_range("inline patch length");
    writePatch(descriptor.target, descriptor.bytes.data(), descriptor.length);
  }
}
