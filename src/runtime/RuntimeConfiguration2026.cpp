#include "Runtime2026.h"
#include <algorithm>

namespace {
namespace layout = san9::layout2026;
namespace names = san9::injection2026;
constexpr DWORD CitySkillBase = 0x124DBAA;
constexpr DWORD SkillIntervalBase = 0x14FDB4C;
constexpr DWORD FormationGarrisonBase = 0x1500BD4;
constexpr DWORD InitialFormationRanges = 0x42DB0C;
}

// Keep each recovered write's width and order: the journal owns partial writes,
// instruction-cache flushing and rollback. Transfer failures throw immediately.
void Runtime2026::writeGameValue(DWORD target, DWORD value, SIZE_T size) {
    writeMemory(field<HANDLE>(layout::ProcessHandle), target, &value, size, nullptr);
}

void Runtime2026::applyGameConfiguration(ModifyReason2026 reason) {
    applyCityConfiguration(reason);
    applySkillConfiguration();
    applyFormationConfiguration(reason);
    if (field<DWORD>(names::UpdateZoneCityEnabled))
        updateZone(field<DWORD>(names::UpdateZoneCityEnabled));
    appendRuntimeLog("CONFIG game configuration applied");
}

// Evidence: 0x409E10, former LABEL_2113..LABEL_2144.
void Runtime2026::applyCityConfiguration(ModifyReason2026 reason) {
    if (!field<DWORD>(layout::CityAutomatic)) {
        if (reason != ModifyReason2026::TimerTick) return;
        for (size_t index = 0; index < san9::CityConfig::Count; ++index) {
            const auto row = layout::CityLimits + index * san9::CityConfig::RecordSize;
            field<BYTE>(row + 1) = field<BYTE>(names::InitialCityMaxSoldier + index);
            field<BYTE>(row + 3) = field<BYTE>(names::InitialCityMaxPeopleHeart + index);
        }
        writeMemory(field<HANDLE>(layout::ProcessHandle), field<DWORD>(names::CityMaxSoldierAddress),
                    state_.data() + names::InitialCityMaxSoldier, san9::CityConfig::Count, nullptr);
        writeMemory(field<HANDLE>(layout::ProcessHandle), field<DWORD>(names::CityMaxPeopleHeartAddress),
                    state_.data() + names::InitialCityMaxPeopleHeart, san9::CityConfig::Count, nullptr);
        appendRuntimeLogFormat("CONFIG city: initial values written for ", san9::CityConfig::Count, " cities");
        return;
    }

    field<DWORD>(layout::CityPending) = 0;
    for (DWORD index = 0; index < san9::CityConfig::Count; ++index) {
        const auto row = layout::CityLimits + index * san9::CityConfig::RecordSize;
        const DWORD target = CitySkillBase + index * san9::CityConfig::GameRecordSize;
        const BYTE flags = field<BYTE>(row);
        writeGameValue(field<DWORD>(names::CityMaxSoldierAddress) + index,
                       (std::min<DWORD>)(DWORD(field<BYTE>(row + 1)), 50u), 1);
        writeGameValue(field<DWORD>(names::CityMaxPeopleHeartAddress) + index,
                       (std::min<DWORD>)(DWORD(field<BYTE>(row + 3)), 10u), 1);

        // Replace the selected skill in the three-byte mask, preserving other flags.
        DWORD skills = 0;
        readMemory(field<HANDLE>(layout::ProcessHandle), target - 1, &skills, 3, nullptr);
        skills &= ~0x7F800Eu;
        const unsigned skill = flags & 0xF;
        if (skill < 11) skills |= 1u << (skill < 3 ? skill + 1 : skill + 12);
        writeGameValue(target - 1, skills, 1);
        writeGameValue(target, skills >> 8, 1);
        writeGameValue(target + 1, skills >> 16, 1);
        writeGameValue(target + 2, 8 * (flags & 0x10), 1);
        writeGameValue(target + 3, (flags & 0x20) != 0, 1);

        // Merchant residency is a reversible setting in bit 0, not a one-way OR.
        DWORD merchant = skills;
        readMemory(field<HANDLE>(layout::ProcessHandle), target + 398, &merchant, 1, nullptr);
        writeGameValue(target + 398, (merchant & ~1u) | ((flags >> 7) & 1u), 1);
        writeGameValue(target + 386, 100 * (std::min<DWORD>)(DWORD(field<BYTE>(row + 4)), 15u));
        writeGameValue(target + 390, 100 * (std::min<DWORD>)(DWORD(field<BYTE>(row + 5)), 15u));
        writeGameValue(target + 374, 100 * (std::min<DWORD>)(DWORD(field<BYTE>(row + 2)), 20u));
    }
    appendRuntimeLogFormat("CONFIG city: ", san9::CityConfig::Count, " city records written");
}

