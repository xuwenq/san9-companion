#include "Pages.h"
#include "../../../assets/ui/resource_ids.h"
#include "FormBinding.h"
#include "ConfigFileText.h"
#include <algorithm>
#include <iterator>
namespace app
{
namespace
{
const wchar_t *statuses[] = {L"君主",   L"都督",   L"军师", L"一般", L"俘虏", L"在野",
                             L"未发现", L"未登场", L"死亡", L"NPC",  L"无效"};
template <class Config, class Snapshot> class RowsPage : public ui::Dialog
{
    friend bool app::smokePages(Controller &);

  public:
    RowsPage(Controller &controller, const Config &shared, int list, int save, const wchar_t *name,
             const wchar_t *title)
        : app(controller), shared(shared), listId(list), saveId(save), filename(name), title(title)
    {
    }

  protected:
    Controller &app;
    const Config &shared;
    Config draft;
    Snapshot snapshot;
    size_t index = 0;
    int listId, saveId;
    const wchar_t *filename;
    const wchar_t *title;
    virtual bool readSnapshot() = 0;
    virtual std::vector<ui::NumberBinding> fields(Config &config) = 0;
    virtual void extraLoad()
    {
    }
    virtual bool extraSave(Config &)
    {
        return true;
    }
    virtual bool committed() = 0;
    void initialize() override
    {
        bool connected = app.hasGameSession();
        bool read = connected && readSnapshot();
        draft = shared;
        if (connected && !read)
            message(std::wstring(L"读游戏") + title + L"信息失败，仍可编辑已保存的配置。");
        if (read)
        {
            draft = snapshot.config;
            if (snapshot.invalidRow)
                message(std::wstring(title) + L" " + std::to_wstring(*snapshot.invalidRow) +
                        L" 的游戏数据超出可编辑范围，保留原配置。");
        }
        std::vector<std::wstring> labels;
        labels.reserve(Config::Count);
        for (size_t i = 0; i < Config::Count; ++i)
        {
            auto label = (i < 10 ? L"0" : L"") + std::to_wstring(i) + L" ";
            label += read ? ui::gameText(snapshot.names[i].data(), snapshot.names[i].size()) : title;
            labels.push_back(label);
        }
        appendItems(listId, labels);
        extraInitialize();
        load();
    }
    virtual void extraInitialize()
    {
    }
    void load()
    {
        select(listId, int(index));
        ui::loadNumbers(*this, fields(draft));
        extraLoad();
    }
    bool save()
    {
        auto candidate = draft;
        if (!ui::saveNumbers(*this, fields(candidate)) || !extraSave(candidate))
            return false;
        if (!candidate.valid())
        {
            message(L"配置超出允许范围。");
            return false;
        }
        draft = candidate;
        return true;
    }
    void exportFile()
    {
        if (!save())
            return;
        auto path = file(true, filename);
        if (!path)
            return;
        const auto result = saveConfigFile(*path, draft);
        if (!result.ok()) message(configFileText(result));
    }
    void command(int id, ui::Event event) override
    {
        if (id == listId && event == ui::Event::Selection)
        {
            int chosen = selected(listId);
            if (chosen < 0 || chosen >= int(Config::Count) || !save())
            {
                select(listId, int(index));
                return;
            }
            index = size_t(chosen);
            load();
        }
        else if (id == saveId && event == ui::Event::Click)
            exportFile();
    }
    void accept() override
    {
        if (save() && committed())
            finish(IDOK);
    }
};
class SkillPage final : public RowsPage<san9::SkillConfig, SkillSnapshot2026>
{
    friend bool app::smokePages(Controller &);

  public:
    explicit SkillPage(Controller &c)
        : RowsPage(c, c.configuration().skills, IDC_KILLER_LIST, IDC_SAVE_KILLER_CONFIG, L"San9PKHardSkill.txt", L"兵法")
    {
    }

  private:
    bool readSnapshot() override
    {
        return app.skillSnapshot(snapshot);
    }
    std::vector<ui::NumberBinding> fields(san9::SkillConfig &c) override
    {
        auto &r = c.entries[index];
        return {ui::bind(IDC_KILLER_INTERVAL, 0, 30, r.interval),
                ui::bind(IDC_KILLER_LEARN_PROFICIENCY, 0, 2500, r.proficiency),
                ui::bind(IDC_KILLER_POWER, 0, 100, r.power)};
    }
    bool committed() override
    {
        return app.commitSkills(draft);
    }
};
class FormationPage final : public RowsPage<san9::FormationConfig, FormationSnapshot2026>
{
    friend bool app::smokePages(Controller &);

