# 会话续接速记（2026-11-07）

> 详细状态见 `docs/HANDOVER.md`（唯一事实源）。
> **逆向结论的单一可信来源是 `docs/KNOWLEDGE_BASE.md`。**
> 逐功能实机清单见 `docs/LIVE_TEST_PLAN.md`。
> 本文件只是快速索引。

## 2026-11-07 第二轮：1047 重新接线（「玩家蛮族阵型」替换「电脑用蛮族阵型」）

> 用户原话：「是的，改完给我发版」「我要开始逐个测试了，首先我会测试 玩家蛮族阵型这个」
> 「的旧功能（2019 工具的「电脑用蛮族阵型」**不需要保留**」
> 「相当于用玩家用蛮族阵型 **替换掉**」

### 0. 一句话

**1047 从「驱动旧的电脑蛮族阵型」改成「驱动玩家蛮族阵型」，
并且真的能开/关（旧功能彻底停用）。发版完成，实机待用户测 T3。**

### 1. 最重要的结论：旧功能和新功能是两套机制

| | 旧：电脑用蛮族阵型 | 新：玩家蛮族阵型 |
|---|---|---|
| 注入名 | `m_bEnableBaramArray` | `PlayerBarbarianFormation`（`state+176`）|
| 代码块 | `codeUseBaramArray` 88B @ `Alloc+0x3EA0` | 6B 桩 @ `Alloc+0xA000` |
| 改什么 | 每个**部队**的阵型指针 `[ecx+0x6C]` | 阵型属性**跳转表** `0x4FB5DC` 的 12/13/14 |
| 手段 | 掷骰 `0x4AEFF0` + 查表 `0x453890` | 3 个 dword 指向返回 `0x3CC` 的桩 |
| 开关 | 块首字节 `0x56`/`0xC3` | ini 开关 AND 复选框 1047 |

**别把两者搞混**——这是接手的人最容易踩的坑。

### 2. 开关逻辑（两个开关都要开）

```
生效 = ini 的 BarbarianFormation  AND  设置窗口的复选框 1047
```

任一关掉 → 12/13/14 **写回游戏原值 `0x4FB5D3`**。

> **必须写回，不能只是「不再写」**：桩在伴侣进程的游戏分配区里。
> 只改不还原 = 游戏里留着指向伴侣进程的指针，伴侣退出后 → 野指针崩溃。

日志：
```
FEATURE barbarianFormation : on,  entries 12/13/14 now return 0x3CC (stub at 0x...)
FEATURE barbarianFormation : off, entries 12/13/14 restored to 0x4FB5D3
                               (the ini switch is off)      ← ini 关
                               (the check box 1047 is off)  ← 复选框关
```

### 3. 改了哪些文件

| 文件 | 改动 |
|---|---|
| `src/runtime/Runtime2026.cpp` | `applyBarbarianFormation()` 重写（两个开关 + 写回原值）|
| `src/runtime/RuntimeInjection2026.cpp` | 删旧写；固定写 `0xC3`（旧功能永久停用）|
| `src/runtime/BarbarianFormation2026.h` | 新增 `OriginalBarbarianFormationEntry = 0x4FB5D3` |
| `src/generated/InjectionNames2026.h` | `EnableBaramArrayEnabled` → `PlayerBarbarianFormation` |
| `src/model/InjectionNames2026.json` | 同上（改了跑 `python tools/injection_names.py`）|
| `src/generated/parameters.json` | 1047 caption → 「启用玩家蛮族阵型」 |
| `tools/dialog_customizations.py` | 1047 规则；**新增 1160「鬥舰」→「斗舰」** |
| `tools/recover_parameters.py` | caption 过 `control_title()` |
| `tests/runtime_test.cpp` | `barbarianFormationTest()` 扩到 5 种情形 |
| `San9Features.ini` / `out/trial/使用说明.txt` | 说明两个开关的关系 |
| `docs/HANDOVER.md` §4.12 / `docs/KNOWLEDGE_BASE.md` §7.4 §7.5 / `docs/LIVE_TEST_PLAN.md` T3 / `CHANGELOG.md` | 文档 |

