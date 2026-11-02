# 注入代码修复 — 关键证据汇总

> 状态：**逆向取证已完成；修复已按本文结论全部落地并通过离线验证**（2026-10-27）。
> 本文 §0~§9 是取证与修复依据；**§11 记录了已实施的改动与实际字节**。
> 本文件是 `docs/HANDOVER.md` §3.1 / §3.7 的支撑材料，所有结论均有反汇编证据。

## 0. 结论速览

| 项 | 结论 |
| --- | --- |
| 崩溃根因 | 四个注入块都用 `(esi - 0x1258EE0) / 0x128` 算武将索引，但 `esi` **从不是**武将表指针 → 野指针读 → 必崩（B1） |
| 正确的武将索引 | `movzx eax, word [CPersonData + 4]` —— 即 `word[记录+4]` 的低 16 位 |
| 该索引的含义 | **就是武将表 `0x1258EE0` 的下标**（= 项目 UI 用的武将索引，0..799） |
| 把握概率 hook 点 | **必须从 `0x4FEB95` 移到 `0x4FEB90`（后者 esi 才是单位对象）** —— ⚠️ 2026-11-02 更正：这是**抵抗骰**，`eax≠0` 表示**抵抗成立**；把握概率应挂在**发动骰** `0x4FEC59`，`0x4FEB90` 须恢复原版字节（见 §14） |
| 间隔 hook 点 | 应 hook `0x4FF08B`（`idiv ecx`），**不能**覆盖后面的 `push eax` / `call 0x4FB760` |
| 兵法表条目数 | **41**（`0x453720`: `cmp eax,0x29`），`SkillConfig::Count=39` 是错的（B6） |
| 武将表条目数 | 850（`0x4531B0`: `cmp eax,0x352`），但**存档只装载前 800**（`0x45415B`: `cmp edi,0x320`） |

## 1. 决定性发现：SanIXSpy.dll 的 patch 地址与 San9PK.exe 完全一致

原版作弊工具的 `SanIXSpy.dll` 注入的就是**当前这个** `San9PK.exe`（不是老版本）。
patch 表（29 条，每条 `0x1C` 字节）在 `0x10004000`，每条格式：

```
+0x00 dword  游戏内被 patch 的地址
+0x04 byte[5] 该地址处应匹配的原始字节
+0x09 byte[5] 保留
+0x0C dword  DLL 内的 stub 地址
+0x10 byte[5] 运行时写入的 jmp rel32（0x1410 填充）
+0x18 dword  (被 0x14C0 用作 "特征 id")
```

**验证结果（`tools/spy_match.py`）：29/29 条目与 `San9PK.exe` 逐字节吻合。**
→ 原版就是在 hook 这些点，其 stub 是**官方可用的参照实现**。

与本项目相关的四条：

| entry | 游戏地址 | 原始字节 | 反汇编 | 语义 |
| --- | --- | --- | --- | --- |
| 16 | `0x4FEB90` | `BE 0A 00 00 00` | `mov esi,0xA` | 把握概率 |
| 17 | `0x4FF08B` | `F7 F9 8B CE 50` | `idiv ecx; mov ecx,esi; push eax` | 发动间隔 |
| 24 | `0x50603B` | `83 C4 0C 85 C0` | `add esp,0xC; test eax,eax` | 威力加倍 |
| 0  | `0x43EF44` | `0F AF B4 24 8C` | `imul esi,[esp+0x8C]` | 威力系数 |

## 2. 武将索引口径 —— 三个独立证据链，全部收敛到同一结论

### 证据 1：RTTI 确认 hook 现场的 esi 就是 `CPersonData`

`0x4FF030`（间隔函数）在 `0x4FF05B` 用类型描述符 `0x6060e8` 做类型匹配。
`0x6060e8` 的 name 指针指向 `0x606100` = `"CPersonData"`，
`+4` 字段 = `0x128` = 武将表记录大小。
→ **`0x4FF08B` 现场的 `esi` 是 `CPersonData`（武将记录）本身。**

### 证据 2：武将表装载时把「表下标」写进 `record+4` 的低 16 位

`0x45415B`（武将表装载循环）：

```
mov  ebx, 0x1258EE0
xor  edi, edi
loop:
  push edi          ; ← 表下标
  push esi          ; ← 存档流
  mov  ecx, ebx     ; this = &table[i]
  call 0x44F830     ; CPersonData::load
  inc  edi
  add  ebx, 0x128
  cmp  edi, 0x320   ; 只装载 800 条
```

`0x44F830` 内部（符号栈模拟 `tools/game_stack.py`）：

```
0x44F85A: mov  eax, [esp+0x18]     ; eax = 表下标（调用方 push 的 edi）
0x44F861: push eax
0x44F864: call 0x446800            ; CPersonData::setId
```

`0x446800`：

```
movzx eax, word [ecx+6]            ; 取高 16 位（种类）
shl   eax, 0x10
and   edx, 0xFFFF
or    eax, edx
mov   [ecx+4], eax                 ; ★ 写入 record+4
```

→ **`word[CPersonData+4]` = 武将表下标 = 项目 UI 索引（0..799）。**

