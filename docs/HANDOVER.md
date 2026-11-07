# 交接文档（San9 Companion）

> 本文件是**跨会话接续的唯一入口**。新会话开始时，先读本文件，再按需读 `docs/REQUIREMENTS_1_4.md`（代码级细节）。
>
> 核实时间：2026-10-26。下列状态均为**实测**得出（实际构建 + 实际跑测试 + 实际读代码 + 实际反汇编），不是历史记录。
> 项目根目录：`G:\aiprojects\game\san9-companion`，分支 `dev`。

---

## 0. 30 秒速览

| 项 | 状态 |
| --- | --- |
| 构建 | ✅ 通过（Debug / Release / **Publish** 均通过；Publish 为静态 CRT，可直接外发） |
| 离线测试 | ✅ 全绿（`San9RuntimeTests.exe --all assets/config` → `exit=0`；Debug + Release 均验过） |
| 工作区 | ⚠️ 全部未提交（用户明确要求不提交） |
| 需求 1~4 | ✅ 均已实现；**P0 注入代码 bug 已修复**（§3.7，2026-10-27）；四类个性化实机验证仍待做 |
| 审查 | ✅ §11.17.1 逻辑一致性 + §11.17.2 计算准确性**均已写完**；§11.17.2 查出 1 个 P0（已修） |
| **注入代码第二轮逆向** | ✅ **已完成**（2026-10-26）。原版 `SanIXSpy.dll` 的 29 条 patch 与本版 `San9PK.exe` **逐字节吻合**，其 stub 即权威参照。详见 §3.7 + `docs/_INJECTION_FIX_NOTES.md` |
| **注入代码修复** | ✅ **已完成**（2026-10-27）。四个注入块全部重写：武将索引统一 `word[ptr+4]`、hook 点/寻址/覆盖长度全部纠正；`SkillConfig::Count` 39→41。离线测试全绿（含新字节断言） |
| UI smoke | ✅ **已转绿**（`--ui-smoke` → `exit=0`；根因是 tooltip 返回值误判，§4.1） |
| 详细日志 | ✅ 已有开关（设置页复选框 / `--log-writes` / `SAN9_LOG_WRITES=1`），见 §4.6 |
| 试用包 | ✅ `out/trial/` **已重新出包**（2026-11-07，Publish 静态 CRT exe + 6 个配置 + 使用说明；含游戏内 ☆/◎/○ 图标重写），可发给用户 |
| **兵法抵挡 bug** | ✅ **根因已确认并修复（2026-11-02）**。旧版把「把握概率」错挂在**抵抗骰** `0x4FEB90` 且强制 `eax=1`（该处 eax≠0 即「抵抗成立」）→ 勾选后**必定被抵挡**且熟练度完全不参与；同时真正的发动骰从未被 hook → 勾选也没效果。**一个根因，两个症状**。已恢复 `0x4FEB90` 原版字节，把握概率改挂发动骰 `0x4FEC59`。详见 §4.8 |
| **熟练度指示图标** | ✅ **已实现（2026-11-02，修改器侧；2026-11-07 两边统一为空心 ☆）**。四档：无 / ☆（1000<上限≤1500）/ ◎（上限>1500）/ ○（该系有缩短发动间隔，优先级最高）。原版二进制里**没有任何可复用的图标位图**（游戏只有光标位图、2019 工具无位图、原版作弊器的位图是打包加密的），故用 Unicode 字形绘制。规则与游戏内注入共用 `ProficiencyMark2026`。详见 §4.9 |
| **游戏内 ★/◎/○ 指示图标** | ✅ **已定位并实现（2026-11-07）；同日实机崩溃已修**。绘制代码 = 2019 工具遗产 `codePersonCharNum`（140 字节，装在**注入分配区** `allocationBase+0x6960`，由游戏 `0x451A5C` 的 `call` 进入），图标是 CP950 字符 ☆=`a1 b8` / ◎=`a1 b7` / ○=`a1 b3`，**游戏本体没有**。已重写为查表块：配置优先、**未配置回落原生**（精通→☆、精进→◎），两边共用 `ProficiencyMark2026` 规则、都是空心 ☆。⚠️ **实机崩溃（rel8 有符号溢出）已修，块 172→188 字节、4 个守卫改近跳转，复验仍未做**。详见 §4.10 / §4.11 |
| **逐功能开关** | ✅ **已实现（2026-11-07）**。`San9Features.ini`（与 exe 同目录，`[Features]` 段，13 个开关，只认精确 `0`/`1`，其它值当 off；启动时读一次）。开关关闭 = **完全不注入**。日志 `FEATURES: summary=1 0 1 ...` 可与 ini 逐项对照。详见 §4.11.3 |
| **玩家蛮族阵型** | ✅ **已实现（2026-11-07）**。游戏用 28 项跳转表 `0x4FB5DC` 查阵型属性，12/13/14 返回异族专用属性 `0x3C3` → 普通势力选不了。我们把这 3 项改指向返回 `0x3CC` 的桩（`AllocationBase+0xA000`），其余 25 项不动。界面 1047 字幕改为「启用玩家蛮族阵型」。详见 §4.11.4 |
| **知识资产化** | ✅ **已实现（2026-11-07）**。新增 `docs/KNOWLEDGE_BASE.md`（逆向结论的单一可信来源：地址→用途→证据→置信度、x86 注入陷阱库、已被推翻的结论、实机状态表）与 `docs/LIVE_TEST_PLAN.md`（逐功能实机清单）。详见 §4.11.5 |
| **用户手册** | ✅ `docs/用户手册.md` 已写完（9 节，含 56 开关 + 32 数值参数的完整说明） |
| **注入代码崩溃排查** | ✅ **崩溃 #3 根因已确认并修复**（上一条的禁用桩破坏了原块的返回值契约，§4.5）；329 块全量排查完成，**无新增隐患** |
| **崩溃 #4（`0x5B6754`）** | ✅ **根因已确认并修复（2026-11-01）**。根因是**我们自己**在 `applyCultivate()` 里写 `0x6613EE`，破坏了 id 表 A 的 id dword `0x6613EC` → 查找失败 → getflag 解引用空指针。已删除该写入 + 新增回归测试 `idTableWriteGuardTest`。详见 §4.7 与 `docs/_CRASH4_HANDOFF_2026-10-31.md` |
| 待办 | 见 §3.6 建议顺序（剩：**实机验证**（崩溃 #4 修复 + 四类个性化 + 游戏内图标）、威力系数 hook 点、存档路线 RE T1–T4、§12.3 三项遗留） |

---

## 1. 怎么继续（推荐流程）

**不要试图恢复旧会话。** 两个相关会话的 transcript 都已超过 Node.js 字符串长度上限，无法 resume：

```
d041e154-…（1.67 GB）→ RangeError: Invalid string length
0dbbfd19-…（3.68 GB）→ RangeError [ERR_FS_FILE_TOO_LARGE]: File size (…) is greater than 2 GiB
```

Claude Code 恢复会话时会把整个 transcript 读成一个 JS 字符串，超过约 512 MB 就崩。所以**磁盘上的文档就是唯一的交接载体**。

建议流程：

1. 在 `G:\aiprojects\game\san9-companion` 目录**新开** Claude Code 会话（不要 `--resume`）。
2. 让它先读本文件，再读 `docs/REQUIREMENTS_1_4.md` 的 §0 和 §11.17。
3. 从 §3「未完成事项」里挑一项开始。
4. 每完成一项，**把状态写回本文件**（改「未完成事项」的勾选状态 + 在「变更日志」追加一行）。这是维持交接链的纪律。

### 1.1 启动提示词（直接复制到新会话）

> 必须回答的问题：**新会话要不要读 `docs/REQUIREMENTS_1_4.md`？**
> **要。** 它是代码级精确记录（内存地址、注入字节布局、已知坑、审查结论），755 行 / 53 KB。
> 但**不要一口气全读**——只读需要的章节，理由见提示词内的「读文档顺序」。
> `docs/HANDOVER.md`（本文件）是状态与待办的唯一事实源；`REQUIREMENTS_1_4.md` 是技术细节手册。

```
项目：G:\aiprojects\game\san9-companion（Win32 x86 C++，分支 dev）

## 第一步：读文档（按此顺序，不要一次全读）

1. docs/HANDOVER.md —— 全文读（约 400 行）。这是状态与待办的唯一事实源：
   §0 速览 / §1 怎么继续 / §3 未完成事项 / §3.6 建议执行顺序 / §4 已知问题 / §5 构建与测试 / §6 工作区状态。
2. docs/REQUIREMENTS_1_4.md —— **按需读，不要全文读**（755 行）。重点：
   - §0 当前状态（先读这里）
   - §1 架构概述、§1.2 关键符号（global<T> / dword_42F27C / lpAddress 的含义）
   - §2 游戏内存结构（武将表 0x1258EE0 / 兵法表 0x14FDAD0 / kakuritsu 0x5FF2F0 / min 0x5FF30C）
   - §4 动态分配 + rebase 机制（**务必理解**）
   - §5 已修复的 bug 清单（避免重犯）
   - §7 注入代码完整字节布局（改注入代码前必读）
   - §11.17.1 逻辑一致性审查 + §11.17.2 计算准确性审查（**含一个未修的 P0**）
3. 开工前再按需查：docs/DESIGN.md（架构）、docs/ida.md（逆向笔记）、
   docs/requirement4_addresses.md（需求 4 hook 地址）、CHANGELOG.md（改动历史）、AGENTS.md（工作区约定）。

## 第二步：认领第一个待办

按 docs/HANDOVER.md §3.6 建议顺序，从**第一项未完成**的待办开始。

**当前状态（2026-10-25 更新）**：

- §3.1.1 的 **P0 已修**（`Runtime2026.cpp` 四处立即数 `0x12588EE0` -> `0x1258EE0`），
  离线字节断言 `injectionCodeBytesTest` 已加并反向验证过。
  **剩下的是实机验证**（离线测试验证不了）：进游戏确认成长加成 / 激活加成 /
  威力加倍 / 兵法间隔四类个性化各命中一条武将。
  试用包已备好（`out/trial/`），日志里会打出 `officerTable=0x1258EE0` 等注入记录供比对。
- §3.5 / §4.1 的 **UI smoke 根因已定位、已修复、已复验转绿**（`TTM_UPDATETIPTEXTW`
  返回值误判，旧结论「崩在 MainWindow 构造内部」已推翻）。`--ui-smoke` 现为 `exit=0`。
- **详细日志开关、Publish 发布包、调试产物清理**均已完成（见 §3.5 / §4.6 / §5）。
  `tools/Build-App.ps1 -Configuration Publish` 产出静态 CRT 版本，可直接外发。

细节与证据链见 REQUIREMENTS_1_4.md §11.17.2.1，修复清单见 §11.17.2.3。

## 纪律（务必遵守）

- **状态回写**：每完成一项，立刻更新 docs/HANDOVER.md —— 勾掉对应 `[ ]` 改成 `[x]`，
  并在 §8 变更日志追加一行（日期 + 改了什么）。这是跨会话接续的唯一机制。
  代码层面的细节改动同步写进 REQUIREMENTS_1_4.md 对应章节。
- **Bash 前缀**：所有命令加 `cd "G:/aiprojects/game/san9-companion" &&`（Bash 工具 cwd 默认是 G:\aiprojects）。
- **构建前杀进程**：`taskkill //IM San9Companion.exe //F`
  （有个僵尸进程 PID 29176 杀不掉——Threads=0、拒绝访问——但它不持有 exe 句柄，
  实测独占打开成功，**直接构建即可**；真遇到 LNK1104 再用管理员 PowerShell 杀）。
- **不要 git commit**：用户明确要求所有改动留在工作区。当前 23 改 + 20 新增全部未提交。
- **不要用 --resume 旧会话**：旧会话 transcript 已超过 Node.js 字符串上限，无法恢复。
- **上下文预算**：每做完 §3.6 的一到两项就新开一个会话（本项目峰值曾撞到 164899 tokens 的 context limit）。
  子 agent 的中间产物一律落盘成文件，不要留在对话里。
- **注入代码改动的验证边界**：离线测试**不执行注入代码本身**，所以注入字节的改动
  必须实机验证（进游戏确认功能生效）。但**要**顺手在 tests/runtime_test.cpp 里
  加字节断言，把这类错误挡在测试网内。
