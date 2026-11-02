# 会话续接速记（2026-11-02）

> 详细状态见 `docs/HANDOVER.md`（唯一事实源）。本文件只是本会话的快速索引。

## 2026-11-02 本轮完成

### 1. 兵法抵挡 bug 根因定位并修复（用户报告的两个症状同源）

**用户报告**：①「兵法抵挡似乎有 bug；经常出现熟练度很高的人发动兵法时被熟练度低的人抵挡」
②「兵法发动概率这个设置也是没有生效的，有武将设置了这个但是没有起效果」。

**根因**：游戏里有两枚独立的骰子，上一版把「把握概率」挂错了。

| 骰子 | 函数 | 掷骰点 | 语义 |
| --- | --- | --- | --- |
| **抵抗骰** | `0x4FEB20` | `0x4FEB95` | 概率 = (对手值 − 我方熟练度) / 10；**eax≠0 ⇒ 抵抗成立** |
| **发动骰** | `0x4FEBB0` | `0x4FEC5C` | 概率 = 100 + 随机抖动；失败 ⇒ `xor eax,eax` 返回 0 ⇒ 兵法不发动 |

旧实现把把握概率 hook 挂在 **`0x4FEB90`（抵抗骰）** 并强制 `eax=1`。因为「eax≠0」就是
「抵抗成立」，所以**勾选把握概率 ⇒ 该武将必定被抵挡，且熟练度完全不参与判定** ——
这正是症状 ①。而真正的发动骰从未被 hook，所以「把握概率」勾了也没效果 —— 症状 ②。

**修复**（`src/runtime/Runtime2026.cpp` `applyOfficerPersonalization()`）：
1. 把 `0x4FEB90` 恢复成原版字节 `BE 0A 00 00 00`（`mov esi,0xA`），抵抗判定完全按原版走；
2. 把握概率改挂到 **发动骰 `0x4FEC59`**（`push 0xB; push edx; call ebp`），命中时跳过掷骰、
   直接 `mov eax,1` 返回 `0x4FEC5E`；未命中则原样重放原调用；
3. 日志打印 `personalization activationBoost: resist roll restored at 0x...` 作为佐证。

**关键约束（改动此桩时必须遵守）**：`kakuritsu` 是 **`__cdecl`**
（导出名 `?kakuritsu@@YAHHW4RANDOM_KIND@@H@Z`，`YAH` = cdecl），由调用方清参数。
原版 `0x4FEC5E` 的 `add esp,0x14`（20 字节）恰好清掉 5 个压栈：
`push eax`(0x4FEC25) + `push ebp`(0x4FEC30) + `push 1`(0x4FEC53) + `push 0xB` + `push edx`。
所以「命中」路径**必须照原版压入 `0xB` 与 `edx`**（虽然不掷骰），多压/少压都会破坏栈平衡。

**桩布局**：58 字节，hook 点 `0x4FEC59`，返回 `0x4FEC5E`，`arrayAddrOffset=0x15`，
`extraJumpRelOffset=0x28`（**rel32 字段偏移 = E9 操作码偏移 + 1**）。
`tools/asm_check.py` 的 `rel32={...}` 用的是 **E9 操作码偏移**（0x27 / 0x35），
两者指向同一段 rel32 字段，但**约定不同**，改代码时不要混用。

### 2. 熟练度状态指示图标（用户 Request F）

**用户原文**：「你告诉我有几个图标可以用，如果数量度上限是1000（默认值）不显示图标，
如果是1500以下（比如1200），显示一个五角星，如果是1500以上（1600、2000）显示一个圈，
如果是发动间隔缩短了，则显示一个环。」

**关于「有几个图标可用」的调查结论：可直接复用的图标位图 = 0 个。**
- 游戏 `San9PK.exe` 只有 12×10 / 33×11 两个**光标**位图；
- 2019 版工具 `recovery/sample-2026/San9PKHard.exe` **没有任何位图资源**；
- 原版作弊器 `SanIXPKCheat.exe` 有 25 BITMAP + 17 ICON，但资源数据是**打包加密**的，
  暴力 XOR 密钥恢复失败、尺寸暴力枚举有歧义，无法提取。