（同样的 `0x446800` 调用也出现在 `0x44FB20`，即 `0x457A33` 那条装载循环。）

### 证据 3：原版 spy 的 stub 用的就是 `word[esi+4]`

间隔 stub `0x10001800`：

```
movzx eax, word [esi+4]            ; ★ 武将索引（esi = CPersonData）
imul  eax, eax, 0x58               ; 原版 cfg 记录步长
add   ebx, eax
```

→ 与证据 1、2 完全一致。

### 结论：项目 UI 索引口径 = hook 现场可得的索引口径

本项目 `readPersonSnapshot` 从 `0x1258EE0` 读 `800 * 0x128`，
UI 的武将索引就是**表下标**；而 hook 现场的 `word[CPersonData+4]` **也是**表下标。
**两边天然对齐，无需换算。** 计划中「改用 `dword[troopMember+0x94]`」的方案是错的：
`+0x94` 是 `CUnitData`（部队单位）上的字段，取的是**另一套**语义。

### `dword[obj+0x94]` 到底是什么

- `0x44CF20`: `mov ecx,[ecx+0x94]; call 0x4531B0` —— 把 `+0x94` 当**武将表下标**用。
- `0x4B3844`: `mov [esi+0x94], eax` —— 写点在 `CUnitData` 构造路径里。
- 布局：`+0x90` / `+0x94` / `+0x98` 三连 dword；`0x4FB760` 写 `[ecx+0x98]`。

→ `+0x94` 是**单位自己的**武将表下标缓存（可被 `0x44CF20` 查属性表），
与 `CPersonData+4` 在正常情况下同值，但**属于不同对象**。
在 `0x4FF08B` 现场 `esi` 是 `CPersonData`，**没有** `+0x94` 这个字段。
→ **用 `+0x94` 的方案在间隔 hook 点直接是野读。**

## 3. 三个 hook 现场的对象语义

### 3.1 间隔（hook `0x4FF08B`）

```
0x4FF045: mov  esi, [eax+8]        ; esi = 部队成员（CHeishiPersonData?）
0x4FF05B: push 0x6060E8            ; 类型描述符
0x4FF060: mov  ecx, esi
0x4FF062: call 0x5DED60            ; dynamic_cast → CPersonData
0x4FF06D: mov  eax, [ebx+0x34]     ; 技能 ID
0x4FF071: call 0x453720            ; → 兵法记录
0x4FF076: mov  eax, [eax+0x7C]     ; 间隔内值
0x4FF07C: cmp  esi, ebp
0x4FF07E: mov  ecx, 3
0x4FF085: mov  ecx, 1              ; 除数 1 或 3
0x4FF08B: idiv ecx                ; ★ hook 点
0x4FF08D: mov  ecx, esi
0x4FF08F: push eax
0x4FF090: call 0x4FB760            ; mov [ecx+0x98],eax; ret 4
0x4FF095: test edi, edi
```

- `esi` = 匹配到 `CPersonData` 的对象 → `word[esi+4]` = 武将索引 ✅
- `ebx` = 单位对象 → `dword[ebx+0x34]` = 技能 ID ✅
- 间隔内值 = 兵法记录 `+0x7C`，**再除以 1 或 3**。
- ⚠️ 计划里的「只覆盖 2 字节 `8B CE`、跳回 `0x4FF08F`」是**对的**：
  这样原来的 `push eax` / `call 0x4FB760` 仍然执行，语义最接近原版。

### 3.2 把握概率（hook 必须移到 `0x4FEB90`）

```
0x4FEB22: mov  esi, ecx            ; esi = 单位对象（CUnitData）
0x4FEB24: mov  eax, [esi+0x34]     ; 兵法 ID
0x4FEB4D: mov  ecx, [esi+0x30]     ; ★ 另一个 CPersonData 指针
0x4FEB52: call 0x44C940            ; 读熟练度
0x4FEB57: mov  ecx, [esi+0x40]     ; ★ 城市
0x4FEB8D: push 0xB
0x4FEB8F: push edx
0x4FEB90: mov  esi, 0xA            ; ★ hook 点（esi 仍是单位对象）
0x4FEB95: call [0x5FF2F0]          ; 把握概率判定
```

- `0x4FEB95` 处 `esi` 已被改成**常数 10** → 当前实现取不到武将 → 必崩/错乱。
- 原版 stub `0x100017B0` 在 `0x4FEB90` **之前**取 `esi`（单位对象），
  存到 `0x10004344`，调用原函数后再取出处理。
- 原版的「找武将」走的是：单位 `+0x40`（城市）→ `+0x88`（链表头）→ 逐节点 `node+8`（`CPersonData`）→ `word[+4]`。
  城市链表为空时回退到单位 `+0x30` → `word[+4]`。
- **本项目可用更短的路径**：单位 `+0x30` 就是 `CPersonData*` → 直接 `word[+4]`。

### 3.3 威力加倍（hook `0x506035`）

```
0x506015: mov  ecx, [esi+0x80]     ; 战斗单位
0x50602F: push 1
0x506031: push 0xB
0x506033: push 0xA
0x506035: call [0x5FF2F0]          ; ★ hook 点
0x506046: mov  ecx, [esi+0x80]
0x50604E: call [edx+0xBC]
0x506054: mov  ecx, [eax+0x98]
```