### 4. 顺手修掉的一个旧失败

`Verify.ps1` 里的 `test_generated_metadata_current` **在 HEAD（29b3e88）上就已经失败**：
提交进仓的 `parameters.json` 里 1160 的 caption 是简体「斗舰」，
而资源证据（`recovery/resources/dialogs.json`）里是繁体「鬥舰」。
本轮把 caption 生成改为过 `control_title()`，并补上 1160 的简化规则 → **31 tests OK**。

### 5. 验证结果

- Debug / Release / Publish 三配置构建通过
- `--all assets/config` → **exit=0**
- `tools/check_injection_blocks.py` → **12 blocks, 0 problems**
- `powershell -NoProfile -ExecutionPolicy Bypass -File ./Verify.ps1` → **31 tests OK**
- `out/trial/` 已重新出包（Publish exe + `San9Features.ini` + 6 个配置 + 使用说明）

### 6. 下一步（用户正在做的事）

**实机跑 `docs/LIVE_TEST_PLAN.md` 的 T3（玩家蛮族阵型）**，
然后按 T1…T11 顺序逐个测。每测完一项，把结果写回
`docs/LIVE_TEST_PLAN.md` 的验证状态表 + `docs/HANDOVER.md` §0 速览。

---

## 2026-11-07 第一轮完成（B 修 bug → C 开关 → A 知识资产化）

### 0. 本轮总览

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| **B** | 修图标崩溃 + 删掉错误的培养时长写入 | ✅ |
| **C** | 13 个逐功能开关 + 玩家蛮族阵型 | ✅ |
| **A** | `docs/KNOWLEDGE_BASE.md` + `docs/LIVE_TEST_PLAN.md` | ✅ |

**剩余唯一阻塞项：实机验证。**

### 1. 图标崩溃（根因用转储坐实）

- 转储：`%LOCALAPPDATA%\CrashDumps\San9PK.exe.29292.dmp`（11:26）
- 异常码 `0xC0000005`，读地址 `0x02227028`，我们的块在 `0x02227080`
- `0x02227028 = 0x02227080 + 0x09 − 97` → 正是守卫跳转的目标
- **根因**：172 字节块里 4 个守卫跳转到块尾，位移 159/152/147/140 **全部 > 127**；
  x86 rel8 是**有符号**的，`0x9F` 被解释成 −97 → 跳到块外 88 字节 → 访问违例。
  原版 140 字节块的位移**恰好**是 127（有符号上限），纯属侥幸。
- **修复**：4 个守卫改**近跳转**（`0F 84/8C/8F` rel32），块 172 → **188 字节**。
  disp32 偏移：图标表 @ **0x4F**，等级表 @ **0x80**。
- 修复时把 disp32 写到 `0x4B`，**被新增的测试断言当场抓住** → 改成 `0x4F`。✅

### 2. 删除错误的培养时长写入

- `0x14FA656` 是**猜的**地址，且是错的：RVA `0xFA656` 落在 **.text**
  → 是**游戏代码的一个字节**，不是变量。每次应用配置都往那儿写 9，**一直在改坏游戏代码**。
- 2019 版工具**根本没有**培养时长功能（只有 `m_dwTrainNumAddr` 0x40CAEF /
  `m_dwTrainNumAddr2` 0x40BF25 / `m_dwOldTrainNum` 50）。
- **处理**：删除该写入，配置项保留但不生效，日志明说。

### 3. 新增 `tools/check_injection_blocks.py`

- 读 `src/runtime/Runtime2026.cpp` 里的 `const BYTE xxx[] = {...}`，capstone
  反汇编，断言每条跳转：① 落在指令边界 ② 在块内 ③ rel32 占位（全 0）跳过
- 当前 **11 个块，0 个问题**
- 坑：注释里的 hex 字面量会被误读 → 已加 `strip_comments`；
  游戏代码片段（growth boost 的 `original[6]`）单独跳过。

