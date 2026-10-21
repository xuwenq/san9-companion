#include "../../../assets/ui/resource_ids.h"
#include "Pages.h"
#include "RuntimeStatusText.h"
#include "ConfigFileText.h"
#include "BuildInfo.h"
namespace app
{
class MainWindow final : public ui::Dialog
{
  public:
    explicit MainWindow(Controller &controller) : app(controller)
    {
    }

  private:
    Controller &app;
    std::wstring lastStatus;
    void initialize() override
    {
        icon(IDR_MAINFRAME);
        text(IDC_COMMIT_DATE, std::wstring(build_info::CommitDate) + L" 版");
        app.setCallbackWindow(endpoint());
        tooltip(IDC_STATUS, L"悬停查看运行状态与失败详情。");
        if (!smoke)
        {
            auto errors = app.load(preference(L"City", L"AutoApply", true),
                                   preference(L"Extensions", L"SkipDurabilitySiegeSelection", true),
                                   {preference(L"Extensions", L"ImproveComputerRewardTiming", true),
                                    preference(L"Extensions", L"SkipComputerAttackStartDelay", false)},
                                   {preferenceNumber(L"Extensions", L"LowDeathRate", 40),
                                    preferenceNumber(L"Extensions", L"DeathThreshold", 100)},
                                   {preferenceString(L"Extensions", L"GrowthBoostList", L""),
                                    preferenceNumber(L"Extensions", L"GrowthBoostMultiplier", 1)},
                                   {preferenceNumber(L"Extensions", L"CultivateDuration", 9)});
            for (const auto &error : errors)
                message(error.file + L"：" + configFileText(error.result));
            const auto rate=preferenceNumber(L"Presentation",L"Speed",1);
            app.commitPresentationSpeed(presentation2026::validMultiplier(rate)?rate:1);
        }
        timer(39, 1000);
    }
    void refresh()
    {
        const auto status = app.status();
        auto value = statusDetails(status);
        if (!value.empty() && value != lastStatus)
        {
            text(IDC_STATUS, statusText(status));
            tooltip(IDC_STATUS, value);
            lastStatus = value;
        }
    }
    void tick(unsigned id) override
    {
        if (id == 39)
        {
            app.poll();
            refresh();
        }
    }
    bool gameMessage(UINT message, WPARAM action, LPARAM argument) override
    {
        if (smoke)
            return false;
        bool handled = app.gameMessage(message, action, argument);
        if (handled)
            refresh();
        return handled;
    }
    void command(int id, ui::Event event) override
    {
        if (event != ui::Event::Click)
            return;
        if (id == IDC_PARA_SETTING)
        {
            showSettings(app, *this);
            refresh();
        }
        else if (id == IDC_START_SAN9)
            ui::launchGame(*this);

    }
    void accept() override
    {
        cancel();
    }
    void cancel() override
    {
        stopTimer(39);
        if (!app.undo())
        {
            timer(39,1000);
            refresh();
            message(L"恢复未完成，修改日志和资源已保留。请再次关闭以重试恢复。");
            return;
        }
        finish(IDCANCEL);
    }
};
int run(bool smoke)
{
    ui::initializeWindows();
    Controller controller;
    MainWindow window(controller);
    if (smoke)
    {
        if (!window.createHidden(IDD_SAN9PKHARD_DIALOG))
            return 2;
        bool passed = ui::smokeTooltipUpdates(window, IDC_STATUS) && smokePages(controller);
        window.destroy();
        return passed ? 0 : 3;
    }
    window.show(IDD_SAN9PKHARD_DIALOG);
    return 0;
}
} // namespace app
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR commandLine, int)
{
    try
    {
        return app::run(std::wstring(commandLine) == L"--ui-smoke");
    }
    catch (const std::exception &error)
    {
        if (std::wstring(commandLine) != L"--ui-smoke")
            ui::fatalError(error.what());
        return 1;
    }
}