- `esi` = 战斗单位对象；`[esi+0x80]` = 关联单位。
- 原版在 `0x50603B`（call **之后**）hook，只**观察**结果；要**改**概率必须 hook 在 call 之前。
- 原版取武将走 `[esi+0x80]` → `+0x88` → `node+8` → `word[+4]`。
- ⚠️ 这里 `esi` 不是 `CPersonData`，**不能**直接 `word[esi+4]`。

## 4. 关键语义（spy stub 反汇编）

### entry 17（间隔）stub @0x10001800

```
push ebx; pushfd
push dword [ebx+0x34]      ; 技能 ID
pop  [0x10004344]
and  [0x10004344], 0x3F    ; 技能 ID & 0x3F  (41 个兵法 → 0..40)
mov  [0x10004350], eax     ; 保存 eax（idiv 之前的被除数）
mov  ebx, [0x10004334]     ; ebx = cfg 数组基址
movzx eax, word [esi+4]    ; ★ 武将索引
imul eax, eax, 0x58        ; ★ 原版每武将 0x58 字节
add  ebx, eax
cmp  ecx, 1                ; ecx = 除数(1 或 3)
je   ...
test dword [ebx], 0x4000   ; 该武将该兵法是否被"自定义"
movzx eax, byte [ebx+eax+8]   ; ★ 间隔值：cfg[武将].intervals[技能ID]，偏移 +8
```

原版 cfg 记录布局（步长 `0x58`）：
`+0x00` flags 位图 · `+0x08` 起 41 个间隔 byte · `+0x20` 起 41 个（另一套）间隔 byte ·
`+0x32` 威力系数 byte · `+0x50` 起 float 倍率。
flag 位：`0x1000`/`0x2000`/`0x4000`/`0x8000`/`0x10000`/`0x100000`。

### entry 16（把握概率）stub @0x100017B0

```
mov  [0x10004344], esi     ; ★ 保存 esi（单位对象）
mov  esi, 0x5FF2F0
push dword [esi]           ; 取原始 [0x5FF2F0] 函数地址
pop  [0x10004350]
mov  esi, 0xA              ; 恢复 esi = 10
call dword [0x10004350]    ; 调用原始把握概率函数
...
mov  eax, [0x10004344]     ; 取回单位对象
mov  [0x10004344], 0x10    ; 特征 id = 0x10
call 0x10001740            ; ★ 处理函数
```

### 处理函数 0x10001740（原版的"武将→配置"查找）

```
mov  ebx, [0x10004334]        ; cfg 数组基址
mov  eax, [eax+0x40]          ; ★ 单位对象 +0x40 = 城市
mov  eax, [eax+0x88]          ; ★ 城市 +0x88 = 链表头
mov  edi, eax
loop:
  mov  eax, [edi+8]           ; 节点 +8 = CPersonData
  movzx eax, word [eax+4]     ; ★ 武将索引
  imul eax, eax, 0x58
  test dword [ebx+eax], 0x100000   ; 该武将是否有"把握概率"标志
  jne  found
  mov  edi, [edi]             ; 下一个节点
  jmp  loop
found: [0x10004350] = 1（概率改为 100）
链表为空时: mov eax,[eax+0x30]; movzx eax,word[eax+4]; test dword[ebx+eax],0x10000
```

### entry 24（威力加倍）→ 公共处理 0x100019F0

```
mov  eax, [esi+0x80]         ; ★ 战斗单位 +0x80
mov  eax, [eax+0x88]         ; → 城市/链表
movzx eax, word [eax+4]      ; 武将索引
imul eax, eax, 0x58
test dword [ebx+eax], 0x8000 ; 威力加倍标志
```

## 5. 熟练度上限：口径已核实

`0x44C980`（真·熟练度上限函数）：

```
0x44C996: push 0x3E8           ; ★ 1000 = 默认上限
0x44C99D: call 0x44C940        ; 读当前熟练度
0x44C9A3: call [0x5FF30C]      ; min(当前, 1000)   ← 2019 工具 patch 的就是这里
0x44C9B0: call 0x44C960        ; 写回
```

`0x44C940` 用 `word [ecx + edx*2 + 0xA4]` 读熟练度（`ecx` = `CPersonData`，
`edx` = 系 0..8）→ 熟练度存在 **`CPersonData+0xA4`**。

2019 参考 `codePersonExperience` 里用的是 `movzx eax, word [esi+4]` + `cmp eax, 320h`
（= 800）—— **和本项目的 `cmp eax,0x320` 一致，口径正确**（B9 不成立）。
`esi` 在那个 hook 点是 `CPersonData`，`word[esi+4]` = 武将索引 ✅

⚠️ 备注：`0x44C920` 是**系号校验**（0..8），不是武将校验。

## 6. 兵法表 41 条 vs 39 条（B6 属实）

`0x453720`：

```
mov  eax, [esp+4]
test eax, eax; jl fail
cmp  eax, 0x29        ; ★ 0x29 = 41
jge fail
imul eax, eax, 0x8C
add  eax, 0x14FDAD0
```