  public:
    explicit FormationPage(Controller &c)
        : RowsPage(c, c.configuration().formations, IDC_ARRAY_LIST, IDC_SAVE_ARRAY_CONFIG, L"San9PKHardArray.txt", L"阵型")
    {
    }

  private:
    bool readSnapshot() override
    {
        return app.formationSnapshot(snapshot);
    }
    std::vector<ui::NumberBinding> fields(san9::FormationConfig &c) override
    {
        auto &r = c.entries[index];
        return {ui::bind(IDC_ARRAY_ATTACK_RANGE, 1, 5, r.range),
                ui::bind(IDC_ARRAY_DEFENCE, 5, 30, r.defence),
                ui::bind(IDC_ARRAY_EXPENSE, 0, 10000, r.expense),
                ui::bind(IDC_ARRAY_SPEED, 5, 50, r.speed),
                ui::bind(IDC_ARRAY_TO_DURABILITY, 5, 60, r.durability),
                ui::bind(IDC_ARRAY_TO_GARRISON, 5, 50, r.garrison),
                ui::bind(IDC_ARRAY_TO_TROOP, 5, 50, r.troop),
                ui::bind(IDC_ARRAY_VIEW_ANGLE, 1, 8, r.angle),
                ui::bind(IDC_ARRAY_VIEW_RADIUS, 0, 10, r.radius)};
    }
    bool committed() override
    {
        return app.commitFormations(draft);
    }
};
class CityPage final : public RowsPage<san9::CityConfig, CitySnapshot2026>
{
    friend bool app::smokePages(Controller &);

  public:
    explicit CityPage(Controller &c)
        : RowsPage(c, c.configuration().cities, IDC_CITY_LIST, IDC_SAVE_CITY_CONFIG, L"San9PKHardCity.txt", L"都市")
    {
    }

  private:
    bool readSnapshot() override
    {
        return app.citySnapshot(snapshot);
    }
    std::vector<ui::NumberBinding> fields(san9::CityConfig &c) override
    {
        auto &r = c.limits[index];
        std::vector<ui::NumberBinding> result{
            ui::bind(IDC_MAX_DURABILITY, 6, 20, r.durability),
            ui::bind(IDC_MAX_HARVEST, 4, 15, r.harvest),
            ui::bind(IDC_MAX_INCOME, 4, 15, r.income),
            ui::bind(IDC_MAX_PEOPLE_HEART, 4, 10, r.peopleHeart),
            ui::bind(IDC_MAX_SOLDIER, 10, 50, r.soldiers),
            ui::bind(IDC_ATTACK_CITY_MAX_DAY, 20, 100, c.attackDays[index])};
        for (size_t i = 0; i < 8; ++i)
            result.push_back(ui::bind(IDC_CITY_RELATION1 + int(i), 0, 50, c.relations[index][i]));
        return result;
    }
    void extraInitialize() override
    {
        const wchar_t *skills[] = {L"齐射", L"连射", L"连弩", L"混乱", L"陷阱", L"攻心",
                                   L"幻术", L"辱骂", L"鼓舞", L"治疗", L"妖术", L"无"};
        appendItems(IDC_CITY_KILLER, std::vector<std::wstring>(std::begin(skills), std::end(skills)), true);
        check(IDC_CITY_CONFIG_AUTO, app.configuration().cityAutomatic);
    }
    void extraLoad() override
    {
        auto flags = draft.limits[index].flags;
        select(IDC_CITY_KILLER, flags & 15, true);
        check(IDC_CITY_STONE, (flags & 0x10) != 0);
        check(IDC_CITY_SHOOT_FAR, (flags & 0x20) != 0);
        check(IDC_CITY_TRADER, (flags & 0x80) != 0);
    }
    bool extraSave(san9::CityConfig &c) override
    {
        int skill = selected(IDC_CITY_KILLER, true);
        if (skill < 0 || skill > 11)
        {
            focus(IDC_CITY_KILLER);
            message(L"请选择有效都市兵法。");
            return false;
        }
        c.limits[index].flags =
            std::uint8_t(skill | (checked(IDC_CITY_STONE) ? 0x10 : 0) |
                         (checked(IDC_CITY_SHOOT_FAR) ? 0x20 : 0) | (checked(IDC_CITY_TRADER) ? 0x80 : 0));
        return true;
    }
    bool committed() override
    {
        bool automatic = checked(IDC_CITY_CONFIG_AUTO);
        if (!savePreference(L"City", L"AutoApply", automatic))
        {
            message(L"无法写入程序目录下的 San9PKHardSettings.ini，设置尚未确认。");
            return false;
        }
        return app.commitCities(draft, automatic);
    }
    void command(int id, ui::Event event) override
    {
        if (id == IDC_LOAD_CITY_CONFIG && event == ui::Event::Click)
        {
            auto path = file(false, filename);
            if (!path)
                return;
            auto candidate = draft;
            const auto result = loadConfigFile(*path, candidate);
            if (!result.ok())
            {
                message(configFileText(result));
                return;
            }
            draft = candidate;
            index = 0;
            load();
            return;
        }
        RowsPage::command(id, event);
    }
};
class PersonPage final : public ui::Dialog
{
    friend bool app::smokePages(Controller &);

