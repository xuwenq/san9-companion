# Changelog

本文件记录 San9 Companion 的所有改动。格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

## [未发布]

### 修复（图标崩溃，2026-11-07）

- **实机崩溃：打开武将列表就崩**。转储坐实（`San9PK.exe.29292.dmp`，异常码 `0xC0000005`，
  读地址 `0x02227028`，而我们的图标块在 `0x02227080`）。
  根因：172 字节图标块里 4 个守卫跳转的位移是 159 / 152 / 147 / 140，
  **全部超过 x86 rel8 的有符号上限 127**。`0x9F` 被解释成 **−97**，
  CPU 跳到块外 88 字节 → 访问违例。
  原版 140 字节块的位移**恰好**是 127（有符号上限），纯属侥幸。
  **修复**：4 个守卫跳转改**近跳转**（`0F 84` / `0F 8C` / `0F 8F`，rel32），
  块 172 → **188 字节**。disp32 偏移：图标表 @ **0x4F**，等级表 @ **0x80**。
  修复过程中把 disp32 写到 `0x4B` 而不是 `0x4F`，被新增的测试断言当场抓住。
- **删除错误的培养时长写入**：`0x14FA656` 是**猜出来的**地址，且是错的 ——
  RVA `0xFA656` 落在 `.text` 段，它是**游戏代码的一个字节**，不是变量。
  之前每次应用配置都往那儿写 9，**一直在改坏游戏代码**。
  2019 版工具**根本没有**培养时长功能（只有 `m_dwTrainNumAddr` `0x40CAEF` /
  `m_dwTrainNumAddr2` `0x40BF25` / `m_dwOldTrainNum` 50）。
  现在删除该写入，配置项保留但不生效，日志明说。
- 新增 `tools/check_injection_blocks.py`：读 `src/runtime/Runtime2026.cpp` 里的
  `const BYTE xxx[] = {...}`，用 capstone 反汇编，断言每条跳转
  ① 落在指令边界 ② 在块内 ③ rel32 占位（全 0）跳过。
  当前 **11 个块，0 个问题**。

### 新增（逐功能开关，2026-11-07）

- 新增 `San9Features.ini`（仓库根目录，与 exe 同目录）：**13 个逐功能开关**，
  用于一个一个地测功能、排除干扰项。
  段名 `[Features]`，值**只认精确的 `0` / `1`**，其它值一律当 off
  （手误不应该打开一个功能）；缺键 = 默认（默认全部 on ⇒ 空文件 ≡ 开关引入前的行为）。
  **启动时读一次**，运行期不再写回。
- 新增 `src/runtime/FeatureSwitches2026.{h,cpp}`：
  `enum class Feature2026`（deathGradient / growthBoost / cultivateTrainNum /
  cultivateBasicAbility / cultivateDuration / proficiencyCap / proficiencyMark /
  activationBoost / criticalHit / interval / barbarianFormation /
  computerWorkTiming / skipSiegeSelection）、
  `loadFeatureSwitches(path)`、`summary()`（输出 "1 0 1 ..." 供日志对照）。
- 开关关闭 = **完全不注入**（不是「注入了但不起作用」），
  日志 `FEATURE xxx : off, nothing is injected`；
  另有一行 `FEATURES: file=... summary=...` 可与 ini 逐项对照。
- `applyOfficerPersonalization` **拆成 3 个独立函数**
  `applyActivationBoost` / `applyCriticalHit` / `applyInterval`，各自有开关。
- 加载路径：`src/runtime/AppPaths.h` 的 `featureSwitchesFile()` →
  `Controller::loadFeatureSwitches(path)` → `MainWindow.cpp` 启动时调用。

### 新增（玩家蛮族阵型，2026-11-07）

- 逆向坐实：游戏用 **28 项跳转表 `0x4FB5DC`** 查阵型属性
  （入口 `0x4FB560`，唯一调用点 `0x4FBEB6`）。
  **12 / 13 / 14 号阵型返回异族专用属性 `0x3C3`**，所以普通势力选不了。
