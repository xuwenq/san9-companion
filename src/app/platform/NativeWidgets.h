#pragma once
#include <Windows.h>
#include <map>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace ui
{
enum class Event
{
    Click,
    Selection,
    Changed,
    Focus
};
struct MenuItem
{
    unsigned id;
    std::wstring text;
    bool checked;
};
class Dialog
{
  public:
    virtual ~Dialog() = default;
    int show(int resource, Dialog *owner = nullptr);
    bool createHidden(int resource);
    void destroy();
    void finish(int result);
    void text(int id, const std::wstring &value);
    std::wstring text(int id) const;
    void number(int id, int value);
    std::optional<int> number(int id) const;
    bool number(int id, int minimum, int maximum, int &value);
    void check(int id, bool value);
    bool checked(int id) const;
    void select(int id, int index, bool combo = false);
    int selected(int id, bool combo = false) const;
    void appendItems(int id, const std::vector<std::wstring> &values, bool combo = false);
    void radio(int id, int value);
    int radio(int id) const;
    void enable(int id, bool value);
    void readOnly(int id);
    void flat(int id, bool value);
    void focus(int id);
    int focused() const;
    void scrollRange(int id, int maximum);
    void scrollPosition(int id, int value);
    void icon(int resource);
    void tooltip(int id, const std::wstring &message);
    void timer(unsigned id, unsigned milliseconds);
    void stopTimer(unsigned id);
    unsigned popup(int anchor, const std::vector<MenuItem> &items);
    void message(const std::wstring &value) const;
    std::optional<std::filesystem::path> file(bool save, const wchar_t *name) const;
    bool savePreference(const wchar_t *section, const wchar_t *key, bool value) const;
    bool preference(const wchar_t *section, const wchar_t *key, bool fallback) const;
    bool savePreferenceNumber(const wchar_t *section, const wchar_t *key, unsigned value) const;
    unsigned preferenceNumber(const wchar_t *section, const wchar_t *key, unsigned fallback) const;
    void settingsPages(const std::vector<MenuItem> &options);
    void settingsNumber(int id, const wchar_t *label, int minimum, int maximum, int value);
    void settingsPage(int page);
    HWND endpoint() const
    {
        return window_;
    }
    bool smoke = false;

  protected:
    virtual void initialize()
    {
    }
    virtual void command(int id, Event event)
    {
    }
    virtual void accept()
    {
        finish(IDOK);
    }
    virtual void cancel()
    {
        finish(IDCANCEL);
    }
    virtual void tick(unsigned id)
    {
    }
    virtual void scroll(int id, int code, int position)
    {
    }
    virtual bool gameMessage(UINT, WPARAM, LPARAM)
    {
        return false;
    }

  private:
    friend bool smokeTooltipUpdates(Dialog &dialog, int id);
    static INT_PTR CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    HWND window_ = nullptr;
    HWND tabs_ = nullptr;
    std::vector<HWND> extensions_;
    std::vector<HWND> originals_;
    HWND tooltip_ = nullptr;
    std::map<int, std::wstring> tooltipText_;
    bool modal_ = false;
    int settingsTop_ = 49;
};
std::wstring gameText(char *value, size_t capacity);
bool smokeTooltipUpdates(Dialog &dialog, int id);
bool smokeListItems(Dialog &dialog, int id, const std::vector<std::wstring> &expected);
std::wstring ansiText(const char *value);
void initializeWindows();
void launchGame(Dialog &owner);
void fatalError(const char *message);
} // namespace ui
