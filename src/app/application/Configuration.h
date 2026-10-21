#pragma once
#include "../../model/ParameterConfig.h"
#include "../../model/PersonConfig.h"
#include "../../model/CityConfig.h"
#include "../../model/SkillConfig.h"
#include "../../model/FormationConfig.h"
#include "../../runtime/ComputerWorkTiming2026.h"
#include "../../runtime/DeathGradient2026.h"
#include "../../runtime/GrowthBoost2026.h"
#include "../../runtime/Cultivate2026.h"

namespace app
{
// Confirming a file-backed configuration preserves its continuous application policy.
enum class ConfigurationSource { Defaults, File, Confirmed, ConfirmedFile };
inline bool fileBacked(ConfigurationSource source)
{
    return source == ConfigurationSource::File || source == ConfigurationSource::ConfirmedFile;
}
inline ConfigurationSource confirmedSource(ConfigurationSource source)
{
    return fileBacked(source) ? ConfigurationSource::ConfirmedFile : ConfigurationSource::Confirmed;
}
struct ConfigurationSources
{
    ConfigurationSource parameters = ConfigurationSource::Defaults;
    ConfigurationSource persons = ConfigurationSource::Defaults;
    ConfigurationSource cities = ConfigurationSource::Defaults;
    ConfigurationSource skills = ConfigurationSource::Defaults;
    ConfigurationSource formations = ConfigurationSource::Defaults;
};
struct Configuration
{
    san9::ParameterConfig parameters;
    san9::PersonConfig persons;
    san9::CityConfig cities;
    san9::SkillConfig skills;
    san9::FormationConfig formations;
    ConfigurationSources sources;
    bool cityAutomatic = false;
    bool skipSiegeSelection = true;
    ComputerWorkTiming2026 computerWorkTiming;
    DeathGradient2026 deathGradient;
    GrowthBoost2026 growthBoost;
    Cultivate2026 cultivate;
    unsigned presentationSpeed = 1;
};
} // namespace app
