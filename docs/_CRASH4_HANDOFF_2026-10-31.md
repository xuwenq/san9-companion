# 崩溃 #4 (`0x5B6754`) 根因定位 —— 交接上下文

> 用途：`/clear` 之后的接手者。写于 2026-10-31。
> 状态：**根因已定位并逐字节验证**；修复方案已与用户确认（§5.5）；代码尚未落地。
>
> **★ 新接手者请先读这三个小节**：
> §5.5（已确认的修复方案 A1–A4）、§5.8（「开启小兵培养」的正确实现）、
> §12.2（逐条可执行的下一步）。
>
> **★ 本文档中已被推翻的旧结论**（不要照着做）：
> §5b（旧称 `0x6613EE` 是 "+0x06 字段" → 实为 id dword 的第 2 字节）、
> §5.6（旧称 `0x14FA656` 在 `.text` 段安全 → 实为 `.data` BSS 的无依据猜测地址）。

---

## 0. 三十秒摘要

崩溃 #4 的根因**已经查明**，不是猜测，是从实机 dump 的栈逐字节还原出来的：

```
游戏原版 setflag(0x5B7B00) 的第一步就是调 getflag(0x5B72F0)，
而 getflag **没有空指针检查**：

  0x5B72F0  mov eax,[esp+4]     ; eax = 要查询的 id
            push esi / push eax
            mov  esi,ecx
            call 0x5B7050        ; lookup by id
            push eax             ; ★ eax 未检查是否为 0
            mov  ecx,esi
            call 0x5B6750        ; movzx eax, byte [eax+0x14]  ← eax==0 时 AV
```

* `0x5B7050`（lookup）遍历 manager 的 vector，`cmp dword [eax+4], ebx` 比对 **id**，
  未命中时 `xor eax,eax` 返回 0。
* 崩溃时 `0x5B6750` 的参数（= lookup 返回值）**确实是 0**（dump 栈上 `[esp+4]=00000000`）。
* 也就是说：**被查询的 id 在 manager 的 vector 里不存在**。

dump 还原出的完整调用链与实参：

| 栈位置 | 值 | 含义 |
| --- | --- | --- |
| `[esp+0x00]` | `0x005B7305` | 返回地址（在 `0x5B72F0` 内） |
| `[esp+0x04]` | `0x00000000` | **lookup 返回值 = 0 → 崩溃根因** |
| `[esp+0x08]` | `0x19067E68` | manager 对象 |
| `[esp+0x0C]` | `0x005B7B0F` | 返回地址（在 `0x5B7B00` 内） |
| `[esp+0x10]` | `0x000003E8` | **被查询的 id = 1000** |
| `[esp+0x14]` | flag 值 | setflag 第 2 参数 |
| `[esp+0x18]` | `0x00000001` | |
| `[esp+0x1C]` | `0x00000001` | |
| `[esp+0x20]` | `0x0052C1F6` | 返回地址（`0x52C1F1: call 0x5B7B00`） |
| `[esp+0x24]` | `0x000003E8` | id = 0x3E8 = 1000 |
| `[esp+0x28]` | `0x00000001` | |
| `[esp+0x2C]` | `0x19067E68` | manager |

两个 dump（`7580.dmp` / `13360.dmp`）**栈内容逐字节一致**，只有 manager 地址不同。

---

## 1. 关键澄清：x86 CONTEXT **不保存 Eax**

这是本次会话最大的一个纠错，之前的分析被它带偏了很久。

Windows 的 x86 `CONTEXT` 结构**只有** `Edi/Esi/Ebx/Edp/Ecx/Ebp/Eip/EFlags/Esp`，
**没有 Eax 字段**。

之前把 dump 里 `+0x9C` 的值当成 "Eax = 0x19067E68" 读出来，
于是和 `ExceptionInformation=[0, 0x14]`（读地址 0x14 ⇒ eax==0）矛盾。
真相是：那个值是**别的寄存器**（manager 对象指针），Eax 根本没被保存。

**正确的 dump 上下文偏移**（本会话用"在原始 context 里搜 ExceptionAddress 的值"定位，3 个 dump 交叉验证）：

```
+0x9C  19067E68   ???  (先前误当 Eax；实为 manager 对象指针)
+0xA0  19067E68   ???
+0xA4  000003E8   ???  (id 1000)
+0xA8  00661148   ???
+0xAC  19067E68   ???
+0xB0  00000000   ???
+0xB4  00000000   ???
+0xB8  005B6754   Eip   ← 异常地址
+0xBC  00000023   EFlags
+0xC0  00010246   ???
+0xC4  001AFC90   Esp   ← 栈指针（用"栈上返回值落在 0x5B6..0x5B8 区间"反推）
+0xC8  0000002B   ???
+0xCC  0020027F   ???
```

> Esp 的确定方法：在候选偏移里找一个"该地址上的 dword 落在 `0x5B6000..0x5B8000`"的值，
> 两个 dump 都唯一命中 `+0xC4`，且 `[esp+0]=0x005B7305` 正好是 `0x5B72F0` 内的返回地址。

---

## 2. 涉及的三个 dump

| 文件 | 时间 | 异常地址 | 说明 |
| --- | --- | --- | --- |
| `San9PK.exe.7580.dmp` | 08:44 | `0x5B6754` | 崩溃 #4，栈同构 |
| `San9PK.exe.13360.dmp` | 12:48 | `0x5B6754` | 崩溃 #4，**禁用 `unk_42C894` 之后仍然崩** |
| `San9PK.exe.12380.dmp` | 09:57 | `0x45C8A0` | 崩溃 #3（`ret 4` 修复**之前**的老 dump） |

**关键推论**：12:48 那个 dump 是在我们把 `unk_42C894` 禁用（`C2 04 00`）**之后**产生的，
所以 **`_INJECTION_FIX_NOTES.md:505` 与 `HANDOVER.md:734` 声称
"禁用 unk_42C894 即消除了 0x5B6754 崩溃"的归因是错的，必须更正。**

dump 里**没有** manager 对象所在的内存段（最近的段是 `0x1341FDAC..0x13420000`），
所以无法从 dump 直接看 vector 内容；这正是需要埋点读回的原因。

---

## 3. 游戏的 id / vector 结构（已完全解码）

### 3.1 manager 对象

* `0x52BF20` / `0x52C280` 是 manager 的方法，`ecx` = manager。
* dump 里 manager = `0x19067E68`。
* `manager + 0x74` = **vector 描述符指针**（`0x5B6BF0: mov ecx,[esi+0x74]`）。
* `manager + 0x136C` = 当前 id（`0x52BF84: mov [esi+0x136c], ebx`）。

### 3.2 vector 布局

```
vec = dword[manager + 0x74]
vec + 0x10 = count
vec + 0x14 = base            (entry 数组指针)
entry 尺寸  = 0x20 字节       (0x5B6BF0: shl eax,5)
entry + 0x00 = vtable 0x6254DC
entry + 0x04 = id      (WORD)  ← 0x5B7050 用 cmp dword [eax+4], ebx 比对的就是它
entry + 0x06 = id2     (WORD)
entry + 0x08 = dword
entry + 0x0C = -1
entry + 0x14 = flags   (dword) ← 0x5B6750 读的 byte [eax+0x14]
```