装载循环 `0x453FD2`：

```
mov  ebx, 0x14FDAD0
xor  edi, edi
loop: ... add ebx, 0x8C; cmp edi, 0x29; jl loop    ; ★ 41 条
```

→ 兵法表**确实是 41 条**。
`docs/HANDOVER.md` §3.3 写的「已证实兵法表实际是 39 条」是**错的**，
需要修正为 41，并同步改 `SkillConfig::Count` 39 → 41。

## 7. 武将表 850 条 vs 存档 800 条

- 武将表本体：**850** 条（`0x4531B0`: `cmp eax,0x352`；`0x453660` 扫描到 `0x351`）。
- 存档装载：**800** 条（`0x45415B`: `cmp edi,0x320`）。
- `0x453690` 附近另有 `push 0x320; call 0x4531B0` —— 同样是「按存档 id 取武将」。
- `0x44C230` / `0x44B950` 有 `0x2BC..0x31F`（700..799）范围检查 —— 存档 id 上界。

→ `OfficerPersonalization2026::Count = 800` 与存档 id 空间**一致**，
**B7 不是 bug**。800..849 是新游戏/额外武将池，存档里不存在。
`Cheat.s9` 的 550 条记录是原版工具的**另一个**数据集（见 `04_vs_San9PK.md`）。

## 8. 本项目现有实现 vs 正确做法

| 项 | 本项目现状 | 正确做法 |
| --- | --- | --- |
| 武将索引 | `(esi-0x1258EE0)/0x128` | `movzx eax, word [esi+4]`（esi 必须是 `CPersonData`）→ 必崩（B1/B8） |
| 间隔 hook 点 | `0x4FF08D` | `0x4FF08B`，且**只覆盖到 `push eax` 之前**（B5） |
| 间隔寻址 | `0F B6 0C 05` = `movzx ecx,byte[eax+eax+disp32]` | 索引被乘 2（B2）→ 用 `0F B6 04 01` = `movzx eax,byte[ecx+eax]` |
| 把握寻址 | `0F B6 0C 01` = `movzx ecx,byte[ecx+eax+1]` | 多 +1 偏移、基址寄存器用错（B3）→ `0F B6 04 01` |
| 把握 hook 点 | `0x4FEB95`（esi=常数 10） | `0x4FEB90`（esi=单位对象）→ 走 `+0x30` → `word[+4]` |
| 威力 hook 点 | `0x506035` | 点对，但 esi 不是 `CPersonData` → 走 `[esi+0x80]` → `+0x88` → 节点 → `word[+4]` |
| 熟练度上限 | `movzx eax,word[esi+4]` + `cmp eax,0x320` | **口径正确**，保持 |
| 覆盖回写 | 用 `originalSize` 字节覆盖 | B4：`original` 只含 6 字节 `call [0x5FF2F0]`，其后的 `add esp,0xC` 被吞 |

## 9. 复现工具（`tools/`）

| 脚本 | 用途 |
| --- | --- |
| `spy_match.py` | 逐条比对 spy patch 表与 `San9PK.exe`（29/29 OK） |
| `game_dis.py <va>[:len]` | 反汇编 `San9PK.exe` 任意地址 |
| `pe_dis.py <file> <va>` | 反汇编任意 PE 任意地址 |
| `spy_dis.py <file> <va>` | 同上（spy 专用入口） |
| `game_xref.py` | 找绝对地址引用（武将表 / 访问器） |
| `game_acccall.py` | 找访问器 `0x4531B0` 的全部调用点及上下文 |
| `game_idsync.py` | 判定索引来源（`dword[+0x94]` vs `word[+4]`） |
| `game_stack.py` | 符号栈模拟 `0x44F830`，判定 `record+4` 写入来源 |
| `game_rtti2.py` / `game_rtti3.py` | RTTI / vtable 解析 |
| `s9_dump.py` | `Cheat.s9` 格式解析 |

## 10. 尚未解决 / 待查

> 2026-10-27 更新：下面 1、3 两条的**实现决策已定并落地**（见 §11）；
> 2、4 两条仍需实机验证。
>
> 2026-11-02 更新：§11.2「把握概率 hook `0x4FEB90`」的**实现决策已作废并被推翻**——
> `0x4FEB90` 是**抵抗骰**而非发动判定。详见 §14。

1. **间隔「天」↔ 兵法记录 `+0x7C` 内值 的换算**。
   **已在 §11.4 落地决策**：本项目 `interval[800][41]` 存的就是「发动间隔(天)」，
   命中时**直接把配置天数写进 eax**（跳过 `idiv`）；未命中走原版 `idiv ecx`。
   原版那套「byte 直接覆盖 + `idiv`」的换算不再使用。
2. **把握 / 威力 两个 hook 现场的对象图**已能走通（见 §3.2 / §3.3），
   但需要确认：
   - 把握：单位 `+0x30` 是否在所有情况下都是 `CPersonData*`，
     还是需要原版的「城市链表」回退路径。
   - 威力：`[esi+0x80] → +0x88` 的链表结构是否与把握路径相同。
   → 两者已在 §11.2 / §11.3 按「当前可用的最短路径」落地；**实机确认链表结构**
     与预期一致仍有必要。