- 实现：把这 3 项改指向我们自己的 6 字节桩
  `B8 CC 03 00 00 C3`（`mov eax,0x3CC; ret`），桩放在 `AllocationBase + 0xA000`；
  **其余 25 项原样不动** → 普通阵型完全不受影响。
  比第三方修改器 `Shape.exe` 温和（它把 `0x4FB564` 改成 `mov eax,0x3CC`，
  **整个函数恒返回 0x3CC，所有阵型属性都被抹平**）。
- 界面字幕：`assets/ui/dialogs.rc` 控制 1047 由「启用电脑蛮族阵型」
  改为「**启用玩家蛮族阵型**」（`tools/dialog_customizations.py` 同步）。
- 回归测试：新增 `--barbarian-formation`（开关关时跳转表不动；
  开关开时只有 12/13/14 被改写，且指向返回 `0x3CC` 的桩）。

### 文档（2026-11-07）

- 新增 `docs/KNOWLEDGE_BASE.md` —— **逆向结论的单一可信来源**：
  地址→用途→证据→置信度总表、CP950 字形表、
  **x86 注入陷阱库**（rel8 有符号！rel32 字段偏移约定不同！栈平衡！…）、
  已被推翻的结论、注入代码地图、实机验证状态表。
- 新增 `docs/LIVE_TEST_PLAN.md` —— 逐功能实机验证清单：
  每项写清「开哪些开关 / 期望结果 / 怎么判断」，含二分法定位步骤。
- `out/trial/` 已重新出包（Publish 静态 CRT exe + 6 个配置 + `San9Features.ini` +
  更新后的 `使用说明.txt`）。

### 变更（游戏内 ★/◎/○ 指示图标，2026-11-07）

- **定位到游戏内图标的绘制代码**：2019 工具遗产 `codePersonCharNum`（140 字节，
  `recovery/reference-2019/InsertAsm.h:10230`），companion 把同样的字节装在**注入分配区**
  `allocationBase+0x6960`（`RuntimeInjection2026.cpp:415`），并通过 descriptor `0x426028`
  把游戏 `0x451A5C` 改写成 `call 0xA6960` 进入它。
  图标是 **CP950 两字节字符**：☆ = `a1 b8`、◎ = `a1 b7`、○ = `a1 b3`，
  **游戏本体没有这些字形**（游戏字符表 `0x61CFA0..0x61CFF8` 只有 `• ＊ ？ 完 未 △ ◎ ○ ×`）。
- **纠正两条此前的错误结论**：
  ① 「140 字节覆盖游戏 `0x425F88`」是**错的**——`PatchCatalog.h` 的 `StaticWrite`
  字段顺序是 `{callVA, functionVA, targetVA, sourceVA, size, bytes}`，
  target 是 `0xA6960`（分配区）、source 是 `0x425F88`（companion 镜像位置）；
  ② 「必须 live 取证」不成立，静态就能查到（`PatchCatalog.h` 也没有被任何 `src/` include）。
- **重写判定段**：新增 `Runtime2026::applyProficiencyMarks()`，宿主算一张
  `uint32[800*9]` 前缀表上传，再写判定块（capstone 逐条核对），
  并在 `allocationBase+0x6960` 放 5 字节 `E9` 跳板
  （判定块放不进 160 字节的原槽位，会压到 `codeMoveGuardTroop@0x6A00`）。
  > ⚠️ 这段最初的版本是 **172 字节**，当天实机即崩溃（rel8 溢出），已修成 **188 字节**，见上。
- **口径（用户确认）**：配置优先、**未配置回落原生**（原生「精通」位 → ☆，等级第 4 档 → ◎）；
  **任意熟练度上限设计保留**，不做 1300/1600 分档。
- **两边统一**：新增 `src/runtime/ProficiencyMark2026.h`，修改器界面与游戏内注入
  **共用同一套规则**（`ProficiencyMarkFor` / `ProficiencyMarkPrefix2026`）。
  修改器侧的五角星由实心 ★ U+2605 改为**空心 ☆ U+2606**（与 `build/res/tags.PNG` 截图一致）。
- **一个格子都没配时不替换块**：原版 140 字节块画的本来就是原生标记，语义一致；
  这样也不会多分配内存（否则 undo 的 release 计数会从 1 变 2）。
