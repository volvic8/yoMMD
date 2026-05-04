#include "menu.hpp"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>
#include "main.hpp"

namespace {
constexpr COLORREF kPanelBg = RGB(28, 31, 38);
constexpr COLORREF kTextPrimary = RGB(242, 244, 248);
HBRUSH getPanelBrush() {
    static HBRUSH brush = CreateSolidBrush(kPanelBg);
    return brush;
}

std::wstring toDisplayName(const std::filesystem::path& path, size_t maxLength = 32) {
    std::wstring name = path.filename().wstring();
    if (name.empty())
        name = path.wstring();
    if (name.size() <= maxLength)
        return name;
    if (maxLength < 4)
        return name.substr(0, maxLength);
    return name.substr(0, maxLength - 3) + L"...";
}

void applyMenuFont(HWND hwnd, HFONT font) {
    if (hwnd && font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

RECT makeRect(int left, int top, int width, int height) {
    return RECT{left, top, left + width, top + height};
}

RECT clampRectToWorkArea(const RECT& rect, const RECT& workArea) {
    RECT result = rect;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    result.left = std::clamp(rect.left, workArea.left + 8, workArea.right - width - 8);
    result.top = std::clamp(rect.top, workArea.top + 8, workArea.bottom - height - 8);
    result.right = result.left + width;
    result.bottom = result.top + height;
    return result;
}

int overlapArea(const RECT& a, const RECT& b) {
    RECT intersection = {};
    if (!IntersectRect(&intersection, &a, &b))
        return 0;
    return (intersection.right - intersection.left) * (intersection.bottom - intersection.top);
}

template <typename T, typename DeleteFunc, DeleteFunc deleteFunc>
class UniqueHandler {
public:
    explicit UniqueHandler(T handler = nullptr) : handler_(handler, deleteFunc) {}
    T GetRawHandler() const { return handler_.get(); }
    operator T() const { return GetRawHandler(); }

private:
    std::unique_ptr<std::remove_pointer_t<T>, DeleteFunc> handler_;
};

using UniqueHWND = UniqueHandler<HWND, decltype(&DestroyWindow), &DestroyWindow>;
using UniqueHMENU = UniqueHandler<HMENU, decltype(&DestroyMenu), &DestroyMenu>;

std::wstring makeMenuLabel(
    const std::filesystem::path& path,
    const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto rel = fs::relative(path, root, ec);
    return (ec ? path.filename() : rel).wstring();
}
}  // namespace

AppMenu::AppMenu() :
    hMenuWindow_(nullptr),
    hTaskbarIcon_(nullptr),
    hMenuFont_(nullptr),
    isMenuOpened_(false) {}

AppMenu::~AppMenu() {
    Terminate();
}

void AppMenu::Setup() {
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = AppMenu::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = wcMenuName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = getPanelBrush();
    RegisterClassW(&wc);

    hMenuFont_ = CreateFontW(
        -16, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    createTaskbar();
}

void AppMenu::Terminate() {
    destroyMenuWindow();

    if (hTaskbarIcon_) {
        DestroyIcon(hTaskbarIcon_);
        hTaskbarIcon_ = nullptr;
    }
    if (hMenuFont_) {
        DeleteObject(hMenuFont_);
        hMenuFont_ = nullptr;
    }

    Shell_NotifyIconW(NIM_DELETE, &taskbarIconDesc_);
    UnregisterClassW(wcMenuName, GetModuleHandleW(nullptr));
}

void AppMenu::destroyMenuWindow() {
    if (hMenuWindow_) {
        DestroyWindow(hMenuWindow_);
        hMenuWindow_ = nullptr;
    }
    isMenuOpened_ = false;
}

void AppMenu::ShowMenu() {
    if (hMenuWindow_) {
        destroyMenuWindow();
        return;
    }

    const HWND parentWin = getAppMain().GetWindowHandle();
    const RECT buttonRect = getAppMain().GetMenuButtonRect();
    const auto& config = getAppMain().GetRoutine().GetConfig();

    constexpr int width = 340;
    constexpr int margin = 14;
    constexpr int rowH = 18;
    constexpr int valueH = 24;
    constexpr int btnH = 28;
    constexpr int gap = 8;
    constexpr int smallBtnW = 42;
    constexpr int midBtnW = 72;
    int y = margin;

    const bool hasMultiScreen = getAllMonitorHandles().size() > 1;
    int height = 286;
    if (hasMultiScreen) {
        height += btnH + gap;
    }

    RECT mainRect = {};
    GetWindowRect(parentWin, &mainRect);
    const auto windowSize = getAppMain().GetWindowSize();
    const auto bounds = getAppMain().GetRoutine().GetModelInteractionBounds(windowSize);
    const RECT modelRect = {
        mainRect.left + static_cast<int>(bounds.x),
        mainRect.bottom - static_cast<int>(bounds.w),
        mainRect.left + static_cast<int>(bounds.z),
        mainRect.bottom - static_cast<int>(bounds.y),
    };

    const HMONITOR monitor = MonitorFromWindow(parentWin, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {.cbSize = sizeof(info)};
    RECT workArea = {};
    if (GetMonitorInfoW(monitor, &info)) {
        workArea = info.rcWork;
    } else {
        workArea = mainRect;
    }

    const int buttonCenterX = (buttonRect.left + buttonRect.right) / 2;
    const int buttonCenterY = (buttonRect.top + buttonRect.bottom) / 2;
    constexpr int gapToModel = 16;
    constexpr int gapToButton = 8;

    const std::array<RECT, 4> candidates = {
        makeRect(modelRect.left - width - gapToModel, buttonCenterY - height / 2, width, height),
        makeRect(modelRect.right + gapToModel, buttonCenterY - height / 2, width, height),
        makeRect(buttonCenterX - width / 2, modelRect.top - height - gapToButton, width, height),
        makeRect(buttonCenterX - width / 2, modelRect.bottom + gapToButton, width, height),
    };

    RECT bestRect = clampRectToWorkArea(candidates[0], workArea);
    int bestScore = overlapArea(bestRect, modelRect);
    for (const RECT& candidate : candidates) {
        const RECT clamped = clampRectToWorkArea(candidate, workArea);
        const int score = overlapArea(clamped, modelRect);
        if (score < bestScore) {
            bestScore = score;
            bestRect = clamped;
        }
    }

    hMenuWindow_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wcMenuName, L"yoMMD Menu",
        WS_POPUP | WS_BORDER, bestRect.left, bestRect.top, width, height, parentWin, nullptr,
        GetModuleHandleW(nullptr), this);
    if (!hMenuWindow_) {
        Err::Log("Failed to create menu window.");
        return;
    }

    const auto makeStatic = [&](std::wstring_view text, int sx, int sy, int sw, int sh, DWORD style) {
        HWND child = CreateWindowExW(
            0, L"STATIC", text.data(), WS_CHILD | WS_VISIBLE | style, sx, sy, sw, sh, hMenuWindow_,
            nullptr, GetModuleHandleW(nullptr), nullptr);
        applyMenuFont(child, hMenuFont_);
        return child;
    };
    const auto makeButton = [&](std::wstring_view text, int sx, int sy, int sw, int sh, UINT_PTR cmd) {
        HWND child = CreateWindowExW(
            0, L"BUTTON", text.data(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_FLAT,
            sx, sy, sw, sh, hMenuWindow_, reinterpret_cast<HMENU>(cmd), GetModuleHandleW(nullptr),
            nullptr);
        applyMenuFont(child, hMenuFont_);
        return child;
    };

    makeStatic(L"yoMMD", margin, y, width - margin * 2, rowH, SS_LEFT);
    y += rowH + 2;
    makeStatic(L"Quick controls", margin, y, width - margin * 2, rowH, SS_LEFT);
    y += rowH + gap;

    makeButton(
        getAppMain().IsMouseInteractionEnabled() ? L"Disable Mouse" : L"Enable Mouse",
        margin, y, width - margin * 2, btnH, Enum::underlyCast(Cmd::EnableMouse));
    y += btnH + gap;

    makeStatic(L"Model", margin, y, width - margin * 2, rowH, SS_LEFT);
    y += rowH + 2;
    makeStatic(
        toDisplayName(config.model, 44), margin, y, width - margin * 2, valueH,
        SS_LEFT | SS_PATHELLIPSIS);
    y += valueH + 4;
    makeButton(L"<", margin, y, smallBtnW, btnH, Enum::underlyCast(Cmd::PrevModel));
    makeButton(
        L"Choose...", margin + smallBtnW + gap, y, width - margin * 2 - smallBtnW * 2 - gap * 2,
        btnH, Enum::underlyCast(Cmd::ChangeModel));
    makeButton(
        L">", width - margin - smallBtnW, y, smallBtnW, btnH,
        Enum::underlyCast(Cmd::NextModel));
    y += btnH + gap;

    std::wstring motionName = L"(no motion)";
    if (!config.motions.empty() && !config.motions.front().paths.empty()) {
        motionName = toDisplayName(config.motions.front().paths.front(), 44);
    }
    makeStatic(L"Motion", margin, y, width - margin * 2, rowH, SS_LEFT);
    y += rowH + 2;
    makeStatic(motionName, margin, y, width - margin * 2, valueH, SS_LEFT | SS_PATHELLIPSIS);
    y += valueH + 4;
    makeButton(L"<", margin, y, smallBtnW, btnH, Enum::underlyCast(Cmd::PrevMotion));
    makeButton(
        L"Choose...", margin + smallBtnW + gap, y, width - margin * 2 - smallBtnW * 2 - gap * 2,
        btnH, Enum::underlyCast(Cmd::ChangeMotion));
    makeButton(
        L">", width - margin - smallBtnW, y, smallBtnW, btnH,
        Enum::underlyCast(Cmd::NextMotion));
    y += btnH + gap;

    makeStatic(L"View", margin, y, width - margin * 2, rowH, SS_LEFT);
    y += rowH + 2;
    makeButton(L"Front", margin, y, midBtnW, btnH, Cmd::Combine(Cmd::SetViewDirection, 0));
    makeButton(
        L"Right", margin + midBtnW + gap, y, midBtnW, btnH,
        Cmd::Combine(Cmd::SetViewDirection, 1));
    makeButton(
        L"Back", margin + (midBtnW + gap) * 2, y, midBtnW, btnH,
        Cmd::Combine(Cmd::SetViewDirection, 2));
    makeButton(
        L"Left", width - margin - midBtnW, y, midBtnW, btnH,
        Cmd::Combine(Cmd::SetViewDirection, 3));
    y += btnH + gap;
    makeButton(
        L"Reset Position", margin, y, width - margin * 2, btnH,
        Enum::underlyCast(Cmd::ResetPosition));
    y += btnH + gap;

    if (hasMultiScreen) {
        makeButton(L"Screen 0", margin, y, 110, btnH, Cmd::Combine(Cmd::SelectScreen, 0));
        makeButton(
            L"Screen 1", margin + 110 + gap, y, 110, btnH,
            Cmd::Combine(Cmd::SelectScreen, 1));
        y += btnH + gap;
    }

    makeButton(
        (GetWindowLongPtrW(parentWin, GWL_STYLE) & WS_VISIBLE) ? L"Hide Window" : L"Show Window",
        margin, y, width - margin * 2, btnH, Enum::underlyCast(Cmd::HideWindow));
    y += btnH + gap;
    makeButton(L"Quit", margin, y, width - margin * 2, btnH, Enum::underlyCast(Cmd::Quit));

    SetWindowPos(
        hMenuWindow_, HWND_TOPMOST, bestRect.left, bestRect.top, width, height, SWP_SHOWWINDOW);
    ShowWindow(hMenuWindow_, SW_SHOWNORMAL);
    SetForegroundWindow(hMenuWindow_);
    isMenuOpened_ = true;
}

void AppMenu::ShowTaskbarMenu() {
    const HWND& parentWin = getAppMain().GetWindowHandle();
    if (!parentWin)
        return;

    POINT point = {};
    if (!GetCursorPos(&point)) {
        Err::Log("Failed to get mouse point");
        return;
    }

    const LONG parentWinExStyle = GetWindowLongW(parentWin, GWL_EXSTYLE);

    UniqueHWND menuWindow(CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wcMenuName, L"", WS_POPUP, 0, 0, 0,
        0, parentWin, nullptr, GetModuleHandleW(nullptr), this));
    if (!menuWindow.GetRawHandler()) {
        Err::Log("Failed to create dummy window for taskbar menu.");
        return;
    }

    UniqueHMENU screensMenu(CreatePopupMenu());
    const HMONITOR curMonitorHandle =
        MonitorFromWindow(getAppMain().GetWindowHandle(), MONITOR_DEFAULTTONULL);
    const auto monitorHandles = getAllMonitorHandles();
    for (int cnt = static_cast<int>(monitorHandles.size()), i = 0; i < cnt; ++i) {
        const std::wstring title(L"&Screen" + std::to_wstring(i));
        const auto op = Cmd::Combine(Cmd::SelectScreen, i);
        AppendMenuW(screensMenu, MF_STRING, op, title.c_str());
        if (monitorHandles[i] == curMonitorHandle) {
            EnableMenuItem(screensMenu, op, MF_DISABLED);
        }
    }

    const auto modelRoot = Path::makeAbsolute("input/model", Path::getWorkingDirectory());
    const auto motionRoot = Path::makeAbsolute("input/motion", Path::getWorkingDirectory());
    const auto& models = getAppMain().GetAvailableModels();
    const auto& motions = getAppMain().GetAvailableMotions();

    UniqueHMENU modelsMenu(CreatePopupMenu());
    for (size_t i = 0; i < models.size(); ++i) {
        const auto op = Cmd::Combine(Cmd::SelectModel, static_cast<Cmd::UnderlyingType>(i));
        const auto title = makeMenuLabel(models[i], modelRoot);
        AppendMenuW(modelsMenu, MF_STRING, op, title.c_str());
    }
    if (!models.empty())
        AppendMenuW(modelsMenu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(modelsMenu, MF_STRING, Enum::underlyCast(Cmd::ChangeModel), L"&Other...");

    UniqueHMENU motionsMenu(CreatePopupMenu());
    for (size_t i = 0; i < motions.size(); ++i) {
        const auto op = Cmd::Combine(Cmd::SelectMotion, static_cast<Cmd::UnderlyingType>(i));
        const auto title = makeMenuLabel(motions[i], motionRoot);
        AppendMenuW(motionsMenu, MF_STRING, op, title.c_str());
    }
    if (!motions.empty())
        AppendMenuW(motionsMenu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(motionsMenu, MF_STRING, Enum::underlyCast(Cmd::ChangeMotion), L"&Other...");

    UniqueHMENU viewDirectionMenu(CreatePopupMenu());
    AppendMenuW(viewDirectionMenu, MF_STRING, Cmd::Combine(Cmd::SetViewDirection, 0), L"&Front");
    AppendMenuW(viewDirectionMenu, MF_STRING, Cmd::Combine(Cmd::SetViewDirection, 1), L"&Right");
    AppendMenuW(viewDirectionMenu, MF_STRING, Cmd::Combine(Cmd::SetViewDirection, 2), L"&Back");
    AppendMenuW(viewDirectionMenu, MF_STRING, Cmd::Combine(Cmd::SetViewDirection, 3), L"&Left");

    UniqueHMENU menu(CreatePopupMenu());
    AppendMenuW(
        menu, MF_STRING, Enum::underlyCast(Cmd::EnableMouse),
        getAppMain().IsMouseInteractionEnabled() ? L"&Disable Mouse" : L"&Enable Mouse");
    AppendMenuW(
        menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modelsMenu.GetRawHandler()), L"Change &Model");
    AppendMenuW(
        menu, MF_POPUP, reinterpret_cast<UINT_PTR>(motionsMenu.GetRawHandler()),
        L"Change M&otion");
    AppendMenuW(
        menu, MF_POPUP, reinterpret_cast<UINT_PTR>(viewDirectionMenu.GetRawHandler()),
        L"View &Direction");
    AppendMenuW(menu, MF_STRING, Enum::underlyCast(Cmd::ResetPosition), L"&Reset Position");
    AppendMenuW(menu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(
        menu, MF_POPUP, reinterpret_cast<UINT_PTR>(screensMenu.GetRawHandler()), L"&Select screen");
    AppendMenuW(menu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(
        menu, MF_STRING, Enum::underlyCast(Cmd::HideWindow),
        (GetWindowLongPtrW(parentWin, GWL_STYLE) & WS_VISIBLE) ? L"&Hide Window" : L"&Show Window");
    AppendMenuW(menu, MF_SEPARATOR, Enum::underlyCast(Cmd::None), L"");
    AppendMenuW(menu, MF_STRING, Enum::underlyCast(Cmd::Quit), L"&Quit");

    if (parentWinExStyle == 0) {
        EnableMenuItem(menu, Enum::underlyCast(Cmd::EnableMouse), MF_DISABLED);
    }
    if (monitorHandles.size() <= 1) {
        EnableMenuItem(menu, reinterpret_cast<UINT_PTR>(screensMenu.GetRawHandler()), MF_DISABLED);
    }

    constexpr UINT menuFlags = TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD;
    SetForegroundWindow(menuWindow);
    isMenuOpened_ = true;
    const auto op = TrackPopupMenuEx(menu, menuFlags, point.x, point.y, menuWindow, nullptr);
    isMenuOpened_ = false;
    executeCommand(op, false);
}

bool AppMenu::IsMenuOpened() const {
    return isMenuOpened_;
}

void AppMenu::handleCommand(UINT_PTR op) {
    executeCommand(op, true);
}

void AppMenu::executeCommand(UINT_PTR op, bool closeCompactMenu) {
    const HWND parentWin = getAppMain().GetWindowHandle();
    const auto& config = getAppMain().GetRoutine().GetConfig();

    switch (Cmd::GetCmd(static_cast<Cmd::UnderlyingType>(op))) {
    case Cmd::EnableMouse:
        getAppMain().SetMouseInteractionEnabled(!getAppMain().IsMouseInteractionEnabled());
        break;
    case Cmd::ChangeModel:
        PostMessageW(parentWin, YOMMD_WM_OPEN_MODEL_DIALOG, 0, 0);
        break;
    case Cmd::ChangeMotion:
        PostMessageW(parentWin, YOMMD_WM_OPEN_MOTION_DIALOG, 0, 0);
        break;
    case Cmd::PrevModel:
    case Cmd::NextModel: {
        const auto& models = getAppMain().GetAvailableModels();
        if (!models.empty()) {
            auto itr = std::find(models.begin(), models.end(), config.model);
            size_t index = itr == models.end() ? 0 : static_cast<size_t>(itr - models.begin());
            const int delta = Cmd::GetCmd(static_cast<Cmd::UnderlyingType>(op)) == Cmd::PrevModel ? -1 : 1;
            const size_t next = (index + models.size() + delta) % models.size();
            PostMessageW(parentWin, YOMMD_WM_SELECT_MODEL, next, 0);
        }
        break;
    }
    case Cmd::PrevMotion:
    case Cmd::NextMotion: {
        const auto& motions = getAppMain().GetAvailableMotions();
        if (!motions.empty()) {
            std::filesystem::path current;
            if (!config.motions.empty() && !config.motions.front().paths.empty())
                current = config.motions.front().paths.front();
            auto itr = std::find(motions.begin(), motions.end(), current);
            size_t index = itr == motions.end() ? 0 : static_cast<size_t>(itr - motions.begin());
            const int delta =
                Cmd::GetCmd(static_cast<Cmd::UnderlyingType>(op)) == Cmd::PrevMotion ? -1 : 1;
            const size_t next = (index + motions.size() + delta) % motions.size();
            PostMessageW(parentWin, YOMMD_WM_SELECT_MOTION, next, 0);
        }
        break;
    }
    case Cmd::SelectModel:
        PostMessageW(
            parentWin, YOMMD_WM_SELECT_MODEL,
            Cmd::GetUserData(static_cast<Cmd::UnderlyingType>(op)), 0);
        break;
    case Cmd::SelectMotion:
        PostMessageW(
            parentWin, YOMMD_WM_SELECT_MOTION,
            Cmd::GetUserData(static_cast<Cmd::UnderlyingType>(op)), 0);
        break;
    case Cmd::SetViewDirection:
        PostMessageW(
            parentWin, YOMMD_WM_SET_VIEW_DIRECTION,
            Cmd::GetUserData(static_cast<Cmd::UnderlyingType>(op)), 0);
        break;
    case Cmd::ResetPosition:
        getAppMain().GetRoutine().ResetModelPosition();
        break;
    case Cmd::SelectScreen:
        getAppMain().ChangeScreen(
            Cmd::GetUserData(static_cast<Cmd::UnderlyingType>(op)));
        break;
    case Cmd::HideWindow:
        if (GetWindowLongPtrW(parentWin, GWL_STYLE) & WS_VISIBLE)
            ShowWindow(parentWin, SW_HIDE);
        else
            ShowWindow(parentWin, SW_SHOWNORMAL);
        break;
    case Cmd::Quit:
        SendMessageW(parentWin, WM_DESTROY, 0, 0);
        break;
    case Cmd::None:
    case Cmd::MenuCount:
        break;
    }

    if (closeCompactMenu) {
        destroyMenuWindow();
    }
}

LRESULT CALLBACK AppMenu::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    AppMenu *menu = reinterpret_cast<AppMenu *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (uMsg == WM_NCCREATE) {
        const CREATESTRUCTW *cs = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        menu = static_cast<AppMenu *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(menu));
    }

    switch (uMsg) {
    case WM_ERASEBKGND: {
        RECT rect = {};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, getPanelBrush());
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, kTextPrimary);
        SetBkColor(hdc, kPanelBg);
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<INT_PTR>(getPanelBrush());
    }
    case WM_ACTIVATE:
        if (menu && LOWORD(wParam) == WA_INACTIVE) {
            menu->destroyMenuWindow();
            return 0;
        }
        break;
    case WM_COMMAND:
        if (menu) {
            menu->handleCommand(static_cast<UINT_PTR>(LOWORD(wParam)));
            return 0;
        }
        break;
    case WM_CLOSE:
        if (menu) {
            menu->destroyMenuWindow();
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void AppMenu::createTaskbar() {
    const HWND& parentWin = getAppMain().GetWindowHandle();
    if (!parentWin) {
        Err::Exit(
            "Internal error:", "Taskbar must be created after the main window is created:",
            __FILE__, __LINE__);
    }

    auto iconData = Resource::getStatusIconData();
    hTaskbarIcon_ = CreateIconFromResource(
        const_cast<PBYTE>(iconData.data()), iconData.length(), TRUE, 0x00030000);
    if (!hTaskbarIcon_) {
        Err::Log("Failed to load icon. Fallback to Windows' default application icon.");
        hTaskbarIcon_ = LoadIconA(nullptr, IDI_APPLICATION);
        if (!hTaskbarIcon_) {
            Err::Exit("Icon fallback failed.");
        }
    }

    taskbarIconDesc_.cbSize = sizeof(taskbarIconDesc_);
    taskbarIconDesc_.hWnd = parentWin;
    taskbarIconDesc_.uID = 100;
    taskbarIconDesc_.hIcon = hTaskbarIcon_;
    taskbarIconDesc_.uVersion = NOTIFYICON_VERSION_4;
    taskbarIconDesc_.uCallbackMessage = YOMMD_WM_SHOW_TASKBAR_MENU;
    wcscpy_s(taskbarIconDesc_.szTip, sizeof(taskbarIconDesc_.szTip), L"yoMMD");
    taskbarIconDesc_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;

    Shell_NotifyIconW(NIM_ADD, &taskbarIconDesc_);
}

constexpr AppMenu::Cmd::Kind AppMenu::Cmd::GetCmd(AppMenu::Cmd::UnderlyingType cmd) {
    constexpr UnderlyingType mask = (UnderlyingType(1) << fieldLength_) - 1;
    return Kind(cmd & mask);
}

constexpr AppMenu::Cmd::UnderlyingType AppMenu::Cmd::GetUserData(
    AppMenu::Cmd::UnderlyingType cmd) {
    constexpr UnderlyingType mask = (UnderlyingType(1) << fieldLength_) - 1;
    return ((cmd >> fieldLength_) & mask);
}

constexpr AppMenu::Cmd::UnderlyingType AppMenu::Cmd::Combine(
    AppMenu::Cmd::Kind kind,
    AppMenu::Cmd::UnderlyingType userData) {
    userData <<= fieldLength_;
    return Enum::underlyCast(kind) | userData;
}