  public:
    explicit PersonPage(Controller &c) : app(c)
    {
    }

  private:
    Controller &app;
    san9::PersonConfig draft;
    san9::PersonSnapshot snapshot;
    size_t index = 0, friendship = 0;
    bool loading = false;
    const int groups[9] = {IDC_FEN_ZHAN, IDC_TU_PO,    IDC_QI_SHE,   IDC_YIQI_SHE, IDC_MENG_CHONG,
                           IDC_JING_LAN, IDC_YING_ZAO, IDC_HUN_LUAN, IDC_RU_MA};
    const int abilityIds[4] = {IDC_COMMAND, IDC_STRENGTH, IDC_INTELLIGENCE, IDC_POLITICS};
    const int goodIds[9] = {IDC_GOOD_BU_BING, IDC_GOOD_QI_BING,  IDC_GOOD_GONG_QI,
                            IDC_GOOD_NU_BING, IDC_GOOD_SHUI_JUN, IDC_GOOD_GONG_CHENG,
                            IDC_GOOD_ZHI_LUE, IDC_GOOD_MOU_LUE,  IDC_GOOD_CE_LUE};
    const int proficiencyIds[9] = {IDC_SLD_BU_BING, IDC_SLD_QI_BING,  IDC_SLD_GONG_QI,
                                   IDC_SLD_NU_BING, IDC_SLD_SHUI_JUN, IDC_SLD_GONG_CHENG,
                                   IDC_SLD_ZHI_LUE, IDC_SLD_MOU_LUE,  IDC_SLD_CE_LUE};
    std::wstring nameQuery, statusQuery;
    std::array<int, 4> abilityQuery{{-1, -1, -1, -1}};
    void initialize() override
    {
        if (app.hasGameSession() && !app.personSnapshot(snapshot))
            message(L"读游戏武将信息失败，仍可按编号编辑配置。");
        draft = app.configuration().persons;
        std::vector<std::wstring> labels;
        labels.reserve(san9::PersonConfig::Count);
        for (size_t i = 0; i < san9::PersonConfig::Count; ++i)
        {
            auto label = std::to_wstring(i) + L" ";
            if (i < snapshot.size())
                label += ui::gameText(snapshot.at(i).name.data(), snapshot.at(i).name.size());
            else
                label += L"武将";
            labels.push_back(label);
        }
        appendItems(IDC_PERSON_LIST, labels);
        readOnly(IDC_FRIENDSHIP_INDEX);
        for (auto id : goodIds)
            enable(id, false);
        scrollRange(IDC_FRIENDSHIP_NAVIGATOR, int(san9::PersonConfig::FriendshipCount) - 1);
        loadPerson();
        loadFriendship();
        number(IDC_MAX_EXPERIENCE, draft.maxExperience);
    }
    void loadPerson()
    {
        loading = true;
        select(IDC_PERSON_LIST, int(index));
        for (size_t i = 0; i < 9; ++i)
            radio(groups[i], (draft.levels[index] >> (2 * i)) & 3);
        if (index < snapshot.size())
        {
            const auto &row = snapshot.at(index);
            text(IDC_PERSON_NAME, ui::ansiText(row.name.data()));
            text(IDC_PERSON_STATUS, statuses[row.status]);
            for (size_t i = 0; i < 4; ++i)
                number(abilityIds[i], row.abilities[i]);
            for (size_t i = 0; i < 9; ++i)
            {
                check(goodIds[i], (row.good & (1u << i)) != 0);
                number(proficiencyIds[i], row.proficiency[i]);
            }
            unsigned mask = 1;
            for (int id = IDC_FEN_ZHAN; id <= IDC_YAO_SHU; ++id)
            {
                if (id == IDC_BU_JING_JIN || id == IDC_QI_JING_JIN || id == IDC_GONG_JING_JIN ||
                    id == IDC_NU_JING_JIN || id == IDC_SHUI_JING_JIN)
                    ++id;
                flat(id, (row.learned & mask) != 0);
                mask <<= 1;
            }
        }
        loading = false;
    }
    bool savePerson()
    {
        std::uint32_t value = 0;
        for (size_t i = 0; i < 9; ++i)
        {
            int level = radio(groups[i]);
            if (level < 0 || level > 3)
            {
                focus(groups[i]);
                message(L"请选择每类兵法的等级。");
                return false;
            }
            value |= std::uint32_t(level) << (2 * i);
        }
        draft.levels[index] = value;
        return true;
    }
    void loadFriendship()
    {
        auto &row = draft.friendships[friendship];
        number(IDC_FRIENDSHIP_INDEX, int(friendship));
        number(IDC_PERSON_INDEX_1, row[0]);
        number(IDC_PERSON_INDEX_2, row[1]);
        number(IDC_FRIENDSHIP, row[2]);
        scrollPosition(IDC_FRIENDSHIP_NAVIGATOR, int(friendship));
    }
    bool saveFriendship()
    {
        auto row = draft.friendships[friendship];
        if (!ui::saveNumbers(*this, {ui::bind(IDC_PERSON_INDEX_1, 0, 799, row[0]),
                                     ui::bind(IDC_PERSON_INDEX_2, 0, 799, row[1]),
                                     ui::bind(IDC_FRIENDSHIP, 0, 60, row[2])}))
            return false;
        draft.friendships[friendship] = row;
        return true;
    }
    bool save()
    {
        int maximum = 0;
        if (!savePerson() || !saveFriendship() || !number(IDC_MAX_EXPERIENCE, 1200, 3000, maximum))
            return false;
        draft.maxExperience = std::uint16_t(maximum);
        return draft.valid();
    }
    void command(int id, ui::Event event) override
    {
        if (id == ID_EXIT && event == ui::Event::Click)
        {
            if (save() && app.commitPersons(draft))
            {
                finish(IDOK);
            }
        }
        else if (id == IDC_SAVE_PERSON_CONFIG && event == ui::Event::Click)
        {
            if (!save())
                return;
            auto path = file(true, L"San9PKHardPerson.txt");
            if (path)
            {
                const auto result = saveConfigFile(*path, draft);
                if (!result.ok()) message(configFileText(result));
            }
        }
        else if (id == IDC_PERSON_LIST && event == ui::Event::Selection)
        {
            int chosen = selected(id);
            if (chosen < 0 || chosen >= int(san9::PersonConfig::Count) || !savePerson())
            {
                select(id, int(index));
                return;
            }
            index = size_t(chosen);
            loadPerson();
        }
        else if (event == ui::Event::Changed && !loading)
        {
            if (id == IDC_PERSON_NAME)
                nameQuery.clear();
            if (id == IDC_PERSON_STATUS)
                statusQuery.clear();
            for (size_t i = 0; i < 4; ++i)
                if (id == abilityIds[i])
                    abilityQuery[i] = -1;
        }
        else if (event == ui::Event::Focus && (id == IDC_PERSON_INDEX_1 || id == IDC_PERSON_INDEX_2))
        {
            auto chosen = number(id);
            if (chosen && *chosen >= 0 && *chosen < int(san9::PersonConfig::Count) && savePerson())
            {
                index = size_t(*chosen);
                loadPerson();
            }
        }
    }
    void accept() override
    {
        if (!snapshot.size() || !savePerson())
            return;
        int id = focused();
        std::optional<size_t> next;
        if (id == IDC_PERSON_NAME || id == IDC_PERSON_STATUS)
        {
            auto &query = id == IDC_PERSON_NAME ? nameQuery : statusQuery;
            if (query.empty())
                query = text(id);
            if (query.empty())
                return;
            next = snapshot.next(index, [&](const san9::PersonInfo &row) {
                auto value = id == IDC_PERSON_NAME ? ui::ansiText(row.name.data())
                                                   : std::wstring(statuses[row.status]);
                return value.find(query) != std::wstring::npos;
            });
        }
        else
            for (size_t i = 0; i < 4; ++i)
                if (id == abilityIds[i])
                {
                    if (abilityQuery[i] < 0)
                    {
                        int value = 0;
                        if (!number(id, 0, 255, value))
                            return;
                        abilityQuery[i] = value;
                    }
                    next = snapshot.nextAbility(index, i, abilityQuery[i]);
                    break;
                }
        if (next)
        {
            index = *next;
            loadPerson();
        }
    }
    void scroll(int id, int code, int position) override
    {
        if (id != IDC_FRIENDSHIP_NAVIGATOR)
            return;
        int chosen = int(friendship);
        if (code == SB_LINELEFT || code == SB_PAGELEFT)
            --chosen;
        else if (code == SB_LINERIGHT || code == SB_PAGERIGHT)
            ++chosen;
        else if (code == SB_THUMBTRACK)
            chosen = position;
        else
            return;
        chosen = (std::max)(0, (std::min)(int(san9::PersonConfig::FriendshipCount) - 1, chosen));
        if (saveFriendship())
        {
            friendship = size_t(chosen);
            loadFriendship();
            focus(IDC_FRIENDSHIP);
        }
    }
};
class SettingsPage final : public ui::Dialog
{
    friend bool app::smokePages(Controller &);