- 回归测试：新增 `--proficiency-mark-injection`（断言图标表内容、判定块关键指令、
  CP950 字形、跳板）；`--proficiency-marker` 改为断言共享规则。

### 调查（游戏内 ★/◎/○ 指示图标，2026-11-07 早些时候的记录，已被上一条取代）

- 静态 RE 确认**游戏本体里没有 ★ (U+2605) 字形**：游戏的 Big5 字符表 `0x61CFA0..0x61CFF8`
  只含 `• ＊ ？ 完 未 △ ◎ ○ ×` 与全角数字；符号指针表 `0x66AE30..0x66AE80` 暴露这些指针。
- 确认 `src/generated/PatchCatalog.h` 的 1087 条注入写入**没有一条落在游戏 .rdata**
  （774 → .text，313 → companion 自有 blob `0xA0000+`）⇒ **游戏内的五角星是 companion 自己注入绘制的**。
- 确认熟练度 / 等级 / 阈值数据模型：熟练度 `officer+0xA4+系*2`（getter `0x44C940`）、
  游戏原版上限 1000 在 `0x406A70`、等级 1–4 由 `0x409510` 计算
  （阈值表 `0x64BD00/0x64BCC4/0x64BC88/0x64BD3C`）、**1500 阈值在 `0x44E9E0`**
  （`0x44E840` 内，唯一调用者 `0x450323`）、精通/精进标志位在 `dword[officer+0xE8]`。
- 确认 companion 的 93 字节上限注入块
  （`recovery/evidence/payload_disassembly/004256B8_000A2190_5D.asm`）
  与 2019 版工具 `recovery/reference-2019/San9PKHardDlg.cpp` **完全同构**
  （等级表基址 `0xA4000`、默认 `0x3FEAA`、精进上限 `0x43FFDE` = 1500）。
- 产出 live 取证工具 `out/_re/livediff.py`（进程内存 vs 磁盘 exe 逐字节 diff）。
- 本节**无代码改动**；详见 `docs/HANDOVER.md` §4.10 与 `docs/SESSION_NOTE.md`。

### 变更（需求 4 武将个性化重做）

- 数据模型：`OfficerPersonalization2026` 从早期 4 个单值数组改为「9 系熟练度上限（默认 1000）+ 41 个兵法发动间隔(天，默认游戏原值) + 把握概率(勾选) + 威力系数 + 威力一定加倍(勾选)」，持久化为 `San9PKHardCheat.txt`，每武将 53 个整数（9 上限 + 41 间隔 + 3 其他）。
- UI：武将配置页加宽到 760，9 系「兵法系/当前熟练度/熟练度上限」表 + 27 个可改间隔兵法的「发动间隔(天)」网格（按系分组）；把握概率/威力一定加倍改为勾选框；移除「精进熟练度上限」全局输入框与前 5 系「精进」radio。
- 熟练度上限 per-system：替换 game `0xA2190` 处的熟练度上限逻辑（原为 `精进 level==3 → maxExperience`，且被 `cmp edi,4` 限制在前 5 系），保留原熟练度增长计算，改为按 `(武将ID=[esi+4], 系=edi)` 直接查 `proficiencyCap[武将][系]`，不再走精进/全局阈值。
- 发动间隔 per-skill：hook `0x4FF08B`（军官 `esi`、技能ID `[ebx+0x34]`），直接查 `interval[officer*41+skill]`，不再「按系」。
- 把握概率：由填数字改为勾选强制发动（hook `0x4FEC59`，即**发动判定骰**上；`0x4FEB90` 恢复原版字节）；威力一定加倍同理（hook `0x50603B`）。
- 修正 per-officer hook 的数组地址偏移（`0x12`→`0x13`，`mov ecx,[disp32]` 地址在 `8B 0D` 后第 3 字节）。
- 主窗口与设置/武将/都市/兵法/阵型子窗口加入居中（`DS_CENTER`）；主窗口加入最大化按钮（`WS_MAXIMIZEBOX`）。

### 新增