* `0x5B9B00` = entry 构造函数：把 `entry+4`/`entry+6` 清 0，`entry+0x14` 清 0。
* `0x5B9A30` = vector insert：`mov word [ecx+4], ax` 写入 id。
* `0x5B6CC0` = 往 manager 的 vector 里插一条 entry 的代码路径。

### 3.3 id 0x3E8..0x3F2 的静态定义表（**本会话的新发现**）

在 `San9PK.exe` 的 `.data` 段里找到 **5 张** id 连续递增的表，
每张 11 条（id 0x3E8..0x3F2），**步长 0x20**，
并且 **id 就在 entry+4**（与 vector 布局完全一致）：

| 表 VA | 说明 |
| --- | --- |
| `0x6613E8` | 表 A |
| `0x665B70` | 表 B |
| `0x667740` | 表 C |
| `0x667C08` | 表 D |
| `0x669F78` | 表 E |

（表 B/C/D/E 各有一个 `.data` 指针指向它们：`0x665B6C` / `0x66773C` / `0x667BF4` / `0x669F64`。）

> 注意：表 A（`0x6613E8`）的第 0 条 entry 的 `+0x04` 是 `0x3E8`，
> 也就是 **我们 `applyCultivate()` 写的 `0x6613EE` 正好落在这张表的 id 字段上**！
> 见 §5。

这 5 张表在 `.text` 里**没有任何直接引用**（`push/mov` 全表扫过，0 命中），
说明是由某个注册器按指针间接灌入 manager 的 vector —— **这条路径还没查清，是下一步的重点**。

---

## 4. 我们（companion）在这条链上的全部写入

| 目标 | 大小 | 内容 | 说明 |
| --- | --- | --- | --- |
| `0x5B8556` | 5 | `e8 b5 0b af ff` (`call 0xA9110`) | **整个 0x5B0000–0x5C0000 区间唯一的补丁** |
| `0x52BC4A` | 54 | 原工具 payload | 属于另一个命令分发函数，与 id 注册无关 |
| `0x51FA13` | 7→3 | 目录写 7 字节；实际被 3 字节 `C2 04 00` 桩覆盖 | `unk_42C894` 禁用桩 |
| `allocationBase+0x9A50` | 3 | `C2 04 00` (`ret 4`) | 同上 |

### 4.1 已确认的事实

* **我们没有写坏 5 张 id 表中的任何一张**：catalog 里 699 条写入，逐条比对，
  **0 条** 与 `0x6613E8/0x665B70/0x667740/0x667C08/0x669F78` 的 11×0x20 字节区域重叠；
  `0x665000..0x66A000` 整段也是 **0 条**我们的写入。
* **重定位表自洽**：catalog 声明的 603 个 `payload_relocations` **603/603** 都在
  `relocations.json` 的 `image_dword` 里（`va` 字段，不是 `table_va`）。
* **payload 内指向工具镜像的绝对指针**：3 处没有重定位，但都是**误报/无害**：
  * `0x4201D2` / `0x4201F2` = `0xA0000` —— 位于两段**数据表**里，不是代码地址；
  * `0x42A39D` = `0xA4440` —— 位于 `mov dword [eax+0x44], 0xa` 的**立即数 0x0A**
    与后续字节的巧合匹配（`c7 40 44 0a 00 00 00`），不是指针。
* **`0x9110` 块**（`asc_42B0B4`，0xEA 字节）原工具确实写同一份 payload 到同一目标
  （`sub_409E10.c:1415`），我们 `RuntimeInjection2026.cpp:341` 也写了 `writePatch(dword_42F07C, asc_42B0B4, 0xEAu)`。
  → **不是与原工具的差异**。
* **`0x5B8556` 的调用契约问题（待查，重要）**：
  游戏原字节 `e8 c5 87 02 00` = `call 0x5E0D20`，该函数返回一个**指针**，
  调用方紧接着 `0x5B8561: mov eax,[eax+0xc]` 依赖它。
  而 `0xA9110` 那块（我们写入的）**所有出口都不返回指针**。
  → **高度可疑的语义差异，必须查证。**
* `0x5B6750` 的字节在 dump 里是 `8b442404 0fb64014 c1e802 f7d0 83e001`，
  与游戏原版一致 → **该函数本身未被我们改动**。

---

### 4.2 分配区写入布局（本次会话新查，320 条）

我们往分配镜像（`0xA0000..0xAB000`）写了 **320** 块，全部互不重叠
（唯一的 5 对"重复"是同一 `writePatch` 语句被解析两次，属幂等重复安装，无害）。

与崩溃相关的两块的位置：

```
0xA9110 .. 0xA91FA   asc_42B0B4（0xEA 字节）  ← 我们在 0x5B8556 调用的那块
0xA9990 .. 0xA9A4E   （相邻的前一块）
0xA9A50 .. 0xA9A52   unk_42C894_disabled 桩（C2 04 00）
0xA9BE0 .. 0xA9C00   （后一块）
```

* `0x42F038` 是**唯一**指向 `0xA9A50` 的槽 → 3 字节禁用桩**不会被别的写入覆盖**。
* `0xA9110` 那块与相邻块**不重叠** → 写入顺序无关。

## 5. ★★★ 根因已锁定：`applyCultivate()` 写坏了 id 0x3E8 ★★★

### 5.1 事实

`src/runtime/Runtime2026.cpp` 的 `applyCultivate()`：

```cpp
// 训练回合/培养时长 (0x14FA656) and the first 小兵 activation flag (0x6613EE).
writePatch(reinterpret_cast<LPVOID>(0x14FA656u), &duration, 1u);
writePatch(reinterpret_cast<LPVOID>(0x6613EEu), &activate, 1u);   // ★ 元凶
```

游戏 `.data` 段实际字节：

```
0x6613E0: ff ff ff ff 00 00 00 00 00 84 00 00 | e8 03 00 00 ...
                                  ↑0x6613E8   ↑0x6613EC
```

* `0x6613EC` 处的 dword = **`0x000003E8`** ← 这就是 id 1000 的字段
* `0x6613EE` = 该 dword 的**第 2 个字节**（byte index 2）

我们往 `0x6613EE` 写 1 字节（值 1）后：

```
0x6613EC 的 dword 变成 0x000103E8     ← id 被破坏！
```

而 `0x5B7050`（lookup）正是用 `cmp dword [eax+4], ebx` 按 **id** 查，
`ebx` = `0x3E8`（= 1000，来自 setflag 的参数）。
**id 被改成 0x103E8 之后，查找 0x3E8 永远失败 → 返回 0 → `0x5B6750` 读 `[0+0x14]` → AV。**

### 5.2 与 dump 完全吻合

