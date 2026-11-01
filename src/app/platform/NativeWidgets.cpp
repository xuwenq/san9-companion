#include "NativeWidgets.h"
#include "../../runtime/AppPaths.h"
#include "../../runtime/GameText.h"
#include "../../../assets/ui/resource_ids.h"
#include <CommCtrl.h>
#include <climits>
#include <commdlg.h>
#include <cstring>
#include <shellapi.h>
#include <stdexcept>
namespace ui
{
namespace
{
HINSTANCE instance()
{
    return GetModuleHandleW(nullptr);
}
std::vector<BYTE> hiddenLayout(int resource)
{
    auto handle = FindResourceW(instance(), MAKEINTRESOURCEW(resource), MAKEINTRESOURCEW(5));
    if (!handle)
        throw std::runtime_error("missing dialog resource");
    auto bytes = static_cast<const BYTE *>(LockResource(LoadResource(instance(), handle)));
    if (!bytes)
        throw std::runtime_error("cannot load dialog resource");
    std::vector<BYTE> layout(bytes, bytes + SizeofResource(instance(), handle));
    const bool extended = reinterpret_cast<const WORD *>(layout.data())[1] == 0xFFFF;
    const size_t styleOffset = extended ? 12 : 0;
    DWORD style = 0;
    std::memcpy(&style, layout.data() + styleOffset, sizeof(style));
    style &= ~WS_VISIBLE;
    std::memcpy(layout.data() + styleOffset, &style, sizeof(style));
    return layout;
}
class ListUpdate
{
  public:
    explicit ListUpdate(HWND window) : window_(window), visible_(IsWindowVisible(window) != FALSE)
    {
        if (visible_)
            SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    }
    ~ListUpdate()
    {
        if (visible_ && IsWindow(window_))
        {
            SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        }
    }
    ListUpdate(const ListUpdate &) = delete;
    ListUpdate &operator=(const ListUpdate &) = delete;

