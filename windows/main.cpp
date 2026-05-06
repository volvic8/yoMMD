#include "main.hpp"
#include <array>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "../constant.hpp"
#include "../keyboard.hpp"
#include "../platform_api.hpp"
#include "glm/vec2.hpp"  // IWYU pragma: keep; silence clangd.
#include "menu.hpp"
#include "msgbox.hpp"
#include "sokol_time.h"

namespace {
struct MenuButtonDragState {
    bool tracking = false;
    bool dragging = false;
    POINT startPoint = {};
};

MenuButtonDragState& getMenuButtonDragState() {
    static MenuButtonDragState state;
    return state;
}

std::vector<std::filesystem::path> parseDialogPaths(const wchar_t *buffer) {
    std::vector<std::filesystem::path> paths;
    if (!buffer || !*buffer)
        return paths;

    const std::wstring directory(buffer);
    const wchar_t *next = buffer + directory.size() + 1;
    if (!*next) {
        paths.emplace_back(directory);
        return paths;
    }

    while (*next) {
        paths.emplace_back(std::filesystem::path(directory) / next);
        next += std::wcslen(next) + 1;
    }
    return paths;
}

LRESULT CALLBACK menuButtonWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_NCCREATE) {
        const CREATESTRUCTW *cs = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    const HWND owner = reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto& dragState = getMenuButtonDragState();
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect = {};
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

        const HICON icon =
            reinterpret_cast<HICON>(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
        if (icon) {
            constexpr int iconSize = 20;
            const int x = (rect.right - rect.left - iconSize) / 2;
            const int y = (rect.bottom - rect.top - iconSize) / 2;
            DrawIconEx(hdc, x, y, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        dragState.tracking = true;
        dragState.dragging = false;
        GetCursorPos(&dragState.startPoint);
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (dragState.tracking && owner) {
            POINT pt = {};
            GetCursorPos(&pt);
            const int dx = pt.x - dragState.startPoint.x;
            const int dy = pt.y - dragState.startPoint.y;
            if (!dragState.dragging && (std::abs(dx) > 4 || std::abs(dy) > 4)) {
                dragState.dragging = true;
                PostMessageW(owner, WM_LBUTTONDOWN, MK_LBUTTON, 0);
            }
            if (dragState.dragging) {
                PostMessageW(owner, WM_MOUSEMOVE, MK_LBUTTON, 0);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hwnd)
            ReleaseCapture();
        if (dragState.dragging && owner) {
            PostMessageW(owner, WM_LBUTTONUP, 0, 0);
        } else if (owner) {
            PostMessageW(owner, AppMenu::YOMMD_WM_SHOW_TASKBAR_MENU, 0, WM_LBUTTONUP);
        }
        dragState = {};
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd) {
            dragState = {};
        }
        return 0;
    case WM_RBUTTONUP:
        if (owner) {
            PostMessageW(owner, AppMenu::YOMMD_WM_SHOW_TASKBAR_MENU, 0, WM_RBUTTONUP);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (owner) {
            PostMessageW(owner, AppMenu::YOMMD_WM_ADJUST_SCALE, wParam, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
}  // namespace

AppMain::AppMain() :
    isRunning_(true),
    mouseInteractionEnabled_(true),
    viewDirectionModeEnabled_(false),
    mouseGestureActive_(false),
    sampleCount_(Constant::PreferredSampleCount),
    hwnd_(nullptr),
    menuButtonHwnd_(nullptr),
    menuButtonIcon_(nullptr) {}

AppMain::~AppMain() {
    Terminate();
}

void AppMain::Setup(const CmdArgs& cmdArgs) {
    routine_.ParseConfig(cmdArgs);
    refreshInputFileLists();
    createWindow();
    createMenuButtonWindow();
    createDrawable();
    menu_.Setup();
    routine_.Init();

    // Every initialization must be finished.  Now let's show window.
    ShowWindow(hwnd_, SW_SHOWNORMAL);
}

void AppMain::UpdateDisplay() {
    updateMouseTransparency();
    routine_.Update();
    routine_.Draw();
    repositionMenuButtonWindow();
    swapChain_->Present(1, 0);
    dcompDevice_->Commit();
}

void AppMain::Terminate() {
    routine_.Terminate();
    destroyMenuButtonWindow();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    UnregisterClassW(windowClassName_, GetModuleHandleW(nullptr));
    UnregisterClassW(menuButtonClassName_, GetModuleHandleW(nullptr));
    menu_.Terminate();
}

bool AppMain::IsRunning() const {
    return isRunning_;
}

void AppMain::ChangeScreen(int screenID) {
    const auto r = getMonitorWorkareaFromID(screenID);
    if (!r.has_value()) {
        // It seems the specified monitor is disconnected.  Do nothing.
        return;
    }
    const auto cx = r->right - r->left;
    const auto cy = r->bottom - r->top;
    constexpr UINT uFlags = SWP_SHOWWINDOW | SWP_NOACTIVATE;
    SetWindowPos(hwnd_, HWND_TOPMOST, r->left, r->top, cx, cy, uFlags);
    repositionMenuButtonWindow();
}

void AppMain::SetMouseInteractionEnabled(bool enabled) {
    mouseInteractionEnabled_ = enabled;
    if (!enabled) {
        mouseGestureActive_ = false;
    }
    updateMouseTransparency();
}

bool AppMain::IsMouseInteractionEnabled() const {
    return mouseInteractionEnabled_;
}

void AppMain::SetViewDirectionModeEnabled(bool enabled) {
    viewDirectionModeEnabled_ = enabled;
    routine_.SetViewDirectionModeEnabled(enabled);
}

bool AppMain::IsViewDirectionModeEnabled() const {
    return routine_.IsViewDirectionModeEnabled();
}

Routine& AppMain::GetRoutine() {
    return routine_;
}
const HWND& AppMain::GetWindowHandle() const {
    return hwnd_;
}

RECT AppMain::GetMenuButtonRect() const {
    RECT rect = {};
    if (menuButtonHwnd_) {
        GetWindowRect(menuButtonHwnd_, &rect);
    }
    return rect;
}

const std::vector<std::filesystem::path>& AppMain::GetAvailableModels() const {
    return availableModels_;
}

const std::vector<std::filesystem::path>& AppMain::GetAvailableMotions() const {
    return availableMotions_;
}

sg_environment AppMain::GetSokolEnvironment() const {
    return sg_environment{
        .defaults =
            {
                .color_format = SG_PIXELFORMAT_BGRA8,
                .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                .sample_count = sampleCount_,
            },
        .d3d11 =
            {
                .device = reinterpret_cast<const void *>(d3Device_.Get()),
                .device_context = reinterpret_cast<const void *>(deviceContext_.Get()),
            },
    };
}

sg_swapchain AppMain::GetSokolSwapchain() const {
    const auto size{Context::getWindowSize()};
    sg_d3d11_swapchain d3d11 = {.depth_stencil_view = depthStencilView_.Get()};
    if (sampleCount_ > 1) {
        d3d11.render_view = msaaRenderTargetView_.Get();
        d3d11.resolve_view = renderTargetView_.Get();
    } else {
        d3d11.render_view = renderTargetView_.Get();
        d3d11.resolve_view = nullptr;
    }
    return sg_swapchain{
        .width = static_cast<int>(size.x),
        .height = static_cast<int>(size.y),
        .sample_count = sampleCount_,
        .color_format = SG_PIXELFORMAT_BGRA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .d3d11 = d3d11,
    };
}

glm::vec2 AppMain::GetWindowSize() const {
    RECT rect;
    if (!GetClientRect(hwnd_, &rect)) {
        Err::Log("Failed to get window rect");
        return glm::vec2(1.0f, 1.0f);  // glm::vec2(0, 0) cause error.
    }
    return glm::vec2(rect.right - rect.left, rect.bottom - rect.top);
}

glm::vec2 AppMain::GetDrawableSize() const {
    D3D11_TEXTURE2D_DESC desc;
    renderTarget_->GetDesc(&desc);
    return glm::vec2(desc.Width, desc.Height);
}

int AppMain::GetSampleCount() const {
    return sampleCount_;
}
bool AppMain::IsMenuOpened() const {
    return menu_.IsMenuOpened();
}

void AppMain::createWindow() {
    constexpr DWORD winStyle = WS_POPUP;
    constexpr DWORD winExStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TOPMOST |
                                 WS_EX_LAYERED | WS_EX_TRANSPARENT;

    const HINSTANCE hInstance = GetModuleHandleW(nullptr);
    const HICON appIcon = LoadIconW(hInstance, L"YOMMD_APPICON_ID");
    if (!appIcon) {
        Err::Log("Failed to load application icon.");
    }

    const Config& config = routine_.GetConfig();
    int targetScreenNumber = 0;  // The main monitor ID should be 0.
    if (config.defaultScreenNumber.has_value()) {
        targetScreenNumber = *config.defaultScreenNumber;
    } else if (getAllMonitorHandles().size() > 1) {
        targetScreenNumber = 1;
    }

    RECT rect = {};
    if (auto r = getMonitorWorkareaFromID(targetScreenNumber); r.has_value()) {
        rect = *r;
    } else {
        // It seems the specified screen not found.  Use the main screen as
        // fallback.
        r = getMonitorWorkareaFromID(0);
        if (!r.has_value()) {
            Err::Log("Internal error: failed to get display device");
        }
        rect = *r;
    }

    WNDCLASSEXW wc = {};

    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = windowProc, wc.hInstance = hInstance;
    wc.lpszClassName = windowClassName_;
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        winExStyle, windowClassName_, L"yoMMD", winStyle, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, this);

    if (!hwnd_)
        Err::Exit("Failed to create window.");

    // Don't call ShowWindow() here.  Postpone showing window until
    // MMD model setup finished.
}

void AppMain::createMenuButtonWindow() {
    constexpr int iconSize = 28;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = menuButtonWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = menuButtonClassName_;
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&wc);

    const auto iconData = Resource::getStatusIconData();
    menuButtonIcon_ = CreateIconFromResource(
        const_cast<PBYTE>(iconData.data()), iconData.length(), TRUE, 0x00030000);
    if (!menuButtonIcon_) {
        menuButtonIcon_ = LoadIconW(GetModuleHandleW(nullptr), L"YOMMD_APPICON_ID");
    }

    menuButtonHwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, menuButtonClassName_,
        L"yoMMD Menu Button", WS_POPUP | WS_VISIBLE, 0, 0, iconSize, iconSize, hwnd_, nullptr,
        GetModuleHandleW(nullptr), hwnd_);
    if (!menuButtonHwnd_) {
        Err::Log("Failed to create menu button window.");
        return;
    }

    if (menuButtonIcon_) {
        SendMessageW(menuButtonHwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(menuButtonIcon_));
    }

    repositionMenuButtonWindow();
}

void AppMain::destroyMenuButtonWindow() {
    if (menuButtonHwnd_) {
        DestroyWindow(menuButtonHwnd_);
        menuButtonHwnd_ = nullptr;
    }
    if (menuButtonIcon_) {
        DestroyIcon(menuButtonIcon_);
        menuButtonIcon_ = nullptr;
    }
}

void AppMain::repositionMenuButtonWindow() {
    if (!menuButtonHwnd_ || !hwnd_)
        return;

    RECT mainRect = {};
    if (!GetWindowRect(hwnd_, &mainRect))
        return;

    RECT buttonRect = {};
    if (!GetWindowRect(menuButtonHwnd_, &buttonRect))
        return;

    const auto winSize = GetWindowSize();
    const auto anchor = routine_.GetModelMenuAnchorPosition(winSize);
    const int width = buttonRect.right - buttonRect.left;
    const int height = buttonRect.bottom - buttonRect.top;
    const int margin = 8;
    int x = mainRect.left + static_cast<int>(anchor.x) - width / 2;
    int y = mainRect.bottom - static_cast<int>(anchor.y) - height / 2;
    x = std::clamp(
        x, static_cast<int>(mainRect.left) + margin,
        static_cast<int>(mainRect.right) - width - margin);
    y = std::clamp(
        y, static_cast<int>(mainRect.top) + margin,
        static_cast<int>(mainRect.bottom) - height - margin);
    SetWindowPos(menuButtonHwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void AppMain::createDrawable() {
    if (!hwnd_) {
        Err::Exit("Internal error: createDrawable() must be called after createWindow()");
    }
    constexpr auto failif = [](HRESULT hr, auto&&...errMsg) {
        if (FAILED(hr))
            Err::Exit(std::forward<decltype(errMsg)>(errMsg)...);
    };

    HRESULT hr;

    // Direct3D 11 setups.
    UINT createFlags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, nullptr,
        0,  // Use highest available feature level
        D3D11_SDK_VERSION, d3Device_.GetAddressOf(), nullptr, deviceContext_.GetAddressOf());
    failif(hr, "Failed to create d3d11 device");

    hr = d3Device_.As(&dxgiDevice_);
    failif(hr, "device_.As() failed:", __FILE__, __LINE__);

    hr = CreateDXGIFactory2(
        0, __uuidof(dxFactory_.Get()), reinterpret_cast<void **>(dxFactory_.GetAddressOf()));
    failif(hr, "Failed to create DXGIFactory2");

    const glm::vec2 size(GetWindowSize());
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = static_cast<UINT>(size.x);
    swapChainDesc.Height = static_cast<UINT>(size.y);
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = dxFactory_->CreateSwapChainForComposition(
        dxgiDevice_.Get(), &swapChainDesc, nullptr, swapChain_.GetAddressOf());
    failif(hr, "Failed to create swap chain.");

    hr = swapChain_->GetBuffer(
        0, __uuidof(renderTarget_.Get()),
        reinterpret_cast<void **>(renderTarget_.GetAddressOf()));
    failif(hr, "Failed to get buffer from swap chain.");

    hr = d3Device_->CreateRenderTargetView(
        renderTarget_.Get(), nullptr, renderTargetView_.GetAddressOf());
    failif(hr, "Failed to get render target view.");

    sampleCount_ = determineSampleCount();

    const D3D11_TEXTURE2D_DESC msaaTextureDesc = {
        .Width = static_cast<UINT>(size.x),
        .Height = static_cast<UINT>(size.y),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc =
            {
                .Count = static_cast<UINT>(Constant::PreferredSampleCount),
                .Quality = D3D11_STANDARD_MULTISAMPLE_PATTERN,
            },
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET,
    };

    if (sampleCount_ > 1) {
        hr = d3Device_->CreateTexture2D(
            &msaaTextureDesc, nullptr, msaaRenderTarget_.GetAddressOf());
        failif(hr, "Failed to create msaa render target.");
        hr = d3Device_->CreateRenderTargetView(
            msaaRenderTarget_.Get(), nullptr, msaaRenderTargetView_.GetAddressOf());
        failif(hr, "Failed to get msaa render target view.");
    }

    const D3D11_TEXTURE2D_DESC stencilDesc = {
        .Width = static_cast<UINT>(size.x),
        .Height = static_cast<UINT>(size.y),
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_D24_UNORM_S8_UINT,
        .SampleDesc = sampleCount_ > 1 ? msaaTextureDesc.SampleDesc : swapChainDesc.SampleDesc,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_DEPTH_STENCIL,
    };
    hr = d3Device_->CreateTexture2D(&stencilDesc, nullptr, depthStencilBuffer_.GetAddressOf());
    failif(hr, "Failed to create depth stencil buffer.");

    const D3D11_DEPTH_STENCIL_VIEW_DESC stencilViewDesc = {
        .Format = stencilDesc.Format,
        .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS,
    };
    hr = d3Device_->CreateDepthStencilView(
        reinterpret_cast<ID3D11Resource *>(depthStencilBuffer_.Get()), &stencilViewDesc,
        depthStencilView_.GetAddressOf());
    failif(hr, "Failed to create depth stencil view.");

    // DirectComposition setups.
    hr = DCompositionCreateDevice(
        dxgiDevice_.Get(), __uuidof(dcompDevice_.Get()),
        reinterpret_cast<void **>(dcompDevice_.GetAddressOf()));
    failif(hr, "Failed to create DirectComposition device.");

    hr = dcompDevice_->CreateTargetForHwnd(hwnd_, true, dcompTarget_.GetAddressOf());
    failif(hr, "Failed to DirectComposition render target.");

    hr = dcompDevice_->CreateVisual(dcompVisual_.GetAddressOf());
    failif(hr, "Failed to create DirectComposition visual object.");

    dcompVisual_->SetContent(swapChain_.Get());
    dcompTarget_->SetRoot(dcompVisual_.Get());
}

void AppMain::updateMouseTransparency() {
    if (!hwnd_)
        return;

    LONG exStyle = GetWindowLongW(hwnd_, GWL_EXSTYLE);
    const LONG transparentBit = WS_EX_TRANSPARENT;

    bool shouldBeTransparent = true;
    if (mouseInteractionEnabled_) {
        POINT pt;
        if (GetCursorPos(&pt) && ScreenToClient(hwnd_, &pt)) {
            const auto winSize = GetWindowSize();
            const auto bounds = routine_.GetModelInteractionBounds(winSize);
            const float x = static_cast<float>(pt.x);
            const float y = winSize.y - static_cast<float>(pt.y);
            const bool inside =
                x >= bounds.x && x <= bounds.z && y >= bounds.y && y <= bounds.w;
            shouldBeTransparent = !(inside || mouseGestureActive_ || menu_.IsMenuOpened());
        }
    }

    const bool isTransparent = (exStyle & transparentBit) != 0;
    if (shouldBeTransparent != isTransparent) {
        if (shouldBeTransparent)
            exStyle |= transparentBit;
        else
            exStyle &= ~transparentBit;
        SetWindowLongW(hwnd_, GWL_EXSTYLE, exStyle);
    }
}

// Check the state of multisampling support.
// https://learn.microsoft.com/ja-jp/windows/uwp/gaming/multisampling--multi-sample-anti-aliasing--in-windows-store-apps
int AppMain::determineSampleCount() const {
    if (!d3Device_.Get()) {
        Err::Exit(
            "Internal error: checkMultisamplingSupported():",
            "D3D11 device is not initialized.");
    }

    HRESULT hr;

    // Check if the buffer format DXGI_FORMAT_B8G8R8A8_UNORM supports
    // multisampling.
    UINT formatSupport = 0;
    hr = d3Device_->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &formatSupport);

    if (FAILED(hr)) {
        Err::Exit("CheckFormatSupport() failed.");
    } else if (!((formatSupport & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE) &&
                 (formatSupport & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET))) {
        // DXGI_FORMAT_B8G8R8A8_UNORM doesn't support multisampling on the
        // curent device.
        return 1;
    }

    // TODO: Fallback to smaller sample count when the given sample count is
    // not supported.
    UINT numQualityFlags = 0;
    hr = d3Device_->CheckMultisampleQualityLevels(
        DXGI_FORMAT_B8G8R8A8_UNORM, Constant::PreferredSampleCount, &numQualityFlags);

    if (FAILED(hr)) {
        Err::Exit("CheckMultisampleQualityLevels() failed.");
    } else if (numQualityFlags <= 0) {
        // Multisampling with the given sample count is not supported.
        return 1;
    }

    return Constant::PreferredSampleCount;
}

LRESULT CALLBACK AppMain::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    using This_T = AppMain;
    This_T *pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
        pThis = static_cast<This_T *>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->hwnd_ = hwnd;
    } else {
        pThis = reinterpret_cast<This_T *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessage(uMsg, wParam, lParam);
    } else {
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

LRESULT AppMain::handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        isRunning_ = false;
        return 0;
    case WM_KEYDOWN:  // fallthrough
    case WM_KEYUP: {
        std::optional<Keycode> key;
        switch (wParam) {
        case VK_SHIFT:
            key = Keycode::Shift;
            break;
        }
        if (!key.has_value())
            break;

        if (uMsg == WM_KEYDOWN)
            Keyboard::OnKeyDown(*key);
        else
            Keyboard::OnKeyUp(*key);
        return 0;
    }
    case WM_LBUTTONDOWN:
        mouseGestureActive_ = true;
        routine_.OnGestureBegin();
        return 0;
    case WM_LBUTTONUP:
        mouseGestureActive_ = false;
        routine_.OnGestureEnd();
        updateMouseTransparency();
        return 0;
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
            routine_.OnMouseDragged();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        const float step = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                           static_cast<float>(WHEEL_DELTA) * 0.08f;
        routine_.SetModelScale(routine_.GetModelScale() + step);
        return 0;
    }
    case AppMenu::YOMMD_WM_ADJUST_SCALE: {
        const float step = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                           static_cast<float>(WHEEL_DELTA) * 0.08f;
        routine_.SetModelScale(routine_.GetModelScale() + step);
        return 0;
    }
    case AppMenu::YOMMD_WM_SHOW_TASKBAR_MENU:
        if (const auto msg = LOWORD(lParam);
            !(msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_CONTEXTMENU))
            return 0;
        if (wParam == 0) {
            routine_.OnGestureEnd();
            mouseGestureActive_ = false;
            menu_.ShowMenu();
            updateMouseTransparency();
        } else {
            menu_.ShowTaskbarMenu();
        }
        return 0;
    case WM_RBUTTONDOWN:
        routine_.OnGestureEnd();
        mouseGestureActive_ = false;
        menu_.ShowMenu();
        updateMouseTransparency();
        return 0;
    case AppMenu::YOMMD_WM_OPEN_MODEL_DIALOG:
        if (const auto modelPath = selectModelFile(); modelPath.has_value()) {
            routine_.ChangeModel(*modelPath);
        }
        return 0;
    case AppMenu::YOMMD_WM_OPEN_MOTION_DIALOG: {
        const auto motionPaths = selectMotionFiles();
        if (!motionPaths.empty()) {
            routine_.ChangeMotion(motionPaths);
        }
        return 0;
    }
    case AppMenu::YOMMD_WM_SELECT_MODEL:
        if (const auto index = static_cast<size_t>(wParam); index < availableModels_.size()) {
            routine_.ChangeModel(availableModels_[index]);
        }
        return 0;
    case AppMenu::YOMMD_WM_SELECT_MOTION:
        if (const auto index = static_cast<size_t>(wParam); index < availableMotions_.size()) {
            routine_.ChangeMotion({availableMotions_[index]});
        }
        return 0;
    case AppMenu::YOMMD_WM_SET_VIEW_DIRECTION:
        switch (wParam) {
        case 0:
        case 1:
        case 2:
        case 3:
            routine_.SetModelViewDirection(
                static_cast<float>(wParam) * (std::numbers::pi_v<float> / 2.0f), 0.0f);
            break;
        case 4:
            routine_.SetModelViewDirection(0.0f, std::numbers::pi_v<float> / 2.0f);
            break;
        case 5:
            routine_.SetModelViewDirection(0.0f, -std::numbers::pi_v<float> / 2.0f);
            break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd_, uMsg, wParam, lParam);
}

void AppMain::refreshInputFileLists() {
    namespace fs = std::filesystem;

    const auto appendFiles = [](const fs::path& root, const auto& exts, auto& out) {
        out.clear();
        if (!fs::exists(root))
            return;

        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file())
                continue;

            const auto ext = entry.path().extension().wstring();
            if (std::find(exts.begin(), exts.end(), ext) != exts.end()) {
                out.push_back(fs::weakly_canonical(entry.path()));
            }
        }

        std::sort(out.begin(), out.end());
    };

    appendFiles(
        Path::makeAbsolute("input/model", Path::getWorkingDirectory()),
        std::array<std::wstring, 2>{L".pmd", L".pmx"}, availableModels_);
    appendFiles(
        Path::makeAbsolute("input/motion", Path::getWorkingDirectory()),
        std::array<std::wstring, 1>{L".vmd"}, availableMotions_);
}