- 需求 1（高能武将阵亡概率渐变）：新增 `DeathGradient2026` 配置（低能阵亡率 `lowRate` 默认 40、高能归零阈值 `threshold` 默认 100），在 `Runtime2026::applyDeathGradient` 实现 `min(lowRate, threshold-MAX(四维))` 渐变；【扩展配置】页新增两个数值框，经 INI（`Extensions/LowDeathRate`、`Extensions/DeathThreshold`）持久化。
- 需求 2（小兵培养增强）：基础能力系数反转（`codeTrainBasicAbility` 两处 `sub`→`add`）；新增 `Cultivate2026` 配置（`durationRounds` 默认 9，范围 3~12，写入 `0x14FA656`）；「开启小兵培养」按原工具实现写 `0x40CAEF`/`0x40BF25` = `0x32`/`0x00`（原计划的 `0x6613EE`=1 已作废——该地址是 id dword `0x6613EC` 的第 2 字节，写入即破坏 id 1000，是崩溃 #4 的根因），经 INI（`Extensions/CultivateDuration`）持久化。
- 需求 3（指定武将能力成长倍率）：新增 `GrowthBoost2026` 配置（武将 ID 列表 + `multiplier` 1~10），在 `Runtime2026::applyGrowthBoost` 解析为 800 字节位图并注入；【扩展配置】页新增「成长倍率武将ID」文本框与「成长倍率(x)」数值框，经 INI（`Extensions/GrowthBoostList`、`Extensions/GrowthBoostMultiplier`）持久化。
- 需求 4（把握概率/威力一定加倍）：`OfficerPersonalization2026` 新增 `activationBoost`、`criticalHit`（0/1），分别 hook `0x4FEC59`（发动判定骰）、`0x50603B`，命中则跳过掷骰直接判定成功。
- Cheat.s9 → `San9PKHardCheat.txt` 参数文件转换：新增 `newwuj/export_cheat_params.py`，解析 SanIXPKCheat 2019 存档（记录起点 `0xD0`、每武将 88 字节），生成 800 武将 × 53 值的 `San9PKHardCheat.txt`；把握概率=byte0x01&0x80、威力加倍=byte0x02&0x02、威力系数 double→百分数（1.0→0、1.1→110、1.2→120）。
- **熟练度状态指示图标（需求 F，2026-11-02）**：武将配置页每个兵法系在「熟练度上限」输入框右侧新增一个 12×12 状态指示控件（`IDC_PROFICIENCY_MARK_1..9` = 1252–1260），按四档显示「无 / ★ / ◎ / ○」：
  - 无 = 上限为默认 1000（或填 0 = 不修改）且该系没有缩短发动间隔；
  - ★ (U+2605) = 上限在 1000 < cap ≤ 1500；
  - ◎ (U+25CE) = 上限 > 1500；
  - ○ (U+25CB) = 该系至少一个兵法的发动间隔比游戏原值短（**优先级最高**：○ > ◎ > ★ > 无）。
  上限输入框或 27 个间隔输入框发生 `EN_CHANGE` 时**实时**刷新，不需要先保存。
  兵法系 → 兵法 id 的映射固化在 `OfficerPersonalization2026::systemSkills`（依据原版武将配置对话框 9 行按钮布局：步兵 0-2 / 骑兵 3-5 / 弓骑 6-8 / 弩兵 9-11 / 水军 12-14 / 攻城 15-18 / 智略 19-22 / 谋略 23-26 / 策略 27-30；31-40 为特殊兵法，不归属任何系）。
  新增回归测试 `proficiencyMarkerTest`（四档判定、环的归属范围、31 个常规兵法全部有归属），注册于 `--all` 与 `--proficiency-marker`。
- **用户手册（2026-11-02）**：新增 `docs/用户手册.md`，逐项说明主窗口 / 设置页（参数页 56 开关 + 32 数值，由 `src/generated/parameter_fields.inc` + `recovery/resources/dialogs.json` 程序化生成）/ 扩展设置页 / 武将配置页 / 都市配置页 / 兵法配置页 / 阵型配置页 / 配置文件 / 诊断 / 卸载。

### 变更