  public:
    explicit SettingsPage(Controller &c) : app(c), draft(c.configuration().parameters)
    {
    }

  private:
    Controller &app;
    san9::ParameterConfig draft;
    void speedText()
    {
        text(IDC_PRESENTATION_SPEED,L"演出 " + std::to_wstring(app.configuration().presentationSpeed) + L"×");
    }
    void initialize() override
    {
        speedText();
        tooltip(IDC_PRESENTATION_SPEED, L"选择进行阶段的地图与普通动画倍率。暂停、指令阶段及独立事件演出保持原速。");
        const auto &timing = app.configuration().computerWorkTiming;
        settingsPages({
            {IDC_SKIP_DURABILITY_SIEGE_SELECTION, L"跳过耐久驱动的攻城阵型优先选择", app.configuration().skipSiegeSelection},
            {IDC_IMPROVE_COMPUTER_REWARD_TIMING, L"优化电脑褒奖时间", timing.improveRewards},
            {IDC_SKIP_COMPUTER_ATTACK_START_DELAY, L"电脑出征规划取消开局等待", timing.skipAttackStartDelay}});
        tooltip(IDC_SKIP_DURABILITY_SIEGE_SELECTION,
                L"跳过原版按城池耐久优先选择攻城阵型的判断，不影响因路径障碍触发的选择。");
        tooltip(IDC_NPC_ALLIE_ATTACK, L"使多个电脑势力更容易集中进攻玩家的城池。");
        tooltip(IDC_IMPROVE_COMPUTER_REWARD_TIMING,
                L"开启：电脑褒奖开局等待初级60天、高级30天，并取消周期窗口；关闭：恢复初级90天、高级45天及30天周期窗口。");
        tooltip(IDC_SKIP_COMPUTER_ATTACK_START_DELAY,
                L"取消电脑出征规划的开局等待；关闭时初级等待120天、高级60天。实际出征仍须满足兵力、目标等条件。");
        const auto &deathGradient = app.configuration().deathGradient;
        settingsNumber(IDC_LOW_DEATH_RATE, L"低能阵亡率(%)", 0, 100, int(deathGradient.lowRate));
        settingsNumber(IDC_DEATH_THRESHOLD, L"高能归零阈值", 80, 250, int(deathGradient.threshold));
        for (const auto &field : san9::ParameterConfig::fields())
        {
            if (field.mask)
                check(field.controlId, draft.get(field.controlId) != 0);
            else
                number(field.controlId, draft.get(field.controlId));
        }
    }
    bool save()
    {
        settingsPage(0);
        auto candidate = draft;
        for (const auto &field : san9::ParameterConfig::fields())
        {
            int value = 0;
            if (field.mask)
                value = checked(field.controlId) ? 1 : 0;
            else if (!number(field.controlId, field.minimum, field.maximum, value))
                return false;
            candidate.set(field.controlId, value);
        }
        auto valid = candidate.validate();
        if (valid.error != san9::ParameterError::none)
        {
            focus(valid.controlId);
            message(L"参数范围或分隔点顺序错误。");
            return false;
        }
        draft = candidate;
        return true;
    }
    void accept() override
    {
        if (!save())
            return;
        bool skip = checked(IDC_SKIP_DURABILITY_SIEGE_SELECTION);
        const ComputerWorkTiming2026 timing{checked(IDC_IMPROVE_COMPUTER_REWARD_TIMING),
                                            checked(IDC_SKIP_COMPUTER_ATTACK_START_DELAY)};
        int lowRate = 0, threshold = 0;
        if (!number(IDC_LOW_DEATH_RATE, 0, 100, lowRate) ||
            !number(IDC_DEATH_THRESHOLD, 80, 250, threshold))
        {
            settingsPage(1);
            return;
        }
        const DeathGradient2026 deathGradient{unsigned(lowRate), unsigned(threshold)};
        if (!savePreference(L"Extensions", L"SkipDurabilitySiegeSelection", skip) ||
            !savePreference(L"Extensions", L"ImproveComputerRewardTiming", timing.improveRewards) ||
            !savePreference(L"Extensions", L"SkipComputerAttackStartDelay", timing.skipAttackStartDelay) ||
            !savePreferenceNumber(L"Extensions", L"LowDeathRate", unsigned(lowRate)) ||
            !savePreferenceNumber(L"Extensions", L"DeathThreshold", unsigned(threshold)))
        {
            settingsPage(1);
            message(L"无法写入 San9PKHardSettings.ini，设置尚未确认。");
            return;
        }
        if (app.commitParameters(draft, skip, timing, deathGradient)) finish(IDOK);
    }
    void command(int id, ui::Event event) override
    {
        if (event != ui::Event::Click)
            return;
        switch (id)
        {
        case IDC_PERSON_CONFIG: {
            PersonPage page(app);
            page.show(IDD_PERSON_CONFIG, this);
            break;
        }
        case IDC_CITY_CONFIG: {
            CityPage page(app);
            page.show(IDD_CITY_CONFIG, this);
            break;
        }
        case IDC_KILLER_CONFIG: {
            SkillPage page(app);
            page.show(IDD_KILLER_CONFIG, this);
            break;
        }
        case IDC_ARRAY_CONFIG: {
            FormationPage page(app);
            page.show(IDD_ARRAY_CONFIG, this);
            break;
        }
        case IDC_SAVE_CONFIG: {
            if (!save())
                return;
            auto path = file(true, L"San9PKHardPara.txt");
            if (path)
            {
                const auto result = saveConfigFile(*path, draft);
                if (!result.ok()) message(configFileText(result));
            }
            break;
        }
        case IDC_PRESENTATION_SPEED: {
            const auto rate=app.configuration().presentationSpeed;
            const auto choice=popup(IDC_PRESENTATION_SPEED,{{33001,L"1×（原速）",rate==1},
                {33002,L"2×",rate==2},{33004,L"4×",rate==4}});
            if (choice==33001 || choice==33002 || choice==33004)
            {
                const unsigned next=choice-33000;
                if (!savePreferenceNumber(L"Presentation",L"Speed",next))
                    message(L"保存演出倍率失败。");
                else
                    app.commitPresentationSpeed(next);
                speedText();
            }
            break;
        }
        case IDC_SELECT_PLAYER_FORCE: {
            std::vector<PlayerForce2026> forces;
            if (!app.forces(forces))
                return;
            std::vector<ui::MenuItem> menu;
            for (auto &force : forces)
                menu.push_back({IDM_PLAYER_FORCE_START + force.index,
                                ui::gameText(force.leaderName.data(), force.leaderName.size()),
                                force.player});
            auto chosen = popup(id, menu);
            if (chosen >= IDM_PLAYER_FORCE_START && chosen <= IDM_PLAYER_FORCE_END &&
                !app.toggleForce(chosen - IDM_PLAYER_FORCE_START))
                message(L"切换玩家势力失败，请查看主窗口的恢复状态。");
            break;
        }
        }
    }
};
} // namespace
void showSettings(Controller &controller, ui::Dialog &owner)
{
    SettingsPage page(controller);
    if (page.show(IDD_PARA_SETTING, &owner) == IDOK)
        controller.applyConfirmedConfiguration();
}
bool smokePages(Controller &c)
{
    SettingsPage settings(c);
    PersonPage person(c);
    CityPage city(c);
    SkillPage skill(c);
    FormationPage formation(c);
    const auto parametersBefore = c.configuration().parameters;
    const auto personsBefore = c.configuration().persons.encodeLevels();
    const auto citiesBefore = c.configuration().cities.encodeLimits();
    const auto skillsBefore = c.configuration().skills.encode();
    const auto formationsBefore = c.configuration().formations.encode();
    if (!settings.createHidden(IDD_PARA_SETTING))
        return false;
    settings.settingsPage(1);
    if (settings.checked(IDC_IMPROVE_COMPUTER_REWARD_TIMING) != c.configuration().computerWorkTiming.improveRewards ||
        settings.checked(IDC_SKIP_COMPUTER_ATTACK_START_DELAY) != c.configuration().computerWorkTiming.skipAttackStartDelay)
        return false;
    settings.check(IDC_SKIP_DURABILITY_SIEGE_SELECTION, !c.configuration().skipSiegeSelection);
    settings.check(IDC_IMPROVE_COMPUTER_REWARD_TIMING, !c.configuration().computerWorkTiming.improveRewards);
    settings.check(IDC_SKIP_COMPUTER_ATTACK_START_DELAY, !c.configuration().computerWorkTiming.skipAttackStartDelay);
    settings.settingsPage(0);
    if (!settings.save())
        return false;
    for (const auto &field : san9::ParameterConfig::fields())
        if (settings.draft.get(field.controlId) != parametersBefore.get(field.controlId))
            return false;
    settings.destroy();
    if (!c.configuration().computerWorkTiming.improveRewards || c.configuration().computerWorkTiming.skipAttackStartDelay)
        return false;
    if (!person.createHidden(IDD_PERSON_CONFIG))
        return false;
    person.radio(IDC_FEN_ZHAN, 1);
    if (!person.savePerson() || !person.saveFriendship() || person.draft.levels[0] % 4 != 1)
        return false;
    person.destroy();
    auto rows = [&](auto &page, int resource) {
        if (!page.createHidden(resource))
            return false;
        std::vector<std::wstring> labels;
        for (size_t row = 0; row < page.draft.Count; ++row)
            labels.push_back((row < 10 ? L"0" : L"") + std::to_wstring(row) + L" " + page.title);
        if (!ui::smokeListItems(page, page.listId, labels))
            return false;
        std::vector<int> edits;
        for (size_t row = 0; row < page.draft.Count; ++row)
        {
            page.select(page.listId, int(row));
            page.command(page.listId, ui::Event::Selection);
            if (page.index != row)
                return false;
            const auto bindings = page.fields(page.draft);
            for (const auto &field : bindings)
                if (page.number(field.id) != field.get())
                    return false;
            const auto &first = bindings.front();
            const int edited = first.minimum + int(row % (first.maximum - first.minimum + 1));
            edits.push_back(edited);
            page.number(first.id, edited);
        }
        // Revisit every row, including the final pending edit, and check draft isolation.
        for (size_t row = 0; row < page.draft.Count; ++row)
        {
            page.select(page.listId, int(row));
            page.command(page.listId, ui::Event::Selection);
            const auto first = page.fields(page.draft).front();
            if (page.index != row || first.get() != edits[row] || page.number(first.id) != edits[row])
                return false;
        }
        page.destroy();
        return true;
    };
    if (!rows(city, IDD_CITY_CONFIG) || !rows(skill, IDD_KILLER_CONFIG) || !rows(formation, IDD_ARRAY_CONFIG))
        return false;
    if (c.configuration().persons.encodeLevels() != personsBefore || c.configuration().cities.encodeLimits() != citiesBefore ||
        c.configuration().skills.encode() != skillsBefore || c.configuration().formations.encode() != formationsBefore)
        return false;
    // A confirmed child is independent of its parent's unconfirmed parameter draft.
    Controller independent;
    const auto independentParameters = independent.configuration().parameters;
    SettingsPage parent(independent);
    SkillPage child(independent);
    if (!parent.createHidden(IDD_PARA_SETTING) || !child.createHidden(IDD_KILLER_CONFIG)) return false;
    parent.number(IDC_MONEY_CUT_1, 13);
    child.number(IDC_KILLER_POWER, 91);
    child.accept();
    parent.finish(IDCANCEL);
    return independent.configuration().skills.entries[0].power == 91 &&
           independent.configuration().parameters.get(IDC_MONEY_CUT_1) == independentParameters.get(IDC_MONEY_CUT_1);
}
} // namespace app