### 4. 逐功能开关（13 项）

**新增文件**
- `src/runtime/FeatureSwitches2026.h` / `.cpp`
  - `enum class Feature2026`：deathGradient / growthBoost / cultivateTrainNum /
    cultivateBasicAbility / cultivateDuration / proficiencyCap / proficiencyMark /
    activationBoost / criticalHit / interval / barbarianFormation /
    computerWorkTiming / skipSiegeSelection（共 13 个）
  - 默认**全部为 on**（空 ini ≡ 开关引入前的行为）
  - `loadFeatureSwitches(path)`：读 ini `[Features]` 段；**只认精确的 `0`/`1`**，
    其它值一律当 off（防手误打开功能）
  - `summary()`：输出 "1 0 1 ..." 供日志对照
- `San9Features.ini`（仓库根目录，含中文注释）—— 全部 13 项默认 1
- `src/runtime/BarbarianFormation2026.h`

**已改代码**
- `Runtime2026.h`：`#include "FeatureSwitches2026.h"`、`setFeatureSwitches()` /
  `featureSwitches()` / `featureEnabled()`、成员 `featureSwitches_`、
  3 个新私有方法 `applyActivationBoost/applyCriticalHit/applyInterval`、
  `applyBarbarianFormation()`
- `Runtime2026.cpp`：
  - `applyDeathGradient` / `applyGrowthBoost` / `applyProficiencyCap` /
    `applyProficiencyMarks` / `applyComputerWorkTiming` 开头加开关守卫
    （关闭时**完全不注入**，日志 `FEATURE xxx : off, nothing is injected`）
  - `applyOfficerPersonalization` **拆成 3 个独立函数**：
    `applyActivationBoost` / `applyCriticalHit` / `applyInterval`，各自有开关
  - `applyCultivate` 加了 `cultivateDuration` 开关（仅记录，未实现）
  - `applyCultivate` 删除 `0x14FA656` 写入
  - **新增 `applyBarbarianFormation()`**
- `RuntimeInjection2026.cpp`：
  - `0x40CAEF`/`0x40BF25` 的写入包在 `cultivateTrainNum` 开关里
  - `codeTrainBasicAbility` 的 `sub`→`add` 两处（偏移 `+0x1D` / `+0x2E`）
    包在 `cultivateBasicAbility` 开关里
  - `NeglectChongCheAddr` 的写入包在 `skipSiegeSelection` 开关里
- `src/runtime/AppPaths.h`：新增 `featureSwitchesFile()` → exe 同目录 `San9Features.ini`
- `src/app/application/Controller.h/.cpp`：新增 `loadFeatureSwitches(path)`，
  日志 `FEATURES: file=... summary=1 0 1 ...`
- `src/app/ui/MainWindow.cpp`：启动时（`!smoke` 分支）调用
  `app.loadFeatureSwitches(san9::featureSwitchesFile())`
- `build-config/Runtime.vcxproj`：加入 `FeatureSwitches2026.cpp`
- `build-config/Companion.vcxproj`：把 `San9Features.ini` 复制到输出目录

### 5. 蛮族阵型（**已实现并通过测试**）

**逆向结论（全部逐字节反汇编核对过）**

游戏里的「阵型属性查表函数」**入口是 0x4FB560**（不是 0x4FB564）：

```
0x4FB560  mov  ecx,[esp+4]              ; 形参：阵型编号
0x4FB564  or   eax,0xFFFFFFFF           ; 越界 -> -1
0x4FB567  cmp  ecx,0x1B
0x4FB56A  ja   0x4FB5D8
0x4FB56C  jmp  dword ptr [ecx*4+0x4FB5DC]   ; 28 项跳转表
0x4FB573  mov  eax,0x39C ; ret          ; case 0 的桩
```

跳转表在 **0x4FB5DC**，28 项，每项指向一段 `mov eax,imm32; ret`：

