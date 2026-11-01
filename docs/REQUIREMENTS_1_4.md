# 需求 1-4 实现文档（San9 Companion 换模型交接）

> 本文件完整记录需求 1~4 的设计、内存修改位置、注入代码与已知坑，供新模型无缝接手。
> 与 `docs/DESIGN.md`、`CHANGELOG.md` 配合阅读。本文是「代码级」的精确记录。

---

## 0. 当前状态（先读这里）

> 最后核实时间：2026-10-27。下列结论均为**实测**（实际构建 + 实际跑测试）得出，不是历史记录。

### 0.1 构建与测试状态：全绿

- **构建通过**：`powershell -NoProfile -ExecutionPolicy Bypass -File Build.ps1 -Configuration Debug`
  产出 `build/app/Debug/San9Companion.exe`、`build/tests/Debug/San9RuntimeTests.exe`、`build/runtime/Debug/San9Runtime.lib`。
- **测试全绿**：`build/tests/Debug/San9RuntimeTests.exe --all assets/config` → `exit=0`，末行 `2026 offline runtime checks passed; no game process or GUI was accessed.`
  覆盖：3536 case 穷举 fault sweep、5 个生命周期场景、20 组格式/往返/边界检查。
- 之前的 `LNK1104`（exe 被占用）**已不再复现**，属历史状态。

### 0.2 上一版 §0 记载的两处改动：已编译进 exe 并通过测试

1. `src/runtime/Runtime2026.cpp` 的 `applyProficiencyCap()`：
   - 熟练度上限函数地址从写死 `0xA2190` 改为 `AllocationBase + 0x2190`。
   - `!any`（无自定义上限）时不再直接 return，而是写回原游戏逻辑 `&global<DWORD>(0x4256B8)`（93 字节，`0x5D`）。
2. `src/runtime/RuntimeInjection2026.cpp` 的 `uploadInjectionPayloads()`：
   - **已删除** `writePatch(dword_42F2B0, &codePersonExperience, 0x5Du);`，原因见 §0.3。

### 0.3 「游戏读档就退出」的根因：已修复并验证

- **根因**：`applyProficiencyCap`（在 `installCoreHooks` 内）先写「上限版」代码到 `AllocationBase+0x2190`；随后 `uploadInjectionPayloads` 又把「原版」`codePersonExperience` 写到同一地址，**覆盖**了上限版。原版里 `mov eax, ds:0A4000h[eax*4]` 依赖的熟练度表指针重定位后是否有效未确认，导致读档时崩溃。
- **修复**：`applyProficiencyCap` 全权负责该地址的写入（`any` 写上限版，`!any` 写原版），并删掉 `uploadInjectionPayloads` 里的重复写入。`applyParameterPatches()` 里针对 `layout2026::MaxExperience` 的 `writePatch` 同样已移除（`maxExperience` 字段已从 `PersonConfig` 删除）。
- **现状**：代码已编译进 exe，离线测试通过。**实机读档仍需人工验证**——离线测试不执行注入代码本身。

### 0.4 工作区状态：全部改动未提交

用户明确要求**不提交本地文件**，因此当前所有成果都只在工作区：

- **已修改（25 个文件）**：`CHANGELOG.md`、`assets/config/San9PKHardPerson.txt`、`assets/ui/dialogs.rc`、`assets/ui/resource_ids.h`、`docs/DESIGN.md`、`src/app/platform/NativeWidgets.{h,cpp}`、`src/app/ui/MainWindow.cpp`、`src/app/ui/Pages.cpp`、`src/generated/InjectionNames2026.h`、`src/generated/parameters.json`、`src/model/InjectionNames2026.json`、`src/model/PersonConfig.{h,cpp}`、`src/runtime/AppPaths.h`、`src/runtime/GameText.{h,cpp}`、`src/runtime/OfficerPersonalization2026.{h,cpp}`、`src/runtime/Runtime2026.{h,cpp}`、`src/runtime/RuntimeInjection2026.cpp`、`tests/runtime_test.cpp`、`tools/Build-App.ps1`、`tools/Write-BuildInfo.ps1`
- **新增未跟踪（20 个）**：`docs/REQUIREMENTS_1_4.md`（本文档）、`docs/HANDOVER.md`、`docs/SESSION_NOTE.md`、`assets/config/San9PKHardCheat.txt`、`restore_deepseek.ps1`、`setup_relay.ps1`、`tools/_addr_check.py`、`tools/_dumporder.py`、`tools/_dumpspec.py`、`tools/_prof_check.py`、`tools/_review_check.py`、`tools/_review_check2.py`、`tools/_review_check3.py`、`tools/_verify_cap.py`、`tools/_verify_growth.py`、`tools/_verify_hooks.py`、`tools/_verify_payloads.py`、`tools/show_env.ps1`、`武将个性化.csv`、`武将个性化_new.csv`

> 完整清单与状态见 `docs/HANDOVER.md` §6。

### 0.5 第三轮审查进度

| 审查项 | 状态 | 结论要点 |
| --- | --- | --- |
| 日志充分性 | ✅ 已完成（**2026-10-25 已扩充**） | 埋点已从 9 处增加到覆盖全部注入路径；逐次写入追踪新增**三条开启途径**（设置页复选框 / `--log-writes` / `SAN9_LOG_WRITES=1`），见 §8 |
| 逻辑一致性 | ✅ 已完成 | 见 §11.17.1 |
| 计算准确性 | ✅ 已完成 | 见 §11.17.2。**查出 1 个 P0**：`Runtime2026.cpp` 四个注入代码块用 `(esi-0x1258EE0)/0x128` 算武将索引（esi 从不是武将表指针 → 实机必崩）。**第二轮逆向已定性，代码已于 2026-10-27 全部重写**（§7），实机验证待做 |

