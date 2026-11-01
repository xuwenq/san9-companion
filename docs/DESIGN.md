# 修改设计文档

> 本文档记录对 San9 Companion 的修改设计、游戏逆向理解、内存地址分析与实现细节。
> 所有新增配置统一放【扩展配置】tab 页；每条需求改动同步记入 `CHANGELOG.md`。

## 1. 概述

San9 Companion 是一个 Win32 x86 修改器，通过 `WriteProcessMemory` / 注入代码对《三国志 9 威力加强版》`San9PK.exe`（基址 `0x400000`，无 ASLR）进行内存修改。

核心机制：运行时把伴侣内置的 `state_`/`data_` 与补丁字节写入游戏进程，并通过 journal 记录改动以便撤销（undo）。

## 2. 游戏内存结构（已逆向确认）

| 结构 | 地址 | 记录大小 | 关键字段 |
| --- | --- | --- | --- |
| 武将表 | `0x1258EE0` | `0x128` | 四维 `0x4C`(武力)/`0x54`(智力)/`0x5C`(政治)/`0x64`(统率)；熟练度 `0xA4+2k`(k=0..8)；状态 `0x84`；兵法标志 `0xE8`(bit2=大将) |
| 兵法表 | `0x14FDAD0` | `0x8C` | 威力 `+0x64`、间隔 `+0x7C`、熟练度 `+0x58` |
| 势力表 | `0x1253C38` | `0xD4` | — |
| 都市表 | `0x124DB58` | — | — |
| 阵型表 | `0x1500B70` | — | — |
| 概率函数 | `kakuritsu` @ `0x5FF2F0` | — | `int kakuritsu(chance, kind, 1)`，`chance` 即百分比(0~100) |
| min 函数 | `[0x5FF308]` | — | `min(int,int)` |
| 武将四维最大值 | `0x44D370` | — | 返回 `max(0x4C,0x54,0x5C,0x64)` |

### 死亡判定（需求 1）

游戏死亡判定在 3 处 `cmp eax, 0x50`（阈值 80）判定「四维最大值 ≥ 80 免死」：

| 路径 | 地址 |
| --- | --- |
| 路径 A | `0x452466`（`cmp eax,0x50`） |
| 路径 A2 | `0x452594` |
| 路径 B | `0x502664`（后续 `mov esi,[esi+0x50]` 于 `0x502667`） |

## 3. 需求实现明细

### 需求 1：高能武将阵亡概率渐变

- 配置：`DeathGradient2026`（`lowRate` 默认 40，`threshold` 默认 100，范围 80~120）
- 开关：复用现有「高能武将可阵亡」复选框（`AllPersonDeadEnabled`）
- 效果：`chance = min(lowRate, threshold - MAX(四维))`；`MAX ≥ threshold` 免死
- 注入：`applyDeathGradient()`（`Runtime2026.cpp`）
  - 3 处阈值字节 `0x50` → `threshold`（clamp 120）
  - 路径 B 的 `0x502667`：`mov esi,[esi+0x50]` → `mov esi,eax`（改用 MAX）
  - `0x50266D`：`mov ecx,0x50` → `threshold`；`0x502677`：`push 0x1E` → `lowRate`

### 需求 2：小兵培养增强

- 能力系数反转：`codeTrainBasicAbility` 内两处 `sub byte [edx],al`(`0x28,0x02`) → `add`(`0x00,0x02`)，实现「数字越大越强」
- 培养时长可配：`Cultivate2026.durationRounds`（默认 9，范围 3~12）写入 `0x14FA656`
  - ⚠️ **未验证地址**：`0x14FA656` 是我们凭猜测定的地址——PE 内零引用、无指针指向、位于 `.data` BSS 区，
    原工具的 `recovery/evidence/global_assignments.json` 里也没有它。**若实机再崩，第一件事就是删掉这行。**