std::optional<std::filesystem::path> AppMain::selectModelFile() const {
    std::array<wchar_t, 32768> fileBuffer = {};
    const auto initialDir =
        Path::makeAbsolute("input/model", Path::getWorkingDirectory()).wstring();
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter =
        L"MMD Models (*.pmx;*.pmd)\0*.pmx;*.pmd\0PMX Models (*.pmx)\0*.pmx\0PMD Models "
        L"(*.pmd)\0*.pmd\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn))
        return std::nullopt;
    return std::filesystem::path(fileBuffer.data());
}

std::vector<std::filesystem::path> AppMain::selectMotionFiles() const {
    std::array<wchar_t, 32768> fileBuffer = {};
    const auto initialDir =
        Path::makeAbsolute("input/motion", Path::getWorkingDirectory()).wstring();
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"VMD Motion (*.vmd)\0*.vmd\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer.data();
    ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_ALLOWMULTISELECT;

    if (!GetOpenFileNameW(&ofn))
        return {};
    return parseDialogPaths(fileBuffer.data());
}

namespace Context {
sg_environment getSokolEnvironment() {
    return getAppMain().GetSokolEnvironment();
}
sg_swapchain getSokolSwapchain() {
    return getAppMain().GetSokolSwapchain();
}
glm::vec2 getWindowSize() {
    return getAppMain().GetWindowSize();
}
glm::vec2 getDrawableSize() {
    return getAppMain().GetDrawableSize();
}
glm::vec2 getMousePosition() {
    // The origin of device positions given by WinAPI is top-left of
    // screens/windows.
    POINT pos;
    if (!GetCursorPos(&pos))
        return glm::vec2();

    const HMONITOR curMonitorHandle =
        MonitorFromWindow(getAppMain().GetWindowHandle(), MONITOR_DEFAULTTONULL);
    if (!curMonitorHandle)
        Err::Exit("Internal error: failed to get current monitor handle.");

    MONITORINFO monitorInfo = {.cbSize = sizeof(monitorInfo)};
    GetMonitorInfoW(curMonitorHandle, &monitorInfo);

    RECT wr = {};
    GetClientRect(getAppMain().GetWindowHandle(), &wr);

    // Get the mouse position that relative to the main window.  Note that
    // origin is top-left of window.
    pos.x = pos.x - monitorInfo.rcMonitor.left - wr.left;
    pos.y = pos.y - monitorInfo.rcMonitor.top - wr.top;

    const auto winHeight = wr.bottom - wr.top;
    return glm::vec2(pos.x, winHeight - pos.y);  // Make origin bottom-left.
}
int getSampleCount() {
    return getAppMain().GetSampleCount();
}
bool shouldEmphasizeModel() {
    return false;
}
}  // namespace Context

