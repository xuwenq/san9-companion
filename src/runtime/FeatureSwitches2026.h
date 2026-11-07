#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// Per-feature switches.
//
// Purpose: every feature we added or changed must be testable **on its own**.
// During a live test the other features are switched off so that a crash or a
// wrong result can only come from the feature under test.
//
// The switches live in a dedicated file next to the executable:
//
//     San9Features.ini
//         [Features]
//         ProficiencyCap=1
//         Interval=0
//         ...
//
// A switch that is missing from the file falls back to its default, which is
// "on" for everything that existed before the switches were introduced.  The
// file is read once at start-up; the runtime never writes it.
namespace san9 {

// Every switch we own.  The order is the order of the ini file.
enum class Feature2026 : std::size_t
{
    // --- features added or changed on the dev branch ---
    deathGradient = 0,     // 阵亡概率渐变（需求 1）
    growthBoost,           // 指定武将能力成长倍率（需求 3）
    cultivateTrainNum,     // 小兵培养：强制提拔（写 0x40CAEF / 0x40BF25）
    cultivateBasicAbility, // 小兵培养：基础能力系数反转（codeTrainBasicAbility）
    cultivateDuration,     // 小兵培养：培养时长（尚未实现，见 applyCultivate）
    proficiencyCap,        // 熟练度上限（per-officer × per-system）
    proficiencyMark,       // 游戏内 ☆/◎/○ 指示图标
    activationBoost,       // 把握概率（hook 0x4FEC59）
    criticalHit,           // 威力一定加倍（hook 0x50603B）
    interval,              // 兵法发动间隔（hook 0x4FF08B）
    barbarianFormation,    // 蛮族阵型（异族阵型）
    computerWorkTiming,    // 电脑奖励时机 / 攻击起始延迟
    skipSiegeSelection,    // 跳过耐久攻城选择
    count
};

inline constexpr std::size_t FeatureCount2026 = static_cast<std::size_t>(Feature2026::count);

// Default state.  Everything that was already shipped stays on, so an empty
// ini behaves exactly like the build before the switches existed.
inline constexpr std::array<bool, FeatureCount2026> FeatureDefault2026 = {
    true,  // deathGradient
    true,  // growthBoost
    true,  // cultivateTrainNum
    true,  // cultivateBasicAbility
    true,  // cultivateDuration
    true,  // proficiencyCap
    true,  // proficiencyMark
    true,  // activationBoost
    true,  // criticalHit
    true,  // interval
    true,  // barbarianFormation
    true,  // computerWorkTiming
    true,  // skipSiegeSelection
};

struct FeatureSwitches2026
{
    std::array<bool, FeatureCount2026> enabled = FeatureDefault2026;

    bool operator[](Feature2026 feature) const
    {
        return enabled[static_cast<std::size_t>(feature)];
    }
    bool& operator[](Feature2026 feature) { return enabled[static_cast<std::size_t>(feature)]; }

    // A compact "on/off" summary for the runtime log, e.g. "1 0 1 1 ...".
    // The order matches the ini file, so a log line can be read next to it.
    std::string summary() const;
};

// The ini keys, in file order.
const std::array<const wchar_t*, FeatureCount2026>& FeatureKeys2026();

// Read San9Features.ini next to the executable.  Missing keys keep their
// default; a malformed value is treated as "off" so a typo cannot silently
// enable a feature.
FeatureSwitches2026 loadFeatureSwitches(const std::filesystem::path& file);

}  // namespace san9
