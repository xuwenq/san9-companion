#pragma once
#include <cstdint>

// 蛮族（异族）阵型：让玩家也能选用蛮族/山越阵型。
//
// 背景（全部已用游戏 exe 反汇编核对过，2026-11-07）：
//
// 游戏里有一个「阵型属性查表函数」，**真正的入口是 0x4FB560**：
//
//   0x4FB560  mov  ecx,[esp+4]        ; 形参：阵型编号
//   0x4FB564  or   eax,0xFFFFFFFF     ; 越界 -> -1
//   0x4FB567  cmp  ecx,0x1B
//   0x4FB56A  ja   0x4FB5D8
//   0x4FB56C  jmp  dword ptr [ecx*4+0x4FB5DC]   ; 28 项跳转表
//
// 表里每一项返回该阵型的**属性 id**（每一项都是一段 "mov eax,imm32; ret"）：
//
//   case  0 -> 0x39C   case  1 -> 0x39F   case  2 -> 0x396   case  3 -> 0x399
//   case  4 -> 0x3A2   case  5 -> 0x3A5   case  6 -> 0x393   case  7 -> 0x3A8
//   case  8 -> 0x3BD   case  9 -> 0x3B7   case 10 -> 0x3BA   case 11 -> 0x3C0
//   case 12 -> 0x3C3   case 13 -> 0x3C3   case 14 -> 0x3C3   case 15..19 -> -1
//   case 20 -> 0x3AB   case 21 -> 0x3AE   case 22 -> 0x3B1   case 23 -> 0x3B4
//   case 24..26 -> -1  case 27 -> 0x3C3
//
// 这个函数只有一个调用点：0x4FBEB6（call 0x4FB560），它在一个「按阵型属性
// 生成 3 段动画/特效」的函数里，用形参 `ebx` 作为阵型编号。
//
// 12/13/14 号阵型是**异族专用**（属性 0x3C3 只属于异族），所以普通势力选不了。
//
// 第三方修改器 build/res/Shape.exe 的做法（已逐字节核对）：把 **0x4FB564**
// 起的 8 字节换成
//
//   B8 CC 03 00 00 90 90 90    ; mov eax,0x3CC ; nop ; nop ; nop
//
// 注意它改的是 0x4FB564（函数体第二句），**没有**动 0x4FB560 的
// "mov ecx,[esp+4]"。所以替换后整个函数的语义变成：
//
//   mov ecx,[esp+4]
//   mov eax,0x3CC
//   nop ; nop ; nop
//   ... 原本的跳转表代码还在，但已经执行不到 ...
//
// 由于 "mov eax,imm32" 一定写 eax，**返回值与原来的 eax 无关**，所以
// Shape.exe 的做法对调用点是安全的（唯一调用点 0x4FBEB6 之后立刻
// "mov ebp,eax" 使用返回值）。
//
// ⚠️ 但把 0x4FB567..0x4FB5D3 这 0x6D 字节全部 nop 掉并不必要，而且我们
// 不想把游戏里 0x4FB560 之后的一整段代码抹掉（那段代码 0x4FB5DC 之后
// 还有别的数据）。因此我们的做法更保守：
//
//   * 只把 12/13/14 三项的跳转表项改成指向我们自己的一段代码，
//     该代码返回普通阵型属性 0x3CC；
//   * 其余 25 项的跳转表项保持原样，普通阵型的属性完全不受影响。
//
// 跳转表在 0x4FB5DC，28 项 × 4 字节 = 112 字节，只改其中 3 项。
//
// 注意：Shape.exe 还改了两个别的地方（0x4701E5 的 push 0x0B->0x15、
// 0x470205 的 push 0x17->0x14）。那两处是 0x4701E0 / 0x470200 这对
// 「构筑阵型」函数的形参（形参是「阵型编号」和「上限」），与属性查表无关，
// 我们不复制。
namespace san9 {

// 「阵型属性查表函数」的入口（含取形参那一句）。
inline constexpr std::uint32_t FormationAttributeLookup = 0x4FB560u;
// 该函数使用的 28 项跳转表。
inline constexpr std::uint32_t FormationAttributeJumpTable = 0x4FB5DCu;
// 跳转表的项数（游戏里 cmp ecx,0x1B -> 0x1B+1 = 28）。
inline constexpr unsigned FormationAttributeCount = 28u;
// 异族阵型（属性 0x3C3）在跳转表里的下标。
inline constexpr unsigned BarbarianFormationCases[] = {12, 13, 14};
// 普通阵型使用的属性 id（Shape.exe 用的就是这个值）。
inline constexpr std::uint32_t NormalFormationAttribute = 0x3CCu;
// 异族阵型原本的属性 id。
inline constexpr std::uint32_t BarbarianFormationAttribute = 0x3C3u;
// 跳转表里 12/13/14 三项**原本**就指向游戏自己的 0x4FB5D3 段代码
//（"mov eax,0x3C3; ret"）。关闭本功能时必须把这个原始值写回去，
//否则游戏里会留下一个指向伴侣进程内存的野指针。
inline constexpr std::uint32_t OriginalBarbarianFormationEntry = 0x4FB5D3u;

}  // namespace san9