> 审查的原始 agent transcript 在 `C:\Users\xuwen\.claude\projects\G--aiprojects\d041e154-5ee7-4aa8-8834-f4653be2921a\subagents\` 下，可追溯。

### 0.6 遗留功能缺口与 P0 缺陷

- **P0（已修，实机待验）**：注入代码的武将索引取法错误（`(esi-0x1258EE0)/0x128`，esi 从不是武将表指针 → 实机必崩）。
  **2026-10-27 四个注入块全部重写**（统一 `word[ptr+4]`，§7 + `docs/HANDOVER.md` §3.7.6），并加离线字节断言。修复清单见 §11.17.2.3 与 `docs/HANDOVER.md` §3.7。
- **需求 4 唯一遗留功能缺口**：兵法威力系数（`0x43EF44`）—— 该 hook 点拿不到武将指针，需另找挂钩点。其余需求 1~4 子项均已实现。

---

## 1. 架构概述

- Win32 x86 修改器，目标游戏 `San9PK.exe`（基址 `0x400000`，无 ASLR，但实际是 ASPack 类压缩壳，运行后解压）。
- 通过 `WriteProcessMemory`/注入代码对游戏做内存修改；所有改动进 journal，可 undo。
- **注入区是动态分配的**（`VirtualAllocEx`），不是旧工具 `SanIXPKCheat` 的固定地址 `0xA0000`。
- 旧工具所有注入地址 = `0xA0000 + offset`；本实现 = `AllocationBase + offset`，靠 `rebase()` 做重定位。

### 1.1 关键文件
| 文件 | 作用 |
| --- | --- |
| `src/runtime/Runtime2026.cpp` | 需求 1/2/3/4 的 `applyXxx` 实现 + `installPerOfficerHook` + 日志 |
| `src/runtime/Runtime2026.h` | `Runtime2026` 类、`writePatch`/`writeMemory` 模板、`global<T>` |
| `src/runtime/RuntimeInjection2026.cpp` | `modifyBranches`/`connectGame`/`installCoreHooks`/`uploadInjectionPayloads` |
| `src/runtime/OfficerPersonalization2026.h/.cpp` | 需求 4 数据模型 + `San9PKHardCheat.txt` 读写 |
| `src/runtime/ProcessSession2026.cpp` | journal、`allocate`/`protect`/`read`/`write`/`restore` |
| `src/runtime/GameSnapshots2026.cpp` | 游戏快照读取 |
| `src/app/ui/Pages.cpp` | 设置页 + 武将配置页（`PersonPage`） |
| `assets/ui/dialogs.rc` | 对话框资源 |
| `src/generated/*` | 从旧工具恢复出的 ABI/补丁/符号（勿手改生成逻辑） |

### 1.2 关键符号（`src/generated/Symbols.inc`）
```c
#define lpAddress            (global<LPVOID>(0x420050))  // 原固定基址(0xA0000)，rebase 后被设为 allocation
#define dword_42F27C         (global<LPVOID>(0x42F27C))  // codeDynamicAddProficiency 地址 (AllocationBase+0x5570)
#define codePersonExperience (global<DWORD>(0x4256B8))   // 原版熟练度上限代码 93 字节
```

---

## 2. 游戏内存结构（已逆向确认）

| 结构 | 地址 | 记录大小 | 关键字段 |
| --- | --- | --- | --- |
| 武将表 | `0x1258EE0` | `0x128` | 四维 `0x4C`(武)/`0x54`(智)/`0x5C`(政)/`0x64`(统)；熟练度 `0xA4+2k`(k=0..8)；状态 `0x84`；兵法标志 `0xE8`；姓名 `0x10..0x13`+`0x15..0x18`(Big5，`0x14` 为分隔符) |
| 兵法表 | `0x14FDAD0` | `0x8C` | **41 条**（`0x453720`: `cmp eax,0x29`）。威力 `+0x64`、间隔 `+0x7C`、熟练度 `+0x58` |
| 势力表 | `0x1253C38` | `0xD4` | — |
| 都市表 | `0x124DB58` | — | — |
| 阵型表 | `0x1500B70` | — | — |
| kakuritsu | `0x5FF2F0` | — | `int kakuritsu(chance, kind, 1)`；调用方式是 `call [0x5FF2F0]`（间接 call） |
| min 函数指针 | `[0x5FF30C]` | — | `jmp [0x5FF30C]` 尾跳到 min |
| 死亡判定 | 3 处 `cmp eax,0x50` | — | 见需求 1 |

### 2.1 熟练度上限函数（关键）
- 旧工具：`dwAddrPersonExperience = dwAddrStackBottom + 0x2190 = 0xA2190`。
- 本实现：`AllocationBase + 0x2190`。
- 游戏内调用点：`0x44C9A3` → `call 0x000A2190`（相对调用，该地址经 `rebase()` 重定位到新分配区）。
- 原版 93 字节含 `mov eax, ds:0A4000h[eax*4]`（熟练度表访问，`0xA4000` 也在重定位表里）。

---

## 3. 需求实现明细

### 需求 1：高能武将阵亡概率渐变 `DeathGradient2026`
- 配置：`lowRate`(默认 40)、`threshold`(默认 100，范围 80~120)。
- 开关：复用「高能武将可阵亡」复选框（`AllPersonDeadEnabled`）。
- 效果：`chance = min(lowRate, threshold - MAX(四维))`，`MAX >= threshold` 免死。
- 实现：`Runtime2026::applyDeathGradient()`（`Runtime2026.cpp`）。
- 修改点（字节级）：
  - `0x502667`：`mov esi,[esi+0x50]`(`8B 76 50`) → `mov esi,eax; nop`(`8B F0 90`)，改用 MAX。
  - `0x50266D`：原 `0x50`(80) → `threshold`（单字节）。
  - `0x502677`：原 `0x1E`(30) → `lowRate`（单字节）。
  - 关闭时写回 `8B 76 50` / `0x50` / `0x1E`。

### 需求 2：小兵培养增强 `Cultivate2026`
- 能力系数反转：`codeTrainBasicAbility` 两处 `sub`(`28 02`)→`add`(`00 02`)。
- 培养时长：`durationRounds`(默认 9，范围 3~12) 写 `0x14FA656`（单字节）。
  - ⚠️ **未验证地址**：PE 内零引用、无指针指向、位于 `.data` BSS 区，原工具
    `recovery/evidence/global_assignments.json` 中也无此地址 → 属**凭猜测定的地址**。
    **若实机再崩，第一件事就是删掉这行。**
- 强制小兵培养/提拔：写 `0x40CAEF` 与 `0x40BF25`（`cmp eax,0x32` 的立即数字节）= `0x32` / `0x00`
  （`RuntimeInjection2026.cpp`，control id 1013「开启小兵培养」），与原工具实现一致。
  - ⚠️ **原「强制提拔：写 `0x6613EE` = 1」已删除（2026-10-31，崩溃 #4 根因）**：
    `0x6613EE` 不是独立标志位，而是 id 表 A 首条记录 id dword `0x6613EC`（值 `0x000003E8`）
    的**第 2 个字节**；写入后 id 1000 变成 `0x000103E8`，游戏按 id 查找失败，
    `0x5B72F0`（getflag，无空指针检查）解引用空 entry → 异常地址 `0x5B6754`。
- 实现：`Runtime2026::applyCultivate()`（培养时长）+ `RuntimeInjection2026.cpp`（开启小兵培养）。

### 需求 3：指定武将能力成长倍率 `GrowthBoost2026`
- 配置：武将 ID 逗号列表 + `multiplier`(1~10)。
- 解析为 800 字节位图；注入代码按武将 ID 乘倍率。
- 实现：`Runtime2026::applyGrowthBoost()`。
- 挂钩点：`codeDynamicAddProficiency` 的尾部（`dword_42F27C + 0x40`）。现场 `esi` = `CPersonData`。
- 原尾部 6 字节：`mov [esp+4],eax; jmp short -0x44`(`89 44 24 04 EB BC`)。
- 注入桩（40 字节，2026-10-27 重写）：`mov ecx,eax` 保存增长概率 → `movzx eax,word[esi+4]`
  （武将索引）→ `cmp eax,0x320; jae skip` → 查 800 字节位图 → 命中 `imul ecx,ecx,mult` →
  `mov [esp+4],ecx; jmp [0x5FF2F0]`。位图（800B）与代码分区连续写入。
- 注意：此处结尾用 `jmp [0x5FF2F0]`（尾跳）是**正确**的——返回地址由 `0x4505F9` 的 `call`(thunk) 压栈。

### 需求 4：武将个性化 `OfficerPersonalization2026`
- 每武将 53 值 = 9 熟练度上限 + 41 发动间隔 + 把握概率(0/1) + 威力系数 + 威力加倍(0/1)。
- 持久化 `San9PKHardCheat.txt`：800 武将 × 53 整数，每行一个，共 42400 行。
- 数据模型默认值：`DefaultCap=1000`；`defaultInterval[41] = {9×12, 0×11, 6,0,9,15, 6,6,15,15, 9×8, 0,0}`。

#### 4.1 熟练度上限（per-system）
- 目标地址：`AllocationBase + 0x2190`（见第 2.1 节，**不要写死 0xA2190**）。
- 查表：`proficiencyCap[武将][系]`（uint16，扁平化 `capFlat[(o*9+s)*2]`）。
- 实现：`applyProficiencyCap()`。
- 注入代码（93 字节）见第 7.1 节。
- 无任何自定义上限时写回原版 93 字节 `&global<DWORD>(0x4256B8)`。

#### 4.2 发动间隔（per-skill）
- hook `0x4FF08B`（原 5 字节 `F7 F9 8B CE 50` = `idiv ecx; mov ecx,esi; push eax`），
  **只覆盖 5 字节**，返回 `0x4FF090`，让原 `call 0x4FB760` 继续执行（B5）。
- 现场：`esi` = `CPersonData`（`word[esi+4]` = 武将索引），`ebx` = 单位对象（`[ebx+0x34]` = 技能ID）。
- 查 `intervalFlat[officer*41+skill]`（`0xFF` = 未自定义 → 走原版 `idiv ecx`；否则直接写天数）。
- 实现：`applyOfficerPersonalization()` 内的 `intervalCode`（77 字节，见 7.4 节）。
- 仅当存在偏离原值的间隔时才注入，否则写回原 5 字节。

#### 4.3 把握概率 / 威力加倍（per-officer 勾选）
- 把握概率 hook `0x4FEB90`（原 5 字节 `BE 0A 00 00 00` = `mov esi,0xA`）。
  现场 `esi` 仍是单位对象（`0x4FEB22: mov esi,ecx`）；`0x4FEB95` 处 `esi` 已是常数 10。
  桩保存 `esi`→`ebx`，重放 `mov esi,0xA; call [0x5FF2F0]; add esp,0xC`，
  沿 `[ebx+0x30]`（`CPersonData*`）→ `word[+4]` 查 `activationBoost`；命中则 `mov eax,1`，
  返回 `0x4FEB9E`。
- 威力加倍 hook `0x50603B`（原 5 字节 `83 C4 0C 85 C0` = `add esp,0xC; test eax,eax`）。
  桩重放 `add esp,0xC`，沿 `[esi+0x80]` → `[+0x88]` 链表逐节点（`node+8` = `CPersonData`）
  → `word[+4]` 查 `criticalHit`；命中则 `mov eax,1`，返回 `0x506040`。
- 实现：`installPerOfficerHook()` + `boostCode`/`criticalCode`（见 7.2/7.3 节）。
- **关键修复（2026-10-27）**：武将索引统一 `word[ptr+4]`；`installPerOfficerHook` 新增
  `returnAddress` 参数、回写**恰好 originalSize 字节**（B4，不再吞 `add esp,0xC`）。

#### 4.4 威力系数（per-officer 百分数）
- 配置字段 `powerPercent`（0=不改，100=1.0x）。
- **待办**：`0x43EF44` 处拿不到武将指针，需另找挂钩点。运行时尚未实现。

---

## 4. 动态分配 + rebase 机制（务必理解）

- `connectGame()`（`RuntimeInjection2026.cpp`）：
  1. 保护 4 个数据页（`0x43F000`/`0x61C000`/`0x610000`/`0x4B0000`，与旧工具一致）。
  2. `allocateMemory(...)` 分配 `pageSize(4096) * AllocPageNum(11) = 0xB000` 字节，`PAGE_EXECUTE_READWRITE`。
  3. `field<DWORD>(AllocationBase) = allocation`。
  4. `if (rebase()) lpAddress = allocation;`
- `rebase()`（`Runtime2026.cpp`）：
  ```cpp
  const DWORD delta = field<DWORD>(AllocationBase) - global<DWORD>(0x420050);
  for (relocation in relocations.inc) { *slot += delta; }
  ```
  - `global<DWORD>(0x420050)` 是原固定基址 `0xA0000`。
  - `relocations.inc` 里是**需要重定位的 DWORD 槽位**（`dword_42Fxxx` 指针、原代码里的 `0xA4000` 熟练度表引用等）。
- 结论：**凡是旧工具写死 `0xA0000+offset` 的地方，本实现都要用 `AllocationBase+offset`，或依赖 rebase 重定位。** 不要新写死 `0xAxxxx` 地址。

---

## 5. 已修复的 bug 清单（重要，避免重犯）

1. **把握/加倍 hook 返回地址丢失**：`pop eax; jmp [0x5FF2F0]` → `pop eax; call [0x5FF2F0]; jmp 0x4FEB9B/0x50603B`。尾跳不压返回地址会崩。
2. **熟练度上限 payload disp32 偏移错一位**：`movzx eax,word[capArr+eax*2]` 的 disp32 在 `0x3D`(61)，不是 `0x3C`(60，那是 SIB 字节 `0x45`)。
3. **熟练度上限地址写死 `0xA2190`**：应写 `AllocationBase + 0x2190`。
4. **`uploadInjectionPayloads` 覆盖上限逻辑**：删掉它对 `dword_42F2B0` 的 `codePersonExperience` 写入，交给 `applyProficiencyCap`。
5. **测试桩不支持个性化新分配**：`tests/runtime_test.cpp` 的 `allocate`/`release`/`write` 桩改为通用分配表，游戏区下界放宽到 `0xA0000`。
6. **日志乱码**：`wofstream` 写中文在默认 locale 下失败；改用 `ofstream` + ASCII 消息。
7. **注入代码武将表基址立即数错 16 MB**：`Runtime2026.cpp` 四处 `sub eax` 写成 `0x12588EE0`（应为 `0x1258EE0`），致四类个性化实机全不生效。见 §11.17.2.1。**已修**。
8. **`TTM_UPDATETIPTEXTW` 返回值误判**：`Dialog::tooltip()` 更新分支曾写成
   `if (!SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, ...)) throw ...`。
   **该 Win32 消息没有返回值契约**（官方文档未定义），把返回值当成功标志会稳定误报，
   表现为 UI smoke 抛 `cannot update tooltip`、退出码 1。
   正确做法：**只对有明确 BOOL 契约的消息判成败**（如 `TTM_ADDTOOLW`），
   `TTM_UPDATETIPTEXTW` 不判。参考 `main` 分支原实现。**已修**。
   另注：`main` 分支原地改写 `entry->second = message` 会让控件持有的 `lpszText`
   指针指向变动/已释放的内存——dev 分支的「先分配 `replacement`、更新后 `std::move` 换入」
   是正确修法，**不要回退**。
9. **命令行整串比较导致 smoke 静默失效**：`wWinMain` 曾写成
   `std::wstring(commandLine) == L"--ui-smoke"`，拿 `lpCmdLine` 整串比。
   `lpCmdLine` 是**原始命令行字符串**，不是 argv 数组——`Start-Process -ArgumentList`
   产生的形式是 `"exe" --ui-smoke `（**带尾随空格**），整串比较必然失败 ->
   smoke 走正常路径弹模态窗口**永久挂起**。正确做法：按空白分词后**逐个 token 精确比较**。
   同类坑：子串查找 `find(L"--log-writes")` 会把 `--log-writes-off` 误判为开启。**已修**。
10. **构建脚本里的无界等待**：`tools/Build-App.ps1` 的 `-UiSmoke` 分支曾用
    `Start-Process -Wait`，smoke 挂死会把构建**永久卡住**且输出里看不出原因。
    改为 `WaitForExit(120000)` + 超时 kill + throw。**已修**。
11. **（2026-10-27）注入代码武将索引用法根本性错误 —— 实机必崩（B1/B8）**：
    四个注入块都用 `(esi - 0x1258EE0) / 0x128` 算武将索引，但 `esi` **从不是**武将表指针
    （是 `CPersonData` / 战斗单位 / 常数 10），→ 野指针读 → 崩。正确取法
    `movzx reg, word [CPersonData+4]`（= 武将表下标 = 本项目 UI 索引）。详见
    `docs/HANDOVER.md` §3.7 与 `docs/_INJECTION_FIX_NOTES.md`。**2026-10-27 全部重写**。
12. **（2026-10-27）间隔寻址被乘 2（B2）+ 把握/威力寻址多 +1（B3）+ hook 点错（B3）+ 覆盖长度错（B4/B5）**：
    `0F B6 0C 05`（索引×2）/`0F B6 0C 01`（多 +1）；把握 hook `0x4FEB95`（esi=常数10）应移
    `0x4FEB90`；威力/把握覆盖 6 字节吞了 call 之后的 `add esp,0xC`；间隔覆盖 8 字节吞了
    原 `call 0x4FB760`。**2026-10-27 全部重写**，见 §7。
13. **（2026-10-27）`SkillConfig::Count=39` 应为 41（B6）**：游戏兵法表访问器 `0x453720`
    `cmp eax,0x29` → 41 条。改 Count=41 + 布局 `state_size` 7692→7844 +
    `FormationFlags/Entries` 后移 7624/7625 + `SkillEntriesSize` 156→164 +
    `San9PKHardSkill.txt` 补两行 + `PatchImage::object_`→7844。**已修**。

---

## 6. 配置文件

- `assets/config/San9PKHardCheat.txt`：800×53 行（9 上限 + 41 间隔 + 把握 + 威力系数 + 威力加倍）。
- 生成脚本：`newwuj/export_cheat_params.py`（解析 `sanpkcheat/Cheat.s9` 旧存档）。
  - Cheat.s9 记录起点 `0xD0`，每条 `0x88`(88 字节)，共 850 条。
  - 熟练度上限 `0x34..0x45`（9×uint16 LE）；间隔 `0x08..0x30`（41×uint8）。
  - 把握概率 = `byte0x01 & 0x80`；威力加倍 = `byte0x02 & 0x02`；威力系数 = `0x50..0x57`(double)，1.0→0、1.1→110、1.2→120。

---

## 7. 注入代码（完整字节）

> ✅ **本节记录的字节就是当前 `src/runtime/Runtime2026.cpp` 里的实际字节**（2026-10-27 重写）。
>
> 2026-10-27 按第二轮逆向结论（`docs/HANDOVER.md` §3.7 + `docs/_INJECTION_FIX_NOTES.md`）
> 全部重写：武将索引统一 `movzx reg, word [ptr+4]`（`CPersonData` 的武将表下标），
> 不再用 `(esi - 0x1258EE0) / 0x128`（B1/B8）。hook 点与覆盖长度同步修正（B2~B5）。
> 离线测试 `tests/runtime_test.cpp` 的 `injectionCodeBytesTest` 现按本节字节做断言
> （断言四个块各自在 `word[ptr+4]` 处读索引、且不含旧 `sub 0x1258EE0` 形式）。

### 7.1 熟练度上限 payload（93 字节，写入 `AllocationBase+0x2190`）
```
99 B9 FA 00 00 00 F7 F1 83 E8 04 F7 D8 85 C0 7F 04
33 C0 B0 01 3C 04 7E 02 B0 04 0F AF 44 24 08 99 B1 03
F7 F1 89 44 24 08
0F B7 46 04 3D 20 03 00 00 7D 17 6B C0 09 03 C7
0F B7 04 45 [capArr]           ; movzx eax, word [capArr + eax*2]，disp32 在偏移 0x3D(61)
66 85 C0 74 05 66 89 44 24 0C  ; test ax,ax; jz +5; mov [esp+0Ch],ax
90 90 90 90 90 90 90 90 90 90 90 90
FF 25 0C F3 5F 00               ; jmp [0x5FF30C]
```
逻辑：前半段保留原计算（`cdq; mov ecx,250; div; sub eax,4; neg; ...; mov [esp+8],eax`），后半段 `officer=[esi+4]`，`cmp eax,800; jge skip`，`eax=eax*9+edi`，查 `capArr`，`test ax,ax; jz skip`，`mov [esp+0Ch],ax`。

### 7.2 把握概率 boostCode（56 字节）
hook `0x4FEB90`（原 5 字节 `BE 0A 00 00 00` = `mov esi,0xA`），返回 `0x4FEB9E`。
现场 `esi` 仍是单位对象；桩保存到 callee-saved `ebx`（跨 call 安全），重放
原指令后沿 `[ebx+0x30]`（`CPersonData*`）取武将索引查 `activationBoost`。
```
8B DE                          ; mov ebx,esi             （保存单位对象）
BE 0A 00 00 00                 ; mov esi,0xA             （重放原指令）
FF 15 F0 F2 5F 00              ; call [0x5FF2F0]         （原把握概率函数）
83 C4 0C                       ; add esp,0xC
8B 5B 30                       ; mov ebx,[ebx+0x30]      （→ CPersonData*）
85 DB                          ; test ebx,ebx
74 1C                          ; je done
0F B7 5B 04                    ; movzx ebx,word[ebx+4]   （★ 武将索引）
81 FB 20 03 00 00              ; cmp ebx,0x320
73 10                          ; jae done
B9 [arr]                       ; mov ecx,activationArray，imm32 在 0x24(36)
80 3C 19 00                    ; cmp byte[ecx+ebx],0
74 05                          ; je done
B8 01 00 00 00                 ; mov eax,1               （命中→概率 100%）
E9 [rel32]                     ; jmp 0x4FEB9E，rel32 在 codeSize-4
```

### 7.3 威力加倍 criticalCode（84 字节）
hook `0x50603B`（原 5 字节 `83 C4 0C 85 C0` = `add esp,0xC; test eax,eax`），返回 `0x506040`。
现场 `esi` = 战斗单位；桩沿 `[esi+0x80]` → `[+0x88]` 链表逐节点（`node+8` = `CPersonData`）
取武将索引查 `criticalHit`。命中则 eax=1；链表空 / 未命中保持原 eax。
```
83 C4 0C                       ; add esp,0xC             （重放原指令 eax 可能是 1/2）
50 53 57                       ; push eax; push ebx; push edi
8B 86 80 00 00 00              ; mov eax,[esi+0x80]      （关联单位）
85 C0                          ; test eax,eax
74 3A                          ; je notfound
8B 80 88 00 00 00              ; mov eax,[eax+0x88]      （链表头）
89 C7                          ; mov edi,eax
85 FF                          ; loop: test edi,edi
74 2E                          ; je notfound
8B 47 08                       ; mov eax,[edi+8]         （节点 → CPersonData*）
0F B7 40 04                    ; movzx eax,word[eax+4]   （★ 武将索引）
3D 20 03 00 00                 ; cmp eax,0x320
73 1C                          ; jae next
BB [arr]                       ; mov ebx,criticalArray，imm32 在 0x2B(43)
80 3C 03 00                    ; cmp byte[ebx+eax],0
74 11                          ; je next
B8 01 00 00 00                 ; mov eax,1               （命中→必定威力）
83 C4 04                       ; found: add esp,4
5F 5B                          ; pop edi; pop ebx
85 C0                          ; test eax,eax
E9 [rel32]                     ; jmp 0x506040
8B 3F                          ; next: mov edi,[edi]     （下一节点）
EB CE                          ; jmp loop
58 5F 5B                       ; notfound: pop eax; pop edi; pop ebx
85 C0                          ; test eax,eax
E9 [rel32]                     ; jmp 0x506040，rel32 在 codeSize-4
```

### 7.4 发动间隔 intervalCode（77 字节）
hook `0x4FF08B`（原 5 字节 `F7 F9 8B CE 50` = `idiv ecx; mov ecx,esi; push eax`），
返回 `0x4FF090`，让原 `call 0x4FB760` 继续执行（B5）。现场 `esi` = `CPersonData`
（`word[esi+4]` = 武将索引）、`ebx` = 单位对象（`[ebx+0x34]` = 技能ID）。
`intervalFlat[o*41+s]` 用 `0xFF` 标记「未自定义」：命中直接写天数跳过 `idiv`，
否则走原版 `idiv ecx`。两条路径尾部都重放 `mov ecx,esi; push eax` 再跳回 `0x4FF090`。
```
53 56 50 51                    ; push ebx; push esi; push eax; push ecx
8B 43 34                       ; mov eax,[ebx+0x34]      （技能 ID）
83 E0 3F                       ; and eax,0x3F
83 F8 29                       ; cmp eax,0x29
73 2F                          ; jae not_custom
0F B7 4E 04                    ; movzx ecx,word[esi+4]   （★ 武将索引）
81 F9 20 03 00 00              ; cmp ecx,0x320
73 23                          ; jae not_custom
6B C9 29                       ; imul ecx,ecx,0x29       （= ×41）
03 C1                          ; add eax,ecx
B9 [arr]                       ; mov ecx,intervalArray，imm32 在 0x21(33)
80 3C 01 FF                    ; cmp byte[ecx+eax],0xFF  （0xFF=未自定义）
74 13                          ; je not_custom
0F B6 0C 01                    ; movzx ecx,byte[ecx+eax] （取配置天数）
8B C1                          ; mov eax,ecx
83 C4 08                       ; add esp,8
5E 5B                          ; pop esi; pop ebx
8B CE                          ; mov ecx,esi
50                             ; push eax
E9 [rel32]                     ; jmp 0x4FF090
59 58                          ; not_custom: pop ecx; pop eax
99                             ; cdq
F7 F9                          ; idiv ecx                 （原版计算）
5E 5B                          ; pop esi; pop ebx
8B CE                          ; mov ecx,esi
50                             ; push eax
E9 [rel32]                     ; jmp 0x4FF090
```
跳转注入：hook 处写 `E9 rel32`（5 字节，覆盖原 `idiv ecx; mov ecx,esi; push eax`）。

### 7.5 成长倍率 growthCode（40 字节）
hook `codeBlockTail` = `dword_42F27C + 0x40`（原 6 字节 `89 44 24 04 EB BC`）。
现场 `eax` = 增长概率、`esi` = `CPersonData`（`word[esi+4]` = 武将索引）。
位图（800 字节）+ 代码区连续写入。
```
89 C1                          ; mov ecx,eax             （保存增长概率）
0F B7 46 04                    ; movzx eax,word[esi+4]   （★ 武将索引）
3D 20 03 00 00                 ; cmp eax,0x320
73 11                          ; jae skip
BA [bitsetAddr]                ; mov edx,bitsetAddr，imm32 在 0x0E(14)
80 3C 02 00                    ; cmp byte[edx+eax],0
74 06                          ; je skip
69 C9 [mult]                   ; imul ecx,ecx,mult，imm32 在 0x1A(26)
89 4C 24 04                    ; skip: mov [esp+4],ecx   （写回原参数槽）
FF 25 F0 F2 5F 00              ; jmp [0x5FF2F0]（尾跳，正确，返回地址由 0x4505F9 的 call 压栈）
```

---

## 8. 日志

- `appendRuntimeLog(const char*)`（`Runtime2026.h` 声明，`Runtime2026.cpp` 定义）。
- 追加写 `San9Companion.log`（exe 同目录），格式 `YYYY-MM-DD HH:MM:SS  message`，8 MB 上限。
- `appendRuntimeLogTrace(const char*, size_t)`：逐次内存写入追踪，**默认关闭**。
  开启途径（优先级从高到低）：
  1. 设置页 →「扩展设置」→「详细日志（逐次记录内存写入，排错用）」复选框
     （`IDC_DETAILED_LOG = 1251`，**运行时由 `Dialog::settingsPages` 动态创建，不需要改 `.rc`**）。
     勾选后**立即生效**（调 `setRuntimeLogTrace`），并持久化到
     `San9PKHardSettings.ini` 的 `[Diagnostics] LogWrites=1`；下次启动自动恢复。
  2. 命令行 `San9Companion.exe --log-writes`。
  3. 环境变量 `SAN9_LOG_WRITES=1`。
- 实现要点：`setRuntimeLogTrace(bool)` 设置进程内 `runtimeLogTraceForced` 标志；
  `traceWritesRequested()` **每次调用时实时求值**——
  **不要写成 `static const bool`**，那会把开关冻结在首次调用时，导致设置页勾选无效。
  命令行匹配是**按空白分词后逐 token 精确比较**，不用子串查找。
- 日志路径显示：主窗口状态栏悬停提示追加「日志：<绝对路径>」；启动时也写一行 `log file: <路径>`。
- 注入埋点：四类个性化（成长加成 / 激活加成 / 威力加倍 / 兵法间隔）每次注入后写一行，
  含注入武将数、hook 地址、注入代码地址、`officerTable=0x1258EE0`。
  **这是实机验证 §11.17.2.1 P0 修复是否生效的唯一手段**（离线测试不执行注入代码）。
- 其他已埋点：`writeMemory`（每次写入目标地址+大小）、`connectGame`（allocationBase）、
  `runOperation` 失败、`PersonPage::initialize` 分步。

---

## 9. 验证/测试

- 构建：`powershell -File Build.ps1 -Configuration Debug -OfflineTests -TestSuite Quick`
- 离线测试：`build\tests\Debug\San9RuntimeTests.exe --all assets\config`
- 注意：离线测试**不执行注入代码本身**，只验证写模式/边界/回滚；注入字节正确性要靠 capstone 反汇编 + 实机验证。
- 日志：`build\app\Debug\San9Companion.log`。

---

## 10. 编译、打包与部署

### 10.1 环境与工具链
- Windows + Visual Studio（含 `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`）。
- 定位 VS：`tools/VisualStudio.ps1` 里 `Get-VisualStudioPath` 用 `vswhere.exe` 找最新安装。
- 构建用 MSBuild：`$vsPath\MSBuild\Current\Bin\MSBuild.exe`。
- 平台工具集：`PlatformToolset=v145`（`build-config/Toolchain.props`）。
- 目标：**Win32（x86）**，`MachineX86`；链接子系统 `Windows`（GUI，入口 `wWinMain`）。

### 10.2 解决方案与项目结构
- 顶层：`San9Companion.sln`。
- `build-config/Companion.vcxproj`：应用（exe），引用 `Runtime.vcxproj`。
- `build-config/Runtime.vcxproj`：运行时静态库（`San9Runtime.lib`）。
- `build-config/RuntimeTests.vcxproj`：离线测试（`San9RuntimeTests.exe`）。
- 输出目录：
  - exe：`build/app/$(Configuration)/San9Companion.exe`
  - 中间文件：`build/obj/app/$(Configuration)/`
  - 测试：`build/tests/$(Configuration)/San9RuntimeTests.exe`
- 另有 CMake 配置 `CMakeLists.txt`（用于 san9-recovered 模型测试，与 MFC 前端无关）。

### 10.3 编译参数（`build-config/Compiler.props`）
- 语言标准 C++17，`ConformanceMode=true`，`MultiProcessorCompilation=true`，无 PCH。
- 告警 `WarningLevel Level3`。
- 字符集：`/source-charset:utf-8 /execution-charset:.936`（源码 UTF-8，执行字符集 GBK/936）。
- 预定义：`WIN32;_WINDOWS;_WIN32_WINNT=0x0A00`；Debug 加 `_DEBUG`，非 Debug 加 `NDEBUG`。
- 运行时库（关键，影响打包）：
  - Debug：`MultiThreadedDebugDLL`（依赖 VC debug 运行库 DLL）
  - Release：`MultiThreadedDLL`（依赖 VC 运行库 DLL）
  - Publish：`MultiThreaded`（**静态链接运行库，可独立分发**）
- 优化：Debug `Disabled`；Release/Publish `MaxSpeed`；Publish 另开 `WholeProgramOptimization`+`LTCG`+`OptimizeReferences`+`COMDATFolding`。
- 链接依赖：`user32.lib;comctl32.lib;comdlg32.lib;shell32.lib;gdi32.lib`。
- Manifest 依赖 Common-Controls 6.0。

### 10.4 资源与版本号
- 资源脚本：`assets/ui/companion.rc`，`#include` 了 `dialogs.rc` 与 `resource_ids.h`。
- `dialogs.rc` 用 `#pragma code_page(65001)`（UTF-8），`LANGUAGE 4,2`（简体中文），`Culture=0x0804`。
- 版本号：`tools/Write-BuildInfo.ps1` 在 `BeforeTargets=ClCompile` 生成 `$(IntDir)BuildInfo.h`，当前内容是**构建当天日期**（东八区 `yyyy-MM-dd`），主窗口显示 `{日期} 版`。

### 10.5 配置文件打包
- `Companion.vcxproj` 的 `CopyConfigurationFiles`（`AfterTargets=Build`）把 `assets/config/*.txt` 拷到 `$(OutDir)`。
- 涉及的配置文件：
  - `San9PKHardPara.txt` / `San9PKHardPerson.txt` / `San9PKHardCity.txt` / `San9PKHardSkill.txt` / `San9PKHardArray.txt`
  - `San9PKHardCheat.txt`（需求 4 武将个性化，新增）
- 运行期设置 `San9PKHardSettings.ini` 是运行后按需生成（不在 assets 里）。

### 10.6 构建命令
```powershell
# 顶层入口（转发到 tools/Build-App.ps1）
powershell -File Build.ps1 -Configuration Debug            # 只构建 exe
powershell -File Build.ps1 -Configuration Debug -OfflineTests -TestSuite Quick   # 构建 + 跑离线测试
powershell -File Build.ps1 -Configuration Release -OfflineTests -UiSmoke          # Release + 测试 + UI 冒烟
powershell -File Build.ps1 -Configuration Publish                                # 静态链接的发布版
```
- `-UiSmoke` 会用 `--ui-smoke` 隐藏启动 exe，校验对话框/控件绑定/草稿隔离。
- 全量校验：`powershell -File Verify.ps1 -Runtime -AllConfigurations`（先跑 python `tools/verify.py` 静态校验 + `tools/offline.py` 重定位，再构建三配置 + `verify_app.py` 结构校验，`-Native` 另跑 `Check-Native.ps1`）。

### 10.7 构建常见坑
- **LNK1104（exe 被占用）**：`San9Companion.exe` 正在运行，先 `taskkill //IM San9Companion.exe //F` 再构建。
- **`BuildInfo.h` 旧日期**：由 `Write-BuildInfo.ps1` 生成，现在取构建当天日期，不再依赖 git 提交时间。
- **新增配置文件没进输出目录**：确认在 `assets/config/` 下（`*.txt` 会被自动拷贝）；`San9PKHardCheat.txt` 若不存在则 `Controller.cpp` 里 `exists()` 检查会跳过加载。

### 10.8 部署与运行
- 运行方式：从 `build/app/$(Configuration)/` 目录直接启动 `San9Companion.exe`。
- exe 通过 `executableFile()` 找同目录文件：
  - `San9PK.exe`（游戏本体，须与修改器同目录）
  - 各 `San9PKHard*.txt` 配置文件
  - 运行期写 `San9PKHardSettings.ini`（`AppPaths::preferencesFile()`）、`San9Companion.log`
- 分发建议：
  - 本机自用/开发：`Debug` 或 `Release` 即可（需装对应 VC 运行库）。
  - 对外分发：用 `Publish`（`MultiThreaded` 静态运行库），把 exe + 配置 txt 打成一个目录即可。
    静态 CRT 已实测：exe 的导入表里 `MSVCP140.dll` / `VCRUNTIME140.dll` /
    `api-ms-win-crt-*` / `ucrtbase.dll` 计数**均为 0**，用户无需安装 VC 运行库。
- 启动流程：先开修改器，再启动游戏；修改器轮询到 `San9PK.exe` 窗口后注入。
- **现成试用包**：`out/trial/`（`San9Companion.exe` + 6 个 `San9PKHard*.txt` + `使用说明.txt`）。
  `San9PK.exe` 是游戏本体，属用户自有文件，**未随包分发**——用户把本包放到
  游戏主目录、与自己的 `San9PK.exe` 并列即可。
- 排错：设置 →「扩展设置」→「详细日志」勾选后，每次内存写入都会记入 `San9Companion.log`；
  日志路径显示在主窗口状态栏悬停提示里。详见 §8。

### 10.9 测试入口
- 离线运行时测试：`build/tests/$(Configuration)/San9RuntimeTests.exe --all assets/config`（或 `--quick`）。
- 分项测试（`--person/--formation/--skill/--city <file>`、`--personalization`、`--lifecycle`、`--presentation-speed` 等，见 `tests/runtime_test.cpp` 的 `focused[]`）。
- `--memory-trace <out> <configDir>`：记录完整读写 trace（`branchTest` + 配置测试）。

---

## 11. 修改记录（全部未提交改动）

> 本章是**完整**的改动清单，覆盖 2026-10-24 至 2026-10-25 工作区中所有未提交的修改。
> 每条给出：改了哪个文件、改了什么、为什么改、当前状态。
> 用户要求这些修改**不提交**，因此全部只存在于工作区。

### 11.0 总览

| 类别 | 涉及文件 | 状态 |
| --- | --- | --- |
| 需求 4 武将个性化重做 | `OfficerPersonalization2026.{h,cpp}`、`Pages.cpp`、`dialogs.rc`、`resource_ids.h`、`Runtime2026.cpp` | ✅ 完成并通过测试 |
| 需求 4 熟练度上限注入重写 | `Runtime2026.cpp`、`RuntimeInjection2026.cpp` | ✅ 完成并通过测试 |
| `maxExperience` 字段移除 | `PersonConfig.{h,cpp}`、`Runtime2026.{h,cpp}` | ✅ 完成并通过测试 |
| Big5 非破坏性转换 | `GameText.{h,cpp}`、`NativeWidgets.{h,cpp}`、`Pages.cpp` | ✅ 完成并通过测试 |
| 日志热路径重构 | `Runtime2026.{h,cpp}` | ✅ 完成并通过测试 |
| tooltip / 滚动条修复 | `NativeWidgets.cpp` | ✅ 完成并通过测试 |
| 版本号改构建日 | `Write-BuildInfo.ps1` | ✅ 完成 |
| 界面简繁转换 | `dialogs.rc`、`parameters.json`、`InjectionNames2026.*` | ✅ 完成 |
| 文档 | `docs/DESIGN.md`、`docs/REQUIREMENTS_1_4.md` | ✅ 完成 |

### 11.1 需求 4：武将个性化数据模型重做

**文件**：`src/runtime/OfficerPersonalization2026.h`、`src/runtime/OfficerPersonalization2026.cpp`

旧模型是每武将 4 个标量（威力系数/发动率/发动间隔/威力加倍），单值数组，粒度太粗。改为每武将 **53 个值**：

| 字段 | 类型 | 语义 | 取值 |
| --- | --- | --- | --- |
| `proficiencyCap[800][9]` | `uint16` | 每系熟练度上限 | 0 = 用原版 1000；1~65535 = 自定义上限 |
| `interval[800][41]` | `uint8` | 每兵法发动间隔（天） | 0 = 无间隔；1~255 |
| `activationBoost[800]` | `uint8` | 把握概率 | 0 = 不改；1 = 强制 100% |
| `powerPercent[800]` | `uint8` | 威力系数（百分数） | 0 = 不改；1~250（100 = 1.0x） |
| `criticalHit[800]` | `uint8` | 威力一定加倍（会心） | 0 = 不改；1 = 必定会心 |

- 常量：`Systems = 9`（步兵/骑兵/弓骑/弩兵/水军/攻城/智略/谋略/策略）、`Skills = 41`（兵法表技能 id 0..40）、`Count = 800`。
- `FieldsPerOfficer = Systems + Skills + 3 = 53`。
- 新增 `defaultInterval[41]` 静态表，存游戏原版每兵法的发动间隔（天）：步兵~弩兵 12 项为 9，水军/攻城/智略 11 项为 0，谋略/策略区间为 6/9/15，特殊/设施为 9/0。
- 新增 `DefaultCap = 1000`（游戏原版熟练度上限）。
- 构造函数：所有 `proficiencyCap` 填 1000，所有 `interval` 填 `defaultInterval`。
- 读写格式改为 `800 × 53` 行整数。读取时逐字段范围校验：熟练度上限 >65535 报 `outOfRange`，间隔 >255 报错，把握概率 >1 报错，威力系数 >250 报错。`write()` 输出顺序与 `read()` 完全对称。
- 错误行号计算从 `i*4+k` 改为 `i*FieldsPerOfficer+k`，保证定位准确。

**为什么改**：原版 SanIXPKCheat 的武将个性化是 per-officer × per-skill × per-system 三个维度，而旧实现只做了 per-officer 标量，无法表达「某武将的弓骑熟练度上限单独提高」这类需求。

### 11.2 需求 4：熟练度上限注入重写（`applyProficiencyCap`）

**文件**：`src/runtime/Runtime2026.cpp`

旧实现只是在 `0x406A75`/`0x406A80` 把立即数 `0x3E8`（1000）改成 `0xFFFF`，是**全局**上限，无法分武将分系。新实现：

1. 目标地址从写死的 `0xA2190` 改为 `AllocationBase + 0x2190`（本项目注入区是动态 `VirtualAllocEx` 分配的，旧工具的 `0xA0000` 固定基址不适用）。
2. 把 `proficiencyCap[800][9]` 扁平化为 `uint8[800*9*2]`（小端），在游戏内 `allocateMemory` 分配 14400 字节 `PAGE_READWRITE` 区并写入。
3. 写入 93 字节替换代码到 `AllocationBase+0x2190`，关键指令：
   - `movzx eax, word [capArr + eax*2]`（偏移 `0x3D` 处填 disp32）
   - 保留原版的熟练度增长计算，只覆盖上限值
   - 查表索引：武将 ID = `[esi+4]`，系 = `edi`
4. **无任何自定义上限时**（`any == false`）写回原版 93 字节 `&global<DWORD>(0x4256B8)`，保持游戏原行为。

**踩过的坑**：
- `movzx eax, word [capArr+eax*2]` 的 disp32 偏移原本写成 `0x3C`，**覆盖了 SIB 字节**，导致查表地址错乱。已修正为 `0x3D`。
- 原来 `applyProficiencyCap` 写完上限版代码后，`uploadInjectionPayloads` 又把**原版** `codePersonExperience` 写到同一地址，**覆盖**了上限版 → 游戏读档即崩溃。现已删除 `uploadInjectionPayloads` 里的那行重复写入，让 `applyProficiencyCap` 全权负责该地址。

**文件**：`src/runtime/RuntimeInjection2026.cpp`
- 删除 `writePatch(dword_42F2B0, &codePersonExperience, 0x5Du);`，并留注释说明原因。
- `applyParameterPatches()` 里删除 `writePatch(*(LPVOID *)(state + 4404), stateBytes(san9::layout2026::MaxExperience, 2u), 2u);`（`MaxExperience` 字段已废弃）。
- `connectGame()` 增加埋点：记录 `allocationBase` 与页数。

### 11.3 需求 4：把握概率 / 威力加倍的注入 bug 修复

> ⚠️ **2026-10-27 已重写**：本节是早期修复的历史记录；第二轮逆向证明旧实现仍用
> `(esi-0x1258EE0)/0x128` 取武将索引（必崩），且 hook 点 / 覆盖长度也有错。
> 把握概率与威力加倍的**当前**实现见 §7.2（56B，hook `0x4FEB90`）与 §7.3（84B，hook `0x50603B`）。

**文件**：`src/runtime/Runtime2026.cpp`

- **致命 bug（早期）**：把握概率与威力加倍的注入代码结尾原为 `jmp [0x5FF2F0]`（尾跳），**丢失返回地址并跳过原 `call` 之后的 `add esp,0Ch` 清栈**，一旦命中必崩。
- **修复（早期）**：改为 `call [0x5FF2F0]` + `E9 rel32` 跳回原 `call` 之后（`0x4FEB9B` / `0x50603B`）。
- `installPerOfficerHook()` 现在**自动补尾部 `jmp rel32`**：目标地址 = `returnAddress`，相对量 = `returnAddress - (codeAddr + codeSize)`（2026-10-27 加 `returnAddress` 参数）。
- payload 长度：`boostCode` 42→51→**56** 字节，`criticalCode` 44→49→**84** 字节（§7.2/§7.3）。
- 把握概率由「填概率数字」改为**勾选强制 100%**：命中时 `mov eax,1`。

### 11.4 需求 4：发动间隔改为 per-skill（天）

> ⚠️ **2026-10-27 已重写**：本节的旧实现（`(esi-0x1258EE0)/0x128` 取军官、hook `0x4FF08D`、
> 55 字节）被第二轮逆向证实为**必崩 bug**，已整体替换为 §7.4 的 77 字节新桩
> （hook `0x4FF08B`、`word[esi+4]` 取武将、返回 `0x4FF090` 保留原 `call 0x4FB760`）。
> 以下保留为历史记录。

**文件**：`src/runtime/Runtime2026.cpp`

- 旧：每武将一个间隔值（旬）。
- 新：每武将 × 41 兵法各一个间隔值（天）。
- 注入代码 77 字节（§7.4），分配 `800*41` 字节数组 + 代码区（`PAGE_EXECUTE_READWRITE`），
  `intervalFlat` 用 `0xFF` 标记「未自定义」。
- **只在有偏离原值的覆盖时才注入**：若所有 `interval` 都等于 `defaultInterval`，直接写回原版 5 字节 `intervalOriginal`，不占用游戏内存。

### 11.5 `maxExperience` 字段整体移除

**文件**：`src/model/PersonConfig.h`、`src/model/PersonConfig.cpp`、`src/runtime/Runtime2026.h`、`src/runtime/Runtime2026.cpp`

- `PersonConfig::maxExperience` 成员删除（原来默认 1500，校验范围 1200~3000）。
- `PersonConfig::decode()` 去掉第三个参数 `maximum`。
- `PersonConfig::read()` 少读一个值，文件从 **917 行变 916 行**。
- `PersonConfig::write()` 不再输出该行。
- `PersonConfig::valid()` 去掉 `maxExperience` 范围检查。
- `Runtime2026` 构造函数删除 `field<WORD>(layout2026::MaxExperience)=1500;`。
- `personConfig()` / `setPersonConfig()` 不再读写该字段。

**为什么删**：熟练度上限语义已从「全局单值」迁移到 per-officer × per-system 的 `proficiencyCap` 表，旧的全局字段既冗余又与新表冲突（两处都能改同一个上限）。

### 11.6 Big5 非破坏性转换：解决快照被破坏

**文件**：`src/runtime/GameText.h`、`src/runtime/GameText.cpp`、`src/app/platform/NativeWidgets.h`、`src/app/platform/NativeWidgets.cpp`、`src/app/ui/Pages.cpp`

- **问题**：`ui::gameText(char*, size_t)` 内部调 `simplifyGameText`，会**原地**把 Big5 字节改写成 GBK。武将表的 name 字段来自只读游戏快照，第一次显示后快照里的 Big5 就变成了 GBK，第二次渲染按 Big5 解码就出乱码——快照被破坏。
- **修复**：新增 `san9::gameWideText(const char* text, size_t capacity, const TextApi& api)`：
  - `MultiByteToWideChar(950, MB_ERR_INVALID_CHARS, ...)` 解码 Big5 → `wchar_t`（**不修改源缓冲**）
  - `LCMapStringW(0x0804, LCMAP_SIMPLIFIED_CHINESE, ...)` 繁 → 简
  - 失败返回空 `wstring`，不写入任何输出
  - 输入校验：`nullptr` / `capacity==0` / `capacity>INT_MAX` / 缺 `decode`/`simplify` 回调 → 返回空
  - 用 `memchr` 找 NUL 终止符，找不到 → 返回空
- `ui::gameText()` 签名改为 `const char*`，内部改用 `gameWideText`，不再原地改写。
- `Pages.cpp` 的 `loadPerson()` 和搜索比较都改用 `ui::gameText(row.name.data(), row.name.size())`。

**踩过的坑**：写测试时把「民」的码位写成了 `U+6C0F`（那是「氏」），正确是 `U+6C11`。调试输出 `56FD,6C0F` 暴露了字面量写错，转换逻辑本身是对的。fixture 里的 `\u6C0F` 也一并改成 `\u6C11`。

**测试**：`tests/runtime_test.cpp` 的 `gameTextTest()` 新增 5 组断言：Big5 快照正确转简体、转换后源字节不变、重复转换结果稳定、破坏性转换只作用于副本不污染快照、无效缓冲（nullptr/零容量/无 NUL 终止/不完整 Big5 序列）被拒且不越界读取。

### 11.7 日志热路径重构

**文件**：`src/runtime/Runtime2026.h`、`src/runtime/Runtime2026.cpp`

- **问题**：日志每次写入都 open/close 文件，且**无容量上限**。全量离线跑一次后日志涨到数百 MB。
- **修复**：
  - 改为**单一持久流**（`std::ofstream` 静态单例），只在首次写入时 open。
  - 引入 `RuntimeLogLimit = 8 MB` 常量。打开时若文件已 ≥8MB，**先删除再新建**；写入累积达 8MB 后关闭流并置 `disabled`。
  - `appendRuntimeLog()` 每次调用后 flush（保证崩溃时也能留下记录）。
  - 全部包在 `try/catch(...)` 里，**日志失败绝不干扰注入流程**。
- **逐写追踪改为环境变量开关**：新增 `appendRuntimeLogTrace()`，只记录 `SAN9_LOG_WRITES` 环境变量非空且非 `0` 时才输出。`writeMemory()` 模板里的逐次写入埋点改走这个函数。追踪内容格式 `write target=0x%08llx size=%llu`，用固定 80 字节栈缓冲 `snprintf`，**不分配内存**，避免拖慢注入事务。
- 新增 `closeRuntimeLog()`，供程序退出/测试结束时释放句柄。
- 日志路径用 `san9::executableFile(L"San9Companion.log")`（exe 同目录）。
- 时间戳格式 `YYYY-MM-DD HH:MM:SS  `（本地时间）。

**实测**：全量离线测试后日志 6,925,603 字节（6.9 MB），在 8 MB 上限内。

### 11.8 tooltip 悬垂指针修复

**文件**：`src/app/platform/NativeWidgets.h`、`src/app/platform/NativeWidgets.cpp`

- **问题**：`tooltipText_` 原为 `std::map<int, std::wstring>`。`TTM_UPDATETIPTEXTW` 后控件会**持有 `lpszText` 指针**，而 `std::wstring` 在被替换或 map 扩容时会移动内存 → 控件指向已释放内存。
- **修复**：
  - 容器改为 `std::map<int, std::unique_ptr<std::wstring>>`，每个 tooltip 文本独占一个**不可移动**的堆缓冲。
  - 更新时先构造 `replacement`（新的 `unique_ptr`），用它填充 `TOOLINFOW`，`TTM_UPDATETIPTEXTW` 成功后再 `entry->second = std::move(replacement)`。失败时抛 `std::runtime_error("cannot update tooltip")`。
  - 首次插入路径也改成先填充 `*entry->second = message` 再取 `.data()`。

### 11.9 垂直滚动条与 smoke 守卫

**文件**：`src/app/platform/NativeWidgets.cpp`

- 垂直滚动条失效：`Dialog::procedure` 里只分发了 `WM_HSCROLL`，漏了 `WM_VSCROLL`。改为 `if (message == WM_HSCROLL || message == WM_VSCROLL)`。
- `Dialog::savePreference(bool)` 补 smoke 守卫：`if (smoke) return true;`——smoke 模式下不写真实 INI。

### 11.10 UI 改造

**文件**：`src/app/ui/Pages.cpp`、`assets/ui/dialogs.rc`、`assets/ui/resource_ids.h`

- 武将配置页宽度加到 760。
- 新增 9 个「熟练度上限」输入框（`IDC_PROFICIENCY_CAP_1..9`），对应 9 个兵法系。
- 新增 27 个「发动间隔(天)」输入框（`IDC_INTERVAL_1..27`），按系分组；通过 `intervalSkillIds[27]` 映射到兵法表技能 id `0..11, 23, 25..38`。
- 把握概率、威力一定加倍由数值框改为**勾选框**（`IDC_ACTIVATION_BOOST`）。
- 移除「精进熟练度上限」全局输入框与前 5 系的「精进」radio。加载时对前 5 系做钳位：`if (i < 5 && level > 2) level = 2;`（前 5 系已无「精进」选项，最高只能是 2）。
- 移除 9 个「兵法系是否擅长」的复选框（原 `goodIds`），改由熟练度表承载。
- 姓名显示改用 `ui::gameText()`（非破坏性 Big5 转换）。
- `PersonPage::initialize` 加 6 步埋点（begin / snapshot done / config copied / labels built / items appended / done）。

### 11.11 界面简繁转换

**文件**：`assets/ui/dialogs.rc`、`src/generated/parameters.json`、`src/generated/InjectionNames2026.h`、`src/model/InjectionNames2026.json`

- 界面繁体中文全部改为简体，含 `dialogs.rc` 的武将个性化区块、`parameters.json` 的「斗舰」caption 等。

### 11.12 版本号改用构建日期

**文件**：`tools/Write-BuildInfo.ps1`

- 原先取 `git log -1 --format=%cI`（最后提交时间）。改为 `[DateTimeOffset]::Now.ToOffset(+8h)`，即**构建当天日期**（东八区 `yyyy-MM-dd`）。
- 好处：不依赖 git 历史，本地未提交状态下也能得到正确日期。

### 11.13 测试桩与测试用例扩展

**文件**：`tests/runtime_test.cpp`

- **内存分配桩泛化**：原 `allocate()` 只认 `0xB000`（主分配）和 `presentation2026::PayloadSize`（速度分配）两种尺寸，其余直接 `check` 失败。改为用 `std::unordered_map<DWORD,SIZE_T> allocations` 跟踪**任意**分配，辅助分配从 `base+0x20000` 起按 `0x10000` 步进。
- **写目标校验放宽**：原只允许 `0x400000~0x2000000`、`base..base+0xB000`、`base+0x10000..base+0x11000` 三个固定区间。改为：游戏区 `0xA0000~0x2000000`，加上**遍历所有已跟踪分配**判断是否落在某个分配区内。游戏区下界从 `0x400000` 放宽到 `0xA0000`，以覆盖低地址 `0xA2190`。
- `release()` 桩改为按跟踪表释放，校验「每个分配只释放一次」。
- 新增 `gameWideText` 的 5 组断言（见 §11.6）。
- `personConfigTest` 适配 916 行格式：删除 `maxExperience` 相关的读取、往返、无效输入用例。
- `personalizationConfigTest` 适配 53 字段新格式。

### 11.14 文档

**文件**：`docs/DESIGN.md`、`docs/REQUIREMENTS_1_4.md`

- `docs/DESIGN.md` 需求 4 章节从「第一批」改为「第一批·修正版」，表格重写为新的地址与语义，持久化说明改为 800×53 行。
- `docs/REQUIREMENTS_1_4.md`（本文档）新增，完整记录需求 1~4 的设计、内存位置、注入字节与坑。

### 11.15 新增文件

| 文件 | 用途 |
| --- | --- |
| `docs/REQUIREMENTS_1_4.md` | 需求 1~4 交接文档（本文档） |
| `assets/config/San9PKHardCheat.txt` | 武将个性化配置（800×53 行，由 `newwuj/export_cheat_params.py` 从 SanIXPKCheat 存档转换） |
| `newwuj/export_cheat_params.py` | Cheat.s9 解析器：记录起点 `0xD0`、每武将 88 字节；把握概率 = `byte0x01 & 0x80`、威力加倍 = `byte0x02 & 0x02`、威力系数 double→百分数（1.0→0、1.1→110、1.2→120） |
| `tools/_review_check{,2,3}.py` | 第三轮审查的临时核对脚本（校验 `parameter_fields.inc` 与 `dialogs.rc` 的控件 ID 重复、fileLine 冲突、masked/numeric 混用） |
| `tools/_addr_check.py`、`_dumporder.py`、`_dumpspec.py`、`_prof_check.py`、`show_env.ps1` | 逆向分析临时脚本 |
| `restore_deepseek.ps1`、`setup_relay.ps1` | 环境恢复 / 中继设置脚本 |
| `武将个性化.csv`、`武将个性化_new.csv` | 武将个性化数据导出 |

### 11.16 尚未完成

- **需求 4 兵法威力系数**：`0x43EF44` 处拿不到武将指针，需另找挂钩点。当前 `powerPercent` 只在配置层与 UI 层就绪，**未注入游戏内存**。
- **实机验证**：所有注入代码的字节正确性只经过离线测试的模式/边界检查，**未在真实游戏进程 + 存档中验证**。尤其 §11.2、§11.3、§11.4 三项涉及自改代码，必须实机确认。
- **第三轮审查的两份报告**：见 §11.17。

### 11.17 第三轮审查结论

> 2026-10-25 重跑。审查范围：逻辑一致性、计算准确性。
>
> 说明：两个审查 agent 在**汇总阶段**都撞上了 `Prompt is too long` 限制，未能输出最终报告。
> 下面的结论是**我逐条独立复核 agent 的中间推理、并自行验证代码后**得出的，不是直接采信 agent 输出。
> 凡是 agent 提出但我复核后**不成立**的猜测，也一并记录，避免下次重查。

#### 11.17.1 逻辑一致性审查

**已确认的问题（4 项）**

| 级别 | 问题 | 位置 | 证据 | 影响 |
| --- | --- | --- | --- | --- |
| P1 | **前 5 系「精进」等级读写不对称，导致加载即改写用户数据** | `src/app/ui/Pages.cpp:352`（`loadPerson`）vs `Pages.cpp:378`（`savePerson`） | `loadPerson` 对 `i < 5` 做 `if (level > 2) level = 2;`；但 `savePerson` 对全部 9 系只校验 `level < 0 \|\| level > 3` 就接受 | 见下方详述 |
| P1 | **UI 间隔输入上限（30）与模型上限（255）不一致** | `Pages.cpp:422` vs `OfficerPersonalization2026.cpp:22` | UI 绑定 `ui::bind(intervalIds[i], 0, 30, ...)`；模型读取却允许 `value > 255` 才报错 | 见下方详述 |
| P2 | **`SkillConfig::Count = 39` 与 `OfficerPersonalization2026::Skills = 41` 不一致** | `src/model/SkillConfig.h:14` vs `src/runtime/OfficerPersonalization2026.h` | 兵法表 `0x14FDAD0` 每条 `0x8C` 字节；快照读取只取 39 条 `39*0x8C`，而个性化表按 41 个技能 id 寻址 | 个性化表索引 39/40 覆盖不到快照；**需确认游戏兵法表实际条目数** |
| P2 | **`loadPerson` 钳位后 `savePerson` 仍可写回 3** | `Pages.cpp:352` / `Pages.cpp:378` | `loadPerson` 钳到 2 但 radio 控件本身只保留 3 个选项（第 4 个 radio 控件已被移除） | 钳位只改显示，底层 radio 组只有 3 项，`radio(id)` 永不返回 3，实际安全；但钳位逻辑与 UI 现状重复且含义模糊 |

**P1-1「精进」等级不对称的详述**

`assets/ui/dialogs.rc` 里 9 个兵法系的 radio 控件数量并不一致：

| 系 | radio 控件 ID | 个数 | 可选等级 |
| --- | --- | --- | --- |
| 步兵 | 1056, 1057, 1058 | 3 | 0,1,2 |
| 骑兵 | 1060, 1061, 1062 | 3 | 0,1,2 |
| 弓骑 | 1064, 1065, 1066 | 3 | 0,1,2 |
| 弩兵 | 1068, 1069, 1070 | 3 | 0,1,2 |
| 水军 | 1072, 1073, 1074 | 3 | 0,1,2 |
| 攻城 | 1076, 1077, 1078, 1079 | 4 | 0,1,2,3 |
| 智略 | 1080, 1081, 1082, 1083 | 4 | 0,1,2,3 |
| 谋略 | 1084, 1085, 1086, 1087 | 4 | 0,1,2,3 |
| 策略 | 1088, 1089, 1090, 1091 | 4 | 0,1,2,3 |

前 5 系的第 4 档（「精进」）radio 控件已被移除，所以 `Dialog::radio(id)` 对前 5 系**永远不会返回 3**。

但配置文件 `assets/config/San9PKHardPerson.txt`（916 行）中，前 5 系仍存在 level 3 的数据：

| 系 | level 3 出现次数 |
| --- | --- |
| 步兵 | 20 |
| 骑兵 | 21 |
| 弓骑 | 22 |
| 弩兵 | 7 |
| 水军 | 6 |
| **前 5 系合计** | **76** |
| 攻城 | 152 |
| 智略 | 173 |
| 谋略 | 150 |
| 策略 | 145 |

后 4 系（攻城/智略/谋略/策略）的 level 3 是**合法**的（对应「象兵/石兵/破陷/教论」等第 4 档兵法），不应改动。

**因此缺陷仅限前 5 系的 76 处数据**：UI 已删除「精进」选项，但配置里这 76 个 level 3 依然存在。`loadPerson` 把它们显示为 2，用户若在 UI 里改动任何字段后点「保存配置」，`savePerson` 会把 76 处全部写成 2——**用户数据被静默改写**。

- 建议修法（二选一）：
  - 保留「精进」radio 控件（恢复前 5 系第 4 个 radio），使 UI 与数据一致；或
  - 明确前 5 系不支持精进，在**加载配置文件时**就把这 76 处 level 3 归一为 2，并一次性落盘，让磁盘数据与 UI 能力对齐；`loadPerson` 里的钳位逻辑随之删除。
- 注意：后 4 系的 level 3 **不可**归一，那会破坏 620 处合法数据。

**P1-2 间隔上限不一致的详述**

- UI 侧：`ui::bind(intervalIds[i], 0, 30, ...)` —— 上限 30 天。
- 模型侧：`OfficerPersonalization2026::read` 允许 `value > 255` 才报错，即上限 255。
- 配置文件实测：`San9PKHardCheat.txt` 共 42400 行（= 800×53，正确）。其中**最大值 2000**（在第 9064 行，即武将 171 的「熟练度上限」字段，字段序号 1），**7221 个值超过 30**。
- 需要区分：这 7221 个超 30 的值里，绝大多数是**熟练度上限字段**（上限本就是 65535），不是间隔字段。间隔字段的实际上限 30 与模型 255 的差异，影响的是**手改配置文件**的场景——写 31~255 能被模型读入，但 UI 只允许到 30，用户在 UI 里保存该武将时会被改写。
- 建议修法：把模型侧间隔上限收敛到 30（`value > 30` 即报错），与 UI 单一事实源对齐；或把 UI 放宽到 255。**取 30 更安全**（对齐原游戏语义），但需先确认游戏原版间隔天数是否可能超过 30。

**P2-1 兵法表 39 vs 41 的详述**

- `SkillConfig::Count = 39`，`GameRecordSize = 0x8C`，快照从 `0x14FDAD0` 读 `39 * 0x8C = 4612` 字节。
- `OfficerPersonalization2026::Skills = 41`，`interval[800][41]` 按技能 id 0..40 寻址；`defaultInterval` 恰好 41 个元素。
- UI 侧只暴露 27 个间隔输入框，映射到技能 id `0..11, 23, 25..38`（跳过 12~22、24、39、40）。
- 需确认：游戏兵法表 `0x14FDAD0` 实际有多少条记录。若确为 41 条，则快照读取少读 2 条；若为 39 条，则个性化表多出 2 个永远取不到有效数据的槽位（id 39、40）。
- **当前注入侧是安全的**：`intervalCode` 里有 `cmp edx, 0x26; ja skip`（技能 id > 38 才跳过；UI 只暴露 id `0..11, 23, 25..38` 这 27 个输入框），配合 `defaultInterval` 的 41 项，两者自洽。

**我复核后否定的不成立猜测**（记录以免重查）

| agent 的猜测 | 复核结论 |
| --- | --- |
| `applyProficiencyCap` 把 disp32 填在 `bytes + 0x3D`（61）会覆盖 SIB 字节，应为 `0x3E`（62） | **不成立**。实测字节：`[57]=0F [58]=B7 [59]=04 [60]=45 [61]=00 [62]=00 [63]=00 [64]=00`。指令是 `0F B7 04 45` = `movzx eax, word [eax*2 + disp32]`，SIB 字节 `0x45` 在偏移 **60**（= 0x3C），disp32 在偏移 **61~64**（= 0x3D）。填 `0x3D` **正确**。agent 自己也纠正了这一点。 |
| `rebase()` 在 `uploadInjectionPayloads()` 之前执行，会导致 payload 里的绝对地址没被重定位 | **不成立**。`rebase()` 改的是源映像 `data_` 里的原值，`uploadInjectionPayloads` 从 `data_` 复制到分配区，复制的是**已重定位**的值。顺序正确。 |
| `writePatch(dword_42F2B0, &patchValue, 2u)` 只写了 2 字节，与 catalog 要求的 93 字节 `codePersonExperience` 不符 | **不成立**（且已按 §11.2 修复）。`dword_42F2B0` 是 `global<LPVOID>`，其**值**是 `0xA2190`，所以写目标就是 `0xA2190` 这个例程本体。`uploadInjectionPayloads` 里那行 93 字节写入**已被删除**，因为它会覆盖 `applyProficiencyCap` 写好的上限版逻辑。 |
| 注入代码里的 `0x5FF2F0` / `0x5FF30C` 等绝对地址未在重定位表中 | **疑似非 bug**。`0x5FFxxx` 属于游戏映像的另一段，且 `0x5FF2F0` 在全部 shellcode 中用法一致，与原工具行为相符。 |

#### 11.17.2 计算准确性审查

> **状态：审查已出结论，修复尚未开始。**
>
> 本节是第三轮审查的「计算准确性」部分，与 §11.17.1（逻辑一致性）并列。
> 原稿此节只有「（待填）」——第三轮的两个审查 agent 都在汇总阶段撞上
> `Prompt is too long`，没能输出最终报告；结论是主 agent 复核 agent 的
> 中间推理后自己写的。
>
> agent 中间推理的原始 transcript 仍在，可追溯：
> `C:\Users\xuwen\.claude\projects\G--aiprojects\d041e154-5ee7-4aa8-8834-f4653be2921a\subagents\agent-a323974.jsonl`（2.9 MB，计算准确性）；
> 同目录 `agent-a921f86.jsonl`（逻辑一致性）、`agent-a9551b6.jsonl`、`agent-abba65d.jsonl`。

##### 11.17.2.1 P0（已证实，必须修）：注入代码里的武将表基址立即数差 16 MB

**现象**：`src/runtime/Runtime2026.cpp` 里四个注入代码块的武将索引换算都写错了立即数。

```cpp
// Runtime2026.cpp:328 / :414 / :428 / :456 —— 四处字节完全相同
0x50, 0x8B, 0xC6, 0x2D, 0xE0, 0x8E, 0x58, 0x12, // push eax; mov eax,esi; sub eax,0x1258EE0
```

`2D` 是 `sub eax, imm32`；`E0 8E 58 12` 按小端读作 **0x12588EE0**，
而注释与设计意图都是 **0x1258EE0**，差值恰好 `0x1000000`（16 MB）。

**正确的编码**是 `0x2D, 0xE0, 0x8E, 0x25, 0x01`。

**证据链**（4 条独立来源互相印证）：

| 证据 | 内容 |
| --- | --- |
| 2019 旧工具源码 | `recovery/reference-2019/InsertAsm.h:3195`：`0xBE, 0xE0, 0x8E, 0x25, 0x01, //mov esi,1258EE0h` —— 用的是正确编码 |
| 游戏本体 | `San9PK.exe` 中 `E0 8E 25 01` 出现 **14** 次（其中 12 次是 `BE E0 8E 25 01` = `mov esi,0x1258EE0`）；`E0 8E 58 12` 出现 **0** 次 |
| 快照读取 | `src/runtime/GameSnapshots2026.cpp:28` 读的是 `0x1258EE0` |
| 离线测试 | `tests/runtime_test.cpp:1458`：`const DWORD personBase=0x1258EE0;` —— 也是正确值 |

同一份 2019 参考里 `InsertAsm.h:10399` 还有 `0xBA, 0xE0, 0x8E, 0x25, 0x01`（`mov edx,1258EE0h`），
生成的 `src/generated/PatchCatalog.h:423` 与 `companion_data.inc:1814` 也都是 `E0 8E 25 01`。
**唯一写错的地方就是 `Runtime2026.cpp` 手写的这四个代码块**——即新写的个性化功能。

**为什么离线测试全绿也发现不了**：`tests/runtime_test.cpp` 里 `E0 8E 58 12` 这个字节序列
出现 **0** 次。测试用的是自己写的 `0x1258EE0` 常量，**从不执行注入代码本身**
（见 `docs/HANDOVER.md` §4.3）。这是测试覆盖的盲区，不是测试写错了。

**运行时后果**：`eax = (esi - 0x12588EE0) / 0x128` 算出的是垃圾值，武将索引完全对不上号。
因此四个按武将查位图的个性化功能**在实机上一条都不会生效**：

| 代码块 | 长度 | 功能 | 位置 |
| --- | --- | --- | --- |
| `code` | 51 | 成长加成 | `Runtime2026.cpp:327` |
| `boostCode` | 51 | 激活加成 | `Runtime2026.cpp:413` |
| `criticalCode` | 49 | 威力加倍（必暴击） | `Runtime2026.cpp:427` |
| `intervalCode` | 55 | 兵法间隔 | `Runtime2026.cpp:455` |

**修法**：把上述四处（以及本文件 §7 字节布局里复述的同一组字节）
的 `0x58, 0x12` 改成 `0x25, 0x01`。改完重建 → 实机验证四类个性化至少各命中一条武将。

##### 11.17.2.2 复核后确认**正确**的计算（记录以免重查）

以下是审查 agent 提出的疑点，逐条核对源码与 2019/2026 参考实现后**确认无问题**：

| 疑点 | 复核结论 |
| --- | --- |
| 93 字节熟练度上限 payload 的 disp32 填在 `bytes + 0x3D`（61）会覆盖 SIB 字节，应为 `0x3E`（62） | **不成立**。指令 `0F B7 04 45` = `movzx eax, word [eax*2+disp32]`，SIB 字节 `0x45` 在偏移 60（0x3C），disp32 在 61~64（0x3D）。填 `0x3D` 正确。（§11.17.1 已有同结论） |
| 熟练度 payload 的分支目标与总长 | `jge`（0x32）→ 0x4B、`je`（0x44）→ 0x4B、NOP 填充 0x4B 起 12 字节、`jmp [0x5FF30C]` 在 0x57 结束于 0x5D = **93 字节**，与声明长度一致 |
| 成长加成代码的 disp32 槽位与分支 | 槽位 0x12 / 0x1E 正确；分支 0x1A→0x28、0x26→0x29 正确 |
| `boostCode` / `criticalCode` / `intervalCode` 长度与分支 | 51（`je` 0x1D→0x27）/ 49（`je` 0x1B→0x25）/ 55（`ja` 0x1A→0x29），均与声明长度自洽 |
| `intervalCode` 的技能 id 上界 `cmp edx, 0x26`（38） | **正确**。UI 侧 `intervalSkillIds` 最大值就是 38（`Pages.cpp:308-310`，映射 id `0..11, 23, 25..38`），`ja` 为无符号比较，id 0~38 全部放行 |
| `defaultInterval` 元素数与 `Skills = 41` | **一致**，程序化核对为恰好 41 个元素 |
| `codeBlockTail = dword_42F27C + 0x40` | **正确**。2019 参考 `InsertAsm.h` 的 `codeDynamicAddProficiency[]` 第 64 字节偏移正是 `mov dword ptr [esp+4],eax`，与 0x40 吻合 |
| `state+6596/6600/6604`、`state+6616/6620/6624` 的死亡梯度阈值写入 | **正确**，与 2026 二进制一致。agent 误把 `recovery/evidence/runtime/sub_409E10.c:5195-5207` 里的 `flOldProtect = 345` 当成了被写入的值——它其实是 `WriteProcessMemory` 失败分支的兜底赋值 |

##### 11.17.2.3 本节遗留的待办

- [x] **修 P0 立即数**：改 `src/runtime/Runtime2026.cpp` 的 :328 / :414 / :428 / :456 四处 `0x58,0x12` → `0x25,0x01`。
  → **已修（2026-10-25）**，证据链已独立复核（2019 参考 `InsertAsm.h:3195`；`San9PK.exe` 中 `E0 8E 25 01` 出现 14 次、`E0 8E 58 12` 出现 0 次）。
- [x] **同步本文件 §7 的字节布局**：§7 各节已按**正确值**（`2D E0 8E 25 01`）书写，并在节首加了警示说明「本节记录的是修复目标，不是当前代码的实际字节」。另修正 `cmp edx` 由 `0x28` 为 `0x26`（与实际代码一致）。
- [x] **补一条离线测试**：在 `tests/runtime_test.cpp` 里断言四个注入块的偏移 0x04..0x07 == `E0 8E 25 01`，防止同类立即数错误再次逃逸测试网。
  → **已加**：`injectionCodeBytesTest`（聚焦开关 `--injection-code-bytes`，并纳入 `--all`）。
  做法：测试桩记录所有写入块，按「块长 + 特征字节」定位成长/激活/威力/间隔四个块，
  断言各自偏移 0x04..0x07 == `E0 8E 25 01`；另有一条全局断言，任何写入块都不得含
  陈旧序列 `E0 8E 58 12`。**反向验证**：临时改回错误立即数 → 报
  `FAILED: no injected block encodes the stale officer table base 0x12588EE0`；恢复 → 通过。
- [ ] **实机验证**：重建 → 进游戏，确认成长加成 / 激活加成 / 威力加倍 / 兵法间隔四类个性化至少各命中一条武将。（离线测试验证不了，见 §11.17.2.1）**← 仍待实机**
- [ ] **§11.17.1 的 P2-1 措辞**（兵法表 39 vs 41）由「需确认」改为已确认结论——详见 `docs/HANDOVER.md` §3.3。



