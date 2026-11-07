# 知识总表（KNOWLEDGE_BASE）

> 本文件是**逆向结论的单一可信来源**。
> 任何地址、偏移、编码、指令语义，改动前先查这里，改动后同步这里。
>
> 配套文档：
> - `docs/_INJECTION_FIX_NOTES.md` —— 注入代码的逐条证据链与历史事故复盘
> - `docs/LIVE_TEST_PLAN.md` —— 逐功能实机验证清单
> - `docs/HANDOVER.md` —— 交付进度与历史时间线
> - `docs/_WORK_STATE.md` —— 临时工作笔记（上下文压缩后用来恢复状态）

---

## 0. 怎么用这份文档

### 0.1 置信度分级

| 级别 | 含义 | 判定标准 |
|---|---|---|
| **A｜实测坐实** | 有崩溃转储 / 实机行为 / 字节级反汇编三方一致 | 可直接依赖 |
| **B｜静态坐实** | 逐字节反汇编核对过，逻辑自洽，但未实机验证 | 可依赖，但改前先看 §6 实机状态表 |
| **C｜推断** | 由多处证据收敛得出，未逐字节确认 | 使用前必须再验一次 |

### 0.2 证据等级

反汇编结论的强度依次是：**崩溃转储** > **原版第三方修改器的字节补丁** > **游戏自身的引用扫描** > **2019 版工具源码** > **猜测**。

> 历史教训：`0x14FA656`（培养时长）是**猜测**出来的地址，被当成事实用了很久，
> 每次应用配置都在改坏游戏代码。见 §6.1。

---

## 1. 基础事实

| 项 | 值 | 证据 |
|---|---|---|
| 游戏本体 | `San9PK.exe`（三国志9 PK），Win32 x86 PE32，MFC42，ANSI | A |
| 仓库内的副本 | `G:\aiprojects\game\san9-companion\San9PK.exe` | 与用户 `E:\games\San9 Tc\San9PK.exe` **md5 完全一致**（`4adc31ec…`）→ 可直接用仓库副本做反汇编 |
| 映像基址 | `0x400000` | A |
| 文本段 | `0x401000..0x4A3000`（`.text`） | B |
| 静态数据段 | `0x4A3000..0x620000`（`.data`） | B |
| 字体/字形表 | `0x61CFA0..0x61CFF8`（Big5/CP950） | B |
| 武将数量 | **800**（存档只装载前 800 条，`0x45415B`: `cmp edi,0x320`） | A |
| 兵法数量 | **41**（`0x453720`: `cmp eax,0x29`） | A |
| 兵法系 | 9 系 → 兵法 id 映射见 §4.3 | B |
| 文本编码 | **CP950 / Big5 繁体**，两字节 | A |

### 1.1 注入分配区（Allocation）

| 项 | 值 |
|---|---|
| 基址 | `AllocationBase`（`layout2026::AllocationBase`，state 偏移 96） |
| 大小 | **11 页 = 0xB000 字节**（`injection2026::AllocPageNum = 11`） |
| 保护 | `PAGE_EXECUTE_READWRITE` |
| 相对范围 | `0xA0000 .. 0xB0000` |
| 已占用 | ≈ 33.2 KB |
| 最大空闲连续段 | `0x095E .. 0x10000`（22.2 KB） |

**重定位（rebase）机制**：所有 `dword_42Fxxx` 描述符槽里存的是**重定位前的**值
（在 `0xA0000` 空间）。`Runtime2026::rebase()` 在连接时给每个槽加上
`delta = AllocationBase - 0x420050`，运行期它们就变成 `AllocationBase + <offset>`。

> ⚠️ 源码里看到的 `dword_42F27C` 之类不是最终地址，是槽地址。

### 1.2 注入分配区的已知占用点

| 偏移 | 内容 |
|---|---|
| `0x2190` | 熟练度上限查表桩（93 字节） |
| `0x6960` | 熟练度图标跳板（5 字节 `E9`） |
| `0xA000` | 蛮族阵型桩（6 字节 `B8 CC 03 00 00 C3`） |

---

## 2. CP950 字形表（本项目必须记住）

游戏的字符表只有 Big5 两字节字形。以下是本项目实际用到的：

| 字形 | Unicode | CP950 字节 | 小端 dword（低位在前） | 用途 |
|---|---|---|---|---|
| ☆ | U+2606 | `a1 b8` | `0x2020B8A1` | 空心五角星 / 原生「精通」 |
| ★ | U+2605 | `a1 b9` | `0x2020B9A1` | 实心五角星（**游戏字体里没有**，不要用） |
| ◎ | U+25CE | `a1 b7` | `0x2020B7A1` | 同心圆 / 原生「精进」 |
| ○ | U+25CB | `a1 b3` | `0x2020B3A1` | 空心圆圈 / 「缩短发动间隔」 |
| △ | U+25B3 | `a1 b5` | — | 游戏自带 |
| × | U+00D7 | `a1 d1` | — | 游戏自带 |
| ： | U+FF1A | `a1 47` | — | 游戏自带 |
| ？ | U+FF1F | `a1 48` | — | 游戏自带 |

