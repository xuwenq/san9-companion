# 需求 4 地址映射（来自 SanIXSpy.dll 补丁表逆向）

## 已确认
- 熟练度上限：`0x406A74`（cmp eax,1000）、`0x406A7F`（mov eax,1000）—— 已实现（改成 65535）
- 兵法威力系数：`0x43EF44`（`imul esi, [esp+0x8c]`，威力=原威力×系数）
- 兵法发动率：`0x4FEB90`（`mov esi,0xa` + `call kakuritsu`，发动概率）

## 待确认（兵法发动间隔候选）
- `0x4FF08B`（`idiv ecx`）、`0x4FF115`（`call 0x4fb760`）
- 或 0x50 段：`0x5006C7`、`0x50079F`、`0x500854`、`0x502443`、`0x5032DC`

## SanIXSpy.dll 补丁表结构
- 表在 .data 段（文件 0x1C00 起），每条 0x1C 字节
- [0]=游戏地址1 [1]=原始字节 [2]=flag [3]=补丁代码指针 [6]=游戏地址2
- 补丁代码在 .text/.rdata/.data（VA 0x10001xxx）

## 说明
SanIXPKCheat 的作弊是"代码补丁"（把游戏地址处代码替换成补丁代码），补丁代码读取共享配置(0x100043xx)里的 per-officer 作弊值。要在伴侣里等价实现，需要：per-officer 配置 + 对应注入代码。