* dump 栈上 `[esp+0x04] = 0x00000000` ← lookup 返回值 = NULL（正是 id 被破坏的表现）；
* `[esp+0x10] = 0x000003E8` ← 查询的 id = 1000；
* 崩溃点 `0x5B6754` = `movzx eax, byte [eax+0x14]`，eax=0 → 读地址 0x14
  → 与 `ExceptionInformation=[0, 0x14]` 完全一致。

### 5.3 为什么之前一直没发现

1. `0x6613EE` 在游戏里**没有任何代码引用**（扫过 `.text`，`push/mov` 全表 0 命中）
   —— 它是一个"孤儿"地址，看起来像无害的独立标志位。
2. 它在 `.data` 里紧邻 id 表，肉眼极难联想到 id 字段。
3. 之前所有分析都盯着 `0x661140`（`unk_42C894` 用的那个），
   而**真正的污染通道是 `0x6613EE`**——不同的地址。
4. `0x3E8` 这批 id 是 1000~1010，
   `0x5B70D0: cmp esi, 0x3E8` 又容易被误读成"id 上限"，掩盖了真实含义。

### 5.4 这个 bug 是**我们自己**引入的

* `recovery/evidence/global_assignments.json` 里**没有** `0x6613EE` 的任何记录
  → **原工具 SanIXPKCheat 从不写这个地址**。
* `docs/DESIGN.md:51` 与 `docs/REQUIREMENTS_1_4.md:127` 把它写成
  "强制小兵提拔：置 `0x6613EE`（小兵激活标志）=1" —— 这是**当时凭猜测定的地址**，
  从未经过 RE 验证。
* → **与用户"是不是你的对 sanixpkcheat 的逆向不太准确"的直觉一致**：
  问题不在原工具的 RE，而在我们自己新增功能时猜了一个地址。

### 5.5 修复方向（★ 已与用户确认，2026-10-31）

**用户已拍板**（原文记录）：

* Q1「`0x14FA656` 这个我们自己猜的地址怎么处置？」→ **"保留写入，标为待验证"**
* Q2「强制小兵提拔的正确实现是什么？」→ **"完全对齐原工具（推荐）"**

具体落地清单：

| # | 动作 | 状态 |
| --- | --- | --- |
| A1 | **删除** `applyCultivate()` 里对 `0x6613EE` 的写入 | 待做 |
| A2 | **保留** `0x14FA656` 的写入，但代码注释与文档统一标注为「未验证的实验地址」 | 待做 |
| A3 | 「强制小兵提拔」功能**完全对齐原工具**：`0x40CAEF` / `0x40BF25` = `0x32`/`0`（已实现，见 §5.8） | 已实现 |
| A4 | 回归测试：断言 `0x6613EE`（及 5 张 id 表全体 id 字段）不得被写入 | 待做 |

> A2 的**已知风险**：`0x14FA656` 落在游戏 `.data` 的 BSS 区，PE 内零引用，
> 写它可能是纯粹的无操作（无害），也可能踩到别的结构（有害）。
> 用户已知悉并选择保留。**若实机再次崩溃，第一件事就是把这行也删掉试。**

> ⚠️ 其它 4 张表（`0x665B70` / `0x667740` / `0x667C08` / `0x669F78`）
> 的对应 id 字段分别在 `+0x04`（= 表基址+4），**我们都没写**。
> 只有表 A 的 `+0x02` 字节（即 `0x6613EE`）被误写。

### 5.6 需要一并复核的地址：`0x14FA656`（结论已修正）

> ⚠️ **本节先前写的"`0x14FA656` 在 `.text` 段，核查完毕安全"是错的，
> 已被本次会话的复核推翻。** 正确结论如下。

`0x14FA656`（= 21997142）是我们自己加的「培养时长(旬)」写入目标。复核结果：

| 检查项 | 结果 |
| --- | --- |
| 落在哪个段 | **`.data`**（VA `0x64B000`–`0x1B44000`），**不是 `.text`** |
| 是否在文件里有字节 | **没有** —— `.data` raw 只有 `0x25A00`，该地址在 BSS 区（运行时零初始化） |
| PE 内是否有绝对引用 | **0 处**（全文件扫 `struct.pack('<I',0x14FA656)`） |
| `.data` 里有没有指针指向它 | **0 个**（扫 `.data` raw 全部 4 字节对齐值，落在 `0x14FA000`–`0x14FB000` 的指针：**0 个**） |
| 原工具的证据记录 | `recovery/evidence/global_assignments.json` 里**没有** `0x14FA656` |
| 2019 参考实现 | **没有** `0x14FA656`（grep `recovery/reference-2019/` 0 命中） |

→ **`0x14FA656` 和 `0x6613EE` 一样，是我们凭猜测定的地址**，没有任何 RE 依据。

**特别提醒（易踩坑）**：原工具 `StateConstants.inc` 里 `field<DWORD>(7616) = 22022040`
（= `0x1500798`）与 `0x14FA656` **不是同一个地址**（`0x14FA656` = 21997142）。
7616 槽在原工具里是「无官禄」参数：

```
sub_409E10.c:5022   flOldProtect = *(_DWORD *)(this + 420);
sub_409E10.c:5024   WriteProcessMemory(hProcess, *(LPVOID*)(this + 7616), &flOldProtect, 4u, nullptr);
sub_408670.c:359    *(this + 1904) = 22022040;      // 1904 == 7616
```

我们的 `RuntimeInjection2026.cpp:1142` 已经**正确**实现了这一条：

```cpp
patchValue = *(_DWORD *)(state + san9::injection2026::NoOfficialSalary);
if (patchValue != 0)
    writePatch(*(LPVOID *)(state + 7616), &patchValue, 4u);   // → 0x1500798
```

> ⚠️ 注意 `RuntimeInjection2026.cpp:1142` 写的是 **4 字节**（与原工具一致）；
> 而 `Runtime2026.cpp:408` 的 `0x14FA656` 写的是 **1 字节**。
> 两者是**不同地址**，不冲突，但后者是猜测。

### 5.7 全量复核结果（本次会话做完，可直接引用）

把**所有运行时写入**（catalog 的 699 条 + 我们自己新增功能的写入）
与 5 张 id 表的 11×0x20 字节区域逐一比对：

```
运行时写入 0x14FA656+1 / 0x6613EE+1 / 0x4B87EB+1 /
           0x502667+3 / 0x50266D+1 / 0x502677+1
        × 5 张 id 表 × 11 条 entry

命中：仅 1 处
   WRITE 0x6613EE+1  overlaps table 0x6613E8 entry[0] id-field @ 0x6613EC
```

**结论：整个项目里唯一破坏 id 的写入就是 `0x6613EE` 这一处。**

`0x14FA656` 与 id 表无重叠（不同段），但它本身是**无 RE 依据的猜测地址**（见 §5.6）。

这同时解释了为什么 audit todo #6（"审计所有对 0x66xxxx id 段的写入"）
一直查不出结果 —— 之前一直在盯 `0x661140`（`unk_42C894` 用的），
而真正的通道是**另一个地址 `0x6613EE`**。

### 5.8 ★★★「强制小兵培养 / 强制小兵提拔」的正确实现（三方证据闭合）