> ⚠️ 早先误用 Big5 `a1 49` 找 ★，得出「游戏里没有 ★」的错误结论，已纠正。
> 正确编码见上表 `a1 b9`。
>
> ⚠️ **Big5 ≠ CP950**。Big5 的 `a1 40..a1 7E` 是图形符号区，
> CP950 把它重新分配成了 `a1 40..a1 7E`（日文假名/符号）。
> 找字形必须用 **CP950** 表，不是 Big5 表。

**图标前缀格式**：图标 + 两个空格，共 4 字节。
`0x2020B8A1` = 字节 `A1 B8 20 20`（☆ 后面跟两个空格）。

---

## 3. x86 注入陷阱库

> 这一节是本项目**最贵**的经验积累。每一个都是真实的事故。

### 3.1 rel8 是**有符号**的

**症状**：改了注入块之后，游戏一进武将列表就崩，异常读地址在注入块**之外**几十字节。

**根因**：短跳转 `jne/jl/jg` 的位移是 **rel8 = 1 字节有符号数**，范围 −128..+127。
位移超过 +127 就会绕成负数，跳到块**前面**去。

**实例（2026-11-07 图标崩溃）**：
- 172 字节块里 4 个守卫跳转到位移 159 / 152 / 147 / 140，**全部 > 127**；
- `0x9F` 被解释成 **−97** → 跳到块外 88 字节 → 访问违例。
- 转储坐实：读地址 `0x02227028`，而我们的块在 `0x02227080`，
  `0x02227028 = 0x02227080 + 0x09 − 97` —— 正是那个守卫跳转的目标。
- 原版 140 字节块的位移**恰好**是 127（有符号上限），纯属侥幸。

**修复**：4 个守卫跳转改**近跳转**（`0F 84` / `0F 8C` / `0F 8F`，rel32）。
块从 172 → **188 字节**。

**规则**：
> 注入块里**任何**跳到块尾（或块外固定位置）的分支，只要块长超过 ~120 字节，
> 一律用 rel32 近跳转。近跳转多花 4 字节，换来永远不会越界。

**校验工具**：`tools/check_injection_blocks.py`
读 `src/runtime/Runtime2026.cpp` 里的 `const BYTE xxx[] = {...}`，用 capstone 反汇编，
断言每条跳转：① 落在指令边界 ② 目标在块内 ③ rel32 占位（全 0）跳过。
当前 **11 个块，0 个问题**。

> 工具的坑：注释里的 hex 字面量会被误读 → 已加 `strip_comments`；
> 游戏代码片段（growth boost 的 `original[6]`）单独跳过。

### 3.2 rel32 **字段偏移**约定各不相同

写「跳转目标占位符」时，rel32 字段在指令里的**字节位置**取决于操作码：

| 指令 | 指令长度 | rel32 字段在块内的偏移 |
|---|---|---|
| `E9 rel32`（jmp near） | 5 | **opcode + 1** |
| `0F 8x rel32`（jcc near） | 6 | **opcode + 2** |
| `EB rel8`（jmp short） | 2 | opcode + 1 |
| `7x rel8`（jcc short） | 2 | opcode + 1 |

> **两套约定勿混用**：`tools/asm_check.py` 的 `rel32` 用的是 `E9` **操作码**偏移
> （0x27 / 0x35），而把握概率桩的 `extraJumpRelOffset = 0x28` 是**字段**偏移。
> 差 1 字节 = 跳到指令中间 = 灾难。

**实例**：修复图标块时把 disp32 写到 `0x4B` 而不是 `0x4F`，
被新增的测试断言当场抓住 → 改成 `0x4F`。✅
**这说明：注入块的每个字节偏移都必须有断言。**

### 3.3 栈平衡

被 hook 的函数如果是 `__cdecl`（调用方清参数），桩里的压栈/弹栈必须**逐条对齐**。

**实例（把握概率桩，58 字节）**：
`kakuritsu` 的导出名 `?kakuritsu@@YAHHW4RANDOM_KIND@@H@Z` —— `YAH` 即 **cdecl**。
原版 `0x4FEC5E` 的 `add esp,0x14`（20 字节）恰好清掉 5 个压栈：
`push eax`(0x4FEC25) + `push ebp`(0x4FEC30) + `push 1`(0x4FEC53) + `push 0xB` + `push edx`。

所以「命中」路径**即使不掷骰也必须照原版压入 `0xB` 与 `edx`**，
否则 `add esp,0x14` 会多弹 8 字节，栈彻底错位。