**原版指示器的真实语义**（与用户描述不同，需知悉）：原版武将配置对话框里那 9 个
8×8 控件（id 1096–1104，x=196）**不是图标**，而是 `BS_CHECKBOX` 复选框
（class ordinal 0x80 = Button，style `0x50010002`），证据：
`recovery/evidence/decompiled/sub_404C70.c` 里是 `DDX_Check(a2, 1096, ...)`…`DDX_Check(a2, 1104, ...)`。
它们绑定的是武将**逐兵法系的「精通」标志位**（`sub_405130.c`：byte `this+12352` 的 bit 0–7
= officer+0x36>>3，word `this+6176` 的 bit 8），**与熟练度数值无关**，只有「空 / ✓」两态。
两个原始二进制里**都不存在** ★/☆/○/●/◎/环 的字形梯度。

**本作实现**：按用户要求实现四档，用**宋体 Unicode 字形**直接画（无需新增位图资源）：
★ U+2605、◎ U+25CE、○ U+25CB（○ 兼作「环」的近似）。

| 状态 | 判定 |
| --- | --- |
| （无） | 上限 = 1000（或填 0 = 不修改），且该系没有缩短间隔 |
| ★ | 上限在 1000 < cap ≤ 1500 |
| ◎ | 上限 > 1500 |
| ○ | 该系至少一个兵法的发动间隔 < 游戏原值（**优先级最高**） |

**改动**：
- `assets/ui/resource_ids.h`：新增 `IDC_PROFICIENCY_MARK_1..9`（1252–1260）；
- `assets/ui/dialogs.rc`：对话框 132 新增 9 个 Static（x=336，y 与各行对齐）；
- `src/runtime/OfficerPersonalization2026.h`：新增 `RefinedCap=1500`、`NoSkill=0xFF`、
  `systemSkills`（9 系 → 兵法表 id 的权威映射，依据原版对话框 9 行布局：
  步兵 0-2 / 骑兵 3-5 / 弓骑 6-8 / 弩兵 9-11 / 水军 12-14 / 攻城 15-18 /
  智略 19-22 / 谋略 23-26 / 策略 27-30；31-40 为特殊兵法，不归属任何系）；
- `src/app/ui/Pages.cpp`：`PersonPage` 加 `proficiencyMark()` / `refreshProficiencyMarks()` /
  `isProficiencyMarkSource()` / `updateProficiencyMarkSources()`，在 `loadPerson()` 后刷新，
  并在上限输入框或 27 个间隔输入框发生 `EN_CHANGE` 时实时刷新；
- `tests/runtime_test.cpp`：新增 `proficiencyMarkerTest()`（四档判定、环的归属范围、
  31 个常规兵法全部有归属），已注册进 `--all` 与 `--proficiency-marker`。

### 3. 用户手册

新增 `docs/用户手册.md`（9 节）：主窗口 / 设置页（2.1 参数页 56 开关 + 32 数值，
由 `src/generated/parameter_fields.inc` + `recovery/resources/dialogs.json` **程序化生成**，
不手写以免出错；2.2 扩展设置页；2.3 其它数值项）/ 武将配置页 / 都市配置页 /
兵法配置页 / 阵型配置页 / 配置文件 / 诊断 / 卸载。

### 4. 清理

删除上一轮遗留的分析中间产物（`msg_*.txt` 5 个、`ref_*.txt` 4 个、
`sanpkcheat/{analysis_utf8,hits,manual_hits,m,s19,dz}.txt` 6 个）与本轮的临时图/转储
（`out/_ind/`、`out/_*.txt`、`out/_*.png` 等）。

---

# 上一轮（2026-11-01）记录

## 已完成并验证

1. **P0 注入代码重写（§3.7，2026-10-27）** — 第二轮逆向确认四个注入块武将索引
   用 `(esi-0x1258EE0)/0x128` 是**根本性错误**（esi 从不是武将表指针 → 实机必崩）。
   已全部重写：统一 `movzx reg, word [ptr+4]`（`CPersonData` 的武将表下标）。
   - 把握 hook 由 `0x4FEB95`（esi=常数10）移到 `0x4FEB90`。
   - 威力 hook 改在 `0x50603B`（call 之后观察）。
   - 间隔 hook `0x4FF08B` 只覆盖 5 字节、返回 `0x4FF090` 保留原 `call 0x4FB760`。
   - `installPerOfficerHook` 增加 `returnAddress` 参数、回写恰好 originalSize 字节。
   - 字节级用 capstone 验证（`tools/asm_check.py`）；`injectionCodeBytesTest` 断言新字节。
   **剩下：实机验证四类个性化。**

