# 会话续接速记（2026-11-01）

> 详细状态见 `docs/HANDOVER.md`（唯一事实源）。本文件只是本会话的快速索引。

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

- Debug 构建 ✅ / Release 构建 ✅ / 离线测试 `--all` ✅ / `--ui-smoke` exit=0 ✅
  （2026-11-01 全绿，含崩溃 #4 修复）
- Publish 构建 ✅ / `out/trial/` 已重新出包 ✅（2026-11-01，exe 含崩溃 #4 修复）
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
   - 四类个性化（成长加成 / 激活加成 / 威力加倍 / 兵法间隔）逐项确认，
     并回传 `San9Companion.log`。
   **若仍崩溃**：转储已默认开启（`%LOCALAPPDATA%\CrashDumps\San9PK.exe.<pid>.dmp`），
   直接解析该文件即可拿到异常地址 / 寄存器 / 调用栈，不必再靠事件日志猜。
2. §3.1.2 兵法威力系数 hook 点（用户已示意暂不开发）。
3. 存档路线 RE T1–T4（`docs/_CRASH4_HANDOFF_2026-10-31.md` §5c.3），其中 **T3 解密 `武将提拔/res_135.bin`** 是支线。
4. §12.3 遗留：`0x5B8556` 返回值语义、5 张 id 表灌入路径、VirtualAllocEx 大小。
5. §6 清理一次性分析脚本 —— 已于 2026-11-01 完成（`tools/_*.py` 52 个全清）。