// Evidence: 0x409E10, former LABEL_2144..LABEL_2166.
void Runtime2026::applySkillConfiguration() {
    const BYTE flags = field<BYTE>(layout::SkillFlags);
    if (!(flags & 3)) {
        appendRuntimeLog("CONFIG skill: no pending change");
        return;
    }
    field<BYTE>(layout::SkillFlags) = flags & 0xFD;
    for (DWORD index = 0; index < san9::SkillConfig::Count; ++index) {
        const auto row = layout::SkillEntries + index * 4;
        const DWORD target = SkillIntervalBase + index * 140;
        writeGameValue(target - 24, (std::min<DWORD>)(DWORD(field<BYTE>(row)), 100u));
        writeGameValue(target, (std::min<DWORD>)(DWORD(field<BYTE>(row + 1)), 30u));
        writeGameValue(target - 36, (std::min<DWORD>)(DWORD(field<WORD>(row + 2)), 2500u));
        appendRuntimeLogFormat("CONFIG skill index=", index, " power=", DWORD(field<BYTE>(row)),
            " interval=", DWORD(field<BYTE>(row + 1)), " proficiency=", DWORD(field<WORD>(row + 2)));
    }
}

// Evidence: 0x409E10, former LABEL_2166..LABEL_2178.
void Runtime2026::applyFormationConfiguration(ModifyReason2026 reason) {
    const BYTE flags = field<BYTE>(layout::FormationFlags);
    if (!(flags & 3)) {
        if (reason != ModifyReason2026::TimerTick) return;
        const BYTE* ranges = pointer<BYTE>(InitialFormationRanges);
        for (size_t index = 0; index < san9::FormationConfig::Count; ++index) {
            auto& rangeAndView = field<WORD>(layout::FormationEntries + index * 8 + 4);
            rangeAndView = (rangeAndView & 0xFFF0u) | (ranges[index] & 0xFu);
        }
        writeMemory(field<HANDLE>(layout::ProcessHandle), field<DWORD>(names::FormationAttackRangeAddress),
                    ranges, san9::FormationConfig::Count, nullptr);
        appendRuntimeLogFormat("CONFIG formation: initial attack ranges written for ", san9::FormationConfig::Count, " formations");
        return;
    }

    field<BYTE>(layout::FormationFlags) = flags & 0xFD;
    for (DWORD index = 0; index < san9::FormationConfig::Count; ++index) {
        const auto row = layout::FormationEntries + index * san9::FormationConfig::RecordSize;
        const DWORD target = FormationGarrisonBase + index * san9::FormationConfig::GameRecordSize;
        const DWORD abilities = field<DWORD>(row);
        const WORD rangeAndView = field<WORD>(row + 4);
        writeGameValue(target - 4, abilities & 0x3F, 1);
        writeGameValue(target, (abilities >> 6) & 0x3F, 1);
        writeGameValue(target + 4, (abilities >> 12) & 0x3F, 1);
        writeGameValue(target + 8, (abilities >> 18) & 0x3F, 1);
        writeGameValue(target - 8, (abilities >> 24) & 0x3F, 1);
        writeGameValue(field<DWORD>(names::FormationAttackRangeAddress) + index, rangeAndView & 0xF, 1);
        writeGameValue(target + 12, (rangeAndView >> 4) & 0xF, 1);
        writeGameValue(target + 16, (rangeAndView >> 8) & 0xF, 1);
        writeGameValue(target + 20, field<WORD>(row + 6), 2);
    }
    appendRuntimeLogFormat("CONFIG formation: ", san9::FormationConfig::Count, " formation records written");
}