**规则**：
> hook 桩的每一条 return 路径，都要把「原版在被 hook 点压了什么」列成清单，
> 逐条对照。多压少压都会坏。

### 3.4 rel8/rel32 混用的块长度会让**后续所有偏移**平移

188 字节块比 172 字节块多 16 字节 → 所有 disp32 占位符的偏移**全部变了**。
所有断言必须写成**符号化的偏移**或集中在表里，不能散落在各处。

### 3.5 写入游戏 `.text` 需要改保护属性

往 `.text` 段写字节要 `VirtualProtectEx` 改页保护。
运行时用的是 `writeBytes`（内部已处理）。

> ⚠️ 测试桩里 `inGame` 的地址范围是 `0xA0000..0x2000000`（**绝对地址**），
> 而实际游戏 VA 是 `0x400000..0x600000`。测试用 `base=0x22000000` 的分支，
> 所以实测通过。这是个**已知的测试与真实地址域不一致**的地方，
> 见 §6.2。

### 3.6 别把「猜出来的地址」当事实

**实例**：`0x14FA656` 被当作「培养时长变量」用了很久。
RVA `0xFA656` 落在 **.text** 段 → 它是**游戏代码的一个字节**，不是变量。
我们每次应用配置都往那儿写 9，**一直在改坏游戏代码**。

**2019 版工具根本没有培养时长功能**（只有 `m_dwTrainNumAddr` `0x40CAEF` /
`m_dwTrainNumAddr2` `0x40BF25` / `m_dwOldTrainNum` 50）。

**处理**：删除该写入，配置项保留但不生效，日志明说。

**规则**：
> 任何一个要写入的地址，必须至少满足：① 在数据段 ② 被证伪为代码。
> 只靠「看起来像个变量」不成立。写之前先查这个地址有没有被引用。

### 3.7 别往静态 id 表上写

**实例（崩溃 #4，地址 `0x5B6754`）**：
往 `0x6613EE` 写 1 字节，而 `0x6613EE` 是 id 表 A 首条记录的 **id dword `0x6613EC`**
（值 `0x000003E8`）的**第 2 个字节**。写完 id 1000 变成 `0x000103E8` →
游戏按 id 查找失败（`0x5B7050`: `cmp dword [entry+4],id`）→
`0x5B72F0`（getflag，**无空指针检查**）解引用空 entry → 异常。

**回归测试**：`idTableWriteGuardTest()` 断言所有写入不覆盖 5 张 id 表区域
（`0x6613E8` / `0x665B70` / `0x667740` / `0x667C08` / `0x669F78` 各 11×`0x20`=`0x160` 字节），
特别断言 `0x6613EE` 与 `0x6613EC`。**反向验证通过**
（临时加回写入 → 测试报 `FAILED: no recorded write overlaps a static id table region` → 恢复 → 全绿）。

### 3.8 注入块装不下就放跳板

172 字节 > 原槽位 `0x6960..0x6A00` 的 160 字节，会压到 `codeMoveGuardTroop@0x6A00`。

**套路**：在原槽位放 5 字节 `E9 rel32` 跳板，真正的块放到别处（自己分配的内存）。
`applyOfficerPersonalization` 的间隔 hook 用的是同一套路。

### 3.9 分配区要核算，不能随便放

图标表 `uint32[800*9]` = 28.8 KB **放不进**分配区（45 KB，已占 33.2 KB）。
所以：图标表 + 判定块一起放进 `applyProficiencyMarks` **自己新分配**的一块内存，
分配区只放 5 字节跳板。**空间充足，无需新方案。**

### 3.10 反向验证（把 bug 加回去看测试会不会红）

每个修复都应该做一次**反向验证**：临时把错误字节/错误写入加回去，
确认测试**会失败**，然后恢复。这一步能证明测试真的在测那件事。

已做过的反向验证：
- 图标块 disp32 写错到 `0x4B` → 测试当场抓出；
- 加回 `0x6613EE` 写入 → `idTableWriteGuardTest` 报错；
- 注入块改回旧错误字节 → `injectionCodeBytesTest` 失败。

---

## 4. 数据模型

### 4.1 布局（`src/generated/RuntimeLayout2026.h`）

`StateSize = 7844` 字节。关键字段：

| 偏移 | 长度 | 含义 |
|---|---|---|
| 96 | 4 | `AllocationBase` |
| 108 | 4 | `TargetWindow` |
| 112 | 4 | `ProcessHandle` |
| 116 | 4 | `ProcessId` |
| 120 | 4 | `OwnerWindow` |
| 124 | 4 | `MessageId` |
| 128 | 4 | `CallbackReturnStub`（`0xC3C3C3C3`） |
| 132 | 4 | `EventSequence` |
| 136 | 4 | `ConnectTicks` |
| 140 | 4 | `ErrorCount` |
| 144 | 4 | `StatusTicks` |
| 500 | 3200 | `PersonLevels` |
| 3704 | 696 | `PersonFriendships` |
| 4408 | 2 | `MaxExperience` |
| 4412 | 4 | `CityAutomatic` |
| 4416 | 4 | `CityPending` |
| 4420 | 300 | `CityLimits` |
| 4836 | 1 | `SkillFlags` |
| 4838 | 164 | `SkillEntries` |
| 7624 | 1 | `FormationFlags` |
| 7625 | 216 | `FormationEntries` |