| case | 返回 | case | 返回 |
|---|---|---|---|
|0|0x39C|14|**0x3C3**|
|1|0x39F|15..19|-1|
|2|0x396|20|0x3AB|
|3|0x399|21|0x3AE|
|4|0x3A2|22|0x3B1|
|5|0x3A5|23|0x3B4|
|6|0x393|24..26|-1|
|7|0x3A8|27|**0x3C3**|
|8|0x3BD| | |
|9|0x3B7| | |
|10|0x3BA| | |
|11|0x3C0| | |
|12|**0x3C3**| | |
|13|**0x3C3**| | |

- 12/13/14 **共用**同一个桩 0x4FB5D3（返回 0x3C3，异族专用属性）
- case 27 也用 0x4FB5D3（返回 0x3C3）
- 15..19 与 24..26 指向 0x4FB5D8（返回 −1）
- 唯一调用点：`0x4FBEB6 call 0x4FB560`（形参 `ebx` 是阵型编号）

**Shape.exe（第三方修改器）的做法**（已逐字节核对）：
- `0x4701E5`：1 字节 `0x0B`→`0x15`（`push 0x0B` 的 imm8）—— 与阵型无关
- `0x470205`：1 字节 `0x17`→`0x14`（`push 0x17` 的 imm8）—— 与阵型无关
- `0x4FB564`：8 字节 `B8 CC 03 00 00 90 90 90` = `mov eax,0x3CC` + 3 nop
  → **整个函数恒返回 0x3CC**，所有阵型属性都被抹平

**我们的实现（比 Shape.exe 温和）**
- 只把跳转表的 **12/13/14 三项**改指向我们自己的 6 字节桩
  `B8 CC 03 00 00 C3`（`mov eax,0x3CC; ret`）
- 其余 25 项**原样不动** → 普通阵型的属性完全不受影响
- 桩放在分配区 `AllocationBase + 0xA000`
  （分配区 11 页 = 0xB000，已用到 0x6960+188，所以 0xA000 是空的）
- 写 3 个 dword 到 `0x4FB5DC + index*4` → 0x4FB60C / 0x4FB610 / 0x4FB614

**注意**：这两处补丁写的是**游戏的 .text**（0x4FB5DC），需要
`VirtualProtectEx` 改保护属性。运行时用的是 `writeBytes`，
测试桩里 `inGame` 范围是 `0xA0000..0x2000000`（绝对地址），
实测通过（测试用 base=0x22000000 的分支）。

**界面字幕**：`assets/ui/dialogs.rc` 控制 1047 由「启用电脑蛮族阵型」
改为「**启用玩家蛮族阵型**」（`tools/dialog_customizations.py` 同步）。
⚠️ `.rc` **不能**用 `tools/reconstruct.py` 重新生成（会丢掉手工布局改动，
并把简体「斗舰」变回繁体「鬥舰」），要改就直接编辑 `.rc`。

**两个开关 + 可逆（2026-11-07 补充）**

生效条件 = ini `BarbarianFormation` **AND** 复选框 1047。
任一关掉 → 12/13/14 **写回游戏原始值 `0x4FB5D3`**（`mov eax,0x3C3; ret`），
日志 `FEATURE barbarianFormation : off, entries 12/13/14 restored to 0x4FB5D3
(the ini switch is off | the check box 1047 is off)`。

> **为什么必须写回**：桩在伴侣进程的游戏分配区里。只改不还原 = 游戏里留着
> 指向伴侣进程的指针，伴侣退出后就是野指针 → 崩溃。

**旧功能「电脑用蛮族阵型」已移除（2026-11-07）**

用户要求「用玩家用蛮族阵型替换掉」旧的「电脑用蛮族阵型」。
两者是**完全不同的机制**：

| | 旧功能（已移除） | 新功能 |
|---|---|---|
| 作用对象 | 每个**部队**的阵型指针 `[ecx+0x6C]` | 阵型**属性跳转表** `0x4FB5DC` 的 12/13/14 |
| 手段 | 掷骰 `0x4AEFF0` + 查表 `0x453890` | 3 个 dword 指向返回 `0x3CC` 的桩 |
| 开关 | 代码块首字节 `0x56`(执行) / `0xC3`(返回) | ini 开关 AND 复选框 1047 |