3. **成长加成（`codeBlockTail`）的 hook 现场**：
   2026-10-27 重核钩点：`codeBlockTail = dword_42F27C + 0x40` 附近
   `mov [esp+4],eax; jmp`——现场 `esi` 就是 `CPersonData`（成长加成函数内），
   **不是** `0x450612` 那条 `call 0x44D2A0` 路径。因此**直接 `word[esi+4]` 可用**，
   并按「按武将列表」语义落地（§11.1）。
4. **实机验证**必须由用户执行；离线测试只能验证字节序列。

## 11. 已实施的修复（2026-10-27）

> 全部改动在 `src/runtime/Runtime2026.cpp`，字节级用 capstone 验证（`tools/asm_check.py`）。
> 测试 `tests/runtime_test.cpp` 的 `injectionCodeBytesTest` 断言新字节、拒绝旧字节。

### 11.1 成长加成（`codeBlockTail` = `dword_42F27C + 0x40`）

原实现（崩）：`sub eax,0x1258EE0; sar eax,7`（B1/B8）→ 换成 40 字节桩：

```
89 C1                    mov  ecx, eax            ; 保存增长概率
0F B7 46 04              movzx eax, word [esi+4] ; ★ 武将索引（esi=CPersonData）
3D 20 03 00 00           cmp  eax, 0x320         ; 界 0..799
73 11                    jae  skip
BA <bitset addr>         mov  edx, bitsetAddr
80 3C 02 00              cmp  byte [edx+eax], 0
74 06                    je   skip
69 C9 <mult>             imul ecx, ecx, mult
89 4C 24 04              skip: mov [esp+4], ecx
FF 25 F0 F2 5F 00        jmp  [0x5FF2F0]         ; 尾部跳进原 API
```

### 11.2 把握概率（hook `0x4FEB90`，原 `mov esi,0xA` 5 字节）

> ⚠️ **2026-11-02：本节实现已作废并被推翻**（见 §14）。`0x4FEB90` 是**抵抗判定**，
> 把握概率已迁移到发动判定骰 `0x4FEC59`。以下保留为历史记录。

56 字节桩：`mov ebx,esi` 保存单位 → 重放 `mov esi,0xA; call [0x5FF2F0]; add esp,0xC`
→ 沿 `[ebx+0x30]`（CPersonData*）→ `word[+4]` 查 `activationBoost`，
命中则 `mov eax,1`，`jmp 0x4FEB9E`。未命中 `jmp 0x4FEB9E`（eax 保持原值）。

### 11.3 威力加倍（hook `0x50603B`，原 `add esp,0xC; test eax,eax` 5 字节）

84 字节桩：重放 `add esp,0xC`；沿 `[esi+0x80]` → `[+0x88]` 链表逐节点
（`node+8` = CPersonData）→ `word[+4]` 查 `criticalHit`，命中则 eax=1，
`jmp 0x506040`。链表为空 / 未命中走原 `test eax,eax; jmp 0x506040`。

### 11.4 发动间隔（hook `0x4FF08B`，原 `idiv ecx; mov ecx,esi; push eax` 5 字节）

77 字节桩，返回 `0x4FF090`（**让原 `call 0x4FB760` 继续执行**，B5 不再吞）：
`push ebx/esi/eax/ecx` → `[ebx+0x34]&0x3F` = 技能ID（<0x29）→ `word[esi+4]` 武将
→ `intervalFlat[o*41+s]`（**0xFF = 未自定义**，走原版 `idiv ecx`）→ 命中直接写天数，
`jmp 0x4FF090`。两条路径尾部都重放 `mov ecx,esi; push eax`。

### 11.5 配套

- `installPerOfficerHook` 新增 `returnAddress` 参数，回写**恰好 originalSize 字节**
  （B4；activation/critical 均 5 字节 `E9 rel32`，不再吞 `add esp,0xC`）。
- `SkillConfig::Count` 39→41（B6）；布局 `state_size` 7692→7844，
  `FormationFlags`/`FormationEntries` 后移 7624/7625（避免撞 `FormationAttackRangeAddress`
  偏移 5212）；`SkillEntriesSize` 156→164；`San9PKHardSkill.txt` 补两行 `0`；
  `PatchImage::object_` → 7844。
- 测试同步：`runtime_test.cpp` 的 `injectionCodeBytesTest` 重写（断言四个块各自
  `word[ptr+4]` 索引 + 拒绝 `sub 0x1258EE0` 形式）；`test_runtime_generation.py` 布局契约；
  `skillConfigTest` 末条 40 合法 / 41 越界；`tools/_addr_check.py` / `tools/offline.py`
  STATE_SIZE=7844。

### 11.6 验证

- Debug 构建 + `San9RuntimeTests.exe --all assets/config` → **全绿**（`exit=0`）。
- `--ui-smoke` → **`exit=0`**。
- Publish 构建通过，`out/trial/` 已重新出包（新 exe + 6 配置 + 使用说明）。
- 字节断言反向验证：把注入块改回旧错误字节 → 测试**失败**。

---

## 12. 崩溃 #3：禁用原工具块时必须保留其返回值契约（2026-10-31）