### 4.2 游戏数据基址

| 基址 | 内容 | stride |
|---|---|---|
| `0xA31B0` | 武将等级/兵法位表 | — |
| `0xA4000` | 武将等级表 `PersonLevels` | 500×6 |
| `0xA5662` / `0xA5630` | 都市最大兵力 / 最大民心 | — |
| `0xA6E00` | 阵型/能力表（0x190 字节） | — |
| `0xA3CC0` | 兵法标志表（0x32 字节） | — |
| `0xA1AE0` | 阵型攻击范围表 | — |
| `0x610740` | 亲密度表（0x2B8 字节） | — |
| `0x124DBAA` | 都市配置记录 | `CityConfig::GameRecordSize` |
| `0x14FDB4C` | 兵法配置记录 | 140 |
| `0x1500BD4` | 阵型驻留配置 | — |
| `0x42DB0C` | 阵型默认攻击范围（只读） | — |

### 4.3 兵法系 → 兵法 id 映射（权威）

| 系 | 兵法 id |
|---|---|
| 步兵 | 0, 1, 2 |
| 骑兵 | 3, 4, 5 |
| 弓骑 | 6, 7, 8 |
| 弩兵 | 9, 10, 11 |
| 水军 | 12, 13, 14 |
| 攻城 | 15, 16, 17, 18 |
| 智略 | 19, 20, 21, 22 |
| 谋略 | 23, 24, 25, 26 |
| 策略 | 27, 28, 29, 30 |

id 31–40 为特殊兵法，不归属任何系。

### 4.4 武将索引的取法（**只有这一种是对的**）

```
movzx eax, word [CPersonData + 4]
```

三条独立证据链收敛：
1. RTTI `0x6060E8` = `"CPersonData"`；
2. `0x44F830` → `0x446800` 把表下标写进 `record+4` 的低 16 位；
3. 原版 stub 同款。

**边界**：所有桩里都要 `cmp eax, 0x320`（800）后再索引。

> ⚠️ 计划中「用 `dword[+0x94]`」是**错的**（那是 `CUnitData` 的字段，
> 间隔 hook 现场的 `esi` 是 `CPersonData`，无此字段 → 野读）。

### 4.5 熟练度图标规则（五档，两处共用）

`src/runtime/ProficiencyMark2026.h` —— 修改器界面与游戏内注入**共用同一份规则**。

| 档 | 条件 | 游戏内前缀 | 界面字形 |
|---|---|---|---|
| `unconfigured` | 该系既没改过上限，也没改过任何兵法间隔 | `0x00000000`（回落原生标记） | 空 |
| `none` | 配了但不该显示图标 | `0x20202020`（4 个空格） | 空 |
| `star` | `1000 < 上限 <= 1500` | `0x2020B8A1`（☆） | ☆ |
| `refined` | `上限 > 1500` | `0x2020B7A1`（◎） | ◎ |
| `interval` | 该系有兵法发动间隔被缩短（**优先级最高**） | `0x2020B3A1`（○） | ○ |

**未配置** = 回落原生（精通 → ☆，精进 → ◎）。
两边都用**空心** ☆ U+2606（不用实心 ★，游戏字体里没有）。

---

## 5. 注入代码地图

### 5.1 我们自己新增/修改的功能（`Feature2026` 开关控制）

