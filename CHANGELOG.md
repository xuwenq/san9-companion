# Changelog

本文件记录 San9 Companion 的所有改动。格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

## [未发布]

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
