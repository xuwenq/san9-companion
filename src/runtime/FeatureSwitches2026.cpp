#include "FeatureSwitches2026.h"
#include "AppPaths.h"
#include <windows.h>
#include <filesystem>
#include <string>

namespace san9 {

const std::array<const wchar_t*, FeatureCount2026>& FeatureKeys2026()
{
    static const std::array<const wchar_t*, FeatureCount2026> keys = {
        L"DeathGradient",
        L"GrowthBoost",
        L"CultivateTrainNum",
        L"CultivateBasicAbility",
        L"CultivateDuration",
        L"ProficiencyCap",
        L"ProficiencyMark",
        L"ActivationBoost",
        L"CriticalHit",
        L"Interval",
        L"BarbarianFormation",
        L"ComputerWorkTiming",
        L"SkipSiegeSelection",
    };
    return keys;
}

std::string FeatureSwitches2026::summary() const
{
    std::string text;
    for (std::size_t i = 0; i < enabled.size(); ++i)
    {
        if (i) text += ' ';
        text += enabled[i] ? '1' : '0';
    }
    return text;
}

FeatureSwitches2026 loadFeatureSwitches(const std::filesystem::path& file)
{
    FeatureSwitches2026 switches;
    if (!std::filesystem::exists(file)) return switches;
    for (std::size_t i = 0; i < FeatureCount2026; ++i)
    {
        wchar_t buffer[16]{};
        const DWORD length = GetPrivateProfileStringW(L"Features", FeatureKeys2026()[i], L"",
                                                     buffer, static_cast<DWORD>(std::size(buffer)),
                                                     file.c_str());
        if (!length) continue;
        // Only an exact 0/1 (with optional surrounding blanks) is accepted.
        // Anything else counts as "off": a typo must never enable a feature.
        const std::wstring value(buffer, length);
        bool on = false;
        if (value == L"1") on = true;
        else if (value == L"0") on = false;
        else {
            std::wstring trimmed = value;
            while (!trimmed.empty() && (trimmed.front() == L' ' || trimmed.front() == L'\t'))
                trimmed.erase(trimmed.begin());
            while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t'))
                trimmed.pop_back();
            on = (trimmed == L"1");
        }
        switches.enabled[i] = on;
    }
    return switches;
}

}  // namespace san9