**结论：原工具 SanIXPKCheat 的「开启小兵培养」参数（control id 1013，`host_offset 148`）
根本不写任何小兵记录、也不写任何"激活标志"。**

它写的是**两条 `cmp eax, 0x32` 指令的立即数字节**：

| 地址 | 游戏字节 | 指令 | 语义 |
| --- | --- | --- | --- |
| `0x40CAEF` | `83 f8 32` @ `0x40CAED` | `cmp eax, 0x32` | 训练数阈值（后接 `0x40CAF4: lea eax,[esi+0x312] … cmp esi,0x32` 的 0..50 号徒弟槽搜索循环） |
| `0x40BF25` | `83 f8 32` @ `0x40BF23` | `cmp eax, 0x32` | 同一条阈值，兄弟路径 |

* 写 `0x32`（=50）→ 阈值可达 → **培养/提拔启用**
* 写 `0x00` → 阈值不可达 → **关闭**

**三条独立证据完全一致**：

1. **原工具反编译** `recovery/evidence/decompiled/sub_409E10.c:5153-5162`
   ```c
   flOldProtect = *(_DWORD *)(this + 148) != 0 ? 0x32 : 0;
   WriteProcessMemory(*(HANDLE *)(this + 112), *(LPVOID *)(this + 6224), &flOldProtect, 1u, nullptr);
   WriteProcessMemory(*(HANDLE *)(this + 112), *(LPVOID *)(this + 6228), &flOldProtect, 1u, nullptr);
   ```
2. **槽位初始化** `recovery/evidence/disassembly/00408670.asm:20-22`
   ```
   004086BA  c7 86 50 18 00 00 ef ca 40 00   mov dword ptr [esi+1850h], (offset loc_40CAEB+4)
   004086C9  c7 86 54 18 00 00 25 bf 40 00   mov dword ptr [esi+1854h], (offset loc_40BF1F+6)
   004086D3  89 ae 58 18 00 00                 mov [esi+1858h], ebp   ; ebp = 0x32 (50) = OldTrainNum
   ```
   → `1850h` = 6224 → `0x40CAEB+4` = **`0x40CAEF`**；
     `1854h` = 6228 → `0x40BF1F+6` = **`0x40BF25`**。
   我们的 `src/generated/StateConstants.inc:4-5` 已经把这两个值转出来了：
   ```
   field<DWORD>(6224) = static_cast<DWORD>(0x40CAEB + 4);
   field<DWORD>(6228) = static_cast<DWORD>(0x40BF1F + 6);
   ```
3. **2019 版参考实现** `recovery/reference-2019/San9PKHardDlg.cpp`
   ```
   :34   m_dwTrainNumAddr(0x40CAEF), m_dwTrainNumAddr2(0x40BF25), m_dwOldTrainNum(50),
   :4261 dwWrData = (m_bEnableTrain == FALSE ? 0 : 50);
   :4262 WriteProcessMemory(m_hSan9PKProcess, (LPVOID)(m_dwTrainNumAddr), &dwWrData, BYTE_SIZE, NULL);
   :4265 WriteProcessMemory(m_hSan9PKProcess, (LPVOID)(m_dwTrainNumAddr2), &dwWrData, BYTE_SIZE, NULL);
   :5331 WriteProcessMemory(..., (LPVOID)(&m_dwOldTrainNum), BYTE_SIZE, NULL);   // 还原为 50
   :5332 WriteProcessMemory(..., (LPVOID)(&m_dwOldTrainNum), BYTE_SIZE, NULL);
   ```
4. **游戏本体反汇编**（本次会话 capstone 复核）
   ```
   0040CAED  83f832       cmp eax, 0x32      ← 0x40CAEF 是这个 0x32
   0040CAF0  7d43         jge 0x40cb35
   0040BF23  83f832       cmp eax, 0x32      ← 0x40BF25 是这个 0x32
   0040BF26  0f8d7f010000 jge 0x40c0ab
   ```

**我们的实现已经在 `src/runtime/RuntimeInjection2026.cpp:1170-1172`，是正确的**：

```cpp
patchValue = *(_DWORD *)(state + san9::injection2026::EnableTrainEnabled) != 0 ? 0x32 : 0;
writePatch(*(LPVOID *)(state + 6224), &patchValue, 1u);   // → 0x40CAEF
writePatch(*(LPVOID *)(state + 6228), &patchValue, 1u);   // → 0x40BF25
```

UI 侧也已暴露：`src/app/ui/Pages.cpp:614-618` 会为**所有 `mask != 0` 的参数**
自动生成复选框，控制 id 1013（`开启小兵培养`）的 `mask = 1` → 自动出现在设置页。

→ **崩溃 #4 与本功能无关。崩溃纯粹来自 `applyCultivate()` 里多写的 `0x6613EE`。**

> ⚠️ 注意：`Runtime2026.cpp:402` 的 `applyCultivate()` 也读同一个 `state + 148`，
> 所以删掉 `0x6613EE` 写入后，"开启小兵培养"开关**仍然通过
> `RuntimeInjection2026.cpp:1170-1172` 生效**，功能不受影响。
> `applyCultivate()` 里剩下的 `0x14FA656` 只是「培养时长(旬)」（独立实验项，见 §5.6）。

## 5b. 次要可疑点：`0x6613EE` 写的到底是什么字段

> **本节先前的表述（"`0x6613EE` = 表 A entry0 的 +0x06，不是 id 本身"）
> 已被 §5.1 的字节级证据推翻。**
> 正确结论：`0x6613EE` **就是** id dword `0x6613EC` 的**第 2 个字节**（byte index 2），
> `0x6613EC` 的值就是 `0x000003E8`。写 `0x6613EE` = 1 字节 → id 变成 `0x000103E8`。
> 详见 §5.1 / §5.2。

`Runtime2026.cpp` 的 `applyCultivate()`：

```cpp
writePatch(reinterpret_cast<LPVOID>(0x14FA656u), &duration, 1u);
writePatch(reinterpret_cast<LPVOID>(0x6613EEu), &activate, 1u);   // ★
```

补充事实：5 张静态 id 表**每一条 entry 的 byte+6 都是 `0x00`**，
所以 `0x6613EE` 更不可能是某个"独立标志位"——它没有任何"标志"的形态。
详见 §3.3。

## 5c. 「武将提拔」修改器（`武将提拔/`）的逆向结论 —— 它改的是**存档**，不是运行时内存

> 用户指示："强制小兵提拔我们有验证的，你可以逆向一下
> `G:\aiprojects\game\san9-companion\武将提拔` 下面修改器 `san9第三版.exe` 来进行。"

### 5c.1 该修改器**不是**运行时内存修改器

