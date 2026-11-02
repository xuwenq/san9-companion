#pragma once
#include "../model/PackedConfigIO.h"
#include <array>
#include <cstdint>
#include <istream>
#include <ostream>

// Per-officer personalization (SanIXPKCheat 武将个性化): one value set per officer.
// Skill systems 0..8: 步兵/骑兵/弓骑/弩兵/水军/攻城/智略/谋略/策略.
// Skill table ids 0..40 follow 兵法表(0x14FDAD0) order.
struct OfficerPersonalization2026 {
    static constexpr size_t Count = 800;
    static constexpr size_t Systems = 9;
    static constexpr size_t Skills = 41;
    // 发动间隔(天)上限。UI 输入框与模型读取共用同一上限，避免手改配置文件写入
    // 界面无法表达的值后又在保存时被改写。游戏原版间隔最大为 15。
    static constexpr std::uint32_t MaxInterval = 30;

    // 游戏原版每个兵法的发动间隔(天)；索引即兵法表技能 id。
    static constexpr std::array<std::uint8_t, Skills> defaultInterval = {
        9, 9, 9,  9, 9, 9,  9, 9, 9,  9, 9, 9,   // 步兵/骑兵/弓骑/弩兵
        0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,        // 水军/攻城/智略
        6, 0, 9, 15,  6, 6, 15, 15,              // 谋略/策略
        9, 9, 9, 9, 9, 9, 9, 9,  0, 0};          // 特殊/设施
    // 游戏原版熟练度上限（1000）。
    static constexpr std::uint16_t DefaultCap = 1000;
    // 原版「精进」熟练度上限。指示器以此为界区分五角星与圈。
    static constexpr std::uint16_t RefinedCap = 1500;

    // 兵法系 -> 兵法表技能 id（0..40）。原版武将配置对话框按这 9 系分行，
    // 每系 3~4 个兵法，0xFF 表示该系没有更多兵法。
    // 步兵:奋战奋斗奋迅 / 骑兵:突破突进突击 / 弓骑:骑射奔射飞射 /
    // 弩兵:齐射连射连弩 / 水军:艨艟楼船斗舰 / 攻城:井栏冲车发石象兵 /
    // 智略:营造石兵破陷教论 / 谋略:混乱陷阱攻心幻术 / 策略:辱骂鼓舞治疗妖术
    static constexpr std::uint8_t NoSkill = 0xFF;
    static constexpr std::array<std::array<std::uint8_t, 4>, Systems> systemSkills = {{
        {{0, 1, 2, NoSkill}},
        {{3, 4, 5, NoSkill}},
        {{6, 7, 8, NoSkill}},
        {{9, 10, 11, NoSkill}},
        {{12, 13, 14, NoSkill}},
        {{15, 16, 17, 18}},
        {{19, 20, 21, 22}},
        {{23, 24, 25, 26}},
        {{27, 28, 29, 30}},
    }};

    // 熟练度上限 (per system): 0 = 不修改(用游戏原值 1000), 1..65535 = 该系上限; 默认 1000.
    std::array<std::array<std::uint16_t, Systems>, Count> proficiencyCap{};
    // 发动间隔 (天, per skill): 0 = 无间隔, 1..255 = 最小间隔; 默认取游戏原值.
    std::array<std::array<std::uint8_t, Skills>, Count> interval{};
    // 提升兵法发动机会把握概率: 0 = 不修改, 1 = 强制 100%.
    std::array<std::uint8_t, Count> activationBoost{};
    // 兵法威力系数 (percent): 0 = 不修改, 1..250 (100 = 1.0x).
    std::array<std::uint8_t, Count> powerPercent{};
    // 兵法威力一定加倍(出动画): 0 = 不修改, 1 = 必定会心.
    std::array<std::uint8_t, Count> criticalHit{};

    OfficerPersonalization2026() {
        interval.fill(defaultInterval);
        for (auto &row : proficiencyCap)
            row.fill(DefaultCap);
    }

    san9::PackedReadResult read(std::istream &input);
    bool write(std::ostream &output) const;
};
