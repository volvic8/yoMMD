#ifndef WINDOWS_MAIN_HPP_
#define WINDOWS_MAIN_HPP_

#include "winver.hpp"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d3d11.h>
#include <d3d11_2.h>
#include <dcomp.h>
#include <dxgi.h>
#include <filesystem>
#include <wrl.h>
#include "../resources.hpp"
#include "../util.hpp"
#include "../viewer.hpp"
#include "menu.hpp"
#include "sokol_gfx.h"

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

class AppMain {
public:
    AppMain();
    ~AppMain();
    void Setup(const CmdArgs& cmdArgs);
    void UpdateDisplay();
    void Terminate();
    bool IsRunning() const;
    void ChangeScreen(int screenID);
    void SetMouseInteractionEnabled(bool enabled);
    bool IsMouseInteractionEnabled() const;
    void SetViewDirectionModeXEnabled(bool enabled);
    void SetViewDirectionModeYEnabled(bool enabled);
    bool IsViewDirectionModeXEnabled() const;
    bool IsViewDirectionModeYEnabled() const;
    Routine& GetRoutine();
    const HWND& GetWindowHandle() const;
    RECT GetMenuButtonRect() const;
    const std::vector<std::filesystem::path>& GetAvailableModels() const;
    const std::vector<std::filesystem::path>& GetAvailableMotions() const;
    sg_environment GetSokolEnvironment() const;
    sg_swapchain GetSokolSwapchain() const;
    glm::vec2 GetWindowSize() const;
    glm::vec2 GetDrawableSize() const;
    int GetSampleCount() const;
    bool IsMenuOpened() const;

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void createWindow();
    void createMenuButtonWindow();
    void destroyMenuButtonWindow();
    void repositionMenuButtonWindow();
    void createDrawable();
    void updateMouseTransparency();
    int determineSampleCount() const;
    LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void refreshInputFileLists();
    std::optional<std::filesystem::path> selectModelFile() const;
    std::vector<std::filesystem::path> selectMotionFiles() const;

private:
    static constexpr PCWSTR windowClassName_ = L"yoMMD AppMain";
    static constexpr PCWSTR menuButtonClassName_ = L"yoMMD Menu Button";

    bool isRunning_;
    bool mouseInteractionEnabled_;
    bool viewDirectionModeXEnabled_;
    bool viewDirectionModeYEnabled_;
    bool mouseGestureActive_;
    int sampleCount_;
    Routine routine_;
    AppMenu menu_;
    HWND hwnd_;
    HWND menuButtonHwnd_;
    HICON menuButtonIcon_;
    std::vector<std::filesystem::path> availableModels_;
    std::vector<std::filesystem::path> availableMotions_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11Texture2D> renderTarget_;
    ComPtr<ID3D11RenderTargetView> renderTargetView_;
    ComPtr<ID3D11Texture2D> msaaRenderTarget_;
    ComPtr<ID3D11RenderTargetView> msaaRenderTargetView_;
    ComPtr<ID3D11Device> d3Device_;
    ComPtr<ID3D11DeviceContext> deviceContext_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<IDXGIFactory2> dxFactory_;
    ComPtr<ID3D11Texture2D> depthStencilBuffer_;
    ComPtr<ID3D11DepthStencilView> depthStencilView_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
};

AppMain& getAppMain();
std::vector<HMONITOR> getAllMonitorHandles();
std::optional<HMONITOR> getMonitorHandleFromID(int monitorID);
std::optional<RECT> getMonitorWorkareaFromID(int monitorID);

#endif