| 开关 | ini 键 | 控制的东西 | 主要地址 |
|---|---|---|---|
| DeathGradient | `DeathGradient` | 阵亡能力渐变 | `0x502667`(3B) / `0x50266D` / `0x502677` |
| GrowthBoost | `GrowthBoost` | 成长倍率 | `dword_42F27C+0x40` 跳板 + 分配的桩 |
| CultivateTrainNum | `CultivateTrainNum` | 小兵培养：强制提拔 | `0x40CAEF` / `0x40BF25` |
| CultivateBasicAbility | `CultivateBasicAbility` | 小兵培养：基础能力系数反转 | `dword_42F11C+0x1D` / `+0x2E` |
| CultivateDuration | `CultivateDuration` | 培养时长（**未实现**，只记录） | — |
| ProficiencyCap | `ProficiencyCap` | 熟练度上限 | `AllocationBase+0x2190` + `uint16[800][9]` 表 |
| ProficiencyMark | `ProficiencyMark` | 武将名后的 ☆/◎/○ | `AllocationBase+0x6960` 跳板 + `uint32[800][9]` 表 + 188B 块 |
| ActivationBoost | `ActivationBoost` | **把握概率**（发动判定骰） | hook `0x4FEC59`，恢复 `0x4FEB90` |
| CriticalHit | `CriticalHit` | **威力一定加倍 / 会心** | hook `0x50603B` |
| Interval | `Interval` | **兵法发动间隔** | hook `0x4FF08B` |
| BarbarianFormation | `BarbarianFormation` | 玩家蛮族阵型 | `0x4FB60C` / `0x4FB610` / `0x4FB614` + `AllocationBase+0xA000`（关掉时写回 `0x4FB5D3`） |
| ComputerWorkTiming | `ComputerWorkTiming` | 电脑作业时机 | `0x6574F0` / `0x6574C0` |
| SkipSiegeSelection | `SkipSiegeSelection` | 跳过耐久攻城选择 | `0x4B87EB` + `state+6824` 分支指针 |

> ⚠️ **开关名与语义曾经对不上**（已修正）：
> `ActivationBoost` = 把握概率，`CriticalHit` = 威力一定加倍，`Interval` = 兵法发动间隔。
> 这三个名字沿用了 `OfficerPersonalization2026` 的字段名，
> `FeatureSwitches2026.h` 的枚举注释是权威。

### 5.2 2019 工具遗产的核心钩子（无条件安装）

`installCoreHooks()` 里约 300 个 `writePatch`，写的是游戏 `.text` 与注入分配区的
`dword_42Fxxx` 槽。这是原版 2019 工具的功能集合（金钱/粮食削减、军师报告系数、
建筑防御、俘虏处理、都市人口系数等），**不属于本轮新增**，因此没有单独开关。

完整清单见 `src/generated/InjectionNames2026.h`（300+ 常量）与
`src/generated/write_catalog.json`（逐调用点的目标/来源/字节）。

**重要**：`dword_42F038` 处的原版遗留块被**故意 stub 成 `C2 04 00`（`ret 4`）**，
因为它导致了崩溃 #3 / #4。**不要「恢复」它。**

### 5.3 关键 hook 点

| 用途 | hook 地址 | 原始字节 | 返回地址 | 说明 |
|---|---|---|---|---|
| 把握概率（发动骰） | `0x4FEC59` | `6A 0B 52 FF D5` | `0x4FEC5E` | `push 0xB; push edx; call ebp` |
| 威力一定加倍 | `0x50603B` | `83 C4 0C 85 C0` | `0x506040` | `add esp,0xC; test eax,eax` |
| 兵法发动间隔 | `0x4FF08B` | `F7 F9 8B CE 50` | `0x4FF090` | `idiv ecx`，**必须**覆盖到 `push eax` 之前，否则会吞掉 `call 0x4FB760` |
| 熟练度图标绘制 | `AllocationBase+0x6960` | — | — | 5 字节 `E9` 跳板 → 188 字节块 |
| 抵抗判定（**只恢复，不 hook**） | `0x4FEB90` | `BE 0A 00 00 00` | — | ⚠️ 旧版误 hook 在这里 |

---

## 6. 逐功能实机验证状态

> 完整清单与操作步骤见 `docs/LIVE_TEST_PLAN.md`。

| 功能 | 离线测试 | 实机 | 备注 |
|---|---|---|---|
| 熟练度图标（图标块/图标表/跳板） | ✅ `--proficiency-mark-injection` | ⬜ | 2026-11-07 崩溃已修，**必须实机复验** |
| 蛮族阵型（跳转表 12/13/14） | ✅ `--barbarian-formation` | ⬜ | 静态坐实，未实机 |
| 熟练度上限 | ✅ | ⬜ | |
| 把握概率 | ✅ | ⬜ | ⚠️ 从抵抗骰改到发动骰后**未实机复验** |
| 威力一定加倍 | ✅ | ⬜ | |
| 兵法发动间隔 | ✅ | ⬜ | |
| 成长倍率 | ✅ | ⬜ | |
| 阵亡渐变 | ✅ | ⬜ | |
| 小兵培养 | ✅ | ⬜ | |
| 电脑作业时机 | ✅ | ⬜ | |
| 跳过耐久攻城选择 | ✅ | ⬜ | |
| 培养时长 | ❌ | ❌ | **功能不存在**，开关是占位 |

---

## 6.1 已被推翻的结论（**不要再走回头路**）