| 证据 | 内容 |
| --- | --- |
| 使用说明（`小兵提拔修改器使用方法.doc`，已提取文本到 `武将提拔/doc_text.txt`） | "战斗,单挑,有概率出现小兵数据.但是系统不会马上提拔… 2.指定师傅编号和激活小兵.**(编号在存档编辑器可以查看到).然后保存** 武将编号=师傅编号 / 小兵头像编号" |
| 二进制引用扫描 | `san9第三版.exe` 全文**零处**出现 `0x6613EE` / `0x6613EC` / `0x6613E8` / `0x1258EE0` / `0x14FDAD0` / `0x5B7B00` / `0x5B72F0` / `0x5B7050` / `0x5B6750` / `0x14FA656` / `0x5B6BF0` / `0x5B8330` |
| 自解压特征 | 导入 `BeginUpdateResourceW` / `UpdateResourceW` / `EndUpdateResourceW` → 自解压插件 |
| 插件框架 | 依赖 `Code_Mapping.dll`（导出 `IsPlugin` / `InitPlgFunc` / `InitPlgFuncEx` / `ReadValueEnd` / `WriteValueBegin` / `GetInfo` / `GetName` / `GetVer`） |
| RT_STRING 136-139 | 通用插件运行器错误提示（"初始配置信息失败! 此修改器不能运行." 等） |

配套的 `武将提拔/San9PKEdit146_GB/San9PKEdit.ini` 明确指向**存档文件**：
```
LastOpenFile=E:\games\San9 Tc\Koei\San9 TC\Savedata\D_Sav034.S9
```

→ **结论：它是一个存档（`.S9`）编辑器**，通过"武将编号 = 师傅编号"、
"小兵头像编号"、"训练模式"、"相性"等字段改存档，
让小兵在下一旬出现。**与运行时内存无关，因此也无法验证 `0x6613EE` 这类内存地址。**

### 5c.2 我们已经知道的正确运行时做法（对照）

原工具 SanIXPKCheat 的「开启小兵培养」= 写 `0x40CAEF` / `0x40BF25` = `0x32`/`0`
（完整证据见 **§5.8**）。我们的实现已经正确。
→ **不需要**再从「武将提拔」修改器里挖运行时地址。

### 5c.3 遗留 RE 任务（存档路线，非阻塞）

若要把「强制小兵提拔」做成比原工具更强的功能（例如指定师傅→徒弟配对、
改相性/训练模式），路线是**存档路线**，而不是内存路线：

- [ ] **T1**：用 `sanpkcheat/Cheat.s9`（已知布局：0xD0 头 + 550 × 0x88 记录）与
      `San9PKEdit.exe` 做对照，定位**存档里**存放"小兵/徒弟/师傅"的结构。
      工具：`tools/s9_dump.py`（已有）。
- [ ] **T2**：定位"武将编号 = 师傅编号"在存档里对应的字段偏移，
      以及"小兵头像编号"、"训练模式"、"相性"字段。
      线索：`San9PKEdit146_GB/readme.txt`（GBK，需转码）提到
      "武将批次(700-799)"、"武力"、"忠诚"、"口才"、"兵法"、"相性"等可编辑项。
- [ ] **T3**：把 `武将提拔/san9第三版.exe` 里的加密插件（资源 135，116188 字节）解出来，
      确认它到底改了存档的哪几个字节。
      解密例程已定位在 `0x406CE0`：
      ```
      00406D47  mov ebx, dword ptr [esp + 0xc]      ; key ptr
      00406D4B  mov ebp, dword ptr [ebx - 0xc]      ; key LENGTH
      00406D58  div ebp                              ; 取模循环
      00406D5B  movsx dx, byte ptr [edx + ebx]
      00406D60  xor word ptr [esi + ecx*2 - 2], dx
      ```
      密钥字面量在 `0x4659B4` = `:>?ZXs4Q`（8 字节），但先经过 `0x407490` 变换，
      **未解开**（本次会话尝试的评分式爆破未收敛，最好 L=24、score 0.693、无 MZ 头）。
      产物：`武将提拔/res_135.bin`（已解出）、`plugin_try.bin`（尝试产物）。
      ⚠️ 这是**支线**，不阻塞崩溃修复。
- [ ] **T4**：`md.txt`（GBK 码位表，13050 项）与游戏无关，可忽略。

### 5c.4 清理

本次会话在 `武将提拔/` 下生成的临时产物：
`res_135.bin`、`res_144.bin`、`plugin_try.bin`、`doc_text.txt`。
→ **T3 未完成前保留 `res_135.bin`**（T3 要用），其余可在 T3 完成后删除。

---

## 6. 已排除的假设（有据可查，不要重复走）

| 假设 | 结论 | 依据 |
| --- | --- | --- |
| B2/B3（`movzx` 寻址 / 多 +1 偏移） | **已修复，不是本崩溃** | 12:48 日志里两处 hook 字节均正确 |
| 禁用 `unk_42C894` 能消除 `0x5B6754` | **错误归因** | 12:48 dump 在禁用之后仍然崩 |
| `unk_42C894` 污染 `entry+4` 导致 id 找不到 | 已被禁用，不成立 | 只有 1 处写 `entry+4`；`0x5B72F0` 无检查但我们已禁 |
| 分配区大小不够（0xB000 vs 工具镜像） | **排除** | 386 个重定位目标偏移最大 `0xA8C0` < `0xB000` |
| payload 里绝对指针没重定位 | **排除** | 603/603 重定位项齐全；3 处"未重定位"是误报 |
| 我们写坏了 5 张静态 id 表 | **排除** | 0 条写入与这些区域重叠 |
| 栈上参数 / 寄存器偏移读错 | **排除** | 两个 dump 栈逐字节一致，且 `[esp+4]=0` 直接证明 lookup 返回 NULL |
| 「强制小兵培养」实现方式写错 | **排除** | 原工具/2019 参考/游戏反汇编三方一致（§5.8），我们的实现本来就是对的 |
| 武将提拔修改器能给出一个运行时激活地址 | **排除** | 它是**存档编辑器**（§5c），不是内存修改器 |

---

## 7. 下一步：按用户的指示加诊断埋点（不要再猜）

用户明确要求："**如果你没法定位，就打印日志或者 dump 之内的，我配合你来启动游戏抓取，不要猜来猜去**"
以及"**你不是一个人，你有我这个队友给你测试的**"。

现在根因已经定位到"**id 0x3E8 不在 manager 的 vector 里**"，所以埋点要回答的
问题非常具体：

### 7.1 注入后回读校验（read-back verification）

`Runtime2026::readMemory`（`Runtime2026.h:242`）已存在，直接可用。
在 `installGameplayPatches()` / `uploadInjectionPayloads()` 之后，
对**每个**写入目标回读并比对：

```
verify target=0x%08X expect=<hex> actual=<hex> OK|MISMATCH
```

重点对象：
* `0x5B8556`（5 字节）、`0x52BC4A`（54 字节）、`0x51FA13`（3 字节桩）
* `allocationBase+0x9A50`（3 字节桩，**并额外确认其后 133 字节没被原块覆盖**）
* `allocationBase+0x9110`（0xEA 字节）
* `0x14FA656`（1 字节）、`0x6613EE`（1 字节）

### 7.2 状态转储（vector 内容）

定时（或在 `modify()` 里）读取并打印：

```
manager=0x%08X  vec=0x%08X  count=%u  base=0x%08X
  entry[%u] id=0x%04X flags=0x%08X
```