- 强制小兵培养/提拔：写 `0x40CAEF` 与 `0x40BF25`（两条 `cmp eax,0x32` 指令的立即数字节）= `0x32` / `0x00`，
  与原工具 SanIXPKCheat 完全一致（`RuntimeInjection2026.cpp` 的「开启小兵培养」，control id 1013）。
  - ⚠️ **原实现已删除**：`0x6613EE`（曾被称为「小兵激活标志」）其实是 id 表 A 首条记录的
    id dword `0x6613EC`（值 `0x000003E8`）的**第 2 个字节**，写入即把 id 1000 破坏成 `0x000103E8`，
    导致游戏查找 id 失败 → `0x5B72F0` 解引用空指针 → **崩溃 #4（异常地址 `0x5B6754`）**。
    该写入已于 2026-10-31 删除，见 `docs/_CRASH4_HANDOFF_2026-10-31.md`。

### 需求 3：指定武将能力成长倍率

- 配置：`GrowthBoost2026`（武将 ID 逗号列表 + `multiplier` 1~10）
- 注入：`applyGrowthBoost()`——解析列表为 800 字节位图，分配游戏内存写入位图/倍率/辅助代码，hook `codeDynamicAddProficiency` 尾部，按武将 ID 乘倍率

### 需求 4：武将个性化（第一批·修正版）

- 配置：`OfficerPersonalization2026`，每武将 53 个值 = 9 系熟练度上限 + 41 个兵法发动间隔(天) + 把握概率(0/1) + 威力系数 + 威力一定加倍(0/1)

| 子项 | 游戏地址 | 语义 | 状态 |
| --- | --- | --- | --- |
| 熟练度上限 | `0xA2190`（熟练度上限逻辑） | 每武将每系独立上限（查 `proficiencyCap[武将][系]`） | ✅ per-system 注入完成 |
| 发动间隔(天) | `0x4FF08D`（`idiv` 之后） | 每个兵法独立间隔（军官 esi、技能ID [ebx+0x34]） | ✅ per-skill 注入完成 |
| 把握概率 | `0x4FEB95`（`kakuritsu`） | 勾选=强制 100% | ✅ |
| 威力一定加倍(出动画) | `0x506035`（10% 会心） | 勾选=强制会心 | ✅ |
| 兵法威力系数 | `0x43EF44` | 威力=原威力×系数 | ⏸ 拿不到武将指针，待另找（已逆向实证：该点 this=势力对象 `0x1253C38+idx*0xD4`、乘数=调用链系数参数，无武将。见 `G:\aiprojects\game\san9pkme\06_43EF44_callchain.md`） |

- 把握概率注入：`applyOfficerPersonalization()` hook `0x4FEB95`，按武将 ID `(esi-0x1258EE0)/0x128` 查 `activationBoost`，命中则 `[esp+4]=100`
- 威力加倍注入：hook `0x506035` 同理，命中则 `[esp+4]=100`
- 发动间隔(天) per-skill：hook `0x4FF08D`，`军官索引=(esi-0x1258EE0)/0x128`，`技能ID=[ebx+0x34]`，查 `interval[officer*41+skill]`；仅当存在偏离原值的覆盖时才注入，否则恢复原字节
- 熟练度上限 per-system：替换 game `0xA2190` 处的熟练度上限逻辑，保留原熟练度增长计算，按 `(武将ID=[esi+4], 系=edi)` 查 `proficiencyCap[武将][系]`（uint16 表），命中则覆盖上限；无任何设置时保持原逻辑

## 4. 配置与持久化

新增配置统一走【扩展配置】tab + INI（`San9PKHardSettings.ini`）持久化：
- `Extensions/LowDeathRate`、`Extensions/DeathThreshold`
- `Extensions/GrowthBoostList`、`Extensions/GrowthBoostMultiplier`
- `Extensions/CultivateDuration`

`OfficerPersonalization2026` 走独立配置文件 `San9PKHardCheat.txt`（每武将一个整数块：9 个熟练度上限 + 41 个发动间隔 + 把握概率 + 威力系数 + 威力一定加倍，共 800×53 行；熟练度上限默认 1000，发动间隔默认取游戏原值）。

## 5. 待办

- 需求 4 威力系数：需另找能拿到武将指针的挂钩点