| 被推翻的结论 | 真相 | 怎么被推翻的 |
|---|---|---|
| 「培养时长变量在 `0x14FA656`」 | 那是 `.text` 里的一个**代码字节**。2019 版工具根本没有培养时长功能 | RVA 落在 .text 段；查无引用 |
| 「往 `0x6613EE` 写 1 字节可以强制提拔」 | 那是 id dword `0x6613EC` 的第 2 字节 → id 被改坏 → 空指针解引用 | 12:48 的崩溃转储，栈上 `[esp+4]=0`、`[esp+0x10]=0x3E8` |
| 「禁用 `unk_42C894` 就能消除 `0x5B6754`」 | **错误归因**。禁用之后仍然崩同一地址 | 12:48 转储证伪 |
| 「140 字节覆盖游戏 `0x425F88`」 | 写的是**注入分配区** `0xA6960`；`0x425F88` 是 companion 镜像的**来源**。全仓核实**没有任何代码写游戏 VA `0x425F88`** | `PatchCatalog.h` 的 `StaticWrite` 字段顺序是 `{callVA, functionVA, targetVA, sourceVA, size, bytes}` |
| 「要定位图标绘制代码必须 live 取证」 | 静态就能查到（`codePersonCharNum`，2019 源码 `InsertAsm.h:10230`） | 静态证据链完整 |
| 「游戏字体里没有 ★」 | 有，CP950 `a1 b9`。错在用了 Big5 的 `a1 49` | 编码表更正 |
| 「兵法表是 39 条」 | **41 条**（`0x453720`: `cmp eax,0x29`） | 静态反汇编 |
| 「阵型属性查表函数在 `0x4FB564`」 | 入口是 **`0x4FB560`**。`0x4FB564` 是函数内第二条指令（`or eax,-1`），Shape.exe 打的正是它 | 逐字节反汇编 |
| 「把握概率 hook 在 `0x4FEB90` / `0x4FEB95`」 | `0x4FEB90` 是**抵抗判定**的掷骰点（`eax≠0` = 抵抗成立）。把握概率要挂**发动骰** `0x4FEC59` | 游戏里有两枚骰子；旧 hook 强制 `eax=1` ⇒ **必定被抵挡**且熟练度完全不参与 |
| 「1047『电脑用蛮族阵型』和『玩家选蛮族阵型』是同一个功能」 | 完全不同。旧功能重写**部队**的阵型指针 `[ecx+0x6C]`（掷骰 + `0x453890` 查表）；新功能改**阵型属性跳转表** `0x4FB5DC` 的 12/13/14 三项 | 两段代码块逐字节反汇编对比；旧功能已按用户要求移除（首字节恒为 `0xC3`） |

---

## 6.2 已知的模型与现实不一致

| 项 | 模型里 | 现实里 | 影响 |
|---|---|---|---|
| `writeBytes` 的 `inGame` 判定范围 | `0xA0000..0x2000000` | 游戏 VA 是 `0x400000..0x600000` | 测试用 `base=0x22000000` 的分支，所以**实测通过**；但往 `.text` 写（蛮族阵型跳转表）依赖 `writeBytes` 内部改保护，实机需复核 |
| `CultivateDuration` 开关 | 存在 | 功能未实现 | 开了也不生效，日志会说明 |

---

## 7. 蛮族阵型（2026-11-07 完整逆向记录）

### 7.1 阵型属性查表函数

**入口 `0x4FB560`**：

```
0x4FB560  mov  ecx,[esp+4]              ; 形参：阵型编号
0x4FB564  or   eax,0xFFFFFFFF           ; 越界 -> -1
0x4FB567  cmp  ecx,0x1B
0x4FB56A  ja   0x4FB5D8
0x4FB56C  jmp  dword ptr [ecx*4+0x4FB5DC]   ; 28 项跳转表
0x4FB573  mov  eax,0x39C ; ret          ; case 0 的桩
```

**跳转表在 `0x4FB5DC`**，28 项，每项指向一段 `mov eax,imm32; ret`：

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

- case 12 / 13 / 14 **共用**同一个桩 `0x4FB5D3`（返回 `0x3C3`，异族专用属性）
- case 27 也用 `0x4FB5D3`
- case 15..19 与 24..26 指向 `0x4FB5D8`（返回 −1）
- **唯一调用点**：`0x4FBEB6 call 0x4FB560`（形参 `ebx` 是阵型编号）

**结论**：普通势力选不了 12/13/14 号阵型，因为它们的属性 `0x3C3` 是异族专用的。

### 7.2 Shape.exe（第三方修改器）的做法

| 地址 | 补丁 | 效果 |
|---|---|---|
| `0x4701E5` | 1 字节 `0x0B`→`0x15`（`push 0x0B` 的 imm8） | **与阵型无关** |
| `0x470205` | 1 字节 `0x17`→`0x14`（`push 0x17` 的 imm8） | **与阵型无关** |
| `0x4FB564` | 8 字节 `B8 CC 03 00 00 90 90 90` = `mov eax,0x3CC` + 3 nop | **整个函数恒返回 0x3CC，所有阵型属性都被抹平** |

### 7.3 我们的实现（比 Shape.exe 温和）