manager 的取法（待定）：
* `0x52BF20` 是 `ecx`=manager 的方法，调用者 `0x52C311` / `0x4F5D78` / `0x4F6019` / `0x510262` / `0x513985` / `0x513AC1`；
* `0x5B8330` 也在 `ecx`=manager 上（调用点 `0x5B8551`、`0x5B85F8`）；
* `0x5B85F0: mov eax,[ecx+0x74]; push 0; push 0; push eax; call 0x5B8330` —— 用 `ecx`=manager 重新初始化 vector；
* 更直接：`manager` 就在 `0x5B8330` 里被写：`mov [esi+0x74], eax`（`esi`=ecx=manager）。

→ **需要在游戏里找一个稳定的 manager 全局指针**（还没找到）；
如果找不到，退而求其次：**在游戏内注入一个小的 hook 代码块**，
在 `0x5B7B00` 入口把 `ecx`（manager）和 `ebx`（id）写进一个我们分配的缓冲区，
companion 侧再回读。这是最可靠的方案，且不改游戏逻辑。

### 7.3 崩溃点守卫（可选、但最直接）

在 `0x5B72F0` 之前插一段我们自己的代码：

```
mov  ecx, <我们缓冲区地址>
mov  [ecx], <manager=esi>
mov  [ecx+4], <id=ebx>
```

这样每次 setflag 都会把 (manager, id) 落盘，崩溃后从缓冲区就能看到
**究竟哪个 id 在哪一次调用里没被找到**。

---

## 8. 尚未查清的问题（交给接手者）

1. **id 0x3E8..0x3F2 的静态表是怎么被灌进 manager 的 vector 的？**
   5 张表在 `.text` 里 0 引用，灌入路径未定位。
   → 这是根因链的最后一环。查法建议：
   * 扫 `.text` 中对 `.data` 表指针（`0x665B6C` / `0x66773C` / `0x667BF4` / `0x669F64`）的引用；
   * 查 `0x5B6CC0` 附近（往 manager vector 插 entry 的路径）的上游调用者；
   * 查 `0x5B9A30`（insert）的 43 个 xref 里哪些与 manager 有关。
2. **`0x5B8556` 的返回值语义差异**：游戏原 `call 0x5E0D20` 返回指针，
   我们换成 `call 0xA9110` 后**不返回指针**，而 `0x5B8561: mov eax,[eax+0xc]` 依赖指针。
   → 这很可能是**另一条独立的崩溃源**（甚至可能是本崩溃的真正成因：
   `0x5B8556` 在 `0x5B8330`（初始化 vector）之后紧邻执行，
   若它破坏了 `eax`/vector，后续 `setflag` 就找不到 id）。
   **优先级最高的未决项。**
3. **`0x6613EE` 写的到底是什么字段** → **已查清：是 id dword `0x6613EC` 的第 2 字节，
   写入即破坏 id 0x3E8。删除该写入即可（见 §5.5 A1）。**
4. 原工具的 `VirtualAllocEx` 大小（`this[7472] × this[7476]`，`sub_409E10.c:449`）
   是否真的等于我们的 `4096 × 11 = 0xB000`。
5. `0x5B70D0: cmp esi, 0x3E8` 是**计数上限**（遍历最多 1000 条），不是 id 上限。
6. **`0x14FA656`（培养时长）到底是什么** → 已确认是**无 RE 依据的猜测地址**（§5.6）。
   用户决定**暂时保留**并标注为待验证。若实机再崩，**第一件事就是删掉这行**。
7. **存档路线**（若要把「强制小兵提拔」做得比原工具更强）：
   见 §5c.3 的 T1–T4。

---

## 9. 工程约束（务必遵守）

**硬性要求**

* "**继续执行修复完毕之后再打包吧，注意每一项修改都要留痕到文档**"
  → 先修完再出包；**每一项修改都要写进文档**。
* "**不要提交**" → **不要 git commit**。
* "后面功能不要开发了" → 兵法威力系数等功能**不做**，只做 RE/文档。
* "**确认修复策略后再改生产代码**" → 改 `src/` 之前先跟用户确认方案。
* 注入代码**只能实机验证**；离线测试只能验字节。
* 用户是**实机测试员**：需要他启动游戏抓日志/dump。

**操作约定**

* 所有 bash 命令前缀：`cd "G:/aiprojects/game/san9-companion" &&`
* 构建前先 `taskkill //IM San9Companion.exe //F`
* 文档回写目标：`docs/HANDOVER.md` §8 变更日志、`docs/_INJECTION_FIX_NOTES.md`、`docs/SESSION_NOTE.md`
* 清理一次性脚本：`tools/_*.py`（HANDOVER §3.6 第 5 条）
* **必须更正的错误归因**：`_INJECTION_FIX_NOTES.md:505` 与 `HANDOVER.md:734`
  声称"禁用 unk_42C894 消除了 0x5B6754 崩溃"——**被 12:48 dump 证伪**。

**辅助工具**

* `/tmp/gdis.py <va> <hexlen>` —— 反汇编 `San9PK.exe` 某地址段（capstone）
  ```python
  import sys, pefile
  from capstone import Cs, CS_ARCH_X86, CS_MODE_32
  pe = pefile.PE('San9PK.exe'); base = pe.OPTIONAL_HEADER.ImageBase
  def read(va, n):
      off = pe.get_offset_from_rva(va - base)
      return pe.__data__[off:off+n]
  start = int(sys.argv[1], 16); n = int(sys.argv[2], 16)
  cs = Cs(CS_ARCH_X86, CS_MODE_32)
  for ins in cs.disasm(read(start, n), start):
      print('%08X  %-24s %s %s' % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
  ```
* dump 解析（minidump）：
  ```python
  from minidump.minidumpfile import MinidumpFile
  md = MinidumpFile.parse(path)
  er = md.exception.exception_records[0].ExceptionRecord   # ExceptionAddress / ExceptionInformation
  ctx = md.exception.exception_records[0].ThreadContext     # MINIDUMP_LOCATION_DESCRIPTOR
  f = md.get_reader().file_handle; f.seek(ctx.Rva); raw = f.read(ctx.DataSize)
  ```
* 常用 dump 路径：`C:/Users/xuwen/AppData/Local/CrashDumps/San9PK.exe.<pid>.dmp`

---

## 10. 相关文件速查

