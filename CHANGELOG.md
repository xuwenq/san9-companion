# Changelog

本文件记录 San9 Companion 的所有改动。格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

## [未发布]

### 新增

- 需求 1（高能武将阵亡概率渐变）：新增 `DeathGradient2026` 配置结构（低能阵亡率 `lowRate` 默认 40、高能归零阈值 `threshold` 默认 100），并接入 `Configuration`/`Controller`/`Runtime2026`。
- 需求 1：在 `Runtime2026::applyDeathGradient` 实现「高能武将可阵亡」的 MAX(四维) 渐变——开启后把原版 `min(30, 80-单项属性)` 改为 `min(lowRate, threshold-MAX)`（阈值 80~120，无编码溢出）。
- 需求 1：在【扩展配置】页新增两个数值输入框（低能阵亡率(%) 0~100、高能归零阈值 80~120），经 INI（`Extensions/LowDeathRate`、`Extensions/DeathThreshold`）持久化。
- 需求 2（基础能力系数反转）：把 `codeTrainBasicAbility` 注入代码里的两处 `sub byte ptr [edx], al` 改为 `add`，使「小兵基础能力系数」语义从“数字越大越弱”变为“数字越大越强”。

### 待办

- 需求 2、3、4 尚未实现。