### 12.1 现象

实机崩溃，Windows 事件日志报 `San9PK.exe` 异常 `0xC0000005`，
故障偏移 `0x0005C8A0` → 游戏地址 `0x45C8A0`。

**仅凭事件日志会判错方向**：第一轮结论猜是「`codeCheckTransportBlock`
未判空就解引用 `call eax` 的返回值」，这是**错的**。

### 12.2 决定性证据：WER 本地转储

启用 Windows Error Reporting 本地完整转储：

```powershell
New-Item -Path "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Force
Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Name DumpType -Value 2
Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps" -Name DumpFolder -Value "$env:LOCALAPPDATA\CrashDumps"
```

转储：`%LOCALAPPDATA%\CrashDumps\San9PK.exe.12380.dmp`（用 `minidump` 包解析）。

| 项 | 值 | 含义 |
| --- | --- | --- |
| `ExceptionAddress` | `0x45C8A0` | 崩溃函数 |
| `ExceptionInformation` | `[0x0, 0x28]` | **读地址 = 0x28** → 基址寄存器为 0 |
| `ECX` | `0x00000000` | `this` 为空 |
| `EIP` | `0x45C8A0` | |
| 栈顶返回地址 | `0x0051FA42` | **调用点在游戏原生代码里**，不在我们上传的块里 |

`0x45C8A0` 是 **thiscall**：

```
0045C8A0: mov eax, dword ptr [ecx + 0x28]   ; ← ecx==0 → 读 0x28 崩溃
```

栈顶返回地址 `0x51FA42` → 调用点 `0x51FA3D`（**游戏原生**）。
若崩溃发生在我们上传的 `codeCheckTransportBlock`（`allocationBase+0x5400`）里，
返回地址应是 `allocationBase+0x5407`——**不是**。所以第一轮结论被推翻。

### 12.3 真正根因：上一轮的「修复」破坏了返回值契约

`0x51FA13` 处我们用 descriptor `0x42C930` 写入了 `call allocationBase+0xA9A50`
（原为游戏原生 `add esp,4`）。而 `allocationBase+0xA9A50` 正是 `unk_42C894` 块。

原块（136 字节）的**返回值契约**是「原样返回入口 eax」：

```
000A9A5D: mov ebp, eax          ; ebp = 入口 eax
   ...
000A9AD0: mov eax, ebp          ; 出口：返回入口 eax
000A9AD2: pop ebx / pop esi / pop ebp
000A9AD5: ret 4
```

**2026-10-30 的禁用桩是 `xor eax,eax; ret 4`** —— 把 eax 抹成 0，
直接违反该契约。崩溃链：

```
0x51FA0E: call 0x453280      ; eax = 对象指针（实测非空：索引 14709 < 0x9C40）
0x51FA13: call 0xA9A50       ; ← 我们的桩：xor eax,eax → eax=0
0x51FA18: xor edx,edx
0x51FA1A: je  0x51FA3B        ; 恒跳转
0x51FA3B: mov ecx,eax        ; ecx = 0
0x51FA3D: call 0x45C8A0      ; ★ 读 [0x00+0x28] → 访问违例
```

注意：`0x453280` 本身**工作正常**（实测索引 14709 在范围内，对象指针 `0x135F748` 非空），
是**我们的桩**把返回值改成了 0。

### 12.4 修复

桩改为 **`C2 04 00`（`ret 4`）**：

- eax 原样返回（= 入口 eax）→ 满足调用方的 `mov ecx,eax`；
- 栈弹出 1 个 stdcall 参数 → 与原块净栈效果一致；
- 不执行任何污染逻辑。

> ⚠️ **2026-10-31 更正**：本节原先写的「→ 崩溃 #2（`0x5B6754`）的根因仍被消除」是**错误归因**，
> 已被 12:48 的 dump 证伪——**禁用 `unk_42C894` 之后游戏仍然崩在 `0x5B6754`**。
> 崩溃 #4 的真正根因是**我们自己**在 `Runtime2026::applyCultivate()` 里写 `0x6613EE`，
> 把 id 表 A 首条记录的 id dword `0x6613EC`（值 `0x000003E8`）的第 2 字节破坏掉，
> 导致游戏按 id `0x3E8` 查找失败 → `0x5B72F0` 解引用空 entry。
> 详见 `docs/_CRASH4_HANDOFF_2026-10-31.md`。
> 保留 `ret 4` 桩仍然正确（它修复的是崩溃 #3 的返回值契约问题，与 #4 无关）。

回归测试 `disabledOriginalToolBlockTest`（`tests/runtime_test.cpp`）锁定：
桩必须是 `ret 4`，且**不得**出现 `33 C0`（`xor eax,eax`）。

**反向验证**：把桩改回 `33 C0 C2 04 00` → 离线测试**失败** → 恢复 → 全绿。

### 12.5 全量排查（举一反三）