- 界面繁体中文全部改为简体（`dialogs.rc` 武将个性化区块、`parameters.json` 的「斗舰」caption 等）。
- 版本号由 git 提交日期改为构建当天日期（东八区 `yyyy-MM-dd`），`Write-BuildInfo.ps1` 不再依赖 git。

### 修复

- **兵法抵挡 bug + 把握概率不生效（2026-11-02，同一根因）**：游戏里有两枚独立的骰子 —— `0x4FEB20` 内的**抵抗骰**（掷骰点 `0x4FEB95`，概率 =(对手值−我方熟练度)/10，**eax≠0 ⇒ 抵抗成立**）与 `0x4FEBB0` 内的**发动骰**（掷骰点 `0x4FEC5C`，概率 = 100+抖动，失败 ⇒ `xor eax,eax` ⇒ 返回 0 ⇒ 兵法不发动）。旧实现把把握概率 hook 挂在 **`0x4FEB90`（抵抗骰）** 并强制 `eax=1`，而「eax≠0」就是「抵抗成立」→ **勾选把握概率的武将必定被抵挡，且熟练度完全不参与判定**；同时真正的发动骰从未被 hook → **勾了把握概率完全不生效**。**已修复**：`0x4FEB90` 恢复原版字节 `BE 0A 00 00 00`（抵抗判定完全按原版走），把握概率改挂到**发动骰 `0x4FEC59`**（58 字节桩，命中时跳过掷骰直接 `mov eax,1` 返回 `0x4FEC5E`），未命中原样重放原调用；日志打印 `personalization activationBoost: resist roll restored at 0x...` 作为佐证。
  > ⚠️ 改这个桩必须遵守：`kakuritsu` 是 **`__cdecl`**（导出名 `?kakuritsu@@YAHHW4RANDOM_KIND@@H@Z`，`YAH` = cdecl），由调用方清参数。原版 `0x4FEC5E` 的 `add esp,0x14`（20 字节）恰好清掉 5 个压栈：`push eax`(0x4FEC25) + `push ebp`(0x4FEC30) + `push 1`(0x4FEC53) + `push 0xB` + `push edx`。所以「命中」路径**必须照原版压入 `0xB` 与 `edx`**（虽然不掷骰），多压/少压都会破坏栈平衡。
  > ⚠️ rel32 字段偏移有两种约定，**不要混用**：C++ 侧 `extraJumpRelOffset` 是 **rel32 字段偏移**（E9 操作码偏移 + 1，本例 `0x28`）；`tools/asm_check.py` 的 `rel32={...}` 用的是 **E9 操作码偏移**（本例 `0x27` / `0x35`）。两者指向同一段 rel32 字段。
- **崩溃 #4（实机异常地址 `0x5B6754`、读取地址 `0x14`）**：根因是 `Runtime2026::applyCultivate()` 往 `0x6613EE` 写了 1 字节，而该地址**不是**独立的「小兵激活标志」，而是 id 表 A 首条记录 `0x6613E8` 的 id dword `0x6613EC`（值 `0x000003E8`）的**第 2 个字节**。写入后 id 1000 变成 `0x000103E8` → 游戏按 id 查找失败（`0x5B7050` 的 `cmp dword [entry+4],id`）→ `0x5B72F0`（getflag，**无空指针检查**）解引用空 entry → 访问违例。**已删除该写入**；「开启小兵培养」本身走 `RuntimeInjection2026.cpp:1170-1172`（写 `0x40CAEF`/`0x40BF25` = `0x32`/`0`，与原工具一致），**实现未改**。新增回归测试 `idTableWriteGuardTest`（断言所有写入不覆盖 5 张 id 表区域 `0x6613E8`/`0x665B70`/`0x667740`/`0x667C08`/`0x669F78` 各 11×`0x20`=`0x160` 字节，特别断言 `0x6613EE` 与 `0x6613EC`；断言「开启小兵培养」开关写 `0x40CAEF`/`0x40BF25` 的值在 `0x32`/`0x00` 间切换），已反向验证。`0x14FA656`（培养时长）保留但标注为**未验证地址**（PE 内零引用、无指针指向、位于 `.data` BSS；若实机再崩先删这行）。同时更正了「禁用 `unk_42C894` 即消除 `0x5B6754`」这一**已被 dump 证伪的错误归因**（禁用后仍崩同一地址）。
- 把握概率/威力加倍注入代码结尾原为 `jmp [0x5FF2F0]`（尾跳、丢失返回地址，命中即崩溃），改为 `call [0x5FF2F0]` + 跳回原 `call` 之后的清栈/分支（把握概率 `0x4FEB9B`、威力加倍 `0x50603B`）；`installPerOfficerHook` 现在自动补尾部 `jmp rel32`。
  > 2026-11-02 更正：把握概率桩后来整体迁到发动判定骰 `0x4FEC59`（见上条），本条描述的 `0x4FEB9B` 跳转目标已随之作废。