- 旧代码块仍上传，但首字节**恒为 `0xC3`**（永不执行）
- 注入名字 `EnableBaramArrayEnabled` → `PlayerBarbarianFormation`
  （`src/model/InjectionNames2026.json` → `src/generated/InjectionNames2026.h`）
- `src/generated/parameters.json` 里 1047 的 caption 也改成「启用玩家蛮族阵型」

### 6. 开关拆分粒度（按用户批准的"你倾向的拆开"）

| 开关 | 控制的东西 |
|---|---|
| DeathGradient | 阵亡能力渐变 |
| GrowthBoost | 成长倍率 |
| CultivateTrainNum | 小兵培养：强制提拔（0x40CAEF / 0x40BF25）|
| CultivateBasicAbility | 小兵培养：基础能力系数反转 |
| CultivateDuration | 培养时长（**未实现**，只记录）|
| ProficiencyCap | 熟练度上限 |
| ProficiencyMark | ☆/◎/○ 图标 |
| ActivationBoost | **把握概率**（发动骰 0x4FEC59）|
| CriticalHit | **威力一定加倍 / 会心**（0x50603B）|
| Interval | **兵法发动间隔**（0x4FF08B）|
| BarbarianFormation | 玩家蛮族阵型 |
| ComputerWorkTiming | 电脑作业时机 |
| SkipSiegeSelection | 跳过耐久攻城选择 |

⚠️ **命名与语义曾经对不上**：`ActivationBoost` / `CriticalHit` / `Interval`
这三个键名沿用了 `OfficerPersonalization2026` 的字段名，实际语义是
「把握概率 / 威力一定加倍 / 兵法发动间隔」。
`FeatureSwitches2026.h` 的枚举注释与 `San9Features.ini` 的注释是权威。

### 7. 测试现状（最新）

- Debug 构建通过
- `--all assets/config` **exit=0**（全部通过）
- `--barbarian-formation` 通过
- `--proficiency-mark-injection` 通过
- `tools/check_injection_blocks.py` 12 块 0 问题
- Release + Publish 构建通过，`out/trial/` 已重新出包（含 `San9Features.ini`）

**测试文件历史事故**：`git checkout tests/runtime_test.cpp` 曾经把
未提交的 `proficiencyMarkInjectionTest`（188 字节断言 + 4 条近跳转断言）
整个丢掉，已用 一个一次性 Python 补丁脚本 重写恢复。**该文件是 CRLF**，
用 `sed -i` 会把整行尾变成 LF → 之后用 Python 读写并显式保留 `\r\n`。
**绝不要 `git checkout` 未提交的工作。**

### 8. 新增文档

- `docs/KNOWLEDGE_BASE.md` —— 知识总表：地址→用途→证据→置信度；数据模型；
  CP950 字形表；**x86 注入陷阱库**（rel8 有符号！rel32 字段偏移约定不同！栈平衡！…）；
  已被推翻的结论；注入代码地图；实机验证状态表。
- `docs/LIVE_TEST_PLAN.md` —— 逐功能实机测试清单：
  每项写清「开哪些开关 / 期望结果 / 怎么判断」+ 二分法定位步骤。

---

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

# 2026-11-07 本轮：游戏内 ★/◎/○ 指示图标（Request F 续）—— **已定位并实现**

> 本段记录最终结论。**上一轮写的「静态 RE 已穷尽、必须 live 取证」是错的，已推翻。**
> 绘制代码静态就能查到，不需要 live 取证。

## 1. 关键结论

### 1.1 游戏本体里没有 ☆/★ 字形 ⇒ 游戏内的五角星是 companion 自己画的