| 方向 | 范围 | 结论 |
| --- | --- | --- |
| 返回值契约被改变 | 329 个上传块 | **只有 `unk_42C894` 一个**的内容被我们替换过（其余 328 个是原工具原样字节，仅重定位）→ 风险面仅此一点，已修 |
| 未判空解引用 | 329 个上传块（`tools/audit_unguarded_deref.py`） | 7 处候选 → **6 处误报**（数据区被反汇编成指令 / 绝对地址写 / `ret` 提前返回）；唯一真实项 `unk_42B718` 的 `call 0x49C650` 是**构造函数**（`mov eax,esi; ret 0x20`），必返回非空 → **无新增隐患** |
| 被改写的游戏调用点 | 2012 处 descriptor（`tools/audit_call_contracts.py`） | 无其他「返回值语义被改变」的实例 |

### 12.6 新增复现工具

| 工具 | 用途 |
| --- | --- |
| `tools/audit_call_contracts.py` | 枚举所有「把游戏调用点改成跳进我们分配区」的 descriptor，比对原字节与新载荷的返回值/栈契约 |
| `tools/audit_unguarded_deref.py` | 扫描所有上传块中「调用后不判空就解引用」的形态 |

---

## 13. 崩溃 #4（`0x5B6754`）根因与修复（2026-10-31）

> 完整证据链见 `docs/_CRASH4_HANDOFF_2026-10-31.md`。本节只记「改了什么、怎么验的」。

### 13.1 根因（一句话）

`Runtime2026::applyCultivate()` 往 `0x6613EE` 写了 1 字节，而 `0x6613EE` 是
id 表 A 首条记录的 **id dword `0x6613EC`（值 `0x000003E8`）的第 2 个字节**。
写入后 id 1000 变成 `0x000103E8` → 游戏按 id 查找失败 → `0x5B72F0`（getflag，
**无空指针检查**）解引用空 entry → 异常地址 `0x5B6754`、读取地址 `0x14`。

字节级复核（`San9PK.exe` 直接读盘）：

```
表 A 0x6613E8 entry0: 00 84 00 00 | e8 03 00 00
                              ↑0x6613EC   ↑0x6613EE
0x6613EC 的 dword = 0x000003E8（id 1000）
```

5 张 id 表（每张 11 条 × 步长 `0x20`，id 在 `entry+0x04`）：

| 表 VA | entry0 的 id |
| --- | --- |
| `0x6613E8` | `0x3E8` |
| `0x665B70` | `0x3E8` |
| `0x667740` | `0x3E8` |
| `0x667C08` | `0x3E8` |
| `0x669F78` | `0x3E8` |

### 13.2 落地修改

| # | 位置 | 改动 |
| --- | --- | --- |
| A1 | `src/runtime/Runtime2026.cpp` `applyCultivate()` | **删除** `writePatch((LPVOID)0x6613EEu, &activate, 1u)` 与 `const BYTE activate` 变量；注释改为指向 `RuntimeInjection2026.cpp:1170-1172` |
| A2 | 同上 + `docs/DESIGN.md` + `docs/REQUIREMENTS_1_4.md` | 保留 `0x14FA656` 写入，标注「⚠️ 未验证地址：PE 内零引用、无指针指向、位于 .data BSS；若实机再崩，先删这行」 |
| A3 | `tests/runtime_test.cpp` | 新增 `idTableWriteGuardTest()`，注册于 focused 表与 `--all` 流程 |
| A4 | `docs/_INJECTION_FIX_NOTES.md` §12.4 / `docs/HANDOVER.md` §8 / `docs/DESIGN.md` / `docs/REQUIREMENTS_1_4.md` / `PLAN.md` / `CHANGELOG.md` | 更正错误归因与错误定址 |

**「开启小兵培养」的正确实现未改动**：`RuntimeInjection2026.cpp:1170-1172` 写
`0x40CAEF` / `0x40BF25`（两条 `cmp eax,0x32` 的立即数字节）= `0x32` / `0x00`，
与原工具 SanIXPKCheat（`sub_409E10.c:5153-5162`）、2019 参考实现
（`San9PKHardDlg.cpp:4261-4265`）、游戏本体反汇编三方一致。

### 13.3 回归测试 `idTableWriteGuardTest`

断言三件事：

1. **所有**录制到的写入都不覆盖 5 张 id 表区域
   （`0x6613E8` / `0x665B70` / `0x667740` / `0x667C08` / `0x669F78`，各 11 × `0x20` = `0x160` 字节）；
2. 特别断言 `0x6613EE` 与 id dword `0x6613EC` 不在任何写入范围内；
3. 「开启小兵培养」打开时确实写到 `0x40CAEF` 与 `0x40BF25` 且值为 `0x32`，关闭时为 `0x00`。

**反向验证**：临时把 `0x6613EE` 写入加回去 → 测试报
`FAILED: no recorded write overlaps a static id table region` → 恢复 → 全绿。

### 13.4 归因更正（重要）

`_INJECTION_FIX_NOTES.md` §12.4 与 `HANDOVER.md` §8 原先写的
「禁用 `unk_42C894` 即消除 `0x5B6754` 崩溃」**是错的**——12:48 的 dump 证明
**禁用之后仍然崩在同一地址**。禁用 `unk_42C894` 本身仍然正确
（它修复的是崩溃 #3 的返回值契约问题），只是与 `0x5B6754` 无关。

---

## 14. 兵法抵挡 bug 与把握概率不生效：两枚骰子挂错了（2026-11-02）