```

### 1.2 上下文用量参考

本项目容易撞上 context limit，实测数据：

| 会话 | 峰值 tokens | 结果 |
| --- | --- | --- |
| `d041e154`（旧） | 164899 | 触发 `Context limit reached` |
| 当前会话 | 111929（截至 20:03） | 正常，但余量不多 |

所以**每做完 §3.6 的一到两项就新开一个会话**，不要在一个会话里连做多项。子 agent 的中间产物一律落盘成文件，不要留在对话里。

> 背景：本项目曾反复撞上 `Context limit reached · /compact or /clear to continue`。当时决定用「文档化」代替 `/clear`——`/clear` 会归零上下文并丢掉 todo 状态，而当时待办的 P1-1/P1-2 修复、UI smoke 调试、§11.17.2 待写全部挂在同一条任务链上。本文件就是那次决策的落地物。历史上**从未真正执行过 `/clear`**（两个会话里 `<command-name>/clear` 出现 0 次）。

---

## 2. 项目概况

- Win32 x86 修改器，目标游戏 `San9PK.exe`（基址 `0x400000`，无 ASLR，ASPack 类压缩壳，运行后解压）。
- 通过 `WriteProcessMemory` + 注入代码对游戏做内存修改；所有改动进 journal，可 undo。
- 注入区是**动态分配**的（`VirtualAllocEx`），不是旧工具 `San9PKCheat` 的固定地址 `0xA0000`。
  旧工具注入地址 = `0xA0000 + offset`；本实现 = `AllocationBase + offset`，靠 `rebase()` 做重定位。
- 关键符号（`src/generated/Symbols.inc`）：
  ```c
  #define lpAddress            (global<LPVOID>(0x420050))  // 原固定基址，rebase 后指向 allocation
  #define dword_42F27C         (global<LPVOID>(0x42F27C))  // codeDynamicAddProficiency (AllocationBase+0x5570)
  #define codePersonExperience (global<DWORD>(0x4256B8))   // 原版熟练度上限代码 93 字节
  ```
- 代码级细节（内存地址、注入代码、字节布局、已知坑）全在 `docs/REQUIREMENTS_1_4.md`，本文件不重复。

### 2.1 关键文件

| 文件 | 作用 |
| --- | --- |
| `src/runtime/Runtime2026.cpp` | 需求 1/2/3/4 的 `applyXxx` 实现 + `installPerOfficerHook` + 日志 |
| `src/runtime/Runtime2026.h` | `Runtime2026` 类、`writePatch`/`writeMemory` 模板、`global<T>` |
| `src/runtime/RuntimeInjection2026.cpp` | `modifyBranches`/`connectGame`/`installCoreHooks`/`uploadInjectionPayloads` |
| `src/runtime/OfficerPersonalization2026.h/.cpp` | 需求 4 数据模型 + `San9PKHardCheat.txt` 读写 |
| `src/runtime/ProcessSession2026.cpp` | journal、`allocate`/`protect`/`read`/`write`/`restore` |
| `src/runtime/GameSnapshots2026.cpp` | 游戏快照读取 |
| `src/app/ui/Pages.cpp` | 设置页 + 武将配置页（`PersonPage`） |
| `src/model/PersonConfig.h` | 兵法系等级常量与 `maxLevel()` |
| `assets/ui/dialogs.rc` | 对话框资源 |
| `src/generated/*` | 从旧工具恢复出的 ABI/补丁/符号（勿手改生成逻辑） |

### 2.2 工作区约定（见 `AGENTS.md`）

- 遵循第一性原理，不做补丁性、兼容性修改。
- 提交信息用英文，格式 `type(scope): summary`。
- 不计算或校验文件哈希，不生成哈希清单。

---

## 3. 未完成事项

> 状态标记：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 已完成
> 建议执行顺序见 §3.6。

### 3.1 功能缺口

#### 3.1.1 P0（最高优先级，刚证实）：注入代码武将表基址立即数错 16 MB

> ⚠️ **2026-10-26 更新：本节的"修复"只是修掉了一个立即数笔误，注入代码整体仍然是错的。**
> 第二轮逆向（§3.7）证明：四个代码块的武将索引换算方式**根本性错误**
> （`(esi-0x1258EE0)/0x128` 里 `esi` 从来不是武将表指针），
> 加上 2 处 hook 点错误 + 2 处寻址错误，**实机必崩**。
> 本节保留为历史记录；**当前待办见 §3.7**。

- [x] **修 `src/runtime/Runtime2026.cpp` :328 / :414 / :428 / :456 四处的 `sub eax` 立即数**：
  `0x2D, 0xE0, 0x8E, 0x58, 0x12` → `0x2D, 0xE0, 0x8E, 0x25, 0x01`。
  现在编码的是 `0x12588EE0`，应为 `0x1258EE0`（差 0x1000000）。
  → **已修（2026-10-25）**：四处全部改为 `0x25, 0x01`。证据链已独立复核：
  2019 参考 `InsertAsm.h:3195` 用 `E0 8E 25 01`；`San9PK.exe` 中 `E0 8E 25 01`
  出现 14 次、`E0 8E 58 12` 出现 **0** 次。
- [x] **同步 `docs/REQUIREMENTS_1_4.md` §7 的字节布局**（同一组错误字节）→ 已按正确值重写并加警示。
- [x] **补离线测试**：断言四个注入块偏移 0x04..0x07 == `E0 8E 25 01`。
  → **已加**（`tests/runtime_test.cpp` 的 `injectionCodeBytesTest`）：捕获写入的字节块，
  按「块长 + 特征字节」定位四个注入块，断言偏移 0x04..0x07 == `E0 8E 25 01`，
  并全局断言**任何**写入块都不含陈旧序列 `E0 8E 58 12`。
  **反向验证**：临时把四处立即数改回错误值 → 测试报
  `FAILED: no injected block encodes the stale officer table base 0x12588EE0`；
  恢复正确值 → 通过。
- [ ] **实机验证**四类个性化各命中一条武将（离线测试验证不了）。**← 仍待实机**
  （注：在 §3.7 修完之前，这个实机验证**必然失败/崩溃**，不要白跑。）

> ⚠️ **2026-10-27 更新：§3.7 的注入代码重写已完成**，下面这段是修复前的历史记录，
> 已不再反映现状。重写后的四点：武将索引统一 `word[ptr+4]`；间隔 hook `0x4FF08B`
> 只覆盖 5 字节返回 `0x4FF090`（不吞 `push eax`/`call 0x4FB760`）；把握 hook 移到
> `0x4FEB90`；威力 hook `0x50603B`（call 之后观察）。详见 §3.7.6。
>
> 详见 `docs/REQUIREMENTS_1_4.md` §11.17.2.1。四个代码块分别是 `code`（成长加成）、
> `boostCode`（激活加成）、`criticalCode`（威力加倍）、`intervalCode`（兵法间隔）——
> **当前实机上一条都不会生效**（武将索引算出来是垃圾值）。
>
> 证据链：2019 参考 `InsertAsm.h:3195` 用的是 `E0 8E 25 01`；`San9PK.exe` 里
> `E0 8E 25 01` 出现 14 次、`E0 8E 58 12` 出现 0 次；`GameSnapshots2026.cpp:28`
> 和 `tests/runtime_test.cpp:1458` 用的都是正确的 `0x1258EE0`。

#### 3.1.2 需求 4 唯一遗留

- [ ] **兵法威力系数 hook 点 `0x43EF44` 拿不到武将指针**，需另找挂钩点。其余需求 1~4 子项均已实现。
  - 备注：这是**唯一的功能性缺口**，也是全部需求里最接近完成但最缺逆向依据的一项。
  - **逆向实证（2026-10-27，`G:\aiprojects\game\san9pkme\06_43EF44_callchain.md`）**：
    `0x43EF44` 所在函数 `0x43EDE0` 的 `this` 是**势力对象**（`0x1253C38 + idx*0xD4`），
    乘数 `[esp+0x8C]` 是调用链上层传入的系数参数（`0x444C05: push 0x168`），
    全程无武将对象——**确认无法在此点做 per-officer**。
    原版 SanIXSpy 因此用全局 double 倍率（`0x100014E0`）统一缩放，而非 per-officer。

### 3.2 文档缺口：§11.17.2 计算准确性审查 — ✅ 已完成

- [x] **§11.17.2 已补齐**（`docs/REQUIREMENTS_1_4.md`）。第三轮审查的计算准确性 agent
  （`agent-a323974.jsonl`）虽在汇总阶段撞上 `Prompt is too long`，其中间推理已被提取并
  **逐条复核**（源码 + 2019/2026 参考 + `San9PK.exe` 字节搜索），产出：
  - 1 个 **P0**（武将表基址立即数错 16 MB，见 §3.1.1）——真实 bug，四处注入代码受影响；
  - 8 条 agent 猜测经复核**否定**（disp32 偏移 0x3D、分支目标、代码块长度、
    `cmp edx,0x26`、`defaultInterval` 41 项、`codeBlockTail+0x40`、死亡梯度阈值写入等均正确）；
  - 5 条待办事项已列入 §11.17.2.3。

> 原稿此节只有「（待填）」。原材料仍在：
> `C:\Users\xuwen\.claude\projects\G--aiprojects\d041e154-5ee7-4aa8-8834-f4653be2921a\subagents\agent-a323974.jsonl`（2.9 MB）。

### 3.3 P2 兵法表 39 vs 41（**上一轮的结论是错的，2026-10-26 已纠正为 41 条**）

- [x] **把 `SkillConfig::Count` 改成 41**，并同步
  `assets/config/San9PKHardSkill.txt`（当前 39 行 → 41 行）、
  `src/generated/RuntimeLayout2026.h` / `src/model/RuntimeLayout2026.json` 的
  `SkillEntriesSize`（156 → 164）、`applySkillConfiguration()` 的循环上界、
  `readSkillSnapshot` 的读取长度。
  → **已修（2026-10-27）**：`SkillConfig::Count = 41`；`SkillEntriesSize` 156→164；
  因 `SkillEntries` 原地增长会和 `FormationAttackRangeAddress`（偏移 5212）冲突，
  `state_size` 7692→7844 并把 `FormationFlags`/`FormationEntries` 后移到 7624/7625；
  `San9PKHardSkill.txt` 追加两行 `0`（38/39 号兵法）；`PatchImage::object_` 同步 7844。
- [x] **把 `docs/REQUIREMENTS_1_4.md` §11.17.1 的「需确认」措辞改成已确认结论（41 条）**。

`docs/REQUIREMENTS_1_4.md` §11.17.1 末尾的「P2-1 兵法表 39 vs 41 的详述」记录了疑问：

- `SkillConfig::Count = 39`，`GameRecordSize = 0x8C`，快照从 `0x14FDAD0` 读 `39 * 0x8C = 4612` 字节。
- `OfficerPersonalization2026::Skills = 41`，`interval[800][41]` 按技能 id 0..40 寻址；`defaultInterval` 恰好 41 个元素。

**2026-10-26 反汇编复核结论：游戏兵法表实际是 41 条**（上一轮 TODO 写的「39 条已证实」是**错的**）：

- 访问器 `0x453720`：`cmp eax, 0x29` → **0x29 = 41**；
  `imul eax, eax, 0x8C; add eax, 0x14FDAD0`。
- 装载循环 `0x453FD2`：`mov ebx,0x14FDAD0` … `add ebx,0x8C; cmp edi,0x29; jl loop` → **41 次**。
- 交叉验证：原版 `SanIXSpy.dll` 的间隔 stub 用 `skillID & 0x3F`（0..63），
  但配合「配置记录 41 个间隔 byte」使用；本项目 `defaultInterval` 恰好 41 项。

→ **`SkillConfig::Count = 39` 是错的**（少读 2 条兵法），
`OfficerPersonalization2026::Skills = 41` 是对的。

顺带纠正 B7（武将数 800 vs 850）：**800 是对的**。
- 武将表本体 850 条（`0x4531B0`: `cmp eax,0x352`）。
- 但存档**只装载前 800 条**（`0x45415B`: `cmp edi,0x320`），
  且 `0x44C230` / `0x44B950` 有 `0x2BC..0x31F`（700..799）范围检查 → 存档 id 上界就是 800。
- 800..849 是新游戏/额外武将池，存档里不存在。**不需要改 `Count = 800`。**

### 3.4 已完成的 P1 修复（供交叉核对，勿重复做）

- [x] **P1-1 前 5 系「精进」等级读写不对称** → 已修。
- [x] **P1-2 UI 间隔上限 30 vs 模型 255** → 已修。

上一份 TODO 把 P1-1/P1-2 标为待办，但**代码里其实已经改完了**。核实结果：

**P1-1 前 5 系「精进」等级读写不对称** → 已修。
- `src/model/PersonConfig.h:11` 新增常量 `RefinedSystems=5, MaxLevel=3, MaxLevelBeforeRefined=2`，`:18` 新增 `maxLevel(system)`。
- `src/app/ui/Pages.cpp:351` `loadPerson` 用 `san9::PersonConfig::maxLevel(i)` 钳位。
- `src/app/ui/Pages.cpp:397-401` `savePerson` 对 `i < RefinedSystems` 且原值为 `MaxLevel` 时**沿用原值**，避免静默改写那 76 处用户数据。

**P1-2 UI 间隔上限 30 vs 模型 255** → 已修。
- `src/runtime/OfficerPersonalization2026.h:17` `MaxInterval = 30`。
- `src/runtime/OfficerPersonalization2026.cpp:22` 改为 `if (value > MaxInterval)` 报错。
- `src/app/ui/Pages.cpp:433` UI 改用 `ui::bind(intervalIds[i], 0, int(OfficerPersonalization2026::MaxInterval), …)`，与模型同源。

### 3.5 UI smoke 测试断点（**根因已定位，修复已落地，smoke 已转绿**）

- [x] **根因定位**：不是 `MainWindow` 构造崩溃。真实断点在
  `smokeTooltipUpdates` -> `Dialog::tooltip()` 的**更新分支**。
  旧的 §4.1 结论「崩在 `MainWindow window(controller);` 构造内部」**已过时**。
- [x] **修复**：`src/app/platform/NativeWidgets.cpp` 的 `Dialog::tooltip()` 里，
  去掉对 `TTM_UPDATETIPTEXTW` **返回值的成功判断**（该消息无返回值契约，
  官方文档未定义，参考 `main` 分支亦不检查）。保留 dev 分支新增的
  **稳定缓冲所有权**设计（`replacement` / `std::move`）——它修的是
  `main` 分支「原地改写旧 string 导致控件持有悬垂指针」的真问题，两者不冲突。
  `TTM_ADDTOOLW`（首次绑定）的 `throw` 保留。
- [x] **复验**：`--ui-smoke` **已转绿**（2026-10-25 实测 `exit=0`，修复前为 `exit=1`）。
- [x] **清理** `smoke_trace` 打点（**共 11 处**：MainWindow.cpp 9 处 + Pages.cpp 2 处）
  与硬编码路径的 `smoke_trace.txt`——**已全部删除**（2026-10-25）。
  `MainWindow.cpp` 整个 diff 就是这些打点，已用 `git checkout --` 还原；
  `Pages.cpp` 有大量真实功能改动，只定点删了那 2 行。
- [x] **顺带修掉 smoke 命令行的解析缺陷**（2026-10-25）：`wWinMain` 原本用
  `std::wstring(commandLine) == L"--ui-smoke"` **整串精确比较** `lpCmdLine`。
  但 `Start-Process -ArgumentList '--ui-smoke'` 产生的命令行是
  `"exe" --ui-smoke `（**带尾随空格**），比较失败 -> smoke 静默失效 ->
  走正常路径弹模态窗口**挂死等点击**。已改为按空白分词后逐个精确匹配。
  这个坑此前没暴露，是因为之前都是直接从 bash 跑 exe（无尾随空格）。
- [x] **给 smoke 加超时保护**：`tools/Build-App.ps1` 的 `-UiSmoke` 原本用
  `Start-Process -Wait`，smoke 挂死会把构建永久卡住、且从输出里看不出原因。
  改为 `WaitForExit(120000)` + 超时则 kill 并抛错。

### 3.6 建议执行顺序

| 序 | 事项 | 章节 | 理由 | 状态 |
| --- | --- | --- | --- | --- |
| 0 | **游戏内 ★/◎/○ 指示图标**：定位绘制点 → 实现四档 | **§4.10** | 用户明确要求游戏内也要显示 | ✅ **已完成**（2026-11-07：绘制代码 = 2019 工具遗产 `codePersonCharNum`；已重写为 **188 字节**查表块 + 5 字节跳板，配置优先/未配置回落原生；⚠️ 实机崩溃已修，**待实机复验**） |
| 1 | **重写四类个性化的注入代码**（武将索引 / hook 点 / 寻址 / 覆盖长度） | **§3.7** | 已按第二轮逆向结论全部重写 | ✅ **已完成**（2026-10-27） |
| 2 | `SkillConfig::Count` 39 → 41（+ 配置 txt + 布局 + 循环 + 快照长度） | §3.3 | 少读 2 条兵法；改动面小但要一次改全 | ✅ **已完成**（2026-10-27） |
| 3 | 重建 Debug → `--all assets/config` → `--ui-smoke` → Publish 重出 `out/trial/` | §5 | 第 1、2 项做完后的验收 | ✅ **已完成**（2026-10-27） |
| 4 | 兵法威力系数 hook 点 | §3.1.2 | 唯一功能缺口，需要逆向依据，最不确定 | ⏳ 未开始 |
| 5 | 清理调试产物（`tools/_*.py` 等） | §6 | 收尾 | ✅ **已完成**（2026-10-31 清理 30 个；2026-11-01 再清理 22 个，累计 52 个） |
| 6 | **实机验证**四类个性化 | §3.1.1 | 离线测试验证不了 | ⏳ 未开始；需用户进游戏逐项确认，判据见 §7.4 |
| 7 | **实机验证崩溃 #4 修复**（不再崩在 `0x5B6754`；「开启小兵培养」小兵能提拔） | §4.7 | 本轮唯一阻塞项 | ⏳ 未开始；**若仍崩，先删 `0x14FA656` 那一行** |
| 8 | 存档路线 RE T1–T4（定位 `.S9` 的师傅/徒弟/相性字段；T3 解密 `武将提拔/res_135.bin`） | `_CRASH4_HANDOFF` §5c.3 | 功能前置研究 | ⏳ 未开始（不阻塞崩溃修复） |
| 9 | §12.3 遗留：`0x5B8556` 返回值语义、5 张 id 表灌入路径、VirtualAllocEx 大小 | `_CRASH4_HANDOFF` §12.3 | 彻底理解游戏内部 | ⏳ 未开始（不阻塞） |

> 顺序变更说明（2026-10-26）：
> 原第 1 项「修 P0 立即数」**只是修掉了一个笔误**，注入代码整体仍然错（§3.7）。
> 原第 2~4 项（UI smoke / 详细日志 / 发布包）均已完成。
> **2026-10-27：重写注入代码（原第 1 项）+ 41 条兵法（原第 2 项）+ 验收重出包（原第 5 项）全部完成。**
> `out/trial/` 已重新出包，可发给用户做实机验证。

### 3.7 【P0】注入代码第二轮逆向结论（2026-10-26 逆向；**2026-10-27 已全部落地修复**）

> 完整证据（含反汇编、stub 逐条分析、复现脚本清单）见
> **`docs/_INJECTION_FIX_NOTES.md`**。本节只给结论与待办。

#### 3.7.1 决定性证据：原版 `SanIXSpy.dll` 就是打在这版游戏上的

`sanpkcheat/SanIXSpy.dll` 的 patch 表在 `0x10004000`（29 条 × `0x1C` 字节）。
`tools/spy_match.py` 逐条比对 → **29/29 条目与本版 `San9PK.exe` 逐字节吻合**。
→ 原版 stub 是**权威可用的参照实现**，可以直接照抄其语义。

相关的四条：

| entry | 游戏地址 | 原始字节 | 反汇编 | 语义 |
| --- | --- | --- | --- | --- |
| 16 | `0x4FEB90` | `BE 0A 00 00 00` | `mov esi,0xA` | 把握概率 |
| 17 | `0x4FF08B` | `F7 F9 8B CE 50` | `idiv ecx; mov ecx,esi; push eax` | 发动间隔 |
| 24 | `0x50603B` | `83 C4 0C 85 C0` | `add esp,0xC; test eax,eax` | 威力加倍 |
| 0 | `0x43EF44` | `0F AF B4 24 8C` | `imul esi,[esp+0x8C]` | 威力系数 |

#### 3.7.2 武将索引的正确取法（三条独立证据链收敛）

**结论：`movzx eax, word [CPersonData + 4]` = 武将表 `0x1258EE0` 的下标 = 本项目 UI 索引。**

1. **RTTI**：`0x4FF030` 在 `0x4FF05B` 用类型描述符 `0x6060E8` 做 `dynamic_cast`；
   `0x6060E8` 的 name 指针 → `0x606100` = `"CPersonData"`，`+4` 字段 = `0x128`（记录大小）。
   → `0x4FF08B` 现场的 `esi` 就是 `CPersonData`。
2. **装载路径**：`0x45415B` 循环 `push edi`(表下标) → `call 0x44F830` →
   `0x44F85A: mov eax,[esp+0x18]` → `call 0x446800` →
   `mov [ecx+4], eax`（低 16 位 = 表下标，高 16 位 = 种类）。
3. **原版 stub**：`0x1000181D: movzx eax, word [esi+4]` —— 与上两条一致。

→ **本项目 UI 的武将索引（读 `0x1258EE0` 的下标）与 hook 现场可得的索引天然对齐，无需换算。**

❌ **计划中「改用 `dword[troopMember+0x94]`」是错的**：
`+0x94` 是 `CUnitData`（部队单位）上的字段（写点 `0x4B3844`，
读点 `0x44CF20` 拿它去 `call 0x4531B0` 查武将属性表），
在 `0x4FF08B` 现场 `esi` 是 `CPersonData`，**根本没有 `+0x94` 这个字段** → 野读。

#### 3.7.3 逐项 bug 清单（现状 → 正确做法）

| # | 位置 | 现状（错） | 正确做法 |
| --- | --- | --- | --- |
| B1 | 四个代码块 | `sub eax,0x1258EE0; div 0x128` 算索引，`esi` 从不是武将表指针 → **野指针读，必崩** | `movzx eax, word [esi+4]`（仅当 `esi` 是 `CPersonData`） |
| B2 | 间隔寻址 | `0F B6 0C 05` = `movzx ecx,byte[eax+eax+disp32]`，索引被**乘 2** | `0F B6 04 01` = `movzx eax,byte[ecx+eax]` |
| B3 | 把握/威力寻址 | `0F B6 0C 01` = `movzx ecx,byte[ecx+eax+1]`，多 **+1** 偏移、基址寄存器用错 | `0F B6 04 01` |
| B4 | `installPerOfficerHook` 回写 | 用 `originalSize`(6) 字节覆盖，其后的 `add esp,0xC` 被吞 | 覆盖长度必须与被替换指令**精确**一致 |
| B5 | 间隔 hook | `0x4FF08D` 覆盖 8 字节，把 `push eax` / `call 0x4FB760` 一起吞了 | hook `0x4FF08B`，只覆盖到 `push eax` 之前 |
| B6 | `SkillConfig::Count` | 39 | **41**（§3.3） |
| B7 | 武将数 800 | 800 | **800 正确**，不改（§3.3） |
| B8 | 成长加成 | 同 B1 | 需重新设计（见 §3.7.5） |
| B9 | 熟练度上限 `word[esi+4]` | — | **口径正确**，保持（`esi` 在 `0x44C9A3` 现场是 `CPersonData`） |

#### 3.7.4 三个 hook 现场的对象语义

- **间隔（`0x4FF08B`）**：`esi` = `CPersonData` → `word[esi+4]` ✅；
  `ebx` = 单位对象 → `dword[ebx+0x34]` = 技能 ID ✅。
  间隔内值 = 兵法记录 `+0x7C`（dword），随后被 `idiv` 除以 1 或 3。
- **把握概率（必须从 `0x4FEB95` 移到 `0x4FEB90`）**：
  `0x4FEB90` 处 `esi` 还是单位对象（`0x4FEB22: mov esi,ecx`）；
  `0x4FEB95` 处 `esi` 已被改成**常数 10** → 现在取不到武将。
  取武将路径：单位 `+0x30` → `CPersonData*` → `word[+4]`；
  原版还带一条回退：单位 `+0x40`（城市）→ `+0x88`（链表头）→ `node+8` → `word[+4]`。
- **威力加倍（`0x506035`）**：`esi` = 战斗单位 → `[esi+0x80]` → `+0x88` → `node+8` → `word[+4]`。
  ⚠️ 这里 `esi` **不是** `CPersonData`，不能直接 `word[esi+4]`。
  原版 hook 在 `0x50603B`（call **之后**）只**观察**结果；要**改**概率必须 hook 在 call 之前。

#### 3.7.5 尚未解决 / 需决策

1. **间隔「天」↔ 兵法记录 `+0x7C` 内值**的换算。
   已知 `+0x7C` 是 dword 且被 `idiv`（1 或 3）除；
   原版用的间隔是 **byte**（0..255）直接覆盖。
   → 需确认本项目 `interval[800][41]` 存的是「天」，注入时该写什么值、是否也过 `idiv`。
2. **成长加成（`codeBlockTail`）**：hook 现场 `0x4505EF: mov esi,ecx`（`this`），
   `0x450612: mov ecx,esi` 后 `call 0x44D2A0`。
   2019 参考的 `codeDynamicAddProficiency` **不取武将 ID**（走 `call [eax+0x3c]` 虚函数），
   说明**原版这个特性是全局的，不分武将**。
   → 本项目「按武将列表」的语义需要重新设计，或改挂到能拿到 `CPersonData` 的点。
   **这是四个特性里唯一需要产品决策的。**
3. **把握 / 威力**是否需要原版那条「城市链表」回退路径，需实机确认。
4. **实机验证**必须由用户执行；离线测试只能验证字节序列。

#### 3.7.6 已落地的修复（2026-10-27，`Runtime2026.cpp`）

四个注入块全部重写，字节级验证（`tools/asm_check.py` + capstone；测试 `injectionCodeBytesTest`）：

| 特性 | hook 点 | 覆盖 | 返回 | 实现要点 |
| --- | --- | --- | --- | --- |
| 成长加成 | `codeBlockTail`（`dword_42F27C+0x40`） | 40B | `jmp [0x5FF2F0]` | `mov ecx,eax; movzx eax,word[esi+4]; cmp eax,0x320; jae skip; …bitset…; imul ecx,ecx,mult; mov [esp+4],ecx` |
| 把握概率 | `0x4FEB90` | 5B | `0x4FEB9E` | 保存 `esi`→ebx，重放 `mov esi,0xA; call [0x5FF2F0]; add esp,0xC`，沿 `[ebx+0x30]→word[+4]` 查 activationBoost |
| 威力加倍 | `0x50603B` | 5B | `0x506040` | 重放 `add esp,0xC`，沿 `[esi+0x80]→[+0x88]链→node+8→word[+4]` 查 criticalHit |
| 发动间隔 | `0x4FF08B` | 5B | `0x4FF090` | 重放 `mov ecx,esi; push eax` 让原 `call 0x4FB760` 执行；`[ebx+0x34]&0x3F` 技能ID + `word[esi+4]` 武将 + `interval[o*41+s]`（0xFF=默认走原版 idiv） |

配套修复：
- `installPerOfficerHook` 增加 `returnAddress` 参数，回写**恰好 originalSize 字节**（B4），不再吞 `add esp,0xC`。
- 间隔 `intervalFlat` 用 `0xFF` 标记「未自定义」，命中则直接写天数、跳过 `idiv`；未命中走原版 `idiv ecx`。
- `SkillConfig::Count` 39→41（B6），布局 `state_size` 7692→7844，`FormationFlags/Entries` 后移到 7624/7625（避免撞 `FormationAttackRangeAddress` 偏移 5212）。

**离线验证**：`San9RuntimeTests.exe --all assets/config` 全绿；`--ui-smoke` `exit=0`；Publish 构建通过并重新出 `out/trial/`。
**字节断言**：`injectionCodeBytesTest` 断言四个块恰好一块匹配、在 `word[ptr+4]` 偏移处读到索引、且不含旧的 `sub 0x1258EE0` 形式（B1/B8）。反向验证已确认：改回错误字节 → 测试失败。

> **仍需实机确认**（§3.7.5 第 3 项）：把握 / 威力两条武将查找路径的链表结构是否与预期一致；
> 以及第 1、2 项（间隔换算已按「直接写天数」落地、成长加成已按「按武将列表」落地）的实际游戏表现。

---

## 4. 已知问题与坑

### 4.1 UI smoke 异常：真实断点在 tooltip 更新（**已定位、已修复、已复验转绿**）

`Build.ps1 -Configuration Debug -UiSmoke` 抛异常。**本节结论已按实测更新**，
旧结论（崩在 `MainWindow` 构造内部）**已推翻**。

**实测 trace**（本会话重新跑出，已推进到比旧文档更靠后的位置）：

```
run enter
init windows done
controller done
window done
main window created
main window hidden
exception: cannot update tooltip
```

**根因**：`Dialog::tooltip()` 的**更新分支**（非首次绑定分支）里，dev 分支新增了

```cpp
if (!SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info)))
    throw std::runtime_error("cannot update tooltip");
```

`TTM_UPDATETIPTEXTW` **没有返回值契约**（官方文档未定义该消息的返回值；
`main` 分支原代码调用它后也**不检查返回值**，只对有明确 BOOL 契约的
`TTM_ADDTOOLW` 判 `throw`）。把无契约消息的返回值当成功标志 -> 更新文本时
稳定误报 -> `smokeTooltipUpdates` 抛异常 -> `wWinMain` 的
`catch (const std::exception&)` 捕获后（smoke 模式不弹 `fatalError`）
`return 1`。这就是 `--ui-smoke` 退出码 1 的来源。

**已落地修复**：删掉该返回值判断，改为无条件调用 `TTM_UPDATETIPTEXTW`；
**保留** dev 分支的稳定缓冲所有权设计（先分配 `replacement`、更新后
`std::move` 换入，旧缓冲最后释放）——它修的是 `main` 分支「原地改写旧
string、控件仍持有 `lpszText` 指针」的真实悬垂风险，与本 bug 无关、不能回退。

**复验结果（2026-10-25）**：重建后 `./build/app/Debug/San9Companion.exe --ui-smoke`
→ **`exit=0`**（修复前 `exit=1`）。**UI smoke 已转绿**，不再阻塞打包交付。

> 顺带确认：`smokeTooltipUpdates` 的回读断言（`TTM_GETTEXTW` 比对 `details + L"99"`）
> 也通过了——说明 tooltip 更新**确实生效**，之前只是返回值判断误报，不是功能坏了。

**后续**：可清理 `MainWindow.cpp` 的打点与 `smoke_trace.txt`（见 §3.5 最后一项）。

**注意**：`smoke_trace.txt` 与 `MainWindow.cpp` 里的打点是调试产物，定位完成后应清理
（路径硬编码 `G:\aiprojects\game\san9-companion\smoke_trace.txt`，已进 git untracked 列表）。
### 4.2 `LNK1104: 无法打开文件 build\app\Debug\San9Companion.exe`

原因是 `San9Companion.exe` 正在运行被占用。构建前先杀掉：

```bash
taskkill //IM San9Companion.exe //F
```

**当前状态（2026-10-25 核实）**：PID 34236 已退出；剩 **PID 29176**，但它是**僵尸进程**——
`Threads=0`、WS=20 KB、父进程 24100 已死且进程表条目无人回收。`taskkill` / `Kill()` 均返回
**「拒绝访问」**（当前会话非管理员；`Start-Process -Verb RunAs` 弹 UAC 后也未成功）。

**但它不阻塞构建**：实测对 `build/app/Debug/San9Companion.exe` 的
`File.Open(..., FileShare.None)` 独占打开**成功**，覆盖写和删除都成功——
说明该僵尸进程并未持有 exe 的文件句柄。所以**直接构建即可**，不必纠结杀进程；
真遇到 `LNK1104` 再用管理员 PowerShell 跑 `taskkill //IM San9Companion.exe //F`。

### 4.3 注入代码不参与离线测试

离线测试全绿**不代表**注入逻辑正确——测试不执行注入代码本身。「游戏读档就退出」这类问题只能实机验证。

### 4.4 §0.3 已修但待实机复验

`applyProficiencyCap` 与 `uploadInjectionPayloads` 曾重复写 `AllocationBase+0x2190`，导致上限版被原版覆盖。现已让 `applyProficiencyCap` 全权负责该地址，并删掉 `uploadInjectionPayloads` 里的 93 字节重复写入；`applyParameterPatches()` 里针对 `layout2026::MaxExperience` 的 `writePatch` 也已移除（`maxExperience` 字段已从 `PersonConfig` 删除）。**实机读档仍需人工验证**。

### 4.5 替换原工具代码块时，**必须保留其返回值契约**（2026-10-31 崩溃 #3 根因）

**这是本项目目前最重要的一条注入代码纪律。**

2026-10-31 的第三次实机崩溃（异常地址 `0x45C8A0`，读取地址 `0x28`），
**根因不是新代码，而是上一轮的「修复」本身**：把 `unk_42C894` 禁用成
`xor eax,eax; ret 4` 桩，**破坏了该块的返回值契约**。

原工具块 `unk_42C894` 的契约是「**原样返回入口 eax**」——
它的收尾是 `mov eax,ebp; ret 4`，而 `ebp` 保存的正是入口 eax
（`0xA9A5D: mov ebp,eax`）。调用链：

```
0x51FA0E: call 0x453280        ; 游戏查对象表，eax = 对象指针（非空）
0x51FA13: call 0xA9A50         ; ← 我们用 descriptor 0x42C930 写入的跳转
0x51FA18: xor edx,edx
0x51FA1A: je  0x51FA3B
0x51FA3B: mov ecx,eax          ; ecx = 0x453280 的返回值
0x51FA3D: call 0x45C8A0        ; ← 以 ecx 为 this；ecx==0 则读 [0x28] 崩溃
```

桩 `xor eax,eax` 把 eax 抹成 0 → `mov ecx,eax` 得到 ecx=0 →
`0x45C8A0`（thiscall，首条即 `mov eax,[ecx+0x28]`）读地址 `0x28` → 访问违例。

**正确做法**：禁用一个原工具块时，桩必须保留三件事——

1. **返回值语义**（这里 = 原样返回入口 eax）；
2. **栈平衡**（原块 `push ebp/esi/ebx` … `pop ebx/esi/ebp` + `ret 4`
   的净效果是弹出 1 个 stdcall 参数；桩用 `ret 4` 即可等价）；
3. **被调用方的寄存器副作用**（若调用方依赖 ecx/edx 等）。

本项目当前采用的桩是 **`C2 04 00`（`ret 4`）**：
eax 原样返回、栈弹出 1 个参数、不执行任何污染逻辑，语义与原块等价。

**取证的正确姿势**：Windows 事件日志只给「故障偏移」，很容易猜错方向。
本次通过 WER 本地转储拿到真实调用栈才一次定位：

```powershell
# 开启 San9PK.exe 的本地完整转储
New-Item -Path "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Force
Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Name DumpType -Value 2
Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Name DumpFolder -Value "$env:LOCALAPPDATA\CrashDumps"
```

转储文件在 `%LOCALAPPDATA%\CrashDumps\San9PK.exe.<pid>.dmp`，
用 `minidump` 包解析：异常地址、异常信息（`ExceptionInformation[1]` = 实际访问地址）、
全部寄存器、以及**栈上的返回地址**（栈顶第一个落在模块内的地址 = 调用点返回地址）。

> 教训：上一轮仅凭事件日志的故障偏移就断定「崩溃在 `codeCheckTransportBlock`」，
> 结论**错误**——转储显示返回地址是 `0x51FA42`，崩溃在**游戏原生** `0x51FA3D`，
> 我们上传的块根本没在崩溃路径上。

### 4.6 运行时日志

- `appendRuntimeLog` 一直在跑，写到可执行文件旁的 `San9Companion.log`（8 MB 上限，超出后从头覆写）。
- `appendRuntimeLogTrace`（逐次内存写入）**默认关闭**，有三条开启途径，优先级从高到低：
  1. **设置 → 扩展设置 → 「详细日志（逐次记录内存写入，排错用）」** 复选框（控件 id `IDC_DETAILED_LOG = 1251`），
     勾选后**立即生效**（无需重启），并持久化到 `San9PKHardSettings.ini` 的 `[Diagnostics] LogWrites=1`；
     下次启动时自动恢复。
  2. 命令行 `San9Companion.exe --log-writes`。
  3. 环境变量 `SAN9_LOG_WRITES=1`。
- 开关实现：`Runtime2026::setRuntimeLogTrace(bool)` 设置进程内 `runtimeLogTraceForced` 标志；
  `traceWritesRequested()` 每次调用时实时求值（**不是** `static const bool`——那会把开关冻结在首次调用时，
  导致设置页勾选无效）。命令行匹配是**按空白分词后逐个精确比较**，不用子串查找，
  否则 `--log-writes-off` 之类的近似参数会被误判为开启。
- 日志路径显示：主窗口状态栏悬停提示里追加「日志：<绝对路径>」；启动时也写一行 `log file: <绝对路径>`。
- 注入埋点：四类个性化（成长加成 / 激活加成 / 威力加倍 / 兵法间隔）每次注入后写一行，
  含**注入武将数、hook 地址、注入代码地址、`officerTable=0x1258EE0`**——
  这是**实机验证 P0 修复是否生效**的唯一手段（离线测试不执行注入代码）。
  例：`personalization growthBoost: officers=3 hook=0x004FF08D codeAddr=0x00A00120 officerTable=0x1258EE0`。
- 设置页的扩展选项是**运行时动态创建**的（`Dialog::settingsPages`），所以新增复选框**不需要改 `.rc`**。

### 4.7 崩溃 #4 的教训：**不要凭猜测定地址**（2026-11-01）

**这是本项目第二条注入代码纪律（第一条是 §4.5 的返回值契约）。**

崩溃 #4（异常地址 `0x5B6754`、读取地址 `0x14`）的根因**不在原工具的代码里，
而在我们自己新增功能时猜的一个地址**：`applyCultivate()` 往 `0x6613EE` 写 1 字节。

`0x6613EE` 看起来像个"孤立的标志位"（游戏 `.text` 里零引用），但它其实是
**id 表 A 首条记录 `0x6613E8` 的 id dword `0x6613EC`（值 `0x000003E8`）的第 2 个字节**。
写 1 → id 变成 `0x000103E8` → 游戏按 id 1000 查找失败 → `0x5B72F0`（getflag，
**没有空指针检查**）解引用空 entry → AV。

**三条应遵守的规则**：

1. **新地址必须有 RE 依据。** `recovery/evidence/global_assignments.json` 里没有的地址
   （`0x6613EE`、`0x14FA656` 都是我们猜的），就不该写。猜的地址要**显式标注**，
   并写明"若实机再崩，先删这行"。
2. **写 `.data` 里的字节前先确认它是什么字段。**
   本次的教训是「id 就在 `entry+0x04`」这种布局知识——同一个 `.data` 段里，
   相邻的字节可能根本不是独立的标志位。改用 `pefile` 读盘核对字节，
   比在反汇编里猜可靠得多。
3. **新增写入一律加回归测试。** `idTableWriteGuardTest` 断言
   **所有**录制写入都不覆盖 5 张 id 表区域（`0x6613E8` / `0x665B70` / `0x667740` /
   `0x667C08` / `0x669F78`，各 11 × `0x20` = `0x160` 字节），
   并单独断言 `0x6613EE` 与 `0x6613EC` 不在任何写入范围内。
   → 以后再加猜测地址时，这个测试会立刻挡住。

> 附带结论：**「开启小兵培养」的功能实现本来就是对的**——
> `RuntimeInjection2026.cpp:1170-1172` 写 `0x40CAEF`/`0x40BF25` = `0x32`/`0`，
> 与原工具 SanIXPKCheat（`sub_409E10.c:5153-5162`）、2019 参考实现
> （`San9PKHardDlg.cpp:4261-4265`）、游戏本体反汇编三方一致。
> **崩溃与该功能无关**，纯粹是 `applyCultivate()` 多写的那一个字节。

### 4.8 兵法抵挡 bug：把握概率挂错了骰子（2026-11-02 已修复）

**用户报告的两条**：
1. 「兵法抵挡似乎有 bug；经常出现熟练度很高的人发动兵法时被熟练度低的人抵挡的情况」；
2. 「兵法发动概率这个设置也是没有生效的，有武将设置了这个但是没有起效果」。

**根因（一个，两个症状）**：游戏里有两枚独立的骰子，旧版把「把握概率」挂到了**抵抗骰**上。

| 骰子 | 所在函数 | 掷骰点 | 语义 |
| --- | --- | --- | --- |
| **抵抗骰** | `0x4FEB20` | `0x4FEB95` | 概率 = (对手值 − 我方熟练度) / 10；**eax≠0 ⇒ 抵抗成立**；唯一调用者 `0x5066A1`，结果在 `0x506838`（`cmp edi,1 / setg dl`）消费 |
| **发动骰** | `0x4FEBB0` | `0x4FEC5C` | 概率 = 100 + 随机抖动；失败 ⇒ `0x4FEC61: test eax,eax; jne` → `0x4FEC65: xor eax,eax` 返回 0 ⇒ 兵法不发动 |

旧实现在 `0x4FEB90`（`mov esi,0xA`）挂桩并强制 `eax=1`。因为「eax≠0」在抵抗骰里表示
「抵抗成立」，所以**勾选把握概率 ⇒ 该武将必定被抵挡，且熟练度完全不参与判定**（症状 1）；
而真正的发动骰从未被 hook，所以**把握概率勾了也没有任何效果**（症状 2）。

**取证**：`tools/pe_dis.py San9PK.exe 0x4FEB80:0x30` → 原版 `0x4FEB90` = `BE 0A 00 00 00`，
`0x4FEB9E: test eax,eax` → 非零则 `mov esi,1` = 抵抗成立。

**修复**（`src/runtime/Runtime2026.cpp` `applyOfficerPersonalization()`）：

1. 把 `0x4FEB90` 恢复成 `BE 0A 00 00 00`，抵抗判定完全按原版执行；
2. 把握概率改挂**发动骰** `0x4FEC59`（`push 0xB; push edx; call ebp`）：
   沿 `esi`（发动单位）→ `[+0x30]` CPersonData → `word[+4]` 取武将索引查表；
   命中则跳过掷骰、`mov eax,1`、返回 `0x4FEC5E`；未命中则原样重放原调用；
3. 日志打印 `personalization activationBoost: resist roll restored at 0x…` 作为佐证。

> ⚠️ **改动此桩时必须遵守的栈账约束**：`kakuritsu` 是 **`__cdecl`**
> （导出名 `?kakuritsu@@YAHHW4RANDOM_KIND@@H@Z`，`YAH` = cdecl），由调用方清参数。
> 原版 `0x4FEC5E` 的 `add esp,0x14`（20 字节）恰好清掉 5 个压栈：
> `push eax`(0x4FEC25) + `push ebp`(0x4FEC30) + `push 1`(0x4FEC53) + `push 0xB` + `push edx`。
> 所以「命中」路径**必须照原版压入 `0xB` 与 `edx`**（虽然不掷骰），多压/少压都会破坏栈平衡。
>
> ⚠️ **rel32 偏移约定有两套，别混用**：
> - `installPerOfficerHook(..., extraJumpRelOffset)` = **rel32 字段偏移**（E9 操作码偏移 + 1），本桩是 `0x28`；
> - `tools/asm_check.py` 的 `rel32={off: tgt}` = **E9 操作码偏移**，本桩是 `0x27` / `0x35`。
>
> 两者指向同一段 rel32 字段，但写代码时按各自的约定填。

**桩布局**（58 字节）：hook `0x4FEC59` → 返回 `0x4FEC5E`，`arrayAddrOffset=0x15`。
`tools/asm_check.py` 用 capstone 逐条验证；`tests/runtime_test.cpp` 的
`injectionCodeBytesTest` 断言 `{"activation boost",58,0x09,6,{0x0F,0xB7,0x40,0x04,0x3D,0x20},…}`。

### 4.9 熟练度指示图标：可复用的图标位图 = 0（2026-11-02 已实现）

**用户原文**：「你告诉我有几个图标可以用，如果数量度上限是1000（默认值）不显示图标，
如果是1500以下（比如1200），显示一个五角星，如果是1500以上（1600、2000）显示一个圈，
如果是发动间隔缩短了，则显示一个环。」

**关于「有几个图标可用」的调查结论：可直接复用的图标位图 = 0 个。**

| 载体 | 结论 |
| --- | --- |
| 游戏 `San9PK.exe` | 只有 12×10 与 33×11 两个**光标**位图，无可用图标 |
| 2019 工具 `recovery/sample-2026/San9PKHard.exe` | **没有任何位图资源** |
| 原版作弊器 `SanIXPKCheat.exe` | 有 25 BITMAP + 17 ICON，但**资源数据打包加密**：暴力 XOR 密钥恢复失败、尺寸暴力枚举有歧义，无法提取 |

**原版指示器的真实语义（与用户描述不同）**：原版武将配置对话框里那 9 个 8×8 控件
（id 1096–1104，x=196，y=102/117/132/147/162/177/192/207/222，groupbox「精通 熟练度」
在 `[186,84,57,160]`）**不是图标**，而是 `BS_CHECKBOX` 复选框
（class ordinal 0x80 = Button，style `0x50010002`）。证据链：
- `recovery/evidence/decompiled/sub_404C70.c`：`DDX_Check(a2, 1096, …)` … `DDX_Check(a2, 1104, …)`；
- `tools/reconstruct.py:206`：class ordinal `{0x80:'Button', 0x81:'Edit', 0x82:'Static', …}`。

它们绑定的是武将**逐兵法系的「精通」标志位**，**与熟练度数值无关**：
`sub_405130.c` 读 byte `this+12352` 的 bit 0–7（= officer+0x36 >> 3）与 word `this+6176` 的 bit 8；
`sub_409E10.c` 消费这些位去改游戏内存。
→ **两个原始二进制里都不存在** ★/☆/○/●/◎/环 的字形梯度。

**本作实现**（按用户要求，四档，用宋体 Unicode 字形直接画，无需新增位图资源）：

| 状态 | 判定 | 字形 |
| --- | --- | --- |
| 无 | 上限 = 1000（或填 0 = 不修改），且该系无缩短间隔 | — |
| ★ | 上限 1000 < cap ≤ 1500 | U+2605 |
| ◎ | 上限 cap > 1500 | U+25CE |
| ○ | 该系至少一个兵法的发动间隔 < 游戏原值（**优先级最高**） | U+25CB（兼作「环」的近似） |

**兵法系 → 兵法表 id 的权威映射**（依据原版对话框的 9 行布局）：
步兵 0–2 / 骑兵 3–5 / 弓骑 6–8 / 弩兵 9–11 / 水军 12–14 / 攻城 15–18 /
智略 19–22 / 谋略 23–26 / 策略 27–30；**31–40 为特殊兵法，不归属任何系**。
放在 `OfficerPersonalization2026::systemSkills`。

**改动清单**：
- `assets/ui/resource_ids.h`：`IDC_PROFICIENCY_MARK_1..9`（1252–1260）；
- `assets/ui/dialogs.rc`：对话框 132 加 9 个 Static（x=336，y=110/125/140/155/170/185/200/215/230）；
- `src/runtime/OfficerPersonalization2026.h`：`RefinedCap=1500`、`NoSkill=0xFF`、`systemSkills`；
- `src/app/ui/Pages.cpp`：`PersonPage` 加 `proficiencyMark()` / `refreshProficiencyMarks()` /
  `isProficiencyMarkSource()` / `updateProficiencyMarkSources()`，`loadPerson()` 后刷新，
  上限或 27 个间隔输入框 `EN_CHANGE` 时实时刷新；
- `tests/runtime_test.cpp`：新增 `proficiencyMarkerTest()`，注册进 `--all` 与 `--proficiency-marker`。

### 4.10 游戏内 ★/◎/○ 指示图标：**已定位并实现（2026-11-07）**

> ⚠️ 本节 2026-11-07 早些时候的版本写着「静态 RE 已穷尽、必须 live 取证」。
> **那是错的，结论已被推翻**：绘制代码就在 companion 自己的注入块里，静态可查。
> 下面是完全重写后的版本。

**结论 1：游戏本体里没有 ☆/★ 字形 ⇒ 游戏内的五角星是 companion 自己画的。**
- 游戏的字符表 `0x61CFA0..0x61CFF8`（**Big5/CP950**，不是 GBK）内容为
  `U+FF19..U+FF10`（全角数字）、`•`、`＊`、`？`、`完`、`未`、`△`、`◎`、`○`、`×`。
  **★ U+2605 与 ☆ U+2606 都不在其中。**
- 符号指针表（游戏 .data）`0x66AE30..0x66AE80`：
  `0x66AE3C`→×、`0x66AE40`→○、`0x66AE44`→◎、`0x66AE48`→△、`0x66AE4C`→未、
  `0x66AE50`→完、`0x66AE54`→？、`0x66AE5C`/`0x66AE60`→＊、`0x66AE64`→•、
  `0x66AE68`→處斬、`0x66AE38`→"  "。
- CP950 编码（本项目新增，必须记住）：
  **☆ U+2606 = `a1 b8`、★ U+2605 = `a1 b9`、◎ U+25CE = `a1 b7`、
  ○ U+25CB = `a1 b3`、△ U+25B3 = `a1 b5`、× = `a1 d1`、： = `a1 47`、？ = `a1 48`**
  （早先误用 Big5 `a1 49` 找 ★，导致「游戏里没有 ★」的假结论，已纠正。）

**结论 2：绘制代码 = 2019 工具遗产的 `codePersonCharNum`（140 字节）。**
- 源码：`recovery/reference-2019/InsertAsm.h:10230`（`DWORD dwAddrPersonCharNum = dwAddrStackBottom + 0x6900;`）
- companion 镜像：`src/generated/companion_data.bin` 偏移 `0x6F88`（companion VA `0x425F88`）
- 运行时落点：`RuntimeInjection2026.cpp:415` `writePatch(dword_42F270, &codePersonCharNum, 0x8Cu);`
  ⇒ 写入**注入分配区 `allocationBase + 0x6960`**（不是游戏 `0x425F88`！）
- 进入路径：descriptor `0x426028`（`{target=0x451A5C, len=5, source=0x426014}`）把游戏
  `0x451A5C` 的 `mov edx,[edi]; mov eax,[edx-8]` 换成 `call 0xA6960`；
  descriptor `0x427890`（`{target=0x50500A, len=10}`）再补一条 `call 0xA6960`。
- ⚠️ **`src/generated/PatchCatalog.h` 的 `StaticWrite` 字段顺序是
  `{callVA, functionVA, targetVA, sourceVA, size, bytes}`**，
  所以那行 `{0x0040C163, 0x00409E10, 0x000A6960, 0x00425F88, 140, ...}`
  的 **target 是 `0xA6960`（分配区）、source 是 `0x425F88`（companion 镜像位置）**。
  早先误读成「覆盖游戏 0x425F88」是错的。`PatchCatalog.h` 也没有被任何 `src/` 文件
  include，只是审计产物（`tools/reconstruct.py` 生成）。

**结论 3：原版 140 字节块的判定逻辑（已完整反汇编）。**
```
mov  ebp,[edi]              ; 文本缓冲
mov  ecx,ebx / test cl,1    ; 守卫：ebx 必须为偶
shr  cl,1 ; cmp cl,'1'/'9'  ; 守卫：系统号 1..9
cmp  byte [esp+0x2C],'8'    ; 守卫：必须是「熟练度」那一行
add  byte [esp+0x5C],4      ; 给图标预留 4 字节
mov  edi,0x20202020         ; 默认 4 个空格（无图标）
mov  eax,[esi+0xE8]         ; 原生「精通」标志位
shr  eax,cl ; and al,1
mov  edi,0x2020B8A1         ; "  " + a1 b8 = ☆   ← 原生精通
; 再查 companion 自己的等级表 0xA4000[officer] 的 2bit*系 == 3
mov  edi,0x2020B7A1         ; "  " + a1 b7 = ◎   ← 原生精进（覆盖 ☆）
```
**它和熟练度上限数值毫无关系**：☆ 由原生「精通」位决定，◎ 由等级表第 4 档决定。

**结论 4：用户确认的口径（2026-11-07）**
1. **配置优先，未配置回落原生**：用户给某武将某系配了熟练度上限或发动间隔 →
   用 companion 自己的图标；没配 → 沿用原生标记（精通 → ☆，精进 → ◎）。
2. **两边都用空心 ☆ U+2606**（与 `build/res/tags.PNG` 截图一致）。
3. **任意熟练度上限设计保留**，不做 1300/1600 分档。

**实现（已完成，2026-11-07）**
- 新增 `src/runtime/ProficiencyMark2026.h`：修改器界面与游戏内注入**共用同一套规则**
  （`ProficiencyMarkFor` / `ProficiencyMarkPrefix2026`），两边不可能再漂移。
  五档：`unconfigured / none / star(☆) / refined(◎) / interval(○)`，
  前缀字节分别为 `0 / 0x20202020 / 0x2020B8A1 / 0x2020B7A1 / 0x2020B3A1`。
- 新增 `Runtime2026::applyProficiencyMarks()`（`src/runtime/Runtime2026.cpp`）：
  宿主算一张 `uint32[800*9]` 前缀表上传，再写判定块（查表 + 原生回落），
  **172 字节（后来因 rel8 溢出修成 188 字节，见 §4.11.1）**，
  并在 `allocationBase+0x6960` 放一个 5 字节 `E9` 跳板指向新块
  （172 > 160 字节的原槽位，会压到 `codeMoveGuardTroop@0x6A00`，故用跳板）。
  块字节由 按 capstone 逐条核对；
  两个运行时地址（图标表、等级表 `allocationBase+0x4000`）以 disp32 打在
  块偏移 `0x3F` 与 `0x70`。
- **一个格子都没配时不替换块**：原版 140 字节块画的本来就是原生标记，语义一致，
  且这样不会多分配内存（否则 undo 的 release 计数会从 1 变 2，
  `runtime_test.cpp` 的 `undo releases allocation and handle` 会挂）。
- 修改器侧 `Pages.cpp::proficiencyMark()` 改为复用共享规则，
  **实心 ★ U+2605 改成空心 ☆ U+2606**（用户要求两边一致）。
- 回归测试：`tests/runtime_test.cpp` 新增 `--proficiency-mark-injection`
  （断言图标表内容、**188** 字节块的关键指令、CP950 字形、跳板），
  `--proficiency-marker` 改为断言共享规则。

**注入空间核算（用户特别要求）**
- 注入分配区 `0xA0000..0xB0000` 共 45KB，已占 33.2KB；
  空闲连续段包括 `0x095E..0x10000`（**22.2KB**）与 `0x094EC..0x09800`（788B）。
- 图标表 `uint32[800*9]` = 28.8KB，**放不进分配区**，所以它和判定块
  一起放在**新分配的一块内存**里（`applyProficiencyMarks` 自己 allocate），
  分配区只放 5 字节跳板。**空间充足，不需要新方案。**

**熟练度 / 等级 / 阈值数据模型（不变，仍然有效）**

| 项 | 地址 |
| --- | --- |
| 熟练度存储 | `officer + 0xA4 + 系*2`（word）；getter `0x44C940` / setter `0x44C960` / limadd `0x44C980` |
| 游戏原版上限 1000 | `0x406A70`（`cmp eax,0x3E8`）；调用者 `0x408687`、`0x409854` |
| 等级 1/2/3/4 | `0x409220` / `0x409510`；阈值表 `0x64BD00`、`0x64BCC4`、`0x64BC88`、`0x64BD3C` |
| 等级 3（精进） | `0x40985F`（`mov dword [esi+4], edx+3`）；阈值 `0x64BD54`=80、`0x64BD5C`=120 |
| **1500 阈值** | `0x44E840`（唯一调用者 `0x450323`）：9 系求和 → `0x44E9E0` `cmp ebx,0x5DC` → `jge 0x44EA97` |
| 精通/精进标志位 | `dword [officer+0xE8]`；set `0x44C8A0` / clear `0x44C8B0` / test `0x44C8D0` |
| 标志位含义 | `PersonSnapshot.cpp:14`：`row.good=(word(0xE8)>>3)&0x1FF` ⇒ **第 3..11 位是 9 个系的精通位** |

**companion 的上限注入块是 93 字节的 companion 自有代码**
（`recovery/evidence/payload_disassembly/004256B8_000A2190_5D.asm`）：
读 `ds:0A4000h[eax*4]`（companion 自有 blob 的每武将等级表）→ 取该系 2-bit 等级
→ `cmp al,3`（等级 3 = 精进）→ `mov ax, ds:43FFDEh`（精进上限，默认 1500）→
`jmp [0x5FF30C]`（sancalc `limadd`）。
`0xA4000` = `state+3700`（`ObjectOffsets[1]=3700` / `ObjectValues[1]=0xA4000`），0xC80 字节。
`0x43FFDE`（`m_wMaxExperience`）**只被这 93 字节读**。
2019 版工具 `recovery/reference-2019/San9PKHardDlg.cpp` 完全同构
（`m_dwAddrPersonConfigInfo = 0xA4000`、默认 `0x3FEAA`、`m_dwMaxExperienceAddr(0x43FFDE)`、
`m_wMaxExperience = 1500`）⇒ **companion 忠实复刻了原版作弊器的数据模型**。
companion 现在更进一步：`applyProficiencyCap()` 用 `uint16[800][9]` 表
（`Runtime2026.cpp`）替换了 1500 这个固定上限，**支持任意熟练度上限**。

**实机待验**：游戏武将列表里打开有熟练度的界面，确认
① 配置过的格子显示 ☆/◎/○；② 没配置的格子仍显示原生 ☆/◎；③ 无崩溃。
（**2026-11-07 的实机崩溃已修，见 §4.11；复验仍未做。**）

---

### 4.11 图标崩溃修复 + 逐功能开关 + 玩家蛮族阵型（2026-11-07）

> 本节是 2026-11-07 下午的一轮工作。详细逆向记录见
> **`docs/KNOWLEDGE_BASE.md`**（新的单一可信来源），逐功能实机清单见
> **`docs/LIVE_TEST_PLAN.md`**。

#### 4.11.1 图标崩溃（已修，根因用转储坐实）

**症状**：用户打开武将列表就崩。

**转储**：`%LOCALAPPDATA%\CrashDumps\San9PK.exe.29292.dmp`（11:26）
- 异常码 `0xC0000005`，读地址 `0x02227028`
- 而我们的图标块在 `0x02227080`
- `0x02227028 = 0x02227080 + 0x09 − 97` —— **正是那个守卫跳转的目标**

**根因**：172 字节块里 4 个守卫跳转到块尾，位移 159 / 152 / 147 / 140
**全部 > 127**。x86 的 rel8 是**有符号**的，`0x9F` 被解释成 **−97**
→ 跳到块外 88 字节 → 访问违例。

> 原版 140 字节块的位移**恰好**是 127（有符号上限），纯属侥幸。

**修复**：4 个守卫跳转改**近跳转**（`0F 84` / `0F 8C` / `0F 8F`，rel32），
块 172 → **188 字节**。disp32 偏移：图标表 @ **0x4F**，等级表 @ **0x80**。

> 修复过程中把 disp32 写到 `0x4B` 而不是 `0x4F`，**被新增的测试断言当场抓住** → 改成 `0x4F`。
> 这说明注入块的每个字节偏移都必须有断言。

**新增工具** `tools/check_injection_blocks.py`：读 `src/runtime/Runtime2026.cpp` 里的
`const BYTE xxx[] = {...}`，capstone 反汇编，断言每条跳转
① 落在指令边界 ② 在块内 ③ rel32 占位（全 0）跳过。
当前 **11 个块，0 个问题**。
（坑：注释里的 hex 字面量会被误读 → 已加 `strip_comments`；
游戏代码片段（growth boost 的 `original[6]`）单独跳过。）

#### 4.11.2 删除错误的培养时长写入

`0x14FA656` 是**猜出来的**地址，而且是错的：RVA `0xFA656` 落在 **.text** 段
→ 它是**游戏代码的一个字节**，不是变量。
之前每次应用配置都往那儿写 9，**一直在改坏游戏代码**。

2019 版工具**根本没有**培养时长功能（只有 `m_dwTrainNumAddr` `0x40CAEF` /
`m_dwTrainNumAddr2` `0x40BF25` / `m_dwOldTrainNum` 50）。

**处理**：删除该写入，配置项保留但不生效，日志明说。

#### 4.11.3 逐功能开关（13 个）

| 项 | 内容 |
| --- | --- |
| 文件 | `San9Features.ini`（仓库根目录；运行时与 exe 同目录） |
| 位置 | 段 `[Features]`；值**只认精确的 `0` / `1`**，其它值一律当 **off**（手误不应该打开功能）；缺键 = 默认 |
| 默认 | 全部 **on** ⇒ 空文件 ≡ 开关引入前的行为 |
| 时机 | **启动时读一次**，运行期不再写回；改完必须重启修改器 |
| 语义 | off = **完全不注入**（不是「注入了但不起作用」） |
| 日志 | `FEATURES: file=... summary=1 0 1 ...`（顺序与 ini 一致）+ 每个被关的功能一行 `FEATURE xxx : off, nothing is injected` |

开关清单：
`deathGradient / growthBoost / cultivateTrainNum / cultivateBasicAbility /
cultivateDuration / proficiencyCap / proficiencyMark / activationBoost /
criticalHit / interval / barbarianFormation / computerWorkTiming / skipSiegeSelection`

> ⚠️ `cultivateDuration` 是**占位**，功能未实现，开了也不生效。
> ⚠️ `activationBoost` / `criticalHit` / `interval` 这三个键名沿用了
> `OfficerPersonalization2026` 的字段名，实际语义分别是
> **把握概率 / 威力一定加倍 / 兵法发动间隔**。
> `FeatureSwitches2026.h` 的枚举注释是权威。

**新增/改动的文件**
- `src/runtime/FeatureSwitches2026.{h,cpp}`（新）
- `src/runtime/AppPaths.h`：`featureSwitchesFile()`
- `src/app/application/Controller.{h,cpp}`：`loadFeatureSwitches(path)`
- `src/app/ui/MainWindow.cpp`：启动时（`!smoke` 分支）调用
- `src/runtime/Runtime2026.{h,cpp}`：各 `apply*` 开头加开关守卫；
  `applyOfficerPersonalization` **拆成 3 个独立函数**
  `applyActivationBoost` / `applyCriticalHit` / `applyInterval`
- `src/runtime/RuntimeInjection2026.cpp`：`0x40CAEF`/`0x40BF25` 归 `cultivateTrainNum`；
  `codeTrainBasicAbility` 的 `sub`→`add` 归 `cultivateBasicAbility`；
  `NeglectChongCheAddr` 归 `skipSiegeSelection`
- `build-config/Runtime.vcxproj`：加入 `FeatureSwitches2026.cpp`
- `build-config/Companion.vcxproj`：把 `San9Features.ini` 复制到输出目录

#### 4.11.4 玩家蛮族阵型（已实现）

**逆向结论（逐字节核对过）**

游戏的「阵型属性查表函数」入口是 **`0x4FB560`**：

```
0x4FB560  mov  ecx,[esp+4]              ; 形参：阵型编号
0x4FB564  or   eax,0xFFFFFFFF           ; 越界 -> -1
0x4FB567  cmp  ecx,0x1B
0x4FB56A  ja   0x4FB5D8
0x4FB56C  jmp  dword ptr [ecx*4+0x4FB5DC]   ; 28 项跳转表
```

跳转表在 **`0x4FB5DC`**，28 项，每项指向一段 `mov eax,imm32; ret`：

| case | 返回 | case | 返回 |
|---|---|---|---|
| 0 | 0x39C | 14 | **0x3C3** |
| 1 | 0x39F | 15..19 | −1 |
| 2 | 0x396 | 20 | 0x3AB |
| 3 | 0x399 | 21 | 0x3AE |
| 4 | 0x3A2 | 22 | 0x3B1 |
| 5 | 0x3A5 | 23 | 0x3B4 |
| 6 | 0x393 | 24..26 | −1 |
| 7 | 0x3A8 | 27 | **0x3C3** |
| 8 | 0x3BD | | |
| 9 | 0x3B7 | | |
| 10 | 0x3BA | | |
| 11 | 0x3C0 | | |
| 12 | **0x3C3** | | |
| 13 | **0x3C3** | | |

- 12 / 13 / 14 **共用**同一个桩 `0x4FB5D3`（返回 `0x3C3`，异族专用属性）
- case 27 也用 `0x4FB5D3`
- 15..19 与 24..26 指向 `0x4FB5D8`（返回 −1）
- **唯一调用点**：`0x4FBEB6 call 0x4FB560`（形参 `ebx` 是阵型编号）
- 消费方 `0x4FBE20`（stdcall，2 参数）：`cmp ebx,0xC` 与 `cmp ebx,0x18`
  两个守卫会**跳过**处理 —— 正好是 12 与 24

**Shape.exe（第三方修改器）的做法**（已逐字节核对）
- `0x4701E5`：1 字节 `0x0B`→`0x15`（`push 0x0B` 的 imm8）—— 与阵型无关
- `0x470205`：1 字节 `0x17`→`0x14`（`push 0x17` 的 imm8）—— 与阵型无关
- `0x4FB564`：8 字节 `B8 CC 03 00 00 90 90 90` = `mov eax,0x3CC` + 3 nop
  → **整个函数恒返回 0x3CC，所有阵型属性都被抹平**

**我们的实现（比 Shape.exe 温和）**
- 只把跳转表的 **12/13/14 三项**改指向我们自己的 6 字节桩
  `B8 CC 03 00 00 C3`（`mov eax,0x3CC; ret`）
- 其余 25 项**原样不动** → 普通阵型的属性完全不受影响
- 桩放在 `AllocationBase + 0xA000`（分配区 11 页 = 0xB000，
  已用到 `0x6960+188`，所以 `0xA000` 是空的）
- 写 3 个 dword 到 `0x4FB5DC + index*4` → `0x4FB60C` / `0x4FB610` / `0x4FB614`
- **这两处补丁写的是游戏的 `.text`**，需要 `VirtualProtectEx` 改保护属性

**界面字幕**：`assets/ui/dialogs.rc` 控制 1047 由「启用电脑蛮族阵型」
改为「**启用玩家蛮族阵型**」（`tools/dialog_customizations.py` 同步）。

> ⚠️ **`assets/ui/dialogs.rc` 不能直接用 `tools/reconstruct.py` 重新生成**：
> 生成器会丢掉手工做过的界面布局改动（对话框 132 的「武将个性化」区块会消失），
> 并把简体「斗舰」变回繁体「鬥舰」。改字幕请**直接编辑 `.rc`**。

**回归测试**：`--barbarian-formation`（开关关时跳转表不动；
开关开时只有 12/13/14 被改写，且指向返回 `0x3CC` 的桩）。

#### 4.11.5 新增文档

| 文件 | 内容 |
| --- | --- |
| `docs/KNOWLEDGE_BASE.md` | **逆向结论的单一可信来源**：地址→用途→证据→置信度总表、数据模型、CP950 字形表、**x86 注入陷阱库**、已被推翻的结论、注入代码地图、实机验证状态表 |
| `docs/LIVE_TEST_PLAN.md` | 逐功能实机验证清单：每项「开哪些开关 / 期望结果 / 怎么判断」+ 二分法定位步骤 |

#### 4.11.6 工作纪律（本轮的教训）

- ❌ **绝不用 `sed -i` 改 CRLF 文件** —— 会把整行尾变成 LF。
  `tests/runtime_test.cpp` 是 CRLF，用 Edit 工具或 Python 显式处理 `\r\n`。
- ❌ **绝不 `git checkout` 未提交的工作** —— 会永久丢失。
  （2026-11-07 就因为 `git checkout tests/runtime_test.cpp` 丢掉了整段未提交的
  `proficiencyMarkInjectionTest`，最后靠 一个一次性 Python 补丁脚本 重写恢复。）
- ❌ **不要自己 `git commit` / `git push`** —— 用户自己提交。

---

## 5. 构建与测试

```bash
cd "G:/aiprojects/game/san9-companion"

# 构建（先杀进程，见 §4.2）
powershell -NoProfile -ExecutionPolicy Bypass -File ./Build.ps1 -Configuration Debug

# 离线测试
./build/tests/Debug/San9RuntimeTests.exe --all assets/config

# UI smoke（现已转绿，见 §4.1）
powershell -NoProfile -ExecutionPolicy Bypass -File ./Build.ps1 -Configuration Debug -UiSmoke

# 发布包（静态 CRT，可直接外发，无需安装 VC 运行库）
powershell -NoProfile -ExecutionPolicy Bypass -File ./tools/Build-App.ps1 -Configuration Publish -OfflineTests
```

> `tools/Build-App.ps1` 的 `-UiSmoke` 分支用的是 `Start-Process -Wait`，若 smoke 因故不退出
> 会**把构建永久挂住**且无任何提示。已改为 `WaitForExit(120000)` + 超时 kill + throw。

产出：

| 产物 | 路径 |
| --- | --- |
| 主程序 | `build/app/Debug/San9Companion.exe` |
| 测试 | `build/tests/Debug/San9RuntimeTests.exe` |
| 运行时库 | `build/runtime/Debug/San9Runtime.lib` |

测试覆盖：3536 case 穷举 fault sweep、5 个生命周期场景、20 组格式/往返/边界检查。末行输出：

```
2026 offline runtime checks passed; no game process or GUI was accessed.
```

> 约定：项目里所有 Bash 命令都要带 `cd "G:/aiprojects/game/san9-companion" &&` 前缀——Bash 工具的 cwd 默认为 `G:\aiprojects`。

### 5.1 崩溃 #4 修复后的验收记录（2026-11-01）

| 步骤 | 命令 | 结果 |
| --- | --- | --- |
| 构建 Debug | `Build.ps1 -Configuration Debug` | ✅ |
| 构建 Release | `Build.ps1 -Configuration Release` | ✅ |
| 构建 Publish | `tools/Build-App.ps1 -Configuration Publish -OfflineTests` | ✅ |
| 离线测试（Debug） | `San9RuntimeTests.exe --all assets/config` | ✅ `exit=0` |
| 离线测试（Release） | `San9RuntimeTests.exe --all assets/config` | ✅ `exit=0` |
| UI smoke | `San9Companion.exe --ui-smoke` | ✅ `exit=0` |
| 定向测试 | `San9RuntimeTests.exe --id-table-write-guard` | ✅ `exit=0` |
| 试用包 | 复制 Publish exe + 6 个配置到 `out/trial/` | ✅ 已刷新 |

**反向验证**（证明新测试真的能挡住回归）：临时把 `0x6613EE` 的写入加回
`applyCultivate()` → 重新构建 → `--id-table-write-guard` 报
`FAILED: no recorded write overlaps a static id table region`（`exit=1`）→ 恢复源码 → 重新构建 → 全绿。

> 注意：Windows 下 PowerShell 管道会吞掉 `exit` 码，**必须用 `; echo "exit=$?"` 单独取**
> （或重定向到文件再 `echo $?`），否则会看到空的 `exit=` 而误以为失败。

---

## 6. 工作区状态（全部未提交）

用户明确要求**不提交本地文件**。

**已修改（25 个文件）**：

```
CHANGELOG.md
assets/config/San9PKHardPerson.txt
assets/ui/dialogs.rc
assets/ui/resource_ids.h
docs/DESIGN.md
src/app/platform/NativeWidgets.cpp
src/app/platform/NativeWidgets.h
src/app/ui/MainWindow.cpp
src/app/ui/Pages.cpp
src/generated/InjectionNames2026.h
src/generated/parameters.json
src/model/InjectionNames2026.json
src/model/PersonConfig.cpp
src/model/PersonConfig.h
src/runtime/AppPaths.h            ← 新增 preferencesFile()
src/runtime/GameText.cpp
src/runtime/GameText.h
src/runtime/OfficerPersonalization2026.cpp
src/runtime/OfficerPersonalization2026.h
src/runtime/Runtime2026.cpp
src/runtime/Runtime2026.h
src/runtime/RuntimeInjection2026.cpp
tests/runtime_test.cpp
tools/Build-App.ps1               ← smoke 超时保护
tools/Write-BuildInfo.ps1
```

**未跟踪新增（20 个）**：

```
assets/config/San9PKHardCheat.txt
docs/REQUIREMENTS_1_4.md      ← 代码级实现文档
docs/HANDOVER.md              ← 本文件
docs/SESSION_NOTE.md          ← 会话速记
docs/_CRASH4_HANDOFF_2026-10-31.md  ← 崩溃 #4 证据链
docs/_INJECTION_FIX_NOTES.md         ← 注入代码修复记录
restore_deepseek.ps1
setup_relay.ps1
tools/show_env.ps1
武将个性化.csv
武将个性化_new.csv
```

> ⚠️ **2026-11-01 更新**：`tools/_*.py` 一次性分析脚本**已全部删除**（累计 52 个，
> 2026-10-31 删 30 个、2026-11-01 再删 22 个）。原先列在下面的
> `tools/_addr_check.py`、`_dumporder.py`、`_dumpspec.py`、`_prof_check.py`、
> `_review_check{,2,3}.py`、`_verify_cap.py`、`_verify_growth.py`、`_verify_hooks.py`、
> `_verify_payloads.py` **均已不存在**。可复现的审计工具保留：
> `tools/audit_call_contracts.py`、`tools/audit_unguarded_deref.py`。
>
> 交付产物在 `out/` 下，**不入版本控制**：
> `out/trial/`（试用包：exe + 6 个配置 txt + `使用说明.txt`）、
> `out/app/{Debug,Publish}.log`（构建日志）。
> `smoke_trace.txt` 已随调试打点一并删除。
> `武将提拔/res_135.bin` **保留**（供存档路线 RE T3 解密用）；
> `res_144.bin`、`plugin_try.bin`、`doc_text.txt` 已于 2026-11-01 删除。

**git 分支**：`dev`（`main` 存在但未同步）。最近提交：

```
796f09c docs: record critical-hit address in design doc
ea97605 chore: remove temporary analysis scripts
5058c4e feat(officer): add per-officer always-critical-hit (威力加倍)
dac1338 feat(officer): add per-officer personalization editing UI
e84ab3c feat(officer): add personalization persistence, design doc and tests
```

---

## 7. 相关文档

| 文件 | 内容 |
| --- | --- |
| `docs/REQUIREMENTS_1_4.md` | **代码级实现文档**。需求 1~4 的设计、内存修改位置、注入代码、已知坑、审查结论。679 行 / 47 KB。与本文件配合阅读。 |
| `docs/DESIGN.md` | 架构设计 |
| `docs/architecture.md` / `configuration.md` / `development.md` | 架构 / 配置 / 开发流程 |
| `docs/optimizations.md` | 优化记录（19 KB） |
| `docs/ida.md` | IDA 逆向笔记 |
| `docs/requirement4_addresses.md` | 需求 4 的 hook 地址 |
| `docs/_CRASH4_HANDOFF_2026-10-31.md` | **崩溃 #4 的完整证据链**（12:48 dump 逐字节对照、5 张 id 表布局、`0x6613EC` 定址推导、§5.5 已确认方案、§5.8 正确实现、§12.2 逐条待办）。2026-11-01 的修复即按此执行。 |
| `docs/_INJECTION_FIX_NOTES.md` | 注入代码修复记录（§12 崩溃 #3 与 329 块全量排查、§13 崩溃 #4） |
| `CHANGELOG.md` | 改动记录，格式遵循 Keep a Changelog |
| `AGENTS.md` | 工作区约定 |
| `PLAN.md` | 计划 |

---

## 8. 变更日志（本文件）

| 日期 | 变更 |
| --- | --- |
| 2026-10-25 | 建立本文件。记录：构建/测试状态、23 改 + 20 新增的未提交工作区、§11.17.2 待填、UI smoke 断点、P1-1/P1-2 已修（交叉核对）、P2 兵法表 39 已证实待写文档、以及旧会话无法 resume 的技术原因。 |
| 2026-10-25 | §3 未完成事项加状态标记（`[ ]`/`[~]`/`[x]`），新增 §3.6 建议执行顺序（5 项：§11.17.2 → P2 文档修正 → UI smoke → 威力系数 hook → 清理调试产物）。 |
| 2026-10-25 | 新增 §1.1 启动提示词（可直接复制到新会话）、§1.2 上下文用量参考（旧会话峰值 164899 tokens 触发 context limit；当前会话 111929）。 |
| 2026-10-25 | **§11.17.2 计算准确性审查已补齐**（`docs/REQUIREMENTS_1_4.md`）。从 `agent-a323974.jsonl` 提取并逐条复核子 agent 中间推理，产出：1 个 **P0**（`Runtime2026.cpp` 四处注入代码的武将表基址立即数写成 `0x12588EE0`，应为 `0x1258EE0`，差 16 MB → 四类个性化实机全不生效）、8 条被复核否定的 agent 猜测、5 条待办。新增 §3.1.1 记录 P0 修复项；§3.2 标记完成；§3.6 建议顺序改为 P0 优先；§0 速览同步更新。 |
| 2026-10-25 | 交接材料自洽化修正：`REQUIREMENTS_1_4.md` 的 §0.5（审查进度指向过时章节）、§0.6（补 P0）、§0.4（文件计数 22/11 → 23/20）、§7（注入字节布局按**正确值**重写并加显著警示；顺带修正 `cmp edx` 0x28→0x26）。`HANDOVER.md` §1.1 重写为完整交接提示词，明确回答「新会话要不要读 REQUIREMENTS_1_4.md」= 要，但按需读章节不全文读。 |
| 2026-10-25 | **P0 修复落地**：`src/runtime/Runtime2026.cpp` 四处注入代码的 `sub eax` 立即数 `0x58,0x12` → `0x25,0x01`（`0x12588EE0` → `0x1258EE0`）。证据链**独立复核**通过：2019 参考 `InsertAsm.h:3195` 用 `E0 8E 25 01`；`San9PK.exe` 中 `E0 8E 25 01` 出现 14 次、`E0 8E 58 12` 出现 **0** 次。新增 `tests/runtime_test.cpp` 的 `injectionCodeBytesTest`（断言四个注入块偏移 0x04..0x07 == `E0 8E 25 01`，并全局拒绝陈旧序列 `E0 8E 58 12`）。**反向验证**：改回错误立即数 → 测试失败；恢复 → `--all` 全套 `exit=0`。 |
| 2026-10-25 | **UI smoke 根因定位并修复**：旧结论「崩在 `MainWindow` 构造内部」**已推翻**——实测 trace 实际推进到 `main window hidden`，真实断点是 `Dialog::tooltip()` 更新分支里对 `TTM_UPDATETIPTEXTW` **返回值**的成功判断。该消息无返回值契约（`main` 分支原代码亦不检查，只检查有 BOOL 契约的 `TTM_ADDTOOLW`），故稳定误报 `cannot update tooltip` → `wWinMain` 捕获 → smoke 退出码 1。已删掉该判断，**保留** dev 分支的稳定缓冲所有权设计（`replacement` / `std::move`，修 `main` 分支原地改写 string 造成的悬垂 `lpszText` 风险）。§3.5 / §4.1 已改写。**修复尚未复验**（需跑一次 `--ui-smoke`）。 |
| 2026-10-25 | **UI smoke 复验通过**：`--ui-smoke` 由 `exit=1` 转为 **`exit=0`**（修复前为 `exit=1`）。`smokeTooltipUpdates` 的 `TTM_GETTEXTW` 回读断言一并通过，确认 tooltip 更新**确实生效**，此前只是返回值判断误报。§3.5 / §4.1 / §0 / §3.6 状态同步更新。 |
| 2026-10-25 | 残留进程核实：PID 34236 已退出；PID 29176 为**僵尸进程**（Threads=0、WS=20 KB、父进程已死），`taskkill`/`Kill()` 均「拒绝访问」（非管理员，`-Verb RunAs` 亦未成功）。**实测该进程不持有 exe 句柄**（`FileShare.None` 独占打开成功、覆盖写与删除均成功）→ 构建不被阻塞。§4.2 与 §1.1 提示词据此改写。 |
| 2026-10-25 | **调试产物清理**：`smoke_trace` 打点**共 11 处**（`MainWindow.cpp` 9 + `Pages.cpp` 2）全部删除，`smoke_trace.txt` 随之删除。`MainWindow.cpp` 的整个 diff 就是这些打点，用 `git checkout --` 整体还原；`Pages.cpp` 有大量真实功能改动，只定点删了那 2 行。 |
| 2026-10-25 | **命令行解析缺陷修复**：`wWinMain` 原用整串精确比较 `lpCmdLine == L"--ui-smoke"`，而 `Start-Process -ArgumentList` 产生的命令行**带尾随空格** -> 比较失败 -> smoke 静默失效、弹模态窗口**永久挂起**（本次实测复现）。改为按空白分词后逐个精确匹配（`hasSwitch`）。同理把 `traceWritesRequested()` 的子串查找改为精确分词匹配（否则 `--log-writes-off` 会被误判为开启）。`tools/Build-App.ps1` 的 `-UiSmoke` 分支由 `Start-Process -Wait` 改为 `WaitForExit(120000)` + 超时 kill + throw，避免 smoke 挂死把构建永久卡住。 |
| 2026-10-25 | **详细日志开关落地**（§4.5 已重写）：新增设置页「扩展设置 → 详细日志（逐次记录内存写入，排错用）」复选框（`IDC_DETAILED_LOG = 1251`，由 `Dialog::settingsPages` **运行时动态创建**，无需改 `.rc`），勾选**立即生效**并持久化到 `San9PKHardSettings.ini` 的 `[Diagnostics] LogWrites`；另有 `--log-writes` 命令行与 `SAN9_LOG_WRITES=1` 环境变量两条途径。`appendRuntimeLogTrace` 改为**每次调用实时求值**（原 `static const bool` 会把开关冻结在首次调用时）。日志路径显示在状态栏悬停提示与启动横幅里。四类个性化注入新增埋点（注入武将数 / hook 地址 / 代码地址 / `officerTable=0x1258EE0`），这是**实机验证 P0 修复的唯一手段**。 |
| 2026-10-25 | **Publish 发布包交付**：`tools/Build-App.ps1 -Configuration Publish -OfflineTests` 构建通过，离线测试全绿，`--ui-smoke` `exit=0`。Publish 为静态 CRT（`MSVCP140`/`VCRUNTIME140`/`api-ms-win-crt`/`ucrtbase` 导入数实测均为 0），可直接外发、无需安装 VC 运行库。试用包装于 `out/trial/`（`San9Companion.exe` + 6 个 `San9PKHard*.txt` + `使用说明.txt`）。`San9PK.exe`（游戏本体，2.6 MB）属用户自有文件，**未随包分发**，`使用说明.txt` 已写明放置方式。 |
| 2026-10-25 | 文档回写：§0 速览、§1.1 启动提示词、§4.5 运行时日志、§5 构建与测试、§6 工作区状态（25 改 + 20 新增）、`docs/SESSION_NOTE.md` 全部同步到当前状态。 |
| 2026-10-26 | **注入代码第二轮逆向（重大发现）**：逆向原版 `sanpkcheat/SanIXSpy.dll`（9728 字节，导出 `_IsSpyActivated@0` / `_SetupSpy@4` / `_UnloadSpy@4`），发现其 patch 表（`0x10004000`，29 条 × `0x1C`）**与本版 `San9PK.exe` 逐字节吻合 29/29**（`tools/spy_match.py`）→ 原版 stub 即权威参照。逐条反汇编其 3 个关键 stub 后确定：①武将索引的正确取法是 `movzx eax, word [CPersonData+4]`（三条独立证据链收敛：RTTI `0x6060E8` = `"CPersonData"`；`0x44F830`→`0x446800` 把表下标写进 `record+4` 低 16 位；原版 stub 同款），**计划中「用 `dword[+0x94]`」是错的**（那是 `CUnitData` 的字段，间隔 hook 现场的 `esi` 是 `CPersonData`，无此字段 → 野读）；②把握概率 hook 点必须从 `0x4FEB95`（esi 已是常数 10）移到 `0x4FEB90`（esi 才是单位对象）；③间隔 hook 应在 `0x4FF08B` 且只覆盖到 `push eax` 之前，否则会吞掉 `call 0x4FB760`；④两个寻址错误（`0F B6 0C 05` 索引被乘 2、`0F B6 0C 01` 多 +1 偏移）应统一为 `0F B6 04 01`。另查明**兵法表是 41 条**（`0x453720`: `cmp eax,0x29`）→ `docs/HANDOVER.md` §3.3 上一轮「39 条已证实」的结论**是错的，已纠正**；**武将数 800 是对的**（存档只装载前 800 条，`0x45415B`: `cmp edi,0x320`）。新增 §3.7（含 B1~B9 逐项清单与待决策项）、`docs/_INJECTION_FIX_NOTES.md`（完整证据）、`tools/` 下 15 个可复现脚本。**代码尚未修改**；`out/trial/` 当前包会崩游戏，已在 §0 标记不可用。 |
| 2026-10-26 | §0 速览新增「注入代码第二轮逆向」行、把「试用包」改为 ⚠️ 不可用；§3.1.1 加警示（上一轮只修了立即数笔误，整体仍错）；§3.3 标题与结论纠正（39→41，并说明 800 无需改）；§3.6 建议执行顺序重排（新第 1 项 = 重写注入代码）。 |
| 2026-10-27 | **注入代码 P0 重写完成**：`Runtime2026.cpp` 四个注入块全部重写，capstone 字节级验证（`tools/asm_check.py`）。武将索引统一 `word[ptr+4]`（B1/B8）；间隔 hook `0x4FF08B` 仅覆盖 5B、返回 `0x4FF090` 保留原 `call 0x4FB760`（B5）；把握 hook 移到 `0x4FEB90`、威力 hook 改在 `0x50603B`（B3）；`installPerOfficerHook` 增加 `returnAddress` 参数、回写精确 originalSize（B4）；间隔 `intervalFlat` 用 0xFF 标记默认走原版 `idiv`。`SkillConfig::Count` 39→41 + `layout state_size` 7692→7844 + `FormationFlags/Entries` 后移 7624/7625 + `SkillEntriesSize` 156→164 + `San9PKHardSkill.txt` 补两行 + `PatchImage::object_` 7844（B6）。`tests/runtime_test.cpp` 的 `injectionCodeBytesTest` 重写（断言 `word[ptr+4]` 索引读取、拒绝旧 `sub 0x1258EE0`；`test_runtime_generation.py` 布局契约、`tools/_addr_check.py`/`offline.py` STATE_SIZE、`skillConfigTest` 末端条数、`runtime_test.cpp` FormationFlags 常量引用同步）。**验证**：Debug 构建 + `--all assets/config` 全绿 + `--ui-smoke` `exit=0`；Publish 构建通过并**重新出 `out/trial/`**（新 exe + 6 配置 + 使用说明）。新增 §3.7.6 已落地修复，§0/§3.3/§3.6 同步。 |
| 2026-10-27 | **`0x43EF44` 威力系数调用链逆向完成（验证结论、无代码改动）**：完整还原 `0x43EF44` 调用链 = `0x444C05(战斗指令 case, push 0x168)` → `0x443280(遍历 50 势力, 累加)` → `0x43F000(r2-r1)` → `0x43EDE0(含 0x43EF44)` + `0x43DA80`。实证：`0x43EDE0` 的 `this` 是**势力对象**（`0x453100` 访问器 = `0x1253C38 + idx*0xD4`），`imul esi,[esp+0x8C]` 的乘数是**调用链上层系数参数**（`0x168`），`esi` 在 imul 前是魔数除法(/600)+`[0x5FF32C]`随机值——**全程无武将对象**。→ **确认 `0x43EF44` 无法做 per-officer 威力系数**；原版因此在 `0x100014E0` 用全局 double 倍率统一缩放。完整证据见 `G:\aiprojects\game\san9pkme\06_43EF44_callchain.md`；§3.1.2 与 `docs/DESIGN.md` 已同步补注。后续接续可选方向（仅记录）：另找携带武将的威力路径 / 改为全局倍率语义 / 实机确认 `0x43EDE0` 计算值的属性用途。 |
| 2026-10-30 | **修复两个注入 stub 的 E9 跳转重定位 bug + 增加每 hook 详细日志（已构建通过，实机仍崩溃，见下一条）**：① `applyOfficerPersonalization()` 的**间隔 hook（0x4FF08B）stub** 两条 `jmp 0x4FF090` 的 rel32 写入偏移写错——旧代码 `memcpy(code+0x39,...)`/`code+0x48` 覆盖了 `E9` opcode 本身，正确应为 `code+0x3A`/`code+0x49`（E9@0x39/0x48，rel32 在其后 1 字节）。② `criticalHit` stub（`criticalCode`）的 **found 分支 `jmp 0x506040`（E9@0x41, rel32@0x42）从未被重定位**（`installPerOfficerHook` 只 patch 末尾那一个 jmp），已新增可选参数 `extraJumpRelOffset` 并在 critical 调用传 `0x42`。③ 日志增强：新增 `hexBytes()/hexDword()` helper，`installPerOfficerHook` 与 interval 安装日志现在输出 `arr/codeAddr/codeSize/arrayAddrOffset/return/jumpRel/jump@hook(5B E9)/code(完整 stub 字节)`。④ 其余注入块（growth boost / proficiency cap / activationBoost）逐一核对，跳转与立即数偏移均正确，无需改。⑤ 构建：Publish + `--all assets/config` 全绿 + `--ui-smoke` `exit=0`（杀死占用 exe 的旧 San9Companion 进程后重建）。capstone 离线验证 interval/critical 修复后所有跳转目标正确。 |
| 2026-10-30 | **实机崩溃根因定位 + 修复（纠正上一条的错误结论）**。崩溃偏移 `allocationBase+0x9A73` 的**真实根因是写空指针**（非上一条记的「读违例/ecx 野指针」，上一条用了错误偏移 `0xB894` 反汇编，正确偏移是 `unk_42C894` = companion_data 偏移 `0xD894`）。正确反汇编崩溃函数（`RuntimeInjection2026.cpp` 原 `uploadInjectionPayloads` 第 300 行 `writePatch(dword_42F038, &unk_42C894, 0x88u)` 上传的 136 字节原工具代码块）：`push ebp/esi/ebx; mov esi,[esi+0x298]; ... push 0x45a; mov ecx,esi; mov eax,0x5b71d0; call eax; mov ecx,[0x661140]; mov [eax+4],ecx` ← **崩溃点**。`0x5b71d0` 是游戏函数（遍历列表找 `[item+4]==0x45a` 的对象，找不到 `xor eax,eax` 返回 0），原工具代码**未判空**直接 `mov [eax+4],ecx` → eax=0 时写地址 4 → `0xc0000005` 写违例。**修复**：新增 `unk_42C894_nullGuard[140]`（在 `call eax` 后插入 `test eax,eax; je +9` 跳过 9 字节写操作），`writePatch` 改用它；capstone 验证 je 目标正确落在后续 `mov eax,[ebp+0x10]`（该指令重赋 eax 不依赖返回值，后续流程安全）。函数槽 0xA9A50→0xA9BE0 间隔 0x190 字节，140 字节不越界。**验证**：Debug 构建 + 离线测试全绿；Publish 构建通过。**实机验证待用户执行**（这是原工具逆向代码块的固有 bug，与四类个性化 hook 无关）。 |
| 2026-10-30 | **实机崩溃（已被上一条修复）：`San9PK.exe` 启动即退，异常 `0xc0000005`（写违例），崩溃偏移 = `allocationBase + 0x9A73`**。证据：Windows 事件日志（`E:\games\San9 Tc\San9PK.exe`），偏移 0x0a729a73（base 0x0a720000）与 0x18079a73（base 0x18070000），低 16 位恒 `0x9A73`。**纠正**：上一条（本表前一条）曾误记「读违例 + ecx 野指针 + `mov eax,[ecx+0xb8]`」，那是用错偏移反汇编的错误结论；正确结论见紧随其后的「根因定位+修复」条。日志文件：`build/app/Publish/San9Companion.log`。 |
| 2026-10-31 | **实机第二层崩溃 + 最终禁用 unk_42C894（原工具遗留代码块）**。判空修复（上一条）挡住 `0x9A73` 写空指针后，实机又崩在**游戏自身代码** `0x5B6754`（fault offset `0x001b6754`，= 游戏基址 0x400000+0x1b6754）。反汇编 `0x5B6750`：`mov eax,[esp+4]; movzx eax,byte[eax+0x14]` ← 读空指针 `[0+0x14]`；其调用者 `0x5B72F0`（游戏函数）`call 0x5b7050`(查找)后**未判空**直接 `call 0x5b6750`。**根因**：`unk_42C894`（`writePatch` 到 `allocationBase+0x9A50` 的原工具代码块）在查找 id=0x45a 成功后，把游戏对象 id 字段 `[eax+4]` 污染成 `[0x661140]`（启动早期未初始化值），导致游戏后续用被污染的 id 查找失败 → 空指针连锁崩溃。两次崩溃（0x9A73 写空指针 / 0x5B6754 读空指针）是**同一代码块的两个失败模式**。**最终修复**：按用户决策**禁用该代码块**——把 `writePatch(dword_42F038, &unk_42C894, 0x88u)` 替换为写入安全桩 `unk_42C894_disabled[5]`（`xor eax,eax; ret 4`，匹配原函数 ret 4 调用约定），不再执行任何污染逻辑。该代码块是原工具 SanIXPKCheat 的遗留功能，与四类武将个性化（hook 0x4FEB90/0x4FF08B/0x50603B，在 `applyOfficerPersonalization`）无关。**验证**：Debug 构建 + 离线测试全绿；Publish 构建通过。**实机验证待用户执行**。**⚠️ 2026-10-31 归因更正**：本条把 `0x5B6754` 归因于 `unk_42C894` 污染 id，并声称禁用它即可消除该崩溃——**这一归因已被 12:48 的 dump 证伪**（禁用之后仍然崩在同一地址）。`0x5B6754` 的真正根因见本表后一条「崩溃 #4」条目。禁用 `unk_42C894` 本身仍然正确（它消除了崩溃 #3 的返回值契约问题）。 |
| 2026-10-31 | **崩溃 #3 根因确认并修复（上一条的「修复」本身就是崩溃源）+ 全量排查完成**。实机崩溃 #3：异常地址 `0x45C8A0`、读取地址 `0x28`（异常信息 `ExceptionInformation=[0x0,0x28]`）、`ECX=0`、栈顶返回地址 `0x51FA42`。**关键澄清**：上一条把根因判为「`codeCheckTransportBlock` 未判空」是**错的**——返回地址 `0x51FA42` 指向**游戏原生** `0x51FA3D`，我们上传的块根本不在崩溃路径上。**真实根因**：上一条的禁用桩 `xor eax,eax; ret 4` **破坏了 `unk_42C894` 的返回值契约**（该块契约是「原样返回入口 eax」，收尾 `mov eax,ebp; ret 4`，`ebp`=入口 eax）。调用链 `0x51FA0E: call 0x453280`（返回对象指针）→ `0x51FA13: call 0xA9A50`（我们用 descriptor `0x42C930` 写入的跳转）→ `0x51FA3B: mov ecx,eax` → `0x51FA3D: call 0x45C8A0`（thiscall，首条 `mov eax,[ecx+0x28]`）；桩把 eax 抹成 0 → ecx=0 → 读 `0x28` → 访问违例。**修复**：桩改为 `C2 04 00`（`ret 4`）——eax 原样返回、栈弹出 1 个 stdcall 参数、不执行污染逻辑，语义与原块等价；并新增回归测试 `disabledOriginalToolBlockTest`（断言桩为 `ret 4` 且**不含** `33 C0`），**反向验证通过**（改回 `xor eax,eax` → 构建失败 → 恢复 → 全绿）。**取证方法学**：事件日志只给故障偏移、易误导；本轮启用 WER 本地完整转储（`HKCU\...\LocalDumps`, DumpType=2）拿到真实调用栈，一次定位（已写入 §4.5 新增小节）。**全量排查（举一反三）**：①**返回值契约**——329 个上传块中**只有 `unk_42C894` 一个的内容被我们替换过**，其余 328 个是原工具原样字节（仅重定位），故该崩溃类风险面仅此一点；②**未判空解引用**——`tools/audit_unguarded_deref.py` 扫出 7 处候选，逐条核实后 **6 处为误报**（数据区被反汇编成指令 / 绝对地址写 / `ret` 提前返回），唯一真实项 `unk_42B718` 的 `call 0x49C650` 经反汇编确认是**构造函数**（`mov eax,esi; ret 0x20`，必返回非空），**无新增隐患**；③2012 个被改写的游戏调用点中，无其他「返回值语义被改变」的实例。**新增工具**：`tools/audit_call_contracts.py`、`tools/audit_unguarded_deref.py`（可复现）。**验证**：Debug + Release 构建 + `--all assets/config` 离线测试**全绿**；Publish 构建通过；`--ui-smoke` `exit=0`。清理了 30 个临时分析脚本（`tools/_*.py`）与 `build/_*.txt`。**实机验证待用户执行**（判据见 §4.5）。 |
| 2026-11-01 | **崩溃 #4（`0x5B6754`）根因定位并修复 —— 这是我们自己的 bug，不是原工具的**。根因：`Runtime2026::applyCultivate()` 往 `0x6613EE` 写 1 字节，而 `0x6613EE` 是 id 表 A 首条记录的 **id dword `0x6613EC`（值 `0x000003E8`）的第 2 个字节**（读盘复核：`0x6613E8` 处字节为 `00 84 00 00 e8 03 00 00`）。写入后 id 1000 变成 `0x000103E8` → 游戏按 id 查找失败（`0x5B7050` 的 `cmp dword [entry+4],id`）→ `0x5B72F0`（getflag，**无空指针检查**）解引用空 entry → 异常地址 `0x5B6754`、读取地址 `0x14`。证据：12:48 的 dump 栈上 `[esp+4]=0`（lookup 返回值）、`[esp+0x10]=0x3E8`（被查 id）；两个 dump 栈逐字节一致。**修复（A1）**：删除该写入与 `activate` 变量，注释指向 `RuntimeInjection2026.cpp:1170-1172`（「开启小兵培养」写 `0x40CAEF`/`0x40BF25` = `0x32`/`0`，与原工具三方一致，本来就是对的）。**A2**：保留 `0x14FA656`（培养时长）但标注「⚠️ 未验证地址：PE 内零引用、无指针指向、位于 `.data` BSS；若实机再崩，先删这行」，代码注释 + `DESIGN.md` + `REQUIREMENTS_1_4.md` 同步。**A3**：新增回归测试 `idTableWriteGuardTest()`（断言所有写入不覆盖 5 张 id 表区域 `0x6613E8`/`0x665B70`/`0x667740`/`0x667C08`/`0x669F78` 各 11×`0x20`=`0x160` 字节，特别断言 `0x6613EE` 与 `0x6613EC`；断言「开启小兵培养」开关写 `0x40CAEF`/`0x40BF25` 的值在 `0x32`/`0x00` 之间切换），**反向验证通过**（临时加回 `0x6613EE` 写入 → 测试报 `FAILED: no recorded write overlaps a static id table region` → 恢复 → 全绿）。**A4 归因更正**：§12.4 与本表先前那条「禁用 `unk_42C894` 即消除 `0x5B6754`」是**错误归因**（已被 12:48 dump 证伪：禁用之后仍崩同一地址），已在 `_INJECTION_FIX_NOTES.md` §12.4/§13 与本表就地加更正注记；`PLAN.md`/`CHANGELOG.md`/`DESIGN.md`/`REQUIREMENTS_1_4.md` 里 `0x6613EE` 的错误定址一并更正。**验证**：Debug + Release 构建、`--all assets/config` `exit=0`、`--ui-smoke` `exit=0`、Publish 构建通过、`out/trial/` 已重新出包。清理了 52 个 `tools/_*.py` 与 `武将提拔/{res_144.bin, plugin_try.bin, doc_text.txt}`（`res_135.bin` 保留供 T3 解密用）。**实机验证待用户执行**。 |
| 2026-11-02 | **兵法抵挡 bug 根因定位并修复 + 熟练度指示图标 + 用户手册**。**① 兵法抵挡**（用户报告「熟练度高的人发动兵法被熟练度低的人抵挡」+「兵法发动概率设置没有生效」两个症状）：根因是**把握概率挂错了骰子**——游戏有抵抗骰（`0x4FEB20` 内 `0x4FEB95`，eax≠0 即抵抗成立）与发动骰（`0x4FEBB0` 内 `0x4FEC5C`，失败即 return 0 不发动）两枚，旧版把把握概率挂在**抵抗骰** `0x4FEB90` 并强制 `eax=1` ⇒ 勾选后**必定被抵挡**且熟练度完全不参与，同时发动骰从未被 hook ⇒ 把握概率勾了也没效果。**一个根因两个症状**。修复：恢复 `0x4FEB90` 原版字节 `BE 0A 00 00 00`，把握概率改挂发动骰 `0x4FEC59`（命中则跳过掷骰、`mov eax,1`、返回 `0x4FEC5E`；未命中原样重放），日志打印 `resist roll restored` 作佐证。**关键栈账约束**：`kakuritsu` 是 `__cdecl`（导出名 `?kakuritsu@@YAHHW4RANDOM_KIND@@H@Z`），原版 `0x4FEC5E` 的 `add esp,0x14` 清 5 个压栈，命中路径**必须照原版压 `0xB` 与 `edx`**。桩 58 字节，`arrayAddrOffset=0x15`、`extraJumpRelOffset=0x28`（rel32 **字段**偏移；`tools/asm_check.py` 的 `rel32` 用的是 E9 **操作码**偏移 0x27/0x35，两套约定勿混用）。**② 熟练度指示图标**（用户 Request F，原文四档：无 / ★（1000<上限≤1500）/ ◎（上限>1500）/ ○（该系有缩短发动间隔，优先级最高））：调查确认**可复用的图标位图 = 0 个**——游戏只有 12×10/33×11 光标位图、2019 工具 `San9PKHard.exe` 无任何位图、原版 `SanIXPKCheat.exe` 的 25 BITMAP + 17 ICON 是打包加密无法提取；且原版那 9 个 8×8 控件（id 1096–1104）实为 `BS_CHECKBOX` 复选框（`sub_404C70.c` 的 `DDX_Check` 证据），绑定的是**「精通」标志位**（`sub_405130.c`）而非熟练度阈值 ⇒ 两个原始二进制都不存在 ★/☆/○/●/◎/环 梯度。实现改用**宋体 Unicode 字形**（★ U+2605、◎ U+25CE、○ U+25CB）：新增 `IDC_PROFICIENCY_MARK_1..9`（1252–1260）+ 对话框 132 的 9 个 Static + `OfficerPersonalization2026::systemSkills`（9 系→兵法表 id 的权威映射：步兵 0-2/骑兵 3-5/弓骑 6-8/弩兵 9-11/水军 12-14/攻城 15-18/智略 19-22/谋略 23-26/策略 27-30，31-40 为特殊兵法不归属任何系）+ `Pages.cpp` 的实时刷新 + 新增 `proficiencyMarkerTest()`。**③ 用户手册**：新增 `docs/用户手册.md`（9 节；56 开关 + 32 数值参数由 `src/generated/parameter_fields.inc` + `recovery/resources/dialogs.json` **程序化生成**，不手写）。**验证**：Debug/Release/Publish 三配置构建 + `--all assets/config` + `--ui-smoke` 全绿（EXIT=0），`out/trial/` 已刷新。清理遗留分析中间产物 15 个（`msg_*.txt` 5、`ref_*.txt` 4、`sanpkcheat/*.txt` 6）。 |