- 游戏的字符表 `0x61CFA0..0x61CFF8`（**Big5/CP950**）只含
  `U+FF19..U+FF10`、`•`(0x61CFD0)、`＊`(0x61CFD4)、`？`(0x61CFDC)、`完`(0x61CFE4)、
  `未`(0x61CFE8)、`△`(0x61CFEC)、`◎`(0x61CFF0)、`○`(0x61CFF4)、`×`(0x61CFF8)。
- **CP950 编码表（本项目必须记住）**：
  **☆ U+2606 = `a1 b8`、★ U+2605 = `a1 b9`、◎ U+25CE = `a1 b7`、
  ○ U+25CB = `a1 b3`、△ U+25B3 = `a1 b5`、× = `a1 d1`、： = `a1 47`、？ = `a1 48`**
  （早先误用 Big5 `a1 49` 找 ★，得出「游戏里没有 ★」的错误结论；用户提示后纠正。）

### 1.2 绘制代码 = 2019 工具遗产 `codePersonCharNum`（140 字节）

| 项 | 位置 |
| --- | --- |
| 2019 源码 | `recovery/reference-2019/InsertAsm.h:10230`（`dwAddrPersonCharNum = dwAddrStackBottom + 0x6900`） |
| companion 镜像 | `src/generated/companion_data.bin` 偏移 `0x6F88`（companion VA `0x425F88`） |
| 运行时落点 | `RuntimeInjection2026.cpp:415` `writePatch(dword_42F270, &codePersonCharNum, 0x8Cu)` ⇒ **注入分配区 `allocationBase+0x6960`** |
| 进入路径 | descriptor `0x426028`（`{target=0x451A5C, len=5, source=0x426014}`）把游戏 `0x451A5C` 的 `mov edx,[edi]; mov eax,[edx-8]` 换成 `call 0xA6960` |

⚠️ **两条被推翻的旧结论**：
1. 「140 字节覆盖游戏 `0x425F88`」**错**。`PatchCatalog.h` 的 `StaticWrite` 字段顺序是
   `{callVA, functionVA, targetVA, sourceVA, size, bytes}` ⇒ 那行
   `{0x0040C163, 0x00409E10, 0x000A6960, 0x00425F88, 140, ...}`
   的 **target = `0xA6960`（分配区）**、**source = `0x425F88`（companion 镜像位置）**。
   全仓核实：**没有任何代码写游戏 VA `0x425F88`**，游戏那 140 字节原样未动。
   `PatchCatalog.h` 也没有被任何 `src/` 文件 include（只是 `tools/reconstruct.py` 生成的审计产物）。
2. 「必须 live 取证」不成立。

### 1.3 原版 140 字节块的判定逻辑（完整反汇编）

```
mov  ebp,[edi]              ; 文本缓冲
mov  ecx,ebx / test cl,1    ; 守卫：ebx 必须为偶
shr  cl,1 ; cmp cl,'1'/'9'  ; 守卫：系统号 1..9
cmp  byte [esp+0x2C],'8'    ; 守卫：必须是「熟练度」那一行
add  byte [esp+0x5C],4      ; 给图标预留 4 字节
mov  edi,0x20202020         ; 默认 4 个空格（无图标）
mov  eax,[esi+0xE8]         ; 原生「精通」标志位（位 3..11 = 9 个系）
shr  eax,cl ; and al,1
mov  edi,0x2020B8A1         ; "  " + a1 b8 = ☆   ← 原生精通
; 再查 companion 自己的等级表 0xA4000[officer] 的 2bit*系 == 3
mov  edi,0x2020B7A1         ; "  " + a1 b7 = ◎   ← 原生精进（覆盖 ☆）
```
**它和熟练度上限数值毫无关系**：☆ 由原生「精通」位决定，◎ 由等级表第 4 档决定。

### 1.4 熟练度 / 等级 / 阈值数据模型（不变）