### 14.1 现象（用户报告的两个症状）

1. 「兵法抵挡似乎有 bug；经常出现**熟练度很高的人发动兵法时被熟练度低的人抵挡**」
2. 「**兵法发动概率这个设置也是没有生效的**，有武将设置了这个但是没有起效果」

### 14.2 根因：游戏里有两枚独立的骰子

| 骰子 | 所在函数 | 掷骰点 | 概率 | 结果语义 |
| --- | --- | --- | --- | --- |
| **抵抗骰** | `0x4FEB20` | `0x4FEB95` | (对手值 − 我方熟练度) / 10 | **eax≠0 ⇒ 抵抗成立** |
| **发动骰** | `0x4FEBB0` | `0x4FEC5C` | 100 + 随机抖动 | 失败 ⇒ `xor eax,eax` ⇒ 返回 0 ⇒ 兵法不发动 |

抵抗骰现场：

```
0x4FEB8D: push 0xB
0x4FEB8F: push edx
0x4FEB90: mov esi, 0xA          ; ★ 旧 hook 点
0x4FEB95: call [0x5FF2F0]        ; ★ 掷骰
0x4FEB9E: test eax, eax
0x4FEB9E+?: mov esi, 1           ; eax≠0 ⇒ esi=1 = 抵抗成立
```

抵抗骰的唯一调用点是 `0x5066A1`，结果在 `0x506838` 被消费
（`cmp edi,1 / setg dl`）。

发动骰现场：

```
0x4FEC53: push 1
0x4FEC59: push 0xB              ; ★ 新 hook 点
0x4FEC5B: push edx
0x4FEC5C: call ebp              ; ★ 掷骰（kakuritsu）
0x4FEC5E: add esp, 0x14         ; ★ 清 5 个压栈
0x4FEC61: test eax, eax
0x4FEC65: xor eax, eax          ; 失败 ⇒ 返回 0 ⇒ 不发动
```

### 14.3 旧实现为什么同时造成两个症状

旧实现把把握概率 hook 挂在 **`0x4FEB90`（抵抗骰）** 并强制 `eax=1`。
由于「eax≠0」在抵抗骰里表示「**抵抗成立**」，
所以「勾选把握概率 ⇒ 该武将**必定被抵挡**，且**熟练度完全不参与判定**」——症状 ①。

同时真正的发动骰 `0x4FEC5C` 从未被 hook，
所以「勾选把握概率」这个开关**根本不影响发动判定**——症状 ②。

**一个根因，两个症状。**

### 14.4 修复（`src/runtime/Runtime2026.cpp` `applyOfficerPersonalization()`）

1. 把 `0x4FEB90` **恢复成原版字节** `BE 0A 00 00 00`（`mov esi,0xA`），
   抵抗判定完全按原版规则走（熟练度照常参与）；
2. 把握概率改挂到**发动骰 `0x4FEC59`**
   （`push 0xB; push edx; call ebp`），命中时跳过掷骰、直接 `mov eax,1`，
   返回 `0x4FEC5E`；未命中则原样重放原调用；
3. 日志打印 `personalization activationBoost: resist roll restored at 0x...` 作为佐证。

### 14.5 ⚠️ 改这个桩必须遵守的两条约束

**（1）`kakuritsu` 是 `__cdecl`。**

导出名 `?kakuritsu@@YAHHW4RANDOM_KIND@@H@Z` —— `YAH` = cdecl（第一个 `Y` = cdecl、
`A` = 无修饰、`H` = int 返回）。参数由**调用方**清。

原版 `0x4FEC5E` 的 `add esp,0x14`（20 字节）恰好清掉 5 个压栈：

```
push eax   (0x4FEC25)
push ebp   (0x4FEC30)
push 1     (0x4FEC53)
push 0xB   (0x4FEC59)
push edx   (0x4FEC5B)
```

所以「命中」路径**必须照原版压入 `0xB` 与 `edx`**（虽然不掷骰），
多压/少压都会破坏栈平衡。

**（2）rel32 字段偏移有两种约定，不要混用。**

| 位置 | 约定 | 本例取值 |
| --- | --- | --- |
| C++ `installPerOfficerHook` 的 `extraJumpRelOffset` | **rel32 字段偏移** = E9 操作码偏移 + 1 | `0x28` |
| `tools/asm_check.py` 的 `rel32={...}` | **E9 操作码偏移** | `0x27` / `0x35` |

两者指向**同一段** rel32 字段，只是约定不同。

### 14.6 桩布局

- 桩大小 58 字节，hook 点 `0x4FEC59`，返回地址 `0x4FEC5E`
- `arrayAddrOffset = 0x15`
- `extraJumpRelOffset = 0x28`

### 14.7 验证

- 字节级用 capstone 验证（`tools/asm_check.py`）；
- `tests/runtime_test.cpp` 的 `injectionCodeBytesTest` 断言新字节、拒绝旧字节；
- Debug / Release / Publish 三种配置构建通过，`--all assets/config` 与 `--ui-smoke` 均 exit=0；
- **实机验证仍待用户执行**（见 `docs/HANDOVER.md` §4.8 的验证清单）。