| 路径 | 作用 |
| --- | --- |
| `src/runtime/RuntimeInjection2026.cpp` | 全部注入写入；`:15-35` 禁用桩；`:322` 写桩；`:341` 写 `0x9110` 块；`:444` `uploadInjectionPayloads()`；`:589` `installGameplayPatches()`；`:1142` 槽 7616（无官禄）；**`:1170-1172` 「开启小兵培养」（正确的实现）** |
| `src/runtime/Runtime2026.cpp` | `:268` `readGameMemory()`；`:400` `applyCultivate()`（`:408` 写 `0x14FA656` 待验证；`:409` 写 `0x6613EE` 元凶）；`:862` `setParameters()` |
| `src/runtime/Runtime2026.h` | `:186-192` 写入原语；`:242` `readMemory()`（回读校验用） |
| `src/runtime/Cultivate2026.h` | `:5` `durationRounds`（默认 9，范围 3~12） |
| `src/app/ui/Pages.cpp` | `:613` 「培养时长(旬)」控件；`:614-618` 为所有 `mask != 0` 的参数自动生成复选框（含 control 1013） |
| `src/generated/StateConstants.inc` | `:4-5` 槽 6224/6228 → `0x40CAEF`/`0x40BF25`；`:352` 槽 7616 = `0x1500798` |
| `src/generated/InjectionNames2026.h` | `:6` `EnableTrainEnabled = 148`；`:142` `NoOfficialSalary = 420`；`:198` `OldTrainNum = 6232` |
| `src/generated/parameters.json` | fields[14] = control 1013「开启小兵培养」，host_offset 148，mask 1 |
| `src/model/PatchImage.h/.cpp` | `:45` `rebase()`；`AllocationSize = 0xB000`；969 个重定位槽 |
| `src/generated/write_catalog.json` | 699 条写入的权威清单（**不含** `0x6613EE` 与 `0x14FA656` —— 那两个是我们自己加的） |
| `src/generated/relocations.json` | 962 `image_dword`（字段 `va`）+ 7 `object_field`（字段 `offset`） |
| `src/generated/Symbols.inc` | `dword_XXXXXX` = `global<LPVOID>`（槽的**值**）；`unk_XXXXXX` = `pointer<char>` |
| `src/generated/companion_data.bin` | 原工具 `.data` 镜像，基址 `0x41F000`，长 `0x11000` |
| `recovery/evidence/text_listing.asm` | 原工具反汇编 |
| `recovery/evidence/decompiled/sub_409E10.c` | 原工具主注入函数（`:5153-5162` 开启小兵培养；`:5022-5024` 无官禄） |
| `recovery/evidence/decompiled/sub_414E40.c` | 原工具还原函数（`:222-223` 写回 50） |
| `recovery/evidence/disassembly/00408670.asm` | 原工具状态初始化（`:20-22` 槽 6224/6228/6232） |
| `recovery/reference-2019/San9PKHardDlg.cpp` | 2019 版参考实现（`:34`、`:4261-4265`、`:5331-5332`） |
| `sanpkcheat/SanIXPKCheat.exe` | 原工具本体（`.text` 加载基址 `0xA0000`） |
| `武将提拔/san9第三版.exe` | 第三方「小兵提拔」修改器（**存档编辑器**，非内存修改器，见 §5c） |
| `武将提拔/San9PKEdit146_GB/` | 第三方存档编辑器（`.ini` 指向 `Savedata\D_Sav034.S9`） |
| `武将提拔/res_135.bin` | 该修改器的加密插件（116188 字节），T3 解密用 |
| `docs/_INJECTION_FIX_NOTES.md` | 崩溃分析笔记（`:505` 归因需更正） |
| `docs/HANDOVER.md` | §8 变更日志（`:734` 归因需更正） |
| `build/app/Publish/San9Companion.log` | 实机日志（12:48 那次含 `allocationBase=0x099c0000 pages=11`） |

---

## 11. 地址速查表

```
游戏本体
0x400000      San9PK.exe 基址
0x401000      .text
0x45C8A0      崩溃 #3 异常点（已修）
0x4FB760      逐字节确认过的 "mov dword [ecx+0x98],eax; ret 4"
0x4FF08B      间隔 hook 点
0x4FEB90      把握概率 hook 点
0x506035      威力加倍 hook 点
0x51FA13      崩溃 #3 的补丁点
0x52BC4A      我们唯一的 0x52B000..0x52D000 补丁（命令分发）
0x52BF20      manager 方法：设置 id 与 flags
0x52C1F1      call 0x5B7B00（id=0x3E8）—— 崩溃调用点之一
0x5B6750      getflag 的实现（AV 点 0x5B6754）
0x5B6810/30   vector grow / 元素插入
0x5B6BF0      取第 i 个 entry
0x5B6CC0      往 manager 的 vector 插 entry
0x5B7050      lookup by id（未命中返回 0）
0x5B7080      第二级查找
0x5B70D0      计数上限 1000
0x5B71D0      find-or-create
0x5B72D0      getflag 兄弟版（有 null 检查）
0x5B72F0      getflag（**无 null 检查** ← 崩溃点）
0x5B7B00      setflag
0x5B8330      manager 初始化（写 [manager+0x74]）
0x5B8556      我们唯一的 0x5B0000..0x5C0000 补丁（call 0xA9110）
0x5B85F0      用 [ecx+0x74] 重置 manager
0x5B9A30      vector insert（写 id 到 entry+4）
0x5B9B00      entry 构造函数
0x5DED60      被 0x9110 块调用的函数
0x5E0BC0      全局管理器遍历
0x5E0D20      原 0x5B8556 调用的函数（返回**指针**）
0x40CAEF      「开启小兵培养」写入点 1（cmp eax,0x32 的立即数字节）← 我们已正确写入
0x40BF25      「开启小兵培养」写入点 2（同上）
0x661140      运行时为 0x6B8（游戏在 0x5FED56 加 0x380）
0x6613E8/0x665B70/0x667740/0x667C08/0x669F78   5 张 id 表
0x6613EC      表 A entry0 的 id dword = 0x000003E8
0x6613EE      ★ 我们 applyCultivate 误写的地址 = 0x6613EC 的第 2 字节 → 必须删除
0x1500798     原工具槽 7616 的目标（无官禄），与 0x14FA656 无关
0x14FA656     ★ 我们猜测的「培养时长」地址（无证据，标注待验证）

companion / 分配区
0x41F000      companion_data.bin 基址
0x420050      工具镜像基址槽
0x9A50        工具镜像内 unk_42C894 块的偏移
0x9110        工具镜像内 asc_42B0B4 块的偏移
0xB000        分配区大小（11 页）
```

---

## 12. 待办清单

### 12.1 已完成（本会话）

- [x] **崩溃 #4 根因定位**：`applyCultivate()` 写 `0x6613EE` 破坏 id `0x3E8` 字段
      （`0x6613EC` dword 的第 2 字节）→ lookup 失败 → `0x5B72F0` 无 null 检查 → AV
- [x] **dump 栈完整还原**（两个 dump 逐字节一致）
- [x] **纠正 x86 CONTEXT 偏移误解**（x86 CONTEXT 无 Eax 字段）
- [x] **全量复核所有写入 vs 5 张 id 表**：仅 `0x6613EE` 一处命中
- [x] **重定位表自洽性**：603/603 payload 重定位项齐全
- [x] **分配区布局**：320 块写入互不重叠；`0x42F038` 是唯一指向 `0xA9A50` 的槽
- [x] **确认我们没写坏 5 张静态 id 表**
- [x] **确认 `0x9110` 块与原工具一致**（原工具写同一 payload 到同一目标）
- [x] **确认 B2/B3 早已修复**，不是本崩溃
- [x] **查明「开启小兵培养」的正确实现**（§5.8）：写 `0x40CAEF`/`0x40BF25` = `0x32`/`0`，
      三方证据（原工具反编译 + 槽位初始化 + 2019 参考实现 + 游戏反汇编）闭合；
      **我们的 `RuntimeInjection2026.cpp:1170-1172` 已经是对的**