- 只把跳转表的 **12/13/14 三项**改指向我们自己的 6 字节桩
  `B8 CC 03 00 00 C3`（`mov eax,0x3CC; ret`）
- 其余 25 项**原样不动** → 普通阵型的属性完全不受影响
- 桩放在 `AllocationBase + 0xA000`（分配区 11 页 = 0xB000，已用到 `0x6960+188`，
  所以 `0xA000` 是空的）
- 写 3 个 dword 到 `0x4FB5DC + index*4` → `0x4FB60C` / `0x4FB610` / `0x4FB614`
- **这两处补丁写的是游戏的 `.text`**（`0x4FB5DC`），需要 `VirtualProtectEx` 改保护属性

### 7.4 开关组合与「写回原值」（2026-11-07 补充）

本功能由**两个开关**共同控制，**两处都开才生效**：

| 开关 | 位置 | 角色 |
|---|---|---|
| `BarbarianFormation` | `San9Features.ini` `[Features]` | 逐功能测试用的总开关（隔离其它功能的干扰） |
| 1047「启用玩家蛮族阵型」 | 设置窗口复选框 | 玩家日常使用的开关 |

生效条件 = `ini 开关 AND 复选框`（`Runtime2026::applyBarbarianFormation()`）。
任一关掉 → 12/13/14 **写回游戏原始值 `0x4FB5D3`**（`mov eax,0x3C3; ret`）。

> ⚠️ **必须写回，不能只是「不再写」**：桩在伴侣进程分配区里，
> 只改不还原 = 游戏里留着指向伴侣进程的指针 → 伴侣退出后野指针崩溃。
> `0x4FB5D3` 与游戏出厂状态**逐字节一致**（已用 `San9PK.exe` 核对）。

日志：
```
FEATURE barbarianFormation : on, entries 12/13/14 now return 0x3CC (stub at 0x...)
FEATURE barbarianFormation : off, entries 12/13/14 restored to 0x4FB5D3
                               (the ini switch is off)
                               (the check box 1047 is off)
```

### 7.5 旧的「电脑用蛮族阵型」是**另一套机制**（已移除）

2019 工具的 `m_bEnableBaramArray`（1047 的原含义）与「玩家选蛮族阵型」无关：

| 项 | 内容 |
|---|---|
| 代码块 | `codeUseBaramArray`，88 字节，上传到 `AllocationBase+0x3EA0` |
| 作用 | 重写**每个部队**的阵型指针 `[ecx+0x6C]` |
| 关键调用 | `0x4AEFF0`（掷骰 wrapper）+ `0x453890`（阵型记录查表） |
| 开关机制 | 把代码块**首字节**写成 `0x56`（执行）或 `0xC3`（直接返回） |

**结论**：它改的是「电脑部队用哪个阵型」，不是「玩家能不能选蛮族阵型」。
按用户要求，本轮用「玩家蛮族阵型」**替换**掉它：
代码块仍上传，但首字节**恒为 `0xC3`**（永不执行）。
注入名字 `EnableBaramArrayEnabled` → `PlayerBarbarianFormation`。

---

## 8. 熟练度图标注入块（188 字节）字节图

| 偏移 | 内容 |
|---|---|
| 0x00 | 入口序列 `8B 2F 8B CB F6 C1 01` |
| 0x07 | 近跳转守卫 `0F 85`（jne） |
| 0x12 | 近跳转守卫 `0F 8C`（jl） |
| 0x1B | 近跳转守卫 `0F 8F`（jg） |
| 0x26 | 近跳转守卫 `0F 85`（jne） |
| 0x2C | `add byte [esp+0x5C], 4`（给图标预留 4 字节） |
| 0x31 | 4 个空格前缀 `BF 20 20 20 20` |
| 0x4C | `mov edi,[eax*4+table]`（`8B 3C 85`） |
| **0x4F** | **disp32 = 图标表地址** |
| 0x53 | `test edi,edi`（`85 FF`）+ `74` 跳回原生标记 |
| **0x68** | 白星 `BF A1 B8 20 20`（☆） |
| **0x80** | **disp32 = 等级表地址**（`AllocationBase+0x4000`） |
| **0x8E** | 靶心 `BF A1 B7 20 20`（◎） |

**等级表地址**：2019 工具 `person_config_info = 0xA4000` ⇒ 运行时
`AllocationBase + 0x4000`。已核实正确。

**精通位**：原版 `sub cl,0x2E`（位 3..11）我们的写法等价。已核实，**不是 bug**。

---

## 9. 逐功能开关机制

### 9.1 文件

- **位置**：与 exe 同目录的 `San9Features.ini`
- **读取时机**：启动时一次（`MainWindow.cpp` 的 `!smoke` 分支 → `app.loadFeatureSwitches(...)`）
- **段名**：`[Features]`
- **值**：只认精确的 `0` / `1`；**其它值一律当 off**（手误不应该打开一个功能）
- **缺键** = 保持默认（默认全部 on ⇒ 空文件 ≡ 开关引入前的行为）
- **运行期不再写回**

