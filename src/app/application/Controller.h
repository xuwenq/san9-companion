#pragma once
#include "../../runtime/Runtime2026.h"
#include "ConfigFiles.h"
#include "Configuration.h"

namespace app
{
struct ConfigLoadError
{
    std::wstring file;
    ConfigFileResult result;
};
struct ApplicationState
{
    SessionPhase2026 phase;
    OperationResult2026 result;
    ConfigurationState2026 runtimeConfiguration;
    bool inputsQueued;
    bool confirmationPending;
};
struct ControllerTestAccess;
class Controller final
{
  public:
    explicit Controller(ProcessApi2026 api = {});
    const Configuration &configuration() const { return configuration_; }
    RuntimeStatus2026 status() const { return runtime_.status(); }
    bool hasGameSession() const { return runtime_.hasGameSession(); }
    ApplicationState state() const;
    void setCallbackWindow(HWND endpoint) { runtime_.setCallbackWindow(endpoint); }
    std::vector<ConfigLoadError> load(bool cityAutomatic, bool skipSelection, ComputerWorkTiming2026 timing,
                                      DeathGradient2026 deathGradient = {}, GrowthBoost2026 growthBoost = {},
                                      Cultivate2026 cultivate = {});
    std::vector<ConfigLoadError> load(const std::filesystem::path &directory, bool cityAutomatic, bool skipSelection,
                                    ComputerWorkTiming2026 timing, DeathGradient2026 deathGradient = {},
                                    GrowthBoost2026 growthBoost = {}, Cultivate2026 cultivate = {});
    bool commitParameters(const san9::ParameterConfig &value, bool skipSelection, ComputerWorkTiming2026 timing,
                          DeathGradient2026 deathGradient, GrowthBoost2026 growthBoost, Cultivate2026 cultivate);
    bool commitPersons(const san9::PersonConfig &value);
    bool commitCities(const san9::CityConfig &value, bool automatic);
    bool commitSkills(const san9::SkillConfig &value);
    bool commitFormations(const san9::FormationConfig &value);
    bool commitPresentationSpeed(unsigned multiplier);
    void poll();
    void applyConfirmedConfiguration();
    bool gameMessage(UINT message, WPARAM action, LPARAM argument);
    bool undo();
    bool retryRecovery();
    bool toggleForce(unsigned index);
    bool personSnapshot(san9::PersonSnapshot &value);
    bool citySnapshot(CitySnapshot2026 &value);
    bool skillSnapshot(SkillSnapshot2026 &value);
    bool formationSnapshot(FormationSnapshot2026 &value);
    bool forces(std::vector<PlayerForce2026> &value);
  private:
    friend struct ControllerTestAccess;
    enum InputGroup : unsigned { Parameters = 1, Persons = 2, Cities = 4, Skills = 8, Formations = 16, Speed = 32, All = 63 };
    Runtime2026 runtime_;
    Configuration configuration_;
    unsigned queued_ = 0;
    bool confirmationPending_ = false;
    std::uint64_t epoch_ = 0;
    void observeLifecycle();
    void stageConfiguration();
    void modify(ModifyReason2026 reason);
};
} // namespace app