| 项 | 地址 |
| --- | --- |
| 熟练度存储 | `officer + 0xA4 + 系*2`（word）；getter `0x44C940` / setter `0x44C960` / limadd `0x44C980` |
| 游戏原版上限 1000 | `0x406A70`（`cmp eax,0x3E8`） |
| 等级 1/2/3/4 | `0x409220` / `0x409510`；阈值表 `0x64BD00`、`0x64BCC4`、`0x64BC88`、`0x64BD3C` |
| **1500 阈值** | `0x44E9E0`（`0x44E840` 内，唯一调用者 `0x450323`） |
| 精通/精进标志位 | `dword [officer+0xE8]`；set `0x44C8A0` / clear `0x44C8B0`；`PersonSnapshot.cpp:14` 读 `(word(0xE8)>>3)&0x1FF` |
| companion 任意上限 | `applyProficiencyCap()` 用 `uint16[800][9]` 表替换 `AllocationBase+0x2190` 的 93 字节块 |

## 2. 用户确认的口径（2026-11-07）

1. **配置优先，未配置回落原生**：配了上限或间隔 → companion 的图标；
   没配 → 原生标记（精通 → ☆，精进 → ◎）。
2. **两边都用空心 ☆ U+2606**（与 `build/res/tags.PNG` 一致）。
3. **任意熟练度上限设计保留**，不做 1300/1600 分档。

## 3. 实现（本轮已完成）

| 文件 | 改动 |
| --- | --- |
| `src/runtime/ProficiencyMark2026.h`（新） | 修改器与游戏内**共用**的规则：`ProficiencyMarkFor` / `ProficiencyMarkPrefix2026` / `ProficiencyMarkText2026`。五档 `unconfigured/none/star/refined/interval`，前缀 `0 / 0x20202020 / 0x2020B8A1 / 0x2020B7A1 / 0x2020B3A1` |
| `src/runtime/Runtime2026.cpp` | 新增 `applyProficiencyMarks()`：算 `uint32[800*9]` 前缀表 → 上传 → 写判定块（**188 字节**，见上）→ 在 `allocationBase+0x6960` 放 5 字节 `E9` 跳板 |
| `src/runtime/RuntimeInjection2026.cpp` | 在 `applyProficiencyCap()` 之后调用 `applyProficiencyMarks()` |
| `src/app/ui/Pages.cpp` | `proficiencyMark()` 改为复用共享规则；**实心 ★ U+2605 → 空心 ☆ U+2606** |
| `tests/runtime_test.cpp` | 新增 `--proficiency-mark-injection`；`--proficiency-marker` 改断言共享规则 |

**为什么用跳板**：判定块 > 原槽位 `0x6960..0x6A00` 的 160 字节，会压到
`codeMoveGuardTroop@0x6A00`。跳板（5 字节 `E9`）是 `applyOfficerPersonalization` 里
间隔 hook 用的同一套路。

**注入空间核算**：注入分配区 `0xA0000..0xB0000` 共 45KB，已占 33.2KB，
空闲连续段有 `0x095E..0x10000`（**22.2KB**）等。图标表 `uint32[800*9]` = 28.8KB
放不进分配区，所以图标表 + 判定块一起放进 `applyProficiencyMarks` 自己新分配的一块内存，
分配区只放 5 字节跳板。**空间充足。**

**一个格子都没配时不替换块**：原版 140 字节块画的本来就是原生标记，语义一致；
且这样不会多分配内存 —— 否则 undo 的 release 计数会从 1 变 2，
`runtime_test.cpp` 的 `undo releases allocation and handle` 会挂（这个坑踩过一次）。

**验证**：Debug/Release/Publish 三个配置全部构建通过；
`--all assets/config` 通过；`--ui-smoke` 通过；全套离线测试通过。

**实机待验**：游戏武将列表里打开有熟练度的界面，确认
① 配置过的格子显示 ☆/◎/○；② 没配置的格子仍显示原生 ☆/◎；③ 无崩溃。

## 4. 工具与脚本

- 教训：脚本名不要叫 `re.py`（遮蔽标准库，导致 capstone 循环导入）；用 `rex.py`。
- 教训：CP950 的 ★ 是 `a1 b9`、☆ 是 `a1 b8`；找字形前先确认编码表。

---

# 上一轮（2026-11-02）记录

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