2. **UI smoke 根因定位并修复（§3.5 / §4.1）** — 旧结论「崩在 `MainWindow` 构造内部」
   **已推翻**。真实断点：`Dialog::tooltip()` 更新分支把
   `TTM_UPDATETIPTEXTW` 的**返回值**当成功标志（该消息无返回值契约，
   `main` 分支也不检查，只检查有 BOOL 契约的 `TTM_ADDTOOLW`）-> 误报
   `cannot update tooltip`。已删该判断；**保留** dev 分支的稳定缓冲所有权
   （`replacement` / `std::move`）。`--ui-smoke` 现为 **exit=0**。

3. **调试产物清理** — 11 处 `smoke_trace` 打点（`MainWindow.cpp` 9 + `Pages.cpp` 2）
   与 `smoke_trace.txt` 已全部删除。

4. **详细日志开关（§4.5）** — 三条开启途径：
   设置页「扩展设置 → 详细日志」复选框（`IDC_DETAILED_LOG=1251`，**运行时动态创建，
   不需要改 `.rc`**）/ `--log-writes` 命令行 / `SAN9_LOG_WRITES=1` 环境变量。
   勾选立即生效并持久化到 `[Diagnostics] LogWrites`。
   日志路径显示在状态栏悬停提示里。

5. **Publish 发布包** — `tools/Build-App.ps1 -Configuration Publish` 构建通过。
   静态 CRT，可直接外发。**2026-10-27 已重新出包** `out/trial/`（新 exe + 6 个配置 txt
   + `使用说明.txt`），`San9PKHardSkill.txt` 已含 41 行。

6. **命令行解析修复** — `wWinMain` 原本用整串比较 `lpCmdLine == L"--ui-smoke"`。
   `Start-Process -ArgumentList` 产生的命令行**带尾随空格**，比较必然失败 ->
   smoke 变成交互模式**永久挂起**。已改为按空白分词后逐个精确匹配。
   同样把 `traceWritesRequested()` 的子串查找换成精确分词匹配。
   `tools/Build-App.ps1` 的 `-UiSmoke` 分支从 `Start-Process -Wait`（会挂死构建）
   改为 `WaitForExit(120000)` + 超时 kill + throw。

7. **兵法表 41 条（B6，2026-10-27）** — `SkillConfig::Count` 39→41；布局
   `state_size` 7692→7844，`FormationFlags/Entries` 后移 7624/7625（避免撞
   `FormationAttackRangeAddress` 偏移 5212）；`SkillEntriesSize` 156→164；
   `San9PKHardSkill.txt` 补两行；`PatchImage::object_` 7844。

8. **崩溃 #3 根因：禁用桩破坏了原块的返回值契约（2026-10-31）** ——
   `unk_42C894` 的契约是「原样返回入口 eax」（收尾 `mov eax,ebp; ret 4`）。
   上一轮的禁用桩 `xor eax,eax; ret 4` 把 eax 抹成 0 ->
   `0x51FA3B: mov ecx,eax` 得到 ecx=0 -> `0x51FA3D: call 0x45C8A0`
   读 `[0+0x28]` 访问违例。**根因不是新代码，是上一轮的修复本身。**
   桩已改为 `C2 04 00`（`ret 4`）：eax 原样返回 + 栈弹 1 参数 + 不执行污染逻辑。
   新增回归测试 `disabledOriginalToolBlockTest`（断言不含 `33 C0`），已反向验证。
   **取证方法**：事件日志只给故障偏移会误导（本轮曾据此判错方向）；
   改用 WER 本地完整转储拿到真实调用栈（栈顶返回地址 `0x51FA42`）才一次定位。
   详见 `docs/HANDOVER.md` §4.5 与 `docs/_INJECTION_FIX_NOTES.md` §12。