### 9.2 语义

| 开关状态 | 行为 |
|---|---|
| on | 注入相关代码 |
| off | **完全不注入**（不是「注入了但不起作用」），日志 `FEATURE xxx : off, nothing is injected` |

### 9.3 日志

```
FEATURES: file=... summary=1 0 1 1 ...   (1=on 0=off，顺序与 ini 一致)
FEATURE barbarianFormation : off, entries 12/13/14 restored to 0x4FB5D3
```

`summary()` 的顺序 = `Feature2026` 枚举顺序 = ini 文件顺序 → 可逐项对照。

---

## 10. 关键文件索引

| 主题 | 文件 |
|---|---|
| 逐功能开关 | `src/runtime/FeatureSwitches2026.{h,cpp}`、`San9Features.ini` |
| 蛮族阵型 | `src/runtime/BarbarianFormation2026.h`（常量 + 逆向记录）|
| 逐功能开关与复选框的关系 | `docs/HANDOVER.md` §4.12、`docs/LIVE_TEST_PLAN.md` T3 |
| 熟练度图标规则 | `src/runtime/ProficiencyMark2026.h` |
| 注入主逻辑 | `src/runtime/Runtime2026.cpp` |
| 2019 遗产钩子 | `src/runtime/RuntimeInjection2026.cpp` |
| 布局 | `src/generated/RuntimeLayout2026.h` |
| 注入地址常量 | `src/generated/InjectionNames2026.h`、`src/generated/StateConstants.inc` |
| 写入调用点审计 | `src/generated/write_catalog.json`、`src/generated/PatchCatalog.h` |
| 分支目标校验 | `tools/check_injection_blocks.py` |
| 字节级反汇编校验 | `tools/asm_check.py` |
| 静态资源重建 | `tools/reconstruct.py` |
| 对话框字幕定制 | `tools/dialog_customizations.py` |

> ⚠️ **`assets/ui/dialogs.rc` 不能直接用 `tools/reconstruct.py` 重新生成**：
> 生成器会丢掉手工做过的界面布局改动（对话框 132 的「武将个性化」区块会消失），
> 并把简体「斗舰」变回繁体「鬥舰」。改动字幕请**直接编辑 `.rc`**，
> 同时更新 `tools/dialog_customizations.py` 保持两者一致。

---

## 11. 工作纪律（血的教训）

### 11.1 文件操作

- ❌ **绝不用 `sed -i` 改 CRLF 文件** —— 会把整行尾变成 LF。
  `tests/runtime_test.cpp` 是 CRLF，用 Python 读写并显式保留 `\r\n`。
- ❌ **绝不 `git checkout` 未提交的工作** —— 会永久丢失。
  （2026-11-07 就因为 `git checkout tests/runtime_test.cpp` 丢掉了整段未提交的
  `proficiencyMarkInjectionTest`，最后靠 一个一次性 Python 补丁脚本 重写恢复。）
- ✅ 改 CRLF 文件用 Edit 工具，或用 Python 显式处理换行。
- ❌ **不要用 `tools/reconstruct.py` 重新生成 `assets/ui/dialogs.rc`** ——
  会丢掉手工布局改动（对话框 132 的「武将个性化」区块消失），
  并把简体「斗舰」变回繁体「鬥舰」。改 `.rc` 直接编辑文件。
- ⚠️ **改「生成物」之前先想清楚生成器**：生成物（`src/generated/*.json`）
  由 `tools/recover_*.py` 生成。如果手工改了生成物而没改生成器，
  下次跑生成器就丢了（而且 `Verify.ps1` 会挂）。
  正确做法是**改生成器 + 改 `tools/dialog_customizations.py` 里的定制规则**，
  再跑 `python tools/recover_parameters.py`。
  （2026-11-07 就是这么发现 `Verify.ps1` 里 `test_generated_metadata_current`
  从 commit `326098d` 起就一直在失败：1160 的 caption 是简体「斗舰」，
  资源证据里是繁体「鬥舰」，两边一直不一致。）

### 11.2 逆向

- ✅ 结论必须有**字节级反汇编**支撑，注释里写清楚反汇编原文。
- ✅ 写进代码的地址必须先确认是**数据**不是**代码**。
- ✅ 做完修复做一次**反向验证**（把 bug 加回去看测试是否变红）。
- ❌ 不要在没弄清整体结构时长时间深挖 —— 先把能交付的交付了。

### 11.3 提交

- ❌ **不要自己 `git commit` / `git push`** —— 用户自己提交。
- ✅ 提交前 `git status --short` 确认没有 `out/`、`build/` 产物。