- 测试桩：`tests/runtime_test.cpp` 的 `allocate`/`release`/`write` 桩支持需求 4 新增的多处个性化内存分配；游戏区下界放宽到 `0xA0000` 以覆盖低地址 `0xA2190`。
- 熟练度上限注入代码 `movzx eax, word [capArr+eax*2]` 的 disp32 偏移写错一位（`0x3C`→`0x3D`），原会覆盖 SIB 字节导致查表地址错乱，已修正。
- `applyProficiencyCap` 原本把 payload 写到固定地址 `0xA2190`，但该地址是原工具固定基址 `0xA0000+0x2190`；本实现动态分配，须写回 `AllocationBase+0x2190`，否则写入未提交内存导致连接失败回滚，已修正。
- 新增运行时诊断日志：连接基址、每次写入的目标地址/大小、修改失败原因（操作/地址/字节数/系统错误）追加到可执行文件旁的 `San9Companion.log`。
- 日志热路径重构：原先每次写入都 open/close 文件且无上限，全量离线跑一次后涨到数百 MB；改为单一持久流 + 8 MB 总量上限（超限后截断重建并停止写入），并把「逐次写入追踪」移到 `SAN9_LOG_WRITES` 环境变量开关后（默认关闭）。`writeMemory` 追踪改用固定栈缓冲 `snprintf`，不在热路径分配内存。
- 武将姓名显示破坏游戏快照：`ui::gameText` 原地把 Big5 改写成 GBK，导致同一快照第二次渲染乱码；新增非破坏性 `san9::gameWideText`（Big5→宽字符→简体，不修改源缓冲），`gameText` 与 `Pages.cpp` 的姓名显示/搜索改用它。
- 垂直滚动条无响应：`Dialog::procedure` 只分发 `WM_HSCROLL`，漏了 `WM_VSCROLL`；已补齐。
- tooltip `lpszText` 悬垂指针：控件会持有文本指针，而 `std::wstring` 在替换或 map 扩容时会移动内存；改为 `map<int, unique_ptr<wstring>>` 独占缓冲，更新成功后再移交所有权。
- `Dialog::savePreference(bool)` 补 smoke 守卫，smoke 模式不再写真实 INI。
- `uploadInjectionPayloads` 删除了对 `AllocationBase+0x2190` 的重复写入（会覆盖 `applyProficiencyCap` 写好的上限版逻辑，导致游戏读档即退出）；该地址现由 `applyProficiencyCap` 全权负责。
- 移除 `PersonConfig::maxExperience` 字段：熟练度上限语义已迁移到 per-officer × per-system 的 `proficiencyCap` 表，旧全局字段冗余且冲突；`San9PKHardPerson.txt` 格式从 917 行改为 916 行。
- 熟练度上限由全局 `0x3E8`→`0xFFFF` 立即数改为 per-officer × per-system 查表注入：替换 `AllocationBase+0x2190` 处 93 字节逻辑，按 `(武将ID=[esi+4], 系=edi)` 查 `proficiencyCap[武将][系]`（uint16 表）；无任何自定义上限时写回原版逻辑。

### 待办

- 需求 4 威力系数：`0x43EF44` 处拿不到武将指针，需另找挂钩点。
- 注入代码字节正确性仅经离线测试的模式/边界检查，尚未在真实游戏进程 + 存档中实机验证。