9. **329 块全量排查完成（2026-10-31）** ——
   ① 返回值契约：329 个上传块中**只有 `unk_42C894` 一个**内容被替换过，
   其余 328 个是原工具原样字节（仅重定位）→ 风险面仅此一点；
   ② 未判空解引用：7 处候选，6 处误报，唯一真实项 `unk_42B718` 的被调函数
   是构造函数（必返回非空）→ **无新增隐患**。
   新增可复现工具 `tools/audit_call_contracts.py`、`tools/audit_unguarded_deref.py`；
   清理了 30 个 `tools/_*.py` 临时脚本。

## 当前验证状态

- Debug 构建 ✅ / Release 构建 ✅ / Publish 构建 ✅
- 离线测试 `--all assets/config` 全绿 ✅ / `--ui-smoke` exit=0 ✅（2026-11-02）
- `out/trial/` 已重新出包 ✅（2026-11-02，含把握概率修复 + 熟练度指示图标）
- 试用包在无 `San9PK.exe` 环境下启动不崩 ✅

## 崩溃 #4（`0x5B6754`）修复 —— 2026-11-01

**根因是自己的 bug**：`Runtime2026::applyCultivate()` 往 `0x6613EE` 写 1 字节，
而 `0x6613EE` 是 id 表 A 首条记录的 **id dword `0x6613EC`（值 `0x000003E8`）的第 2 字节**。
写 1 → id 1000 变成 `0x000103E8` → 游戏按 id 查找失败 → `0x5B72F0`（getflag，
无空指针检查）解引用空 entry → 异常 `0x5B6754`、读地址 `0x14`。

**已落地**：

| # | 改动 |
| --- | --- |
| A1 | 删除 `applyCultivate()` 里对 `0x6613EE` 的写入与 `activate` 变量；注释指向 `RuntimeInjection2026.cpp:1170-1172` |
| A2 | 保留 `0x14FA656`，标注「⚠️ 未验证地址：PE 内零引用、无指针指向、位于 .data BSS；若实机再崩，先删这行」（代码 + `DESIGN.md` + `REQUIREMENTS_1_4.md`） |
| A3 | 新增回归测试 `idTableWriteGuardTest()`（5 张 id 表禁写 + `0x40CAEF`/`0x40BF25` 的 `0x32`/`0x00` 切换），已反向验证 |
| A4 | 更正错误归因（`_INJECTION_FIX_NOTES.md` §12.4/§13、`HANDOVER.md` §4.7/§8）与错误定址（`DESIGN.md`、`REQUIREMENTS_1_4.md`、`PLAN.md`、`CHANGELOG.md`） |

**「开启小兵培养」的实现本来就是对的**（写 `0x40CAEF`/`0x40BF25` = `0x32`/`0`，
与原工具三方证据一致），**与崩溃无关**。

**遗留**：若实机仍崩 → **第一件事删掉 `0x14FA656` 那一行**（用户已知悉此约定）。
详见 `docs/_CRASH4_HANDOFF_2026-10-31.md`（完整证据链）与 `docs/HANDOVER.md` §4.7。

## 下一步（§3.6 顺序）

1. **实机验证**（唯一剩余阻塞项）——把 `out/trial/` 交给用户，进游戏确认：
   - 不再崩在 `0x5B6754`；
   - 「开启小兵培养」勾选后小兵能被提拔；
   - **兵法抵挡**：勾选「提升兵法发动机会把握概率」的武将应当**必定发动**，
     而**不再**出现「必定被抵挡」；日志里应有 `resist roll restored` 一行；
   - 四类个性化（成长加成 / 激活加成 / 威力加倍 / 兵法间隔）逐项确认，
     并回传 `San9Companion.log`。
   **若仍崩溃**：转储已默认开启（`%LOCALAPPDATA%\CrashDumps\San9PK.exe.<pid>.dmp`），
   直接解析该文件即可拿到异常地址 / 寄存器 / 调用栈，不必再靠事件日志猜。
   **用户已表示会让游戏长时间运行**，届时读取长日志判定。
2. §3.1.2 兵法威力系数 hook 点（用户已示意暂不开发）。
3. 存档路线 RE T1–T4（`docs/_CRASH4_HANDOFF_2026-10-31.md` §5c.3），其中 **T3 解密 `武将提拔/res_135.bin`** 是支线。
4. §12.3 遗留：`0x5B8556` 返回值语义、5 张 id 表灌入路径、VirtualAllocEx 大小。
5. §6 清理一次性分析脚本 —— 已于 2026-11-01 完成（`tools/_*.py` 52 个全清）。
