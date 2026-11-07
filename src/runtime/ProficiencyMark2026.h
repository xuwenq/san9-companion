#pragma once
#include "OfficerPersonalization2026.h"
#include <cstddef>
#include <cstdint>

// 熟练度指示图标：修改器界面与游戏内注入共用同一套规则，
// 保证「武将配置」页看到的图标和游戏武将列表里画出来的图标完全一致。
//
// 字形一律用游戏字体（CP950/Big5 繁体）里的两字节字符，游戏里已经验证可用：
//   空心五角星 ☆ U+2606 -> a1 b8
//   同心圆     ◎ U+25CE -> a1 b7
//   空心圆圈   ○ U+25CB -> a1 b3
// 不用位图：游戏 exe 的 .rsrc 里只有光标，2019 版工具也没有位图资源。
namespace san9 {

// 五档：unconfigured 表示「用户没有对该武将该系做任何配置」，
// 游戏里这种情况回落原生标记（精通位 -> ☆，精进 -> ◎）。
enum class ProficiencyMark2026 : std::uint8_t
{
    unconfigured = 0,  // 未配置 -> 游戏内回落原生
    none = 1,         // 明确配置，但不该显示图标
    star = 2,         // 空心五角星 ☆
    refined = 3,      // 同心圆   ◎
    interval = 4,     // 空心圆圈 ○
};

// 游戏内注入用的一格前缀（4 字节，含前后两个空格）。
// 空格是为了让图标和后面的数字之间留出间距。
inline constexpr std::uint32_t ProficiencyMarkPrefix2026[5] = {
    0x00000000u,  // unconfigured：游戏内改用原生标记
    0x20202020u,  // none：4 个空格（不显示图标）
    0x2020B8A1u,  // "  " + a1 b8 = ☆
    0x2020B7A1u,  // "  " + a1 b7 = ◎
    0x2020B3A1u,  // "  " + a1 b3 = ○
};

// 修改器界面用的宽字符。unconfigured 与 none 都显示为空。
inline const wchar_t* ProficiencyMarkText2026(ProficiencyMark2026 mark)
{
    switch (mark)
    {
    case ProficiencyMark2026::star: return L"☆";
    case ProficiencyMark2026::refined: return L"◎";
    case ProficiencyMark2026::interval: return L"○";
    default: return L"";
    }
}

// 该系是否有兵法的发动间隔被缩短。
inline bool ProficiencyIntervalShortened(
    const OfficerPersonalization2026& personalization,
    std::size_t officer,
    std::size_t system)
{
    for (auto skill : OfficerPersonalization2026::systemSkills[system])
    {
        if (skill == OfficerPersonalization2026::NoSkill) break;
        if (personalization.interval[officer][skill] <
            OfficerPersonalization2026::defaultInterval[skill])
        {
            return true;
        }
    }
    return false;
}

// 四档语义（用户原始需求）：
//   上限为 1000（默认）            -> 无图标
//   1000 < 上限 <= 1500            -> 空心五角星
//   上限 > 1500                    -> 同心圆
//   该系有兵法的发动间隔被缩短       -> 空心圆圈（优先级最高）
//
// 「未配置」= 该系既没有改过熟练度上限，也没有改过任何兵法的发动间隔。
// 这种情况下游戏里回落原生标记；修改器里仍然显示为空。
inline ProficiencyMark2026 ProficiencyMarkFor(
    const OfficerPersonalization2026& personalization,
    std::size_t officer,
    std::size_t system)
{
    const bool shortened = ProficiencyIntervalShortened(personalization, officer, system);
    const auto cap = personalization.proficiencyCap[officer][system];
    const bool capChanged = cap != 0 && cap != OfficerPersonalization2026::DefaultCap;

    if (!shortened && !capChanged) return ProficiencyMark2026::unconfigured;
    if (shortened) return ProficiencyMark2026::interval;
    if (cap > OfficerPersonalization2026::RefinedCap) return ProficiencyMark2026::refined;
    return ProficiencyMark2026::star;
}

}  // namespace san9