namespace Dialog {
void messageBox(std::string_view msg) {
    MsgBox::Show(msg);
}
}  // namespace Dialog

std::vector<HMONITOR> getAllMonitorHandles() {
    static const MONITORENUMPROC proc = [](HMONITOR hMonitor, HDC hdc, LPRECT rect,
                                           LPARAM param) -> BOOL {
        (void)hdc, (void)rect;
        std::vector<HMONITOR>& handles = *reinterpret_cast<std::vector<HMONITOR> *>(param);
        handles.push_back(hMonitor);
        return TRUE;
    };
    std::vector<HMONITOR> handles;
    EnumDisplayMonitors(nullptr, nullptr, proc, reinterpret_cast<LPARAM>(&handles));
    return handles;
}
std::optional<HMONITOR> getMonitorHandleFromID(int monitorID) {
    struct Data {
        const int monitorID;
        int curMonitorID;
        std::optional<HMONITOR> handle;
    };
    static const MONITORENUMPROC proc = [](HMONITOR hMonitor, HDC hdc, LPRECT rect,
                                           LPARAM param) -> BOOL {
        (void)hdc, (void)rect;
        Data *data = reinterpret_cast<Data *>(param);
        if (data->curMonitorID == data->monitorID) {
            data->handle = hMonitor;
            return FALSE;
        }
        data->curMonitorID++;
        return TRUE;
    };
    Data data = {.monitorID = monitorID, .curMonitorID = 0, .handle = std::nullopt};
    EnumDisplayMonitors(nullptr, nullptr, proc, reinterpret_cast<LPARAM>(&data));
    return data.handle;
}
std::optional<RECT> getMonitorWorkareaFromID(int monitorID) {
    const auto handle = getMonitorHandleFromID(monitorID);
    if (!handle.has_value())
        return std::nullopt;

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(*handle, &info);
    return info.rcWork;
}

AppMain& getAppMain() {
    static AppMain appMain{};
    return appMain;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)pCmdLine;
    (void)nCmdShow;

    int argc = 0;
    const LPWSTR cmdline = GetCommandLineW();
    const LPWSTR *argv = CommandLineToArgvW(cmdline, &argc);

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(String::wideToMulti<char>(argv[i]));
    }

    const auto cmdArgs = CmdArgs::Parse(args);

    args.clear();

    MsgBox::Init();
    getAppMain().Setup(cmdArgs);

    MSG msg = {};
    constexpr double millSecPerFrame = 1000.0 / Constant::FPS;
    uint64_t timeLastFrame = stm_now();
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!getAppMain().IsRunning())
            break;

        getAppMain().UpdateDisplay();

        const double elapsedMillSec = stm_ms(stm_since(timeLastFrame));
        const auto shouldSleepFor = millSecPerFrame - elapsedMillSec;
        timeLastFrame = stm_now();

        if (shouldSleepFor > 0 && static_cast<DWORD>(shouldSleepFor) > 0) {
            Sleep(static_cast<DWORD>(shouldSleepFor));
        }
    }
    getAppMain().Terminate();
    MsgBox::Terminate();

    return 0;
}