- [x] **查明 `0x14FA656` 是无依据的猜测地址**（§5.6）：`.data` BSS、PE 内零引用、
      无指针指向；**推翻了文档原先"在 .text 段、安全"的说法**
- [x] **查明「武将提拔」修改器改的是存档而非运行时内存**（§5c）
- [x] **与用户确认修复方案**（§5.5）：A1 删 `0x6613EE`；A2 留 `0x14FA656` 并标注待验证；
      A3 功能对齐原工具
- [x] 写本文档（`docs/_CRASH4_HANDOFF_2026-10-31.md`）

### 12.2 下一步（按优先级）—— ✅ **已于 2026-11-01 全部执行完毕**

> **执行记录（2026-11-01）**：下列 A1–A4 与收尾项全部完成，Debug/Release/Publish 三配置构建通过，
> `--all assets/config` 与 `--ui-smoke` 均 `exit=0`，`out/trial/` 已刷新。
> 逐项留痕见 `docs/HANDOVER.md` §4.7 / §5.1 / §8、`docs/_INJECTION_FIX_NOTES.md` §13、
> `docs/SESSION_NOTE.md`、`CHANGELOG.md`「修复」。
> **唯一剩余项是实机验证**（用户执行）。

- [x] **★ A1 删除 `0x6613EE` 写入** ✅
      `src/runtime/Runtime2026.cpp:409`
      ```cpp
      writePatch(reinterpret_cast<LPVOID>(0x6613EEu), &activate, 1u);   // ← 已删掉这一行
      ```
      同时把 `const BYTE activate = enabled ? 1 : 0;`（`:406`）一并删除（不再使用）。
      注释改成说明"本函数不再写小兵激活标志；强制小兵培养由
      `RuntimeInjection2026.cpp:1170-1172` 写 `0x40CAEF`/`0x40BF25` 实现"。
      → 现状见 `Runtime2026.cpp:400-419`（`activate` 变量已无，写入已无，
      注释含完整根因说明：`0x6613EE` = id dword `0x6613EC` 的第 2 字节）。
- [x] **★ A2 保留 `0x14FA656`，标注为待验证** ✅
      `src/runtime/Runtime2026.cpp` 保留，注释已写明：
      「⚠️ 未验证地址：PE 内零引用、无指针指向、位于 .data BSS；
        若实机再崩，先删这行」。
      同步在 `docs/DESIGN.md` 与 `docs/REQUIREMENTS_1_4.md` 标注。
- [x] **A3（已实现，无需改代码）** 「强制小兵培养」对齐原工具： ✅
      `RuntimeInjection2026.cpp:1170-1172` 写 `0x40CAEF`/`0x40BF25` = `0x32`/`0`。
      → 文档里已从"未实现"改成"已实现（已验证）"（`DESIGN.md` / `REQUIREMENTS_1_4.md`）。
- [x] **A4 回归测试**（`tests/runtime_test.cpp`） ✅
      新增 `idTableWriteGuardTest()`：
      * 断言**所有**录制到的写入中，没有任何一次覆盖 5 张 id 表区域
        （`0x6613E8` / `0x665B70` / `0x667740` / `0x667C08` / `0x669F78`，
        各 11 × `0x20` = `0x160` 字节）；
      * 特别断言 `0x6613EE` 与 id dword `0x6613EC` **不在**任何写入范围内；
      * 断言「开启小兵培养」打开时确实写到了 `0x40CAEF` 与 `0x40BF25`，
        值为 `0x32`；关闭时为 `0x00`。
      已接入测试注册处（focused 表 `--id-table-write-guard` + `--all` 流程那一行）。
      **反向验证通过**：临时加回 `0x6613EE` 写入 → 报
      `FAILED: no recorded write overlaps a static id table region` → 恢复 → 全绿。
- [x] **更正文档里的错误归因**（逐处改，别漏）： ✅
      * `docs/_INJECTION_FIX_NOTES.md` §12.4（"禁用 unk_42C894 即消除 0x5B6754 崩溃"）
        → 就地加更正注记，并新增 §13 记录崩溃 #4 的完整修复
      * `docs/HANDOVER.md` §8 对应条目 → 就地加更正注记；另新增 §4.7 纪律小节
      * `docs/DESIGN.md` / `docs/REQUIREMENTS_1_4.md`（`0x6613EE` 的错误定址）
      * `docs/DESIGN.md` / `docs/REQUIREMENTS_1_4.md`（`0x14FA656` 标注待验证）
      * `PLAN.md` 子需求 1/2、`CHANGELOG.md`「新增」与新增「修复」条目
- [x] 改完后重建（Debug → Release → Publish），跑
      `San9RuntimeTests.exe --all assets/config` 与 `--ui-smoke`，两者都要 exit=0 ✅
- [ ] 交给用户实机验证（**判据**：进游戏 → 战斗/单挑出现小兵数据 →
      「开启小兵培养」勾选后小兵能被提拔；日志里不应再出现 `0x5B6754` 崩溃）
      → **待用户执行**。追加判据：`San9Companion.log` 里不应有任何 `0x6613E` 开头的写入记录。
- [ ] 若实机仍崩：**第一步删掉 `0x14FA656` 写入**（用户已知悉此约定）
- [x] 刷新 `out/trial/`（"修复完毕之后再打包"） ✅（Publish exe + 6 个配置）
- [x] 回写 `docs/HANDOVER.md` §8 / `_INJECTION_FIX_NOTES.md` / `SESSION_NOTE.md`（每项留痕） ✅
- [x] 删除 `tools/_*.py` 一次性脚本（**当前 52 个**） ✅
- [x] 删除 `武将提拔/` 下临时产物 `res_144.bin` / `plugin_try.bin` / `doc_text.txt`
      （`res_135.bin` 保留给 T3） ✅

### 12.3 仍需查证（不阻塞修复）

- [ ] `0x5B8556` 的返回值语义差异：游戏原 `call 0x5E0D20` 返回**指针**
      （`0x5E0D20: mov eax,[eax+0xc]; ret`），而我们换成的 `0x9110` 块
      两个出口（`0xA914B` / `0xA91F1`）**都不返回指针**，
      而调用方 `0x5B8561: mov eax,[eax+0xc]` 依赖指针。
      **原工具写的是同一份 payload，所以这不是与原工具的差异**，
      但仍需确认它是否会造成另一处独立崩溃。
- [ ] 5 张 id 表 → manager vector 的灌入路径（`.text` 里 0 直接引用）尚未定位
- [ ] 原工具 `VirtualAllocEx` 大小（`this[7472] × this[7476]`）是否 = 我们的 `0xB000`
- [ ] 存档路线 T1–T4（见 §5c.3），其中 **T3 解密 `res_135.bin`** 是支线
