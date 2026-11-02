#include "Controller.h"
#include "../../runtime/AppPaths.h"
#include "../../runtime/Runtime2026.h"
namespace app
{
Controller::Controller(ProcessApi2026 api) : runtime_(api)
{
    // One-time recovered defaults, including friendships and city relations.
    configuration_.persons = runtime_.personConfig();
    configuration_.cities = runtime_.cityConfig();
    configuration_.skills = runtime_.skillConfig();
    configuration_.formations = runtime_.formationConfig();
    appendRuntimeLog("APP controller constructed with recovered defaults");
}
namespace {
// The runtime log is a byte stream; wide strings are converted here so the
// paths and officer lists stay readable in the log file.
std::string narrow(const std::wstring &value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
// Counts the entries of a per-officer array that carry a non-zero value.
template <size_t N, class Predicate>
size_t countIf(const std::array<std::uint8_t, N> &values, Predicate predicate)
{
    size_t total = 0;
    for (const auto value : values)
        if (predicate(value)) ++total;
    return total;
}
// The queued groups are a bit mask. The name of each bit makes the log
// readable without a lookup table in the reader's head.
const char *groupName(unsigned group) {
    switch (group) {
    case 1: return "Parameters";
    case 2: return "Persons";
    case 4: return "Cities";
    case 8: return "Skills";
    case 16: return "Formations";
    case 32: return "Personalization";
    case 64: return "Speed";
    default: return "All";
    }
}
}
ApplicationState Controller::state() const
{
    return {runtime_.session().phase, runtime_.session().result, runtime_.configurationState(),
            queued_ != 0 || epoch_ != runtime_.configurationEpoch(), confirmationPending_};
}
std::vector<ConfigLoadError> Controller::load(bool automatic, bool skipSelection, ComputerWorkTiming2026 timing,
                                           DeathGradient2026 deathGradient, GrowthBoost2026 growthBoost,
                                           Cultivate2026 cultivate)
{
    return load(san9::executableFile(L""), automatic, skipSelection, timing, deathGradient, growthBoost, cultivate);
}
std::vector<ConfigLoadError> Controller::load(const std::filesystem::path &directory, bool automatic, bool skipSelection,
                                           ComputerWorkTiming2026 timing, DeathGradient2026 deathGradient,
                                           GrowthBoost2026 growthBoost, Cultivate2026 cultivate)
{
    std::vector<ConfigLoadError> errors;
    configuration_.cityAutomatic = automatic;
    configuration_.skipSiegeSelection = skipSelection;
    configuration_.computerWorkTiming = timing;
    configuration_.deathGradient = deathGradient;
    configuration_.growthBoost = growthBoost;
    configuration_.cultivate = cultivate;
    queued_ |= Parameters | Cities;
    appendRuntimeLogFormat("APP load begin: folder=", narrow(directory.wstring()),
        " cityAutomatic=", automatic ? "yes" : "no",
        " skipSiegeSelection=", skipSelection ? "yes" : "no",
        " lowDeathRate=", deathGradient.lowRate, " deathThreshold=", deathGradient.threshold,
        " growthList=", growthBoost.officerList, " growthMultiplier=", growthBoost.multiplier,
        " cultivateRounds=", cultivate.durationRounds);
    auto load = [&](auto &value, ConfigurationSource &source, InputGroup group, const wchar_t *name) {
        const auto result = loadConfigFile(directory / name, value);
        if (!result.ok()) {
            errors.push_back({name, result});
            appendRuntimeLogFormat("APP load FAILED: ", name);
        }
        else {
            source = ConfigurationSource::File;
            queued_ |= group;
            appendRuntimeLogFormat("APP load ok: ", name, " group=", groupName(group));
        }
    };
    auto &sources = configuration_.sources;
    load(configuration_.parameters, sources.parameters, Parameters, L"San9PKHardPara.txt");
    load(configuration_.persons, sources.persons, Persons, L"San9PKHardPerson.txt");
    load(configuration_.cities, sources.cities, Cities, L"San9PKHardCity.txt");
    load(configuration_.skills, sources.skills, Skills, L"San9PKHardSkill.txt");
    load(configuration_.formations, sources.formations, Formations, L"San9PKHardArray.txt");
    // Personalization is optional: a missing file keeps the all-zero defaults.
    if (std::filesystem::exists(directory / L"San9PKHardCheat.txt"))
        load(configuration_.personalization, sources.personalization, Personalization, L"San9PKHardCheat.txt");
    confirmationPending_ = true;
    stageConfiguration();
    return errors;
}
bool Controller::commitParameters(const san9::ParameterConfig &value, bool skipSelection, ComputerWorkTiming2026 timing,
                                  DeathGradient2026 deathGradient, GrowthBoost2026 growthBoost, Cultivate2026 cultivate)
{
    if (value.validate().error != san9::ParameterError::none) return false;
    configuration_.parameters = value;
    configuration_.skipSiegeSelection = skipSelection;
    configuration_.computerWorkTiming = timing;
    configuration_.deathGradient = deathGradient;
    configuration_.growthBoost = growthBoost;
    configuration_.cultivate = cultivate;
    configuration_.sources.parameters = confirmedSource(configuration_.sources.parameters);
    queued_ |= Parameters;
    confirmationPending_ = true;
    appendRuntimeLogFormat("APP commit parameters: skipSiegeSelection=", skipSelection ? "yes" : "no",
        " computerWorkTiming.improveRewards=", timing.improveRewards ? "yes" : "no",
        " computerWorkTiming.skipAttackStartDelay=", timing.skipAttackStartDelay ? "yes" : "no",
        " lowDeathRate=", deathGradient.lowRate, " deathThreshold=", deathGradient.threshold,
        " growthOfficers=", narrow(growthBoost.officerList), " growthMultiplier=", growthBoost.multiplier,
        " cultivateRounds=", cultivate.durationRounds);
    stageConfiguration();
    return true;
}
bool Controller::commitPersons(const san9::PersonConfig &value)
{
    if (!value.valid()) return false;
    configuration_.persons = value;
    configuration_.sources.persons = confirmedSource(configuration_.sources.persons);
    queued_ |= Persons;
    confirmationPending_ = true;
    appendRuntimeLogFormat("APP commit persons: officers=", value.levels.size(),
        " friendships=", value.friendships.size());
    stageConfiguration();
    return true;
}
bool Controller::commitPersonalization(const OfficerPersonalization2026 &value)
{
    configuration_.personalization = value;
    configuration_.sources.personalization = confirmedSource(configuration_.sources.personalization);
    queued_ |= Personalization;
    confirmationPending_ = true;
    appendRuntimeLogFormat("APP commit personalization: activationBoost=", countIf(value.activationBoost, [](unsigned char v) { return v != 0; }),
        " criticalHit=", countIf(value.criticalHit, [](unsigned char v) { return v != 0; }),
        " powerPercent=", countIf(value.powerPercent, [](unsigned char v) { return v != 0; }));
    stageConfiguration();
    return true;
}
bool Controller::commitCities(const san9::CityConfig &value, bool automatic)
{
    if (!value.valid()) return false;
    configuration_.cities = value;
    configuration_.cityAutomatic = automatic;
    configuration_.sources.cities = confirmedSource(configuration_.sources.cities);
    queued_ |= Cities;
    // Relations and attack days are written by the parameter application phase.
    confirmationPending_ = true;
    appendRuntimeLogFormat("APP commit cities: rows=", value.limits.size(),
        " cityAutomatic=", automatic ? "yes" : "no");
    stageConfiguration();
    return true;
}
bool Controller::commitSkills(const san9::SkillConfig &value)
{
    if (!value.valid()) return false;
    configuration_.skills = value;
    configuration_.sources.skills = confirmedSource(configuration_.sources.skills);
    queued_ |= Skills;
    appendRuntimeLogFormat("APP commit skills: entries=", value.entries.size());
    stageConfiguration();
    return true;
}
bool Controller::commitFormations(const san9::FormationConfig &value)
{
    if (!value.valid()) return false;
    configuration_.formations = value;
    configuration_.sources.formations = confirmedSource(configuration_.sources.formations);
    queued_ |= Formations;
    appendRuntimeLogFormat("APP commit formations: entries=", value.entries.size());
    stageConfiguration();
    return true;
}
void Controller::observeLifecycle()
{
    if (epoch_ == runtime_.configurationEpoch()) return;
    epoch_ = runtime_.configurationEpoch();
    queued_ |= All;
}
bool Controller::commitPresentationSpeed(unsigned multiplier)
{
    if (!presentation2026::validMultiplier(multiplier)) return false;
    configuration_.presentationSpeed=multiplier;
    queued_|=Speed;
    stageConfiguration();
    if (runtime_.hasGameSession()) modify(ModifyReason2026::PresentationSpeed);
    return true;
}
void Controller::stageConfiguration()
{
    observeLifecycle();
    const auto phase = runtime_.session().phase;
    if (!queued_ || (phase != SessionPhase2026::Idle && phase != SessionPhase2026::Active)) return;
    const auto groups = queued_;
    auto flags = runtime_.configurationState();
    const auto &sources = configuration_.sources;
    if (queued_ & Speed) runtime_.setPresentationSpeed(configuration_.presentationSpeed);
    if (queued_ & Parameters)
    {
        runtime_.setParameters(configuration_.parameters);
        runtime_.setSkipDurabilitySiegeSelection(configuration_.skipSiegeSelection);
        runtime_.setComputerWorkTiming(configuration_.computerWorkTiming);
        runtime_.setDeathGradient(configuration_.deathGradient);
        runtime_.setGrowthBoost(configuration_.growthBoost);
        runtime_.setCultivate(configuration_.cultivate);
    }
    if (queued_ & Personalization) runtime_.setPersonalization(configuration_.personalization);
    if (queued_ & Persons) runtime_.setPersonConfig(configuration_.persons);
    if (queued_ & Cities)
    {
        runtime_.setCityConfig(configuration_.cities);
        flags.cityAutomatic = configuration_.cityAutomatic;
        flags.cityPending = sources.cities != ConfigurationSource::Defaults || flags.cityAutomatic;
    }
    if (queued_ & Skills)
    {
        runtime_.setSkillConfig(configuration_.skills);
        flags.skillLoaded = fileBacked(sources.skills);
        flags.skillPending = sources.skills != ConfigurationSource::Defaults;
    }
    if (queued_ & Formations)
    {
        runtime_.setFormationConfig(configuration_.formations);
        flags.formationLoaded = fileBacked(sources.formations);
        flags.formationPending = sources.formations != ConfigurationSource::Defaults;
    }
    runtime_.setConfigurationState(flags);
    queued_ = 0;
    appendRuntimeLogFormat("APP stage: groups=", groupName(groups),
        " cityAutomatic=", flags.cityAutomatic ? "yes" : "no",
        " cityPending=", flags.cityPending ? "yes" : "no",
        " skillLoaded=", flags.skillLoaded ? "yes" : "no",
        " skillPending=", flags.skillPending ? "yes" : "no",
        " formationLoaded=", flags.formationLoaded ? "yes" : "no",
        " formationPending=", flags.formationPending ? "yes" : "no");
}
void Controller::modify(ModifyReason2026 reason)
{
    stageConfiguration();
    const bool connected = runtime_.hasGameSession();
    if (reason == ModifyReason2026::TimerTick && confirmationPending_ && connected)
        reason = ModifyReason2026::ParametersConfirmed;
    runtime_.modify(reason);
    // Inputs written by the parameter phase and confirmed during Applying
    // belong to the next operation.
    if (!(queued_ & (Parameters | Persons | Cities)) && runtime_.session().result == OperationResult2026::Committed && runtime_.hasGameSession() &&
        (!connected || reason == ModifyReason2026::ParametersConfirmed)) confirmationPending_ = false;
    observeLifecycle();
}
void Controller::poll() { modify(ModifyReason2026::TimerTick); }
void Controller::applyConfirmedConfiguration()
{
    appendRuntimeLog("APP apply confirmed configuration");
    confirmationPending_ = true;
    modify(ModifyReason2026::ParametersConfirmed);
}
bool Controller::gameMessage(UINT message, WPARAM action, LPARAM argument)
{
    if (!runtime_.isGameMessage(message)) return false;
    stageConfiguration();
    const bool result = runtime_.handleGameMessage(message, action, argument);
    observeLifecycle();
    return result;
}
bool Controller::undo()
{
    appendRuntimeLog("APP undo requested");
    const bool result = runtime_.undo();
    observeLifecycle();
    appendRuntimeLogFormat("APP undo result=", result ? "ok" : "failed");
    return result;
}
bool Controller::retryRecovery()
{
    appendRuntimeLog("APP retry recovery requested");
    const bool result = runtime_.retryRollback();
    observeLifecycle();
    appendRuntimeLogFormat("APP retry recovery result=", result ? "ok" : "failed");
    if (result) stageConfiguration();
    return result;
}
bool Controller::toggleForce(unsigned index)
{
    const bool result = runtime_.togglePlayerForce(index);
    observeLifecycle();
    appendRuntimeLogFormat("APP toggle force index=", index, " result=", result ? "on" : "off");
    return result;
}
bool Controller::personSnapshot(san9::PersonSnapshot &value)
{
    const bool result = runtime_.readPersonSnapshot(value);
    observeLifecycle();
    return result;
}
bool Controller::citySnapshot(CitySnapshot2026 &value)
{
    const bool result = runtime_.readCitySnapshot(value);
    observeLifecycle();
    if (result && (queued_ & Cities)) { value.config = configuration_.cities; value.invalidRow.reset(); }
    return result;
}
bool Controller::skillSnapshot(SkillSnapshot2026 &value)
{
    const bool result = runtime_.readSkillSnapshot(value);
    observeLifecycle();
    if (result && (queued_ & Skills)) { value.config = configuration_.skills; value.invalidRow.reset(); }
    return result;
}
bool Controller::formationSnapshot(FormationSnapshot2026 &value)
{
    const bool result = runtime_.readFormationSnapshot(value);
    observeLifecycle();
    if (result && (queued_ & Formations)) { value.config = configuration_.formations; value.invalidRow.reset(); }
    return result;
}
bool Controller::forces(std::vector<PlayerForce2026> &value)
{
    const bool result = runtime_.readPlayerForces(value);
    observeLifecycle();
    return result;
}
} // namespace app