  private:
    HWND window_;
    bool visible_;
};
class WindowLayout
{
  public:
    void add(HWND window, int x, int y, UINT flags)
    {
        items_.push_back({window, x, y, flags});
    }
    void apply(HWND parent)
    {
        if (items_.empty())
            return;
        auto batch = BeginDeferWindowPos(int(items_.size()));
        if (!batch)
            throw std::runtime_error("cannot begin window layout");
        for (const auto &item : items_)
        {
            batch = DeferWindowPos(batch, item.window, nullptr, item.x, item.y, 0, 0,
                                  item.flags | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            if (!batch)
                throw std::runtime_error("cannot queue window layout");
        }
        if (!EndDeferWindowPos(batch))
            throw std::runtime_error("cannot apply window layout");
        if (IsWindowVisible(parent))
            RedrawWindow(parent, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

  private:
    struct Item
    {
        HWND window;
        int x, y;
        UINT flags;
    };
    std::vector<Item> items_;
};
HWND child(HWND window, int id)
{
    auto handle = GetDlgItem(window, id);
    if (!handle)
        throw std::runtime_error("missing dialog control");
    return handle;
}
std::filesystem::path preferences()
{
    return san9::executableFile(L"San9PKHardSettings.ini");
}
std::vector<HWND> radios(HWND start)
{
    std::vector<HWND> result;
    for (auto handle = start; handle; handle = GetWindow(handle, GW_HWNDNEXT))
    {
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        if (handle != start && (style & WS_GROUP))
            break;
        const auto type = style & BS_TYPEMASK;
        wchar_t name[32]{};
        GetClassNameW(handle, name, 32);
        if (_wcsicmp(name, L"Button") == 0 && (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON))
            result.push_back(handle);
    }
    return result;
}
} // namespace
void initializeWindows()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
    if (!InitCommonControlsEx(&controls))
        throw std::runtime_error("common control initialization failed");
}
int Dialog::show(int resource, Dialog *owner)
{
    modal_ = true;
    // The dialog manager shows the modal window after WM_INITDIALOG completes.
    auto layout = hiddenLayout(resource);
    auto result = DialogBoxIndirectParamW(instance(), reinterpret_cast<const DLGTEMPLATE *>(layout.data()),
                                         owner ? owner->window_ : nullptr, procedure, reinterpret_cast<LPARAM>(this));
    if (result == -1)
        throw std::runtime_error("dialog creation failed");
    return int(result);
}
bool Dialog::createHidden(int resource)
{
    modal_ = false;
    smoke = true;
    auto layout = hiddenLayout(resource);
    window_ = CreateDialogIndirectParamW(instance(), reinterpret_cast<const DLGTEMPLATE *>(layout.data()),
                                         nullptr, procedure, reinterpret_cast<LPARAM>(this));
    return window_ && IsWindow(window_);
}
void Dialog::destroy()
{
    if (window_)
        DestroyWindow(window_);
}
void Dialog::finish(int result)
{
    if (modal_)
        EndDialog(window_, result);
    else
        destroy();
}
void Dialog::text(int id, const std::wstring &value)
{
    SetWindowTextW(child(window_, id), value.c_str());
}
std::wstring Dialog::text(int id) const
{
    auto handle = child(window_, id);
    const int size = GetWindowTextLengthW(handle);
    std::wstring value(size + 1, L'\0');
    value.resize(GetWindowTextW(handle, value.data(), size + 1));
    return value;
}
void Dialog::number(int id, int value)
{
    text(id, std::to_wstring(value));
}
std::optional<int> Dialog::number(int id) const
{
    const auto value = text(id);
    if (value.empty())
        return {};
    size_t i = 0;
    bool negative = false;
    if (value[0] == L'-')
    {
        negative = true;
        i = 1;
    }
    else if (value[0] == L'+')
        i = 1;
    if (i == value.size())
        return {};
    unsigned long long result = 0;
    for (; i < value.size(); ++i)
    {
        if (value[i] < L'0' || value[i] > L'9')
            return {};
        result = result * 10 + (value[i] - L'0');
        if (result > (negative ? 2147483648ull : 2147483647ull))
            return {};
    }
    if (negative && result == 2147483648ull)
        return INT_MIN;
    return negative ? -int(result) : int(result);
}
bool Dialog::number(int id, int minimum, int maximum, int &value)
{
    auto result = number(id);
    if (!result || *result < minimum || *result > maximum)
    {
        focus(id);
        message(L"请输入范围内的完整整数：" + std::to_wstring(minimum) + L"～" + std::to_wstring(maximum));
        return false;
    }
    value = *result;
    return true;
}
void Dialog::check(int id, bool value)
{
    SendMessageW(child(window_, id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}
bool Dialog::checked(int id) const
{
    return SendMessageW(child(window_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}
void Dialog::select(int id, int index, bool combo)
{
    SendMessageW(child(window_, id), combo ? CB_SETCURSEL : LB_SETCURSEL, index, 0);
}
int Dialog::selected(int id, bool combo) const
{
    return int(SendMessageW(child(window_, id), combo ? CB_GETCURSEL : LB_GETCURSEL, 0, 0));
}
void Dialog::appendItems(int id, const std::vector<std::wstring> &values, bool combo)
{
    if (values.empty())
        return;
    auto handle = child(window_, id);
    ListUpdate update(handle);
    size_t bytes = 0;
    for (const auto &value : values)
        bytes += (value.size() + 1) * sizeof(wchar_t);
    auto storage = SendMessageW(handle, combo ? CB_INITSTORAGE : LB_INITSTORAGE,
                                WPARAM(values.size()), LPARAM(bytes));
    if (storage == (combo ? CB_ERRSPACE : LB_ERRSPACE))
        throw std::runtime_error("cannot reserve list storage");
    for (const auto &value : values)
    {
        auto result = SendMessageW(handle, combo ? CB_ADDSTRING : LB_ADDSTRING, 0,
                                   reinterpret_cast<LPARAM>(value.c_str()));
        if (result < 0)
            throw std::runtime_error("cannot append list item");
    }
}
bool smokeListItems(Dialog &dialog, int id, const std::vector<std::wstring> &expected)
{
    const auto list = child(dialog.endpoint(), id);
    if ((GetWindowLongPtrW(list, GWL_STYLE) & LBS_SORT) ||
        SendMessageW(list, LB_GETCOUNT, 0, 0) != LRESULT(expected.size()))
        return false;
    for (size_t row = 0; row < expected.size(); ++row)
    {
        const auto length = SendMessageW(list, LB_GETTEXTLEN, row, 0);
        if (length < 0)
            return false;
        std::wstring label(size_t(length) + 1, L'\0');
        if (SendMessageW(list, LB_GETTEXT, row, reinterpret_cast<LPARAM>(label.data())) != length)
            return false;
        label.resize(size_t(length));
        if (label != expected[row])
            return false;
    }
    return true;
}
void Dialog::radio(int id, int value)
{
    auto group = radios(child(window_, id));
    for (size_t i = 0; i < group.size(); ++i)
        SendMessageW(group[i], BM_SETCHECK, int(i) == value ? BST_CHECKED : BST_UNCHECKED, 0);
}
int Dialog::radio(int id) const
{
    auto group = radios(child(window_, id));
    for (size_t i = 0; i < group.size(); ++i)
        if (SendMessageW(group[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
            return int(i);
    return -1;
}
void Dialog::enable(int id, bool value)
{
    EnableWindow(child(window_, id), value);
}
void Dialog::readOnly(int id)
{
    SendMessageW(child(window_, id), EM_SETREADONLY, TRUE, 0);
}
void Dialog::flat(int id, bool value)
{
    auto handle = child(window_, id);
    auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    SetWindowLongPtrW(handle, GWL_STYLE, value ? (style | BS_FLAT) : (style & ~BS_FLAT));
    InvalidateRect(handle, nullptr, TRUE);
}
void Dialog::focus(int id)
{
    SetFocus(child(window_, id));
}
int Dialog::focused() const
{
    return GetDlgCtrlID(GetFocus());
}
void Dialog::scrollRange(int id, int maximum)
{
    SetScrollRange(child(window_, id), SB_CTL, 0, maximum, TRUE);
}
void Dialog::scrollPosition(int id, int value)
{
    SetScrollPos(child(window_, id), SB_CTL, value, TRUE);
}
void Dialog::icon(int resource)
{
    auto value = LoadIconW(instance(), MAKEINTRESOURCEW(resource));
    SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(value));
    SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(value));
}
void Dialog::tooltip(int id, const std::wstring &message)
{
    const auto control = child(window_, id);
    if (!tooltip_)
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, instance(), nullptr);
    if (!tooltip_)
        throw std::runtime_error("cannot create tooltip");
    auto [entry, inserted] = tooltipText_.try_emplace(id, std::make_unique<std::wstring>());
    TOOLINFOW info{sizeof(info)};
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = window_;
    info.uId = reinterpret_cast<UINT_PTR>(control);
    if (inserted) {
        *entry->second = message;
        info.lpszText = entry->second->data();
        wchar_t className[16]{};
        GetClassNameW(control, className, _countof(className));
        if (lstrcmpiW(className, L"Static") == 0)
            SetWindowLongPtrW(control, GWL_STYLE, GetWindowLongPtrW(control, GWL_STYLE) | SS_NOTIFY);
        if (!SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info))) {
            tooltipText_.erase(entry);
            throw std::runtime_error("cannot bind tooltip");
        }
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 420);
        SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
        return;
    }
    // The control keeps the text pointer, so the replacement must own a stable
    // buffer of its own and the previous one may only be released once the
    // control no longer references it.
    auto replacement = std::make_unique<std::wstring>(message);
    info.lpszText = replacement->data();
    // TTM_UPDATETIPTEXTW has no documented return value, so its result cannot
    // be used as a success flag; only the initial TTM_ADDTOOLW bind is checked.
    SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
    entry->second = std::move(replacement);
}
bool smokeTooltipUpdates(Dialog &dialog, int id)
{
    if (!dialog.smoke) return false;
    dialog.tooltip(id, L"失败详情");
    const auto window = dialog.tooltip_;
    const auto count = SendMessageW(window, TTM_GETTOOLCOUNT, 0, 0);
    const std::wstring details(600, L'测');
    for (int index = 0; index < 100; ++index) dialog.tooltip(id, details + std::to_wstring(index));
    TOOLINFOW info{sizeof(info)};
    info.uFlags = TTF_IDISHWND;
    info.hwnd = dialog.endpoint();
    info.uId = reinterpret_cast<UINT_PTR>(child(dialog.endpoint(), id));
    wchar_t actual[1024]{};
    info.lpszText = actual;
    SendMessageW(window, TTM_GETTEXTW, _countof(actual), reinterpret_cast<LPARAM>(&info));
    return window == dialog.tooltip_ && SendMessageW(window, TTM_GETTOOLCOUNT, 0, 0) == count
        && dialog.tooltipText_.size() == static_cast<size_t>(count)
        && std::wstring(actual) == details + L"99";
}
void Dialog::timer(unsigned id, unsigned milliseconds)
{
    if (!smoke && !SetTimer(window_, id, milliseconds, nullptr))
        throw std::runtime_error("cannot create timer");
}
void Dialog::stopTimer(unsigned id)
{
    KillTimer(window_, id);
}
unsigned Dialog::popup(int anchor, const std::vector<MenuItem> &items)
{
    auto menu = CreatePopupMenu();
    if (!menu)
        throw std::runtime_error("cannot create popup menu");
    for (const auto &item : items)
        if (!AppendMenuW(menu, MF_STRING | (item.checked ? MF_CHECKED : 0), item.id, item.text.c_str()))
        {
            DestroyMenu(menu);
            throw std::runtime_error("cannot append popup item");
        }
    RECT rect{};
    GetWindowRect(child(window_, anchor), &rect);
    auto chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_LEFTBUTTON, rect.left, rect.top, 0,
                                 window_, nullptr);
    DestroyMenu(menu);
    return unsigned(chosen);
}
void Dialog::message(const std::wstring &value) const
{
    if (smoke)
        throw std::runtime_error("unexpected message during UI smoke check");
    MessageBoxW(window_, value.c_str(), L"San9 Companion", MB_OK | MB_ICONINFORMATION);
}
std::optional<std::filesystem::path> Dialog::file(bool save, const wchar_t *name) const
{
    wchar_t path[32768]{};
    wcscpy_s(path, name);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"配置文本 (*.txt)\0*.txt\0所有文件\0*.*\0\0";
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
        return std::filesystem::path(path);
    if (CommDlgExtendedError())
        throw std::runtime_error("file dialog failed");
    return {};
}
bool Dialog::savePreference(const wchar_t *section, const wchar_t *key, bool value) const
{
    if (smoke) return true;
    return WritePrivateProfileStringW(section, key, value ? L"1" : L"0", preferences().c_str()) != FALSE;
}
bool Dialog::savePreferenceNumber(const wchar_t *section, const wchar_t *key, unsigned value) const
{
    if (smoke) return true;
    return WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(),
                                     preferences().c_str()) != FALSE;
}
unsigned Dialog::preferenceNumber(const wchar_t *section, const wchar_t *key, unsigned fallback) const
{
    return GetPrivateProfileIntW(section,key,fallback,
                                 preferences().c_str());
}
bool Dialog::preference(const wchar_t *section, const wchar_t *key, bool fallback) const
{
    return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, preferences().c_str()) != 0;
}
bool Dialog::savePreferenceString(const wchar_t *section, const wchar_t *key, const std::wstring &value) const
{
    if (smoke) return true;
    return WritePrivateProfileStringW(section, key, value.c_str(), preferences().c_str()) != FALSE;
}
std::wstring Dialog::preferenceString(const wchar_t *section, const wchar_t *key, const std::wstring &fallback) const
{
    std::wstring buffer(4096, L'\0');
    const auto length = GetPrivateProfileStringW(section, key, fallback.c_str(), buffer.data(),
                                                 static_cast<DWORD>(buffer.size()), preferences().c_str());
    buffer.resize(length);
    return buffer;
}
void Dialog::settingsPages(const std::vector<MenuItem> &options)
{
    RECT rect{};
    GetWindowRect(window_, &rect);
    RECT shift{0, 0, 0, 24};
    MapDialogRect(window_, &shift);
    SetWindowPos(window_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top + shift.bottom,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    WindowLayout layout;
    for (auto handle = GetWindow(window_, GW_CHILD); handle; handle = GetWindow(handle, GW_HWNDNEXT))
    {
        RECT position{};
        GetWindowRect(handle, &position);
        MapWindowPoints(nullptr, window_, reinterpret_cast<POINT *>(&position), 2);
        layout.add(handle, position.left, position.top + shift.bottom, 0);
        int id = GetDlgCtrlID(handle);
        if (id != IDOK && id != IDCANCEL && id != IDC_SAVE_CONFIG && id != IDC_PERSON_CONFIG &&
            id != IDC_CITY_CONFIG && id != IDC_KILLER_CONFIG && id != IDC_ARRAY_CONFIG &&
            id != IDC_SELECT_PLAYER_FORCE && id != IDC_PRESENTATION_SPEED)
            originals_.push_back(handle);
    }
    layout.apply(window_);
    RECT tab{11, 3, 540, 24};
    MapDialogRect(window_, &tab);
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, tab.left, tab.top,
                            tab.right - tab.left, tab.bottom - tab.top, window_,
                            reinterpret_cast<HMENU>(1200), instance(), nullptr);
    if (!tabs_)
        throw std::runtime_error("cannot create settings tabs");
    SendMessageW(tabs_, WM_SETFONT, SendMessageW(window_, WM_GETFONT, 0, 0), TRUE);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t *>(L"原有设置");
    TabCtrl_InsertItem(tabs_, 0, &item);
    item.pszText = const_cast<wchar_t *>(L"扩展设置");
    TabCtrl_InsertItem(tabs_, 1, &item);
    int top = 49;
    for (const auto &item : options)
    {
        RECT option{23, top, 510, top + 12};
        MapDialogRect(window_, &option);
        const auto handle = CreateWindowExW(0, L"Button", item.text.c_str(),
            WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, option.left, option.top,
            option.right - option.left, option.bottom - option.top, window_,
            reinterpret_cast<HMENU>(item.id), instance(), nullptr);
        if (!handle) throw std::runtime_error("cannot create extension checkbox");
        extensions_.push_back(handle);
        SendMessageW(handle, WM_SETFONT, SendMessageW(window_, WM_GETFONT, 0, 0), TRUE);
        check(item.id, item.checked);
        top += 22;
    }
    settingsTop_ = top;
    settingsPage(0);
}
void Dialog::settingsNumber(int id, const wchar_t *label, int minimum, int maximum, int value)
{
    (void)minimum;
    (void)maximum;
    RECT captionRect{23, settingsTop_, 200, settingsTop_ + 12};
    MapDialogRect(window_, &captionRect);
    const auto caption = CreateWindowExW(0, L"Static", label, WS_CHILD,
                                         captionRect.left, captionRect.top,
                                         captionRect.right - captionRect.left,
                                         captionRect.bottom - captionRect.top, window_,
                                         nullptr, instance(), nullptr);
    if (!caption) throw std::runtime_error("cannot create settings label");
    extensions_.push_back(caption);
    SendMessageW(caption, WM_SETFONT, SendMessageW(window_, WM_GETFONT, 0, 0), TRUE);

    RECT fieldRect{230, settingsTop_, 420, settingsTop_ + 12};
    MapDialogRect(window_, &fieldRect);
    const auto handle = CreateWindowExW(0, L"Edit", L"", WS_CHILD | WS_TABSTOP | WS_BORDER | ES_NUMBER,
                                        fieldRect.left, fieldRect.top,
                                        fieldRect.right - fieldRect.left,
                                        fieldRect.bottom - fieldRect.top, window_,
                                        reinterpret_cast<HMENU>(id), instance(), nullptr);
    if (!handle) throw std::runtime_error("cannot create settings number field");
    extensions_.push_back(handle);
    SendMessageW(handle, WM_SETFONT, SendMessageW(window_, WM_GETFONT, 0, 0), TRUE);
    number(id, value);
    settingsTop_ += 22;
}
void Dialog::settingsText(int id, const wchar_t *label, const std::wstring &value)
{
    RECT captionRect{23, settingsTop_, 200, settingsTop_ + 12};
    MapDialogRect(window_, &captionRect);
    const auto caption = CreateWindowExW(0, L"Static", label, WS_CHILD,
                                         captionRect.left, captionRect.top,
                                         captionRect.right - captionRect.left,
                                         captionRect.bottom - captionRect.top, window_,
                                         nullptr, instance(), nullptr);
    if (!caption) throw std::runtime_error("cannot create settings label");
    extensions_.push_back(caption);
    SendMessageW(caption, WM_SETFONT, SendMessageW(window_, WM_GETFONT, 0, 0), TRUE);

    RECT fieldRect{230, settingsTop_, 420, settingsTop_ + 12};
    MapDialogRect(window_, &fieldRect);
    const auto handle = CreateWindowExW(0, L"Edit", L"", WS_CHILD | WS_TABSTOP | WS_BORDER,
                                        fieldRect.left, fieldRect.top,
                                        fieldRect.right - fieldRect.left,
                                        fieldRect.bottom - fieldRect.top, window_,
                                        reinterpret_cast<HMENU>(id), instance(), nullptr);
    if (!handle) throw std::runtime_error("cannot create settings text field");
    extensions_.push_back(handle);
    SendMessageW(handle, WM_SETFONT, SendMessageW(window_, WM_GETFONT, 0, 0), TRUE);
    text(id, value);
    settingsTop_ += 22;
}
void Dialog::settingsPage(int page)
{
    if (!tabs_)
        return;
    TabCtrl_SetCurSel(tabs_, page);
    WindowLayout layout;
    auto visibility = [&](HWND handle, bool visible) {
        if (((GetWindowLongPtrW(handle, GWL_STYLE) & WS_VISIBLE) != 0) != visible)
            layout.add(handle, 0, 0, SWP_NOMOVE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    };
    for (auto handle : originals_)
        visibility(handle, page == 0);
    for (auto handle : extensions_)
        visibility(handle, page == 1);
    layout.apply(window_);
}
INT_PTR CALLBACK Dialog::procedure(HWND window, UINT message, WPARAM first, LPARAM second)
{
    auto self = reinterpret_cast<Dialog *>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        self = reinterpret_cast<Dialog *>(second);
        self->window_ = window;
        SetWindowLongPtrW(window, DWLP_USER, second);
    }
    if (!self)
        return FALSE;
    try
    {
        if (message == WM_INITDIALOG)
        {
            self->initialize();
            return TRUE;
        }
        if (message == WM_COMMAND)
        {
            int id = LOWORD(first), notification = HIWORD(first);
            if (id == IDOK && notification == BN_CLICKED)
                self->accept();
            else if (id == IDCANCEL && notification == BN_CLICKED)
                self->cancel();
            else if (notification == BN_CLICKED)
                self->command(id, Event::Click);
            else if (notification == LBN_SELCHANGE || notification == CBN_SELCHANGE)
                self->command(id, Event::Selection);
            else if (notification == EN_CHANGE)
                self->command(id, Event::Changed);
            else if (notification == EN_SETFOCUS)
                self->command(id, Event::Focus);
            return TRUE;
        }
        if (message == WM_CLOSE)
        {
            self->cancel();
            return TRUE;
        }
        if (message == WM_TIMER)
        {
            self->tick(unsigned(first));
            return TRUE;
        }
        if (message == WM_HSCROLL || message == WM_VSCROLL)
        {
            self->scroll(GetDlgCtrlID(reinterpret_cast<HWND>(second)), LOWORD(first), HIWORD(first));
            return TRUE;
        }
        if (message == WM_NOTIFY && reinterpret_cast<NMHDR *>(second)->hwndFrom == self->tabs_ &&
            reinterpret_cast<NMHDR *>(second)->code == TCN_SELCHANGE)
        {
            self->settingsPage(TabCtrl_GetCurSel(self->tabs_));
            return TRUE;
        }
        if (message == WM_NCDESTROY)
        {
            self->window_ = nullptr;
            self->tooltip_ = nullptr;
            self->tooltipText_.clear();
            return FALSE;
        }
        if (self->gameMessage(message, first, second))
            return TRUE;
    }
    catch (const std::exception &error)
    {
        if (self->smoke)
        {
            self->finish(-1);
            return TRUE;
        }
        MessageBoxW(window, ansiText(error.what()).c_str(), L"操作失败", MB_OK | MB_ICONERROR);
        if (message == WM_INITDIALOG)
            self->finish(IDCANCEL);
    }
    return FALSE;
}
std::wstring ansiText(const char *value)
{
    int size = MultiByteToWideChar(936, 0, value, -1, nullptr, 0);
    if (!size)
        return L"?";
    std::wstring result(size, L'\0');
    MultiByteToWideChar(936, 0, value, -1, result.data(), size);
    result.pop_back();
    return result;
}
std::wstring gameText(const char *value, size_t capacity)
{
    auto result = san9::gameWideText(value, capacity);
    if (result.empty())
        return L"?";
    return result;
}
void fatalError(const char *message)
{
    MessageBoxW(nullptr, ansiText(message).c_str(), L"San9 Companion", MB_OK | MB_ICONERROR);
}
void launchGame(Dialog &owner)
{
    if (FindWindowW(L"KOEI_SAN9WINDOW", nullptr))
    {
        owner.message(L"39已运行!");
        return;
    }
    auto path = san9::executableFile(L"San9PK.exe");
    auto result =
        ShellExecuteW(owner.endpoint(), L"open", path.c_str(), nullptr, path.parent_path().c_str(), SW_SHOW);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
        owner.message(L"启动39失败，请确认程序放在39的根目录下。");
}
} // namespace ui
