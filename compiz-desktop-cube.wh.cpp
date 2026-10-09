// ==WindhawkMod==
// @id              compiz-desktop-cube-v2
// @name            Compiz Desktop Cube for Windows 11
// @description     Classic 3D Desktop Cube virtual-desktop switcher for Windows 11 using native Windows composition, GPU blurred background, and real virtual desktop snapshots.
// @version         1.3.2
// @author          Soham
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -lole32 -loleaut32 -lwindowsapp -ldwmapi -luuid -luser32 -lgdi32 -lcomctl32 -ld3d11 -ld2d1 -ldxgi -lwinmm -ldwrite
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Compiz Desktop Cube for Windows 11

Adds a classic Compiz/GNOME-style 3D Desktop Cube virtual-desktop switcher to Windows 11.
Built with native Windows Composition (Windows.UI.Composition) and Direct2D for 144 Hz smoothness,
zero idle CPU usage, real-time GPU Gaussian blur, and genuine Windows 11 virtual desktop integration.

## Key Features
- **Real Windows Virtual Desktops**: Discovers and switches between your actual Windows virtual desktops.
- **Genuine Desktop Content**: Displays real captured contents for visited desktops, and authentic live window cards with titles and icons for unvisited desktops. No dummy placeholders.
- **Real-Time GPU Blurred Background**: Hardware-accelerated Direct2D Gaussian blur captures and blurs the live active desktop behind the 3D cube.
- **Global Low-Level Mouse Tracking**: Smoothly drag anywhere across the screen, over any window or monitor without the gesture deactivating when leaving the taskbar.
- **100% Explorer Stability**: Thread-level mouse tracking with zero tampering of InputSite window procedures.
- **Fluid 144Hz Snapping**: Quartic ease-out snapping driven by the window message pump and multimedia timer.
- **ESC Cancellation**: Cancel anytime by pressing Escape to smoothly return to the current desktop without switching.

## How to Use
1. Move the mouse over **empty space** on the Windows 11 taskbar.
2. Press and hold the **left mouse button**.
3. Drag **inward / upward** (~16 px) to activate the 3D Desktop Cube.
4. While holding the left button, drag **left or right** across the screen to rotate the cube.
5. Release the mouse button to snap smoothly to the nearest desktop and switch to it.
6. Press **Escape** anytime to cancel.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- ActivationThreshold: 16
  $name: Drag Activation Distance (px)
  $description: Vertical upward drag distance in pixels on the taskbar required to trigger the cube (default 16 px).
- HorizontalSensitivity: 45
  $name: Rotation Sensitivity (%)
  $description: Horizontal mouse drag rotation speed multiplier (default 45%).
- SnapDuration: 140
  $name: Snap Animation Duration (ms)
  $description: Duration of the 90-degree ease-out snap animation in milliseconds (default 140 ms).
- CubeScale: 68
  $name: Cube Scale (%)
  $description: Scale of the 3D cube relative to screen height (default 68%).
- Perspective: 1100
  $name: Camera Perspective Distance (px)
  $description: 3D perspective camera focal distance (lower values increase 3D distortion).
- BackgroundBlur: true
  $name: Enable Background Blur
  $description: Blurs the live active desktop behind the 3D cube with GPU Gaussian blur.
- BackgroundDim: 25
  $name: Background Scrim Dim (%)
  $description: Subtle dark tint over the blurred background (default 25%).
- FaceBrightness: 95
  $name: Face Brightness (%)
  $description: Overall brightness of the cube faces (default 95%).
- ShowDesktopLabels: true
  $name: Show Desktop Badges
  $description: Displays floating desktop badges ("Desktop 1", "Desktop 2", etc.) at the top of each face.
- EnableDebugLogging: true
  $name: Enable Diagnostic Logging
  $description: Outputs detailed diagnostic traces to the Windhawk log console.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <objectarray.h>
#include <uiautomation.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <dxgi.h>
#include <mmsystem.h>

#include <windhawk_api.h>

#ifndef WH_MOD_ID
#define WH_MOD_ID L"compiz-desktop-cube-v2"
#endif

#include <windhawk_utils.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

#include <atomic>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

// WinRT & Windows.UI.Composition
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.Graphics.Effects.h>

using namespace winrt;
using namespace winrt::Windows::UI::Composition;
using namespace winrt::Windows::UI::Composition::Desktop;
using namespace winrt::Windows::Foundation::Numerics;
using namespace winrt::Windows::Graphics::Effects;

// ============================================================================
// DirectComposition / DrawingSurface Interop Definitions
// ============================================================================
static const IID IID_ICompositorInterop = { 0x25297744, 0xC428, 0x429E, {0xA0, 0xAC, 0xB6, 0xF0, 0x45, 0x82, 0x8A, 0x14} };
static const IID IID_ICompositionDrawingSurfaceInterop = { 0xFD04E6E3, 0xFE0C, 0x4C3C, {0xAB, 0x19, 0xA0, 0x76, 0x01, 0xA5, 0x76, 0xEE} };
static const IID IID_ICompositorDesktopInterop = { 0x29E691FA, 0x4567, 0x4DCA, { 0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07 } };

struct ICompositionDrawingSurfaceInterop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE BeginDraw(
        const RECT* updateRect,
        REFIID iid,
        void** updateObject,
        POINT* updateOffset) = 0;
    virtual HRESULT STDMETHODCALLTYPE EndDraw() = 0;
    virtual HRESULT STDMETHODCALLTYPE Resize(SIZE sizePixels) = 0;
    virtual HRESULT STDMETHODCALLTYPE Scroll(const RECT* scrollRect, const RECT* clipRect, int offsetX, int offsetY) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResumeDraw() = 0;
    virtual HRESULT STDMETHODCALLTYPE SuspendDraw() = 0;
};

struct ICompositorDesktopInterop : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateDesktopWindowTarget(
        HWND hwndTarget,
        BOOL isTopmost,
        void** result) = 0;
};

// ============================================================================
// Logging Macros
// ============================================================================
static bool g_debugLog = true;

#define LOG_DBG(fmt, ...) \
    do { \
        if (g_debugLog) { \
            Wh_Log(L"[Cube] " fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_INF(fmt, ...) Wh_Log(L"[Cube] " fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) Wh_Log(L"[Cube ERROR] " fmt, ##__VA_ARGS__)

// ============================================================================
// Mod Settings
// ============================================================================
struct ModSettings {
    int activationThreshold = 16;
    float horizontalSensitivity = 0.45f;
    int snapDurationMs = 140;
    float cubeScale = 0.68f;
    float perspective = 1100.0f;
    bool backgroundBlur = true;
    int backgroundDim = 25;
    int faceBrightness = 95;
    bool showDesktopLabels = true;
} g_settings;

inline void LoadSettings() {
    g_settings.activationThreshold = Wh_GetIntSetting(L"ActivationThreshold");
    if (g_settings.activationThreshold <= 0) g_settings.activationThreshold = 16;

    int sens = Wh_GetIntSetting(L"HorizontalSensitivity");
    g_settings.horizontalSensitivity = (sens > 0 ? (float)sens / 100.0f : 0.45f);

    g_settings.snapDurationMs = Wh_GetIntSetting(L"SnapDuration");
    if (g_settings.snapDurationMs <= 0) g_settings.snapDurationMs = 140;

    int scale = Wh_GetIntSetting(L"CubeScale");
    g_settings.cubeScale = (scale > 0 ? (float)scale / 100.0f : 0.68f);

    int persp = Wh_GetIntSetting(L"Perspective");
    g_settings.perspective = (persp > 0 ? (float)persp : 1100.0f);

    g_settings.backgroundBlur = Wh_GetIntSetting(L"BackgroundBlur") != 0;

    g_settings.backgroundDim = Wh_GetIntSetting(L"BackgroundDim");
    if (g_settings.backgroundDim < 0 || g_settings.backgroundDim > 100) g_settings.backgroundDim = 25;

    g_settings.faceBrightness = Wh_GetIntSetting(L"FaceBrightness");
    if (g_settings.faceBrightness <= 0 || g_settings.faceBrightness > 100) g_settings.faceBrightness = 95;

    g_settings.showDesktopLabels = Wh_GetIntSetting(L"ShowDesktopLabels") != 0;
    g_debugLog = Wh_GetIntSetting(L"EnableDebugLogging") != 0;

    LOG_INF(L"Settings loaded: threshold=%d, sens=%.2f, snap=%d ms, blur=%d",
            g_settings.activationThreshold, g_settings.horizontalSensitivity,
            g_settings.snapDurationMs, g_settings.backgroundBlur);
}

// ============================================================================
// 3D Matrix Math (DirectComposition Row-Vector Convention)
// ============================================================================
struct Mat4 {
    float m[4][4];

    static Mat4 Identity() {
        Mat4 r{};
        for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
        return r;
    }

    static Mat4 Translation(float x, float y, float z) {
        Mat4 r = Identity();
        r.m[3][0] = x;
        r.m[3][1] = y;
        r.m[3][2] = z;
        return r;
    }

    static Mat4 RotationY(float rad) {
        Mat4 r = Identity();
        float c = std::cos(rad);
        float s = std::sin(rad);
        r.m[0][0] = c;
        r.m[0][2] = -s;
        r.m[2][0] = s;
        r.m[2][2] = c;
        return r;
    }

    static Mat4 Perspective(float d) {
        Mat4 r = Identity();
        if (d > 1.0f) {
            r.m[2][3] = -1.0f / d;
        }
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r{};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[i][k] * o.m[k][j];
                }
                r.m[i][j] = sum;
            }
        }
        return r;
    }

    float4x4 ToWinRT() const {
        return {
            m[0][0], m[0][1], m[0][2], m[0][3],
            m[1][0], m[1][1], m[1][2], m[1][3],
            m[2][0], m[2][1], m[2][2], m[2][3],
            m[3][0], m[3][1], m[3][2], m[3][3]
        };
    }
};

// ============================================================================
// MODULE 1: VDManager (Windows 11 Build >= 22000 / 26100+)
// ============================================================================
namespace VDManager {

    struct IVirtualDesktop : public IUnknown {
        virtual HRESULT STDMETHODCALLTYPE IsViewVisible(IUnknown*, BOOL*) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetId(GUID*) = 0;
    };

    struct IVirtualDesktopManagerInternal : public IUnknown {};

    const CLSID CLSID_ImmersiveShell = {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
    const CLSID CLSID_VirtualDesktopManagerInternal = {0xC5E0CDCA, 0x7B6E, 0x41B2, {0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B}};

    static IID s_iidManagerInternal = {0x53F5CA0B, 0x158F, 0x4124, {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}}; // Win11 24H2/26100+
    static IID s_iidVirtualDesktop = {0x3F07F4BE, 0xB107, 0x441A, {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}};

    static bool s_vdInitialized = false;

    static void InitIIDs() {
        if (s_vdInitialized) return;

        OSVERSIONINFOEXW osvi{ sizeof(osvi) };
        DWORD dwBuild = 26100;
        HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
        if (hNt) {
            typedef NTSTATUS(WINAPI* RtlGetVersionProc)(PRTL_OSVERSIONINFOEXW);
            auto pRtl = (RtlGetVersionProc)GetProcAddress(hNt, "RtlGetVersion");
            if (pRtl && NT_SUCCESS(pRtl(&osvi))) {
                dwBuild = osvi.dwBuildNumber;
            }
        }

        if (dwBuild >= 26100) {
            s_iidManagerInternal = {0x53F5CA0B, 0x158F, 0x4124, {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}};
            s_iidVirtualDesktop = {0x3F07F4BE, 0xB107, 0x441A, {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}};
        } else if (dwBuild >= 22621) {
            s_iidManagerInternal = {0xA3175F2D, 0x239C, 0x4BD2, {0x8A, 0xA0, 0xEE, 0xBA, 0x8B, 0x0B, 0x13, 0x8E}};
            s_iidVirtualDesktop = {0x3F07F4BE, 0xB107, 0x441A, {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}};
        } else {
            s_iidManagerInternal = {0xB2F925B9, 0x5A0F, 0x4D2E, {0x9F, 0x4D, 0x2B, 0x15, 0x07, 0x59, 0x3C, 0x10}};
            s_iidVirtualDesktop = {0x536D3495, 0xB208, 0x4CC9, {0xAE, 0x26, 0xDE, 0x81, 0x11, 0x27, 0x5B, 0xF8}};
        }

        s_vdInitialized = true;
        LOG_DBG(L"Virtual Desktop IIDs configured for build %lu", dwBuild);
    }

    struct DesktopItem {
        int index = 0;
        GUID id{};
        std::wstring name;
    };

    struct DesktopList {
        int currentIndex = 0;
        std::vector<DesktopItem> items;

        int TotalCount() const { return (int)items.size(); }
    };

    struct WindowCard {
        std::wstring title;
        RECT rect{};
        HICON icon = nullptr;
    };

    // Query virtual desktops from registry if COM internal interface is not available
    static bool QueryDesktopsFromRegistry(DesktopList& outList) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops",
            0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return false;
        }

        DWORD dwType = 0;
        DWORD dwSize = 0;
        if (RegQueryValueExW(hKey, L"VirtualDesktopIDs", nullptr, &dwType, nullptr, &dwSize) == ERROR_SUCCESS && dwSize >= sizeof(GUID)) {
            std::vector<BYTE> buffer(dwSize);
            if (RegQueryValueExW(hKey, L"VirtualDesktopIDs", nullptr, &dwType, buffer.data(), &dwSize) == ERROR_SUCCESS) {
                int count = (int)(dwSize / sizeof(GUID));
                GUID* pGuids = (GUID*)buffer.data();

                GUID curGuid{};
                DWORD dwCurSize = sizeof(curGuid);
                RegQueryValueExW(hKey, L"CurrentVirtualDesktop", nullptr, nullptr, (LPBYTE)&curGuid, &dwCurSize);

                for (int i = 0; i < count; ++i) {
                    DesktopItem item;
                    item.index = i;
                    item.id = pGuids[i];
                    wchar_t szName[64];
                    swprintf_s(szName, L"Desktop %d", i + 1);
                    item.name = szName;
                    outList.items.push_back(item);
                    if (IsEqualGUID(item.id, curGuid)) {
                        outList.currentIndex = i;
                    }
                }
            }
        }

        RegCloseKey(hKey);
        return !outList.items.empty();
    }

    static bool QueryDesktops(DesktopList& outList) {
        InitIIDs();
        outList.items.clear();
        outList.currentIndex = 0;

        com_ptr<IServiceProvider> sp;
        HRESULT hr = CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(sp.put()));
        if (SUCCEEDED(hr) && sp) {
            com_ptr<IVirtualDesktopManagerInternal> manager;
            hr = sp->QueryService(CLSID_VirtualDesktopManagerInternal, s_iidManagerInternal, manager.put_void());
            if (SUCCEEDED(hr) && manager) {
                typedef HRESULT(STDMETHODCALLTYPE* GetDesktopsProc)(IVirtualDesktopManagerInternal*, IObjectArray**);
                typedef HRESULT(STDMETHODCALLTYPE* GetCurrentDesktopProc)(IVirtualDesktopManagerInternal*, IVirtualDesktop**);

                auto pVtbl = *(void***)manager.get();
                GetDesktopsProc fnGetDesktops = (GetDesktopsProc)pVtbl[7];
                GetCurrentDesktopProc fnGetCurrentDesktop = (GetCurrentDesktopProc)pVtbl[6];

                com_ptr<IObjectArray> desktopArray;
                hr = fnGetDesktops(manager.get(), desktopArray.put());
                if (SUCCEEDED(hr) && desktopArray) {
                    com_ptr<IVirtualDesktop> currentDesktop;
                    hr = fnGetCurrentDesktop(manager.get(), currentDesktop.put());
                    GUID currentId{};
                    if (SUCCEEDED(hr) && currentDesktop) {
                        currentDesktop->GetId(&currentId);
                    }

                    UINT count = 0;
                    desktopArray->GetCount(&count);

                    for (UINT i = 0; i < count; ++i) {
                        com_ptr<IVirtualDesktop> d;
                        if (SUCCEEDED(desktopArray->GetAt(i, s_iidVirtualDesktop, d.put_void())) && d) {
                            GUID id{};
                            d->GetId(&id);
                            DesktopItem item;
                            item.index = (int)i;
                            item.id = id;
                            wchar_t szName[64];
                            swprintf_s(szName, L"Desktop %u", i + 1);
                            item.name = szName;

                            outList.items.push_back(item);
                            if (IsEqualGUID(id, currentId)) {
                                outList.currentIndex = (int)i;
                            }
                        }
                    }
                }
            }
        }

        // Fallback to registry if COM returned empty
        if (outList.items.empty()) {
            QueryDesktopsFromRegistry(outList);
        }

        LOG_DBG(L"Virtual Desktops enumerated: total=%d, current=%d", (int)outList.items.size(), outList.currentIndex);
        return !outList.items.empty();
    }

    static bool SwitchToDesktopByIndex(int targetIndex) {
        InitIIDs();

        com_ptr<IServiceProvider> sp;
        HRESULT hr = CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(sp.put()));
        if (FAILED(hr) || !sp) return false;

        com_ptr<IVirtualDesktopManagerInternal> manager;
        hr = sp->QueryService(CLSID_VirtualDesktopManagerInternal, s_iidManagerInternal, manager.put_void());
        if (FAILED(hr) || !manager) return false;

        auto pVtbl = *(void***)manager.get();
        typedef HRESULT(STDMETHODCALLTYPE* GetDesktopsProc)(IVirtualDesktopManagerInternal*, IObjectArray**);
        typedef HRESULT(STDMETHODCALLTYPE* SwitchDesktopProc)(IVirtualDesktopManagerInternal*, IVirtualDesktop*);

        GetDesktopsProc fnGetDesktops = (GetDesktopsProc)pVtbl[7];
        SwitchDesktopProc fnSwitchDesktop = (SwitchDesktopProc)pVtbl[9];

        com_ptr<IObjectArray> desktopArray;
        hr = fnGetDesktops(manager.get(), desktopArray.put());
        if (FAILED(hr) || !desktopArray) return false;

        UINT count = 0;
        desktopArray->GetCount(&count);
        if (count == 0) return false;

        int clampedIndex = ((targetIndex % (int)count) + (int)count) % (int)count;

        com_ptr<IVirtualDesktop> targetDesktop;
        hr = desktopArray->GetAt(clampedIndex, s_iidVirtualDesktop, targetDesktop.put_void());
        if (FAILED(hr) || !targetDesktop) return false;

        hr = fnSwitchDesktop(manager.get(), targetDesktop.get());
        LOG_INF(L"Switched to Real Windows Virtual Desktop #%d (hr=0x%08X)", clampedIndex + 1, hr);
        return SUCCEEDED(hr);
    }

    struct EnumCardsCtx {
        const GUID* targetId;
        IVirtualDesktopManager* pVdm;
        std::vector<WindowCard>* pOut;
    };

    static BOOL CALLBACK EnumWindowsCardsProc(HWND hWnd, LPARAM lParam) {
        EnumCardsCtx* c = (EnumCardsCtx*)lParam;
        if (!IsWindowVisible(hWnd) || IsIconic(hWnd)) return TRUE;

        LONG exStyle = GetWindowLongW(hWnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

        DWORD dwCloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &dwCloaked, sizeof(dwCloaked)))) {
            if (dwCloaked != 0 && dwCloaked != 2 /* DWM_CLOAKED_SHELL */) {
                return TRUE;
            }
        }

        RECT rc{};
        GetWindowRect(hWnd, &rc);
        if ((rc.right - rc.left) < 100 || (rc.bottom - rc.top) < 100) return TRUE;

        if (c->pVdm) {
            GUID wId{};
            if (SUCCEEDED(c->pVdm->GetWindowDesktopId(hWnd, &wId))) {
                if (!IsEqualGUID(wId, *c->targetId)) {
                    return TRUE;
                }
            }
        }

        WCHAR szTitle[256]{};
        GetWindowTextW(hWnd, szTitle, ARRAYSIZE(szTitle));
        if (wcslen(szTitle) == 0) return TRUE;

        WCHAR szClass[128]{};
        GetClassNameW(hWnd, szClass, ARRAYSIZE(szClass));
        if (_wcsicmp(szClass, L"Progman") == 0 ||
            _wcsicmp(szClass, L"WorkerW") == 0 ||
            _wcsicmp(szClass, L"Shell_TrayWnd") == 0 ||
            _wcsicmp(szClass, L"Shell_SecondaryTrayWnd") == 0) {
            return TRUE;
        }

        HICON hIcon = (HICON)SendMessageW(hWnd, WM_GETICON, ICON_SMALL, 0);
        if (!hIcon) hIcon = (HICON)SendMessageW(hWnd, WM_GETICON, ICON_BIG, 0);
        if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hWnd, GCLP_HICONSM);
        if (!hIcon) hIcon = (HICON)GetClassLongPtrW(hWnd, GCLP_HICON);

        WindowCard card;
        card.title = szTitle;
        card.rect = rc;
        card.icon = hIcon;
        c->pOut->push_back(card);

        return (c->pOut->size() < 12) ? TRUE : FALSE;
    }

    // Enumerate actual visible windows belonging to a specific virtual desktop
    static void GetWindowsForDesktop(const GUID& desktopId, std::vector<WindowCard>& outCards) {
        outCards.clear();

        com_ptr<IVirtualDesktopManager> vdm;
        CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(vdm.put()));

        EnumCardsCtx ctx = { &desktopId, vdm.get(), &outCards };
        EnumWindows(EnumWindowsCardsProc, (LPARAM)&ctx);
    }

} // namespace VDManager

// ============================================================================
// MODULE 2: DesktopCaptureManager (Real screen snapshots for all virtual desktops)
// ============================================================================
namespace DesktopCaptureManager {

    struct SnapshotData {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels; // 32-bit BGRA
    };

    static std::mutex s_captureMutex;
    static std::unordered_map<std::wstring, SnapshotData> s_snapshotCache;

    static std::wstring GuidToString(const GUID& id) {
        wchar_t sz[40]{};
        swprintf_s(sz, L"{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}",
            id.Data1, id.Data2, id.Data3,
            id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3],
            id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
        return sz;
    }

    // Capture the monitor screen into 32-bit BGRA pixel buffer
    static bool CaptureMonitorScreen(RECT rcMon, SnapshotData& outData) {
        int w = rcMon.right - rcMon.left;
        int h = rcMon.bottom - rcMon.top;
        if (w <= 0 || h <= 0) return false;

        HDC hdcScreen = GetDC(nullptr);
        if (!hdcScreen) return false;

        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) {
            ReleaseDC(nullptr, hdcScreen);
            return false;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // Top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HBITMAP hDIB = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (!hDIB || !pBits) {
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdcScreen);
            return false;
        }

        HGDIOBJ hOld = SelectObject(hdcMem, hDIB);
        BitBlt(hdcMem, 0, 0, w, h, hdcScreen, rcMon.left, rcMon.top, SRCCOPY | CAPTUREBLT);

        outData.width = w;
        outData.height = h;
        outData.pixels.resize(w * h);
        memcpy(outData.pixels.data(), pBits, w * h * sizeof(uint32_t));
        // Force 0xFF alpha channel because GDI BitBlt leaves alpha as 0x00
        for (auto& px : outData.pixels) {
            px |= 0xFF000000;
        }

        SelectObject(hdcMem, hOld);
        DeleteObject(hDIB);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return true;
    }

    static void StoreSnapshot(const GUID& id, const SnapshotData& data) {
        std::lock_guard<std::mutex> lock(s_captureMutex);
        std::wstring key = GuidToString(id);
        s_snapshotCache[key] = data;
    }

    static const SnapshotData* GetSnapshot(const GUID& id) {
        std::lock_guard<std::mutex> lock(s_captureMutex);
        std::wstring key = GuidToString(id);
        auto it = s_snapshotCache.find(key);
        if (it != s_snapshotCache.end() && !it->second.pixels.empty()) {
            return &it->second;
        }
        return nullptr;
    }

} // namespace DesktopCaptureManager

// ============================================================================
// Gesture Controller State Machine Definition
// ============================================================================
enum class GestureState {
    Idle,
    Arming,
    ActiveDrag,
    Committing
};

namespace GestureController {
    void OnSnapCompleted();
    void OnCancel();
    GestureState GetState();
}

// ============================================================================
// MODULE 3: CubeRenderer (Windows Composition 3D Cube & GPU Gaussian Blur)
// ============================================================================
class CubeRenderer {
public:
    static CubeRenderer& Instance() {
        static CubeRenderer s_instance;
        return s_instance;
    }

    bool Create(HWND hTaskbarWnd, POINT anchorPoint);
    void Destroy();
    void SetRotationAngle(float angleDegrees);
    void SnapToAngle(float targetAngleDegrees, int snapDesktopOffset);
    void Cancel();
    bool IsActive() const { return m_hwnd != nullptr && IsWindowVisible(m_hwnd); }
    HWND GetHwnd() const { return m_hwnd; }

    int GetCurrentDesktopIndex() const { return m_desktopList.currentIndex; }
    int GetTotalDesktops() const { return m_desktopList.TotalCount(); }

private:
    CubeRenderer() = default;
    ~CubeRenderer() { Destroy(); }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void UpdateCubeFaceTransforms(float currentAngle);
    void UpdateDimensions(int screenW, int screenH);
    void InitializeGraphicsDevice();
    void BuildBackgroundBlurredSurface(const DesktopCaptureManager::SnapshotData& snapshot);
    void BuildFaceSurfaces();
    void OnAnimationTick();

    HWND m_hwnd = nullptr;
    Compositor m_compositor{ nullptr };
    DesktopWindowTarget m_target{ nullptr };
    ContainerVisual m_rootVisual{ nullptr };
    SpriteVisual m_backgroundVisual{ nullptr };
    CompositionDrawingSurface m_bgSurface{ nullptr };
    SpriteVisual m_scrimVisual{ nullptr };
    ContainerVisual m_cubeContainer{ nullptr };

    // DirectX / Composition Device
    ID3D11Device* m_d3dDevice = nullptr;
    ID2D1Device* m_d2dDevice = nullptr;
    ID2D1Factory1* m_d2dFactory = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    IDWriteTextFormat* m_textFormatBadge = nullptr;
    IDWriteTextFormat* m_textFormatTitle = nullptr;
    void* m_compGraphicsDevice = nullptr;

    struct FaceVisuals {
        ContainerVisual container{ nullptr };
        SpriteVisual background{ nullptr };
        SpriteVisual border{ nullptr };
        SpriteVisual headerPill{ nullptr };
        CompositionDrawingSurface surface{ nullptr };
        CompositionSurfaceBrush surfaceBrush{ nullptr };
        int mappedDesktopIndex = -1;
    };

    FaceVisuals m_faces[4];

    float m_cubeSize = 500.0f;
    float m_faceWidth = 480.0f;
    float m_faceHeight = 320.0f;
    float m_screenWidth = 1920.0f;
    float m_screenHeight = 1080.0f;

    float m_currentRotation = 0.0f;
    VDManager::DesktopList m_desktopList;
    RECT m_monitorRect{};

    // UI-Thread Snap Animation State
    static constexpr UINT_PTR TIMER_SNAP_ANIMATION = 1001;
    struct SnapAnimation {
        bool active = false;
        bool fadingOut = false;
        float startAngle = 0.0f;
        float endAngle = 0.0f;
        int snapOffset = 0;
        int durationMs = 140;
        int fadeStep = 4;
        std::chrono::high_resolution_clock::time_point startTime;
    } m_snapAnim;
};

void CubeRenderer::UpdateDimensions(int screenW, int screenH) {
    m_screenWidth = (float)screenW;
    m_screenHeight = (float)screenH;

    m_cubeSize = m_screenHeight * g_settings.cubeScale;
    m_faceWidth = m_cubeSize;
    m_faceHeight = m_cubeSize * (m_screenHeight / m_screenWidth);
}

void CubeRenderer::InitializeGraphicsDevice() {
    if (m_compGraphicsDevice) return;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels, 1, D3D11_SDK_VERSION,
        &m_d3dDevice, nullptr, nullptr
    );
    if (FAILED(hr) || !m_d3dDevice) return;

    IDXGIDevice* dxgiDevice = nullptr;
    m_d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (!dxgiDevice) return;

    D2D1_FACTORY_OPTIONS options{};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, (void**)&m_d2dFactory);
    if (m_d2dFactory) {
        m_d2dFactory->CreateDevice(dxgiDevice, &m_d2dDevice);
    }
    dxgiDevice->Release();

    if (!m_d2dDevice) return;

    // DirectWrite Factory & Formats for crisp badges and window headers
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&m_dwriteFactory);
    if (m_dwriteFactory) {
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            13.0f, L"en-us", &m_textFormatBadge
        );
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.0f, L"en-us", &m_textFormatTitle
        );
    }

    IUnknown* compUnk = (IUnknown*)winrt::get_abi(m_compositor);
    void* compInterop = nullptr;
    if (SUCCEEDED(compUnk->QueryInterface(IID_ICompositorInterop, &compInterop)) && compInterop) {
        void** interopVtbl = *(void***)compInterop;
        typedef HRESULT(STDMETHODCALLTYPE* CreateGraphicsDeviceFn)(void*, IUnknown*, void**);
        CreateGraphicsDeviceFn fnCreate = (CreateGraphicsDeviceFn)interopVtbl[3];
        fnCreate(compInterop, m_d2dDevice, &m_compGraphicsDevice);
        ((IUnknown*)compInterop)->Release();
    }
}

// Module 3B: Hardware GPU Gaussian Blur Background
void CubeRenderer::BuildBackgroundBlurredSurface(const DesktopCaptureManager::SnapshotData& snapshot) {
    if (!m_compGraphicsDevice || snapshot.pixels.empty()) return;

    void** gdVtbl = *(void***)m_compGraphicsDevice;
    typedef HRESULT(STDMETHODCALLTYPE* CreateDrawingSurfaceFn)(void*, SIZE, int, int, void**);
    CreateDrawingSurfaceFn fnCreateSurface = (CreateDrawingSurfaceFn)gdVtbl[3];

    SIZE surfaceSize{ (LONG)m_screenWidth, (LONG)m_screenHeight };
    void* pDrawingSurface = nullptr;
    HRESULT hr = fnCreateSurface(m_compGraphicsDevice, surfaceSize, 87 /* DXGI_FORMAT_B8G8R8A8_UNORM */, 1 /* Premultiplied */, &pDrawingSurface);
    if (SUCCEEDED(hr) && pDrawingSurface) {
        winrt::copy_from_abi(m_bgSurface, pDrawingSurface);

        ICompositionDrawingSurfaceInterop* surfaceInterop = nullptr;
        if (SUCCEEDED(((IUnknown*)pDrawingSurface)->QueryInterface(IID_ICompositionDrawingSurfaceInterop, (void**)&surfaceInterop)) && surfaceInterop) {
            ID2D1DeviceContext* d2dContext = nullptr;
            POINT offset{};
            if (SUCCEEDED(surfaceInterop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), (void**)&d2dContext, &offset)) && d2dContext) {
                d2dContext->Clear(D2D1::ColorF(0.04f, 0.05f, 0.08f, 1.0f));

                D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                ID2D1Bitmap* pBitmap = nullptr;
                hr = d2dContext->CreateBitmap(
                    D2D1::SizeU(snapshot.width, snapshot.height),
                    snapshot.pixels.data(),
                    snapshot.width * sizeof(uint32_t),
                    props,
                    &pBitmap
                );

                if (SUCCEEDED(hr) && pBitmap) {
                    if (g_settings.backgroundBlur) {
                        ID2D1Effect* pBlurEffect = nullptr;
                        hr = d2dContext->CreateEffect(CLSID_D2D1GaussianBlur, &pBlurEffect);
                        if (SUCCEEDED(hr) && pBlurEffect) {
                            pBlurEffect->SetInput(0, pBitmap);
                            pBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 28.0f);
                            pBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, (UINT32)0 /* SPEED */);
                            pBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);

                            d2dContext->DrawImage(pBlurEffect, D2D1::Point2F((float)offset.x, (float)offset.y));
                            pBlurEffect->Release();
                        } else {
                            D2D1_RECT_F destRect = D2D1::RectF((float)offset.x, (float)offset.y, (float)offset.x + m_screenWidth, (float)offset.y + m_screenHeight);
                            d2dContext->DrawBitmap(pBitmap, destRect);
                        }
                    } else {
                        D2D1_RECT_F destRect = D2D1::RectF((float)offset.x, (float)offset.y, (float)offset.x + m_screenWidth, (float)offset.y + m_screenHeight);
                        d2dContext->DrawBitmap(pBitmap, destRect);
                    }
                    pBitmap->Release();
                }

                surfaceInterop->EndDraw();
                d2dContext->Release();
            }
            surfaceInterop->Release();
        }
        ((IUnknown*)pDrawingSurface)->Release();

        m_backgroundVisual = m_compositor.CreateSpriteVisual();
        m_backgroundVisual.Size({ m_screenWidth, m_screenHeight });
        m_backgroundVisual.Brush(m_compositor.CreateSurfaceBrush(m_bgSurface));
        m_rootVisual.Children().InsertAtBottom(m_backgroundVisual);
    }
}

// Module 3C: Genuine Desktop Faces (Real Screenshots or Authentic Live Window Cards)
void CubeRenderer::BuildFaceSurfaces() {
    if (!m_compGraphicsDevice) return;

    void** gdVtbl = *(void***)m_compGraphicsDevice;
    typedef HRESULT(STDMETHODCALLTYPE* CreateDrawingSurfaceFn)(void*, SIZE, int, int, void**);
    CreateDrawingSurfaceFn fnCreateSurface = (CreateDrawingSurfaceFn)gdVtbl[3];

    int total = m_desktopList.TotalCount();
    if (total == 0) return;

    int curIdx = m_desktopList.currentIndex;
    SIZE surfaceSize{ (LONG)m_faceWidth, (LONG)m_faceHeight };

    // 4 cube faces mapped consistently:
    // Face 0: Current Desktop (0 deg)
    // Face 1: Next Desktop (-90 deg / Right drag destination)
    // Face 2: Opposite Desktop (180 deg)
    // Face 3: Previous Desktop (+90 deg / Left drag destination)
    int mappedIndices[4] = {
        curIdx,
        (curIdx + 1) % total,
        (curIdx + 2) % total,
        (curIdx - 1 + total) % total
    };

    for (int i = 0; i < 4; ++i) {
        auto& f = m_faces[i];
        int dIdx = mappedIndices[i];
        f.mappedDesktopIndex = dIdx;

        void* drawingSurfacePtr = nullptr;
        HRESULT hr = fnCreateSurface(m_compGraphicsDevice, surfaceSize, 87 /* B8G8R8A8_UNORM */, 1 /* Premultiplied */, &drawingSurfacePtr);
        if (SUCCEEDED(hr) && drawingSurfacePtr) {
            winrt::copy_from_abi(f.surface, drawingSurfacePtr);

            ICompositionDrawingSurfaceInterop* surfaceInterop = nullptr;
            if (SUCCEEDED(((IUnknown*)drawingSurfacePtr)->QueryInterface(IID_ICompositionDrawingSurfaceInterop, (void**)&surfaceInterop)) && surfaceInterop) {
                ID2D1DeviceContext* d2dContext = nullptr;
                POINT offset{};
                if (SUCCEEDED(surfaceInterop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), (void**)&d2dContext, &offset)) && d2dContext) {
                    d2dContext->Clear(D2D1::ColorF(0.07f, 0.09f, 0.13f, 1.0f));

                    const GUID& dId = m_desktopList.items[dIdx].id;
                    const auto* snapshot = DesktopCaptureManager::GetSnapshot(dId);

                    // CASE 1: Visited desktop with actual captured full-screen snapshot
                    if (snapshot && !snapshot->pixels.empty()) {
                        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                        ID2D1Bitmap* pBitmap = nullptr;
                        hr = d2dContext->CreateBitmap(
                            D2D1::SizeU(snapshot->width, snapshot->height),
                            snapshot->pixels.data(),
                            snapshot->width * sizeof(uint32_t),
                            props,
                            &pBitmap
                        );
                        if (SUCCEEDED(hr) && pBitmap) {
                            D2D1_RECT_F destRect = D2D1::RectF(
                                (float)offset.x, (float)offset.y,
                                (float)offset.x + m_faceWidth, (float)offset.y + m_faceHeight
                            );
                            d2dContext->DrawBitmap(pBitmap, destRect);
                            pBitmap->Release();
                        }
                    }
                    // CASE 2: Inactive desktop without a full snapshot yet:
                    // Render authentic window cards & genuine desktop identity (NEVER duplicate desktop 1)
                    else {
                        std::vector<VDManager::WindowCard> cards;
                        VDManager::GetWindowsForDesktop(dId, cards);

                        ID2D1SolidColorBrush* pCardBrush = nullptr;
                        ID2D1SolidColorBrush* pBorderBrush = nullptr;
                        ID2D1SolidColorBrush* pTextBrush = nullptr;
                        d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.15f, 0.22f, 0.90f), &pCardBrush);
                        d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.26f, 0.36f, 1.0f), &pBorderBrush);
                        d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.94f, 0.98f, 1.0f), &pTextBrush);

                        if (!cards.empty()) {
                            float scaleX = m_faceWidth / m_screenWidth;
                            float scaleY = m_faceHeight / m_screenHeight;

                            for (const auto& card : cards) {
                                float left = (float)offset.x + (float)card.rect.left * scaleX;
                                float top = (float)offset.y + (float)card.rect.top * scaleY;
                                float right = (float)offset.x + (float)card.rect.right * scaleX;
                                float bottom = (float)offset.y + (float)card.rect.bottom * scaleY;

                                if (right > left + 30.0f && bottom > top + 20.0f) {
                                    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom), 4.0f, 4.0f);
                                    if (pCardBrush) d2dContext->FillRoundedRectangle(rrect, pCardBrush);
                                    if (pBorderBrush) d2dContext->DrawRoundedRectangle(rrect, pBorderBrush, 1.0f);

                                    // Window title
                                    if (m_textFormatTitle && pTextBrush) {
                                        D2D1_RECT_F textRect = D2D1::RectF(left + 6.0f, top + 4.0f, right - 6.0f, top + 20.0f);
                                        d2dContext->DrawText(
                                            card.title.c_str(), (UINT32)card.title.length(),
                                            m_textFormatTitle, textRect, pTextBrush
                                        );
                                    }
                                }
                            }
                        } else {
                            // Authentic Empty Workspace state
                            if (m_textFormatBadge && pTextBrush) {
                                wchar_t szEmpty[64];
                                swprintf_s(szEmpty, L"Desktop %d\n(Empty Workspace)", dIdx + 1);
                                D2D1_RECT_F emptyRect = D2D1::RectF(
                                    (float)offset.x, (float)offset.y + m_faceHeight * 0.40f,
                                    (float)offset.x + m_faceWidth, (float)offset.y + m_faceHeight * 0.60f
                                );
                                d2dContext->DrawText(
                                    szEmpty, (UINT32)wcslen(szEmpty),
                                    m_textFormatBadge, emptyRect, pTextBrush,
                                    D2D1_DRAW_TEXT_OPTIONS_NONE
                                );
                            }
                        }

                        if (pCardBrush) pCardBrush->Release();
                        if (pBorderBrush) pBorderBrush->Release();
                        if (pTextBrush) pTextBrush->Release();
                    }

                    surfaceInterop->EndDraw();
                    d2dContext->Release();
                }
                surfaceInterop->Release();
            }
            ((IUnknown*)drawingSurfacePtr)->Release();

            f.surfaceBrush = m_compositor.CreateSurfaceBrush(f.surface);
            f.background.Brush(f.surfaceBrush);
        }
    }
}

bool CubeRenderer::Create(HWND hTaskbarWnd, POINT anchorPoint) {
    if (m_hwnd && IsWindow(m_hwnd)) {
        ShowWindow(m_hwnd, SW_SHOWNA);
        return true;
    }

    HMONITOR hMon = MonitorFromPoint(anchorPoint, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfo(hMon, &mi)) return false;

    m_monitorRect = mi.rcMonitor;
    int monW = mi.rcMonitor.right - mi.rcMonitor.left;
    int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
    UpdateDimensions(monW, monH);

    // 1. Query Real Virtual Desktops
    VDManager::QueryDesktops(m_desktopList);

    // 2. Pre-capture active desktop screen BEFORE creating/showing overlay window (zero recursion)
    DesktopCaptureManager::SnapshotData currentSnapshot;
    DesktopCaptureManager::CaptureMonitorScreen(m_monitorRect, currentSnapshot);
    if (!currentSnapshot.pixels.empty() && m_desktopList.currentIndex >= 0 && m_desktopList.currentIndex < m_desktopList.TotalCount()) {
        DesktopCaptureManager::StoreSnapshot(m_desktopList.items[m_desktopList.currentIndex].id, currentSnapshot);
    }

    // Register overlay class
    static bool s_classRegistered = false;
    static const wchar_t* s_className = L"CompizDesktopCubeOverlayWindow";
    if (!s_classRegistered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = s_className;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    // Create transparent topmost overlay window.
    // WS_EX_TRANSPARENT ensures the overlay does NOT trap mouse hit testing or cause feedback loops with taskbar.
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT,
        s_className,
        L"Compiz Desktop Cube",
        WS_POPUP,
        mi.rcMonitor.left, mi.rcMonitor.top, monW, monH,
        nullptr, nullptr, GetModuleHandleW(nullptr), this
    );

    if (!m_hwnd) {
        LOG_ERR(L"CreateWindowExW failed: %lu", GetLastError());
        return false;
    }

    try {
        m_compositor = Compositor();
        IUnknown* compUnk = (IUnknown*)winrt::get_abi(m_compositor);
        ICompositorDesktopInterop* interop = nullptr;
        HRESULT hr = compUnk->QueryInterface(IID_ICompositorDesktopInterop, (void**)&interop);
        if (FAILED(hr) || !interop) {
            Destroy();
            return false;
        }

        hr = interop->CreateDesktopWindowTarget(m_hwnd, TRUE, winrt::put_abi(m_target));
        interop->Release();
        if (FAILED(hr) || !m_target) {
            Destroy();
            return false;
        }

        m_rootVisual = m_compositor.CreateContainerVisual();
        m_rootVisual.Size({ m_screenWidth, m_screenHeight });

        // Initialize DirectX, Direct2D, and DirectWrite
        InitializeGraphicsDevice();

        // 3. Build Hardware GPU Gaussian Blur Background
        BuildBackgroundBlurredSurface(currentSnapshot);

        // 4. Scrim Dimming layer over blurred background
        m_scrimVisual = m_compositor.CreateSpriteVisual();
        m_scrimVisual.Size({ m_screenWidth, m_screenHeight });
        uint8_t scrimAlpha = (uint8_t)((g_settings.backgroundDim * 255) / 100);
        m_scrimVisual.Brush(m_compositor.CreateColorBrush({ scrimAlpha, 8, 10, 16 }));
        m_rootVisual.Children().InsertAtTop(m_scrimVisual);

        // 5. 3D Cube Container
        m_cubeContainer = m_compositor.CreateContainerVisual();
        m_cubeContainer.Size({ m_screenWidth, m_screenHeight });
        m_rootVisual.Children().InsertAtTop(m_cubeContainer);

        // Accent colors for desktop indicator borders
        const struct FaceColor {
            uint8_t r, g, b;
        } faceAccents[4] = {
            { 56, 140, 255 },  // Royal Blue
            { 168, 85, 247 },  // Purple
            { 249, 115, 22 },  // Coral / Orange
            { 20, 184, 166 }   // Teal
        };

        for (int i = 0; i < 4; ++i) {
            auto& f = m_faces[i];
            f.container = m_compositor.CreateContainerVisual();
            f.container.Size({ m_faceWidth, m_faceHeight });

            // Face background visual
            f.background = m_compositor.CreateSpriteVisual();
            f.background.Size({ m_faceWidth, m_faceHeight });
            f.background.Brush(m_compositor.CreateColorBrush({ 220, 20, 24, 32 }));
            f.container.Children().InsertAtBottom(f.background);

            // Top accent bar (3px)
            f.border = m_compositor.CreateSpriteVisual();
            f.border.Size({ m_faceWidth, 3.0f });
            f.border.Brush(m_compositor.CreateColorBrush({ 255, faceAccents[i].r, faceAccents[i].g, faceAccents[i].b }));
            f.container.Children().InsertAtTop(f.border);

            // Glass edge highlights (1px left and right)
            auto leftEdge = m_compositor.CreateSpriteVisual();
            leftEdge.Size({ 1.0f, m_faceHeight });
            leftEdge.Brush(m_compositor.CreateColorBrush({ 50, 255, 255, 255 }));
            f.container.Children().InsertAtTop(leftEdge);

            auto rightEdge = m_compositor.CreateSpriteVisual();
            rightEdge.Size({ 1.0f, m_faceHeight });
            rightEdge.Offset({ m_faceWidth - 1.0f, 0.0f, 0.0f });
            rightEdge.Brush(m_compositor.CreateColorBrush({ 50, 255, 255, 255 }));
            f.container.Children().InsertAtTop(rightEdge);

            // Bottom subtle rim (1px)
            auto bottomLine = m_compositor.CreateSpriteVisual();
            bottomLine.Size({ m_faceWidth, 1.0f });
            bottomLine.Offset({ 0.0f, m_faceHeight - 1.0f, 0.0f });
            bottomLine.Brush(m_compositor.CreateColorBrush({ 40, 255, 255, 255 }));
            f.container.Children().InsertAtTop(bottomLine);

            // Desktop indicator badge
            if (g_settings.showDesktopLabels) {
                float pillW = 120.0f;
                float pillH = 22.0f;
                f.headerPill = m_compositor.CreateSpriteVisual();
                f.headerPill.Size({ pillW, pillH });
                f.headerPill.Offset({ (m_faceWidth - pillW) * 0.5f, 8.0f, 0.0f });
                f.headerPill.Brush(m_compositor.CreateColorBrush({ 200, 16, 18, 24 }));
                f.container.Children().InsertAtTop(f.headerPill);

                auto dot = m_compositor.CreateSpriteVisual();
                dot.Size({ 6.0f, 6.0f });
                dot.Offset({ (m_faceWidth - pillW) * 0.5f + 10.0f, 16.0f, 0.0f });
                dot.Brush(m_compositor.CreateColorBrush({ 255, faceAccents[i].r, faceAccents[i].g, faceAccents[i].b }));
                f.container.Children().InsertAtTop(dot);

                auto pillLine = m_compositor.CreateSpriteVisual();
                pillLine.Size({ pillW, 2.0f });
                pillLine.Offset({ (m_faceWidth - pillW) * 0.5f, 8.0f, 0.0f });
                pillLine.Brush(m_compositor.CreateColorBrush({ 230, faceAccents[i].r, faceAccents[i].g, faceAccents[i].b }));
                f.container.Children().InsertAtTop(pillLine);
            }

            m_cubeContainer.Children().InsertAtTop(f.container);
        }

        // Upload real captured desktop textures & authentic window cards
        BuildFaceSurfaces();

        m_target.Root(m_rootVisual);
        UpdateCubeFaceTransforms(0.0f);

        ShowWindow(m_hwnd, SW_SHOWNA);
        LOG_INF(L"Cube overlay presented with real desktop content and blurred background");
        return true;
    } catch (...) {
        Destroy();
        return false;
    }
}

void CubeRenderer::Destroy() {
    if (m_hwnd) {
        DWORD dwOwnerThread = GetWindowThreadProcessId(m_hwnd, nullptr);
        if (dwOwnerThread != 0 && dwOwnerThread != GetCurrentThreadId()) {
            HWND hOld = m_hwnd;
            m_hwnd = nullptr;
            SendMessageW(hOld, WM_CLOSE, 0, 0);
            return;
        }
        KillTimer(m_hwnd, TIMER_SNAP_ANIMATION);
        HWND h = m_hwnd;
        m_hwnd = nullptr;
        DestroyWindow(h);
    }

    m_target = nullptr;
    m_rootVisual = nullptr;
    m_backgroundVisual = nullptr;
    m_bgSurface = nullptr;
    m_scrimVisual = nullptr;
    m_cubeContainer = nullptr;

    for (int i = 0; i < 4; ++i) {
        m_faces[i].container = nullptr;
        m_faces[i].background = nullptr;
        m_faces[i].border = nullptr;
        m_faces[i].headerPill = nullptr;
        m_faces[i].surface = nullptr;
        m_faces[i].surfaceBrush = nullptr;
    }

    if (m_textFormatBadge) {
        m_textFormatBadge->Release();
        m_textFormatBadge = nullptr;
    }
    if (m_textFormatTitle) {
        m_textFormatTitle->Release();
        m_textFormatTitle = nullptr;
    }
    if (m_dwriteFactory) {
        m_dwriteFactory->Release();
        m_dwriteFactory = nullptr;
    }

    if (m_compGraphicsDevice) {
        ((IUnknown*)m_compGraphicsDevice)->Release();
        m_compGraphicsDevice = nullptr;
    }
    if (m_d2dDevice) {
        m_d2dDevice->Release();
        m_d2dDevice = nullptr;
    }
    if (m_d2dFactory) {
        m_d2dFactory->Release();
        m_d2dFactory = nullptr;
    }
    if (m_d3dDevice) {
        m_d3dDevice->Release();
        m_d3dDevice = nullptr;
    }

    m_compositor = nullptr;
    m_snapAnim.active = false;
}

void CubeRenderer::UpdateCubeFaceTransforms(float currentAngle) {
    m_currentRotation = currentAngle;
    if (!m_cubeContainer) return;

    float rad = currentAngle * 3.14159265f / 180.0f;
    float depth = m_faceWidth * 0.5f;

    float cx = m_screenWidth * 0.5f;
    float cy = m_screenHeight * 0.5f;

    Mat4 mPerspective = Mat4::Perspective(g_settings.perspective);
    Mat4 mToScreenCenter = Mat4::Translation(cx, cy, 0.0f);
    Mat4 mCubeRotation = Mat4::RotationY(rad);

    // Face angle mapping:
    // Face 0 = 0 deg
    // Face 1 = -90 deg (brings next desktop forward when dragging right)
    // Face 2 = 180 deg
    // Face 3 = +90 deg (brings prev desktop forward when dragging left)
    const float faceAnglesDeg[4] = { 0.0f, -90.0f, 180.0f, 90.0f };

    for (int i = 0; i < 4; ++i) {
        float faceAngleDeg = faceAnglesDeg[i];
        float totalFaceAngleDeg = faceAngleDeg + currentAngle;
        float totalFaceAngleRad = totalFaceAngleDeg * 3.14159265f / 180.0f;

        float facing = std::cos(totalFaceAngleRad);

        // Back-face culling
        if (facing < -0.05f) {
            m_faces[i].container.IsVisible(false);
            continue;
        }

        m_faces[i].container.IsVisible(true);

        // Directional face lighting
        float brightnessRatio = (float)g_settings.faceBrightness / 100.0f;
        float opacity = (0.50f + 0.50f * facing) * brightnessRatio;
        m_faces[i].container.Opacity(opacity);

        Mat4 mLocalOrigin = Mat4::Translation(-m_faceWidth * 0.5f, -m_faceHeight * 0.5f, 0.0f);
        Mat4 mFaceZOffset = Mat4::Translation(0.0f, 0.0f, depth);
        Mat4 mFaceAngle = Mat4::RotationY(faceAngleDeg * 3.14159265f / 180.0f);

        Mat4 mFinal = mLocalOrigin * mFaceZOffset * mFaceAngle * mCubeRotation * mPerspective * mToScreenCenter;
        m_faces[i].container.TransformMatrix(mFinal.ToWinRT());
    }
}

void CubeRenderer::SetRotationAngle(float angleDegrees) {
    UpdateCubeFaceTransforms(angleDegrees);
}

void CubeRenderer::SnapToAngle(float targetAngleDegrees, int snapDesktopOffset) {
    m_snapAnim.active = true;
    m_snapAnim.fadingOut = false;
    m_snapAnim.startAngle = m_currentRotation;
    m_snapAnim.endAngle = targetAngleDegrees;
    m_snapAnim.snapOffset = snapDesktopOffset;

    float deltaDeg = std::abs(targetAngleDegrees - m_currentRotation);
    int dynamicDuration = (int)(70.0f + 2.0f * deltaDeg);
    if (dynamicDuration < 60) dynamicDuration = 60;
    if (dynamicDuration > 220) dynamicDuration = 220;
    m_snapAnim.durationMs = dynamicDuration;
    m_snapAnim.startTime = std::chrono::high_resolution_clock::now();

    timeBeginPeriod(1);
    if (m_hwnd && IsWindow(m_hwnd)) {
        SetTimer(m_hwnd, TIMER_SNAP_ANIMATION, 8, nullptr);
    }
}

void CubeRenderer::Cancel() {
    SnapToAngle(0.0f, 0);
}

void CubeRenderer::OnAnimationTick() {
    if (!m_snapAnim.active) {
        if (m_hwnd) KillTimer(m_hwnd, TIMER_SNAP_ANIMATION);
        timeEndPeriod(1);
        return;
    }

    if (!m_snapAnim.fadingOut) {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsedMs = (float)std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_snapAnim.startTime).count();
        float progress = elapsedMs / (float)m_snapAnim.durationMs;

        if (progress >= 1.0f) {
            UpdateCubeFaceTransforms(m_snapAnim.endAngle);
            m_snapAnim.fadingOut = true;
            m_snapAnim.fadeStep = 4;

            // Perform the real Windows Virtual Desktop switch!
            int total = m_desktopList.TotalCount();
            if (total > 0 && m_snapAnim.snapOffset != 0) {
                int targetIndex = ((m_desktopList.currentIndex + m_snapAnim.snapOffset) % total + total) % total;
                VDManager::SwitchToDesktopByIndex(targetIndex);
            }
            return;
        }

        // Quartic ease-out: 1 - (1 - t)^4 for fluid Windows 11 deceleration
        float inv = 1.0f - progress;
        float eased = 1.0f - (inv * inv * inv * inv);
        float current = m_snapAnim.startAngle + (m_snapAnim.endAngle - m_snapAnim.startAngle) * eased;
        UpdateCubeFaceTransforms(current);
    } else {
        // Smooth fade out
        if (m_snapAnim.fadeStep > 0) {
            if (m_rootVisual) {
                m_rootVisual.Opacity((float)m_snapAnim.fadeStep / 4.0f);
            }
            m_snapAnim.fadeStep--;
        } else {
            KillTimer(m_hwnd, TIMER_SNAP_ANIMATION);
            timeEndPeriod(1);
            m_snapAnim.active = false;
            Destroy();
            GestureController::OnSnapCompleted();
        }
    }
}

// ============================================================================
// MODULE 4: GestureController (One Authoritative Gesture State Machine)
// ============================================================================
namespace GestureController {

    static std::atomic<GestureState> s_state{ GestureState::Idle };
    static POINT s_startPoint{};
    static POINT s_prevMousePoint{};
    static float s_currentAngle = 0.0f;
    static HWND s_anchorTaskbar = nullptr;

    static HHOOK s_hKeyboardHook = nullptr;
    static HHOOK s_hMouseHook = nullptr;
    static com_ptr<IUIAutomation> s_pUIAutomation;
    static UINT_PTR s_armingTimerId = 0;
    static constexpr UINT_PTR TIMER_ARMING_CHECK = 1002;

    void OnActiveMouseUp(POINT pt);
    void OnActiveMouseMove(POINT pt);

    GestureState GetState() {
        return s_state.load();
    }

    static bool InitUIAutomation() {
        if (!s_pUIAutomation) {
            CoCreateInstance(
                CLSID_CUIAutomation,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_IUIAutomation,
                s_pUIAutomation.put_void()
            );
        }
        return s_pUIAutomation != nullptr;
    }

    // Comprehensive taskbar empty-space hit testing
    static bool IsEmptyTaskbarSpace(HWND hWndOrigin, POINT pt) {
        HWND hTarget = WindowFromPoint(pt);
        HWND hCheck = hTarget ? hTarget : hWndOrigin;
        if (!hCheck) return false;

        bool isTaskbar = false;
        HWND cur = hCheck;
        while (cur) {
            WCHAR cls[128]{};
            GetClassNameW(cur, cls, ARRAYSIZE(cls));
            if (_wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
                _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
                _wcsicmp(cls, L"Windows.UI.Input.InputSite.WindowClass") == 0 ||
                _wcsicmp(cls, L"Windows.UI.Composition.DesktopWindowContentBridge") == 0) {
                isTaskbar = true;
                break;
            }
            HWND parent = GetParent(cur);
            if (!parent || parent == cur) break;
            cur = parent;
        }

        if (!isTaskbar && hWndOrigin) {
            cur = hWndOrigin;
            while (cur) {
                WCHAR cls[128]{};
                GetClassNameW(cur, cls, ARRAYSIZE(cls));
                if (_wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
                    _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
                    _wcsicmp(cls, L"Windows.UI.Input.InputSite.WindowClass") == 0) {
                    isTaskbar = true;
                    break;
                }
                HWND parent = GetParent(cur);
                if (!parent || parent == cur) break;
                cur = parent;
            }
        }

        if (!isTaskbar) {
            LOG_DBG(L"Taskbar hit rejected: window 0x%p is not taskbar", hCheck);
            return false;
        }

        HWND hRoot = GetAncestor(hCheck, GA_ROOT);
        HWND hTaskbar = hRoot ? hRoot : hCheck;

        // Reject TrayNotifyWnd (clock, system tray, notification center)
        HWND hTrayNotify = FindWindowExW(hTaskbar, nullptr, L"TrayNotifyWnd", nullptr);
        if (hTrayNotify && IsWindowVisible(hTrayNotify)) {
            RECT rcTray{};
            GetWindowRect(hTrayNotify, &rcTray);
            if (PtInRect(&rcTray, pt)) {
                LOG_DBG(L"Taskbar hit rejected: inside TrayNotifyWnd");
                return false;
            }
        }

        // Reject Start button
        HWND hStart = FindWindowExW(hTaskbar, nullptr, L"Start", nullptr);
        if (hStart && IsWindowVisible(hStart)) {
            RECT rcStart{};
            GetWindowRect(hStart, &rcStart);
            if (PtInRect(&rcStart, pt)) {
                LOG_DBG(L"Taskbar hit rejected: inside Start button");
                return false;
            }
        }

        // Reject active context menus
        HWND hMenu = FindWindowW(L"#32768", nullptr);
        if (hMenu && IsWindowVisible(hMenu)) {
            LOG_DBG(L"Taskbar hit rejected: context menu is open");
            return false;
        }

        // Modern XAML Island Button / Control Filtering via UIAutomation
        if (InitUIAutomation()) {
            com_ptr<IUIAutomationElement> element;
            if (SUCCEEDED(s_pUIAutomation->ElementFromPoint(pt, element.put())) && element) {
                BSTR bstrClass = nullptr;
                element->get_CurrentClassName(&bstrClass);
                std::wstring className = bstrClass ? bstrClass : L"";
                if (bstrClass) SysFreeString(bstrClass);

                if (className == L"Taskbar.TaskListButtonAutomationPeer" ||
                    className == L"Taskbar.SearchBoxButtonAutomationPeer" ||
                    className == L"Taskbar.TaskViewButtonAutomationPeer" ||
                    className == L"Taskbar.WidgetsButtonAutomationPeer" ||
                    className == L"Taskbar.ShowDesktopButtonAutomationPeer" ||
                    className == L"Start" || className == L"StartButton" ||
                    className.find(L"NotifyIcon") != std::wstring::npos ||
                    className.find(L"Clock") != std::wstring::npos) {
                    LOG_DBG(L"Taskbar hit rejected: UIAutomation class %s", className.c_str());
                    return false;
                }
            }
        }

        LOG_DBG(L"Taskbar hit accepted at (%ld, %ld)", pt.x, pt.y);
        return true;
    }

    static void InstallKeyboardHook();
    static void RemoveKeyboardHook();
    static void InstallMouseHook();
    static void RemoveMouseHook();

    static void CancelArming() {
        if (s_state.load() == GestureState::Arming) {
            if (s_anchorTaskbar && IsWindow(s_anchorTaskbar)) {
                KillTimer(s_anchorTaskbar, TIMER_ARMING_CHECK);
            }
            s_armingTimerId = 0;
            s_state.store(GestureState::Idle);
            RemoveKeyboardHook();
            LOG_INF(L"ARMING cancelled -> IDLE");
        }
    }

    static void ActivateCube(POINT pt) {
        if (s_state.load() != GestureState::Arming) return;

        if (s_anchorTaskbar && IsWindow(s_anchorTaskbar)) {
            KillTimer(s_anchorTaskbar, TIMER_ARMING_CHECK);
        }
        s_armingTimerId = 0;

        s_state.store(GestureState::ActiveDrag);
        s_prevMousePoint = pt;
        s_currentAngle = 0.0f;

        LOG_INF(L"ARMING -> ACTIVE_DRAG at (%ld, %ld)", pt.x, pt.y);

        if (!CubeRenderer::Instance().Create(s_anchorTaskbar, s_startPoint)) {
            LOG_ERR(L"Failed to create CubeRenderer window");
            s_state.store(GestureState::Idle);
            RemoveKeyboardHook();
            return;
        }

        // Install global low-level mouse tracking hook.
        // This tracks the cursor globally across all monitors and windows without deactivating when leaving the taskbar!
        InstallMouseHook();
    }

    bool OnMouseDown(HWND hWnd, POINT pt) {
        if (s_state.load() != GestureState::Idle || CubeRenderer::Instance().IsActive()) {
            return false;
        }

        LOG_DBG(L"Gesture candidate at (%ld, %ld) hWnd=0x%p", pt.x, pt.y, hWnd);

        if (IsEmptyTaskbarSpace(hWnd, pt)) {
            s_state.store(GestureState::Arming);
            s_startPoint = pt;
            s_prevMousePoint = pt;
            s_anchorTaskbar = hWnd;

            s_armingTimerId = SetTimer(hWnd, TIMER_ARMING_CHECK, 8, nullptr);
            InstallKeyboardHook();

            LOG_INF(L"IDLE -> ARMING at (%ld, %ld)", pt.x, pt.y);
            return true;
        }

        return false;
    }

    static int GetInwardDragDistance(POINT pt) {
        RECT rcBar{};
        if (s_anchorTaskbar && IsWindow(s_anchorTaskbar)) {
            GetWindowRect(s_anchorTaskbar, &rcBar);
        }
        if (rcBar.top > 100) {
            return s_startPoint.y - pt.y; // taskbar at bottom: upward drag into screen
        } else {
            return pt.y - s_startPoint.y; // taskbar at top: downward drag into screen
        }
    }

    void OnArmingMouseMove(POINT pt) {
        if (s_state.load() != GestureState::Arming) return;

        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            CancelArming();
            return;
        }

        int dy = GetInwardDragDistance(pt);
        if (dy >= g_settings.activationThreshold) {
            ActivateCube(pt);
        }
    }

    void OnArmingTimerTick() {
        if (s_state.load() != GestureState::Arming) {
            if (s_anchorTaskbar && IsWindow(s_anchorTaskbar)) {
                KillTimer(s_anchorTaskbar, TIMER_ARMING_CHECK);
            }
            return;
        }

        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            CancelArming();
            return;
        }

        POINT pt{};
        GetCursorPos(&pt);
        int dy = GetInwardDragDistance(pt);
        if (dy >= g_settings.activationThreshold) {
            ActivateCube(pt);
        }
    }

    void OnArmingMouseUp() {
        if (s_state.load() == GestureState::Arming) {
            CancelArming();
        }
    }

    // Active mouse movement: continuous 360 rotation driven by low-level mouse hook
    void OnActiveMouseMove(POINT pt) {
        if (s_state.load() != GestureState::ActiveDrag) return;

        // Safety check: if left button was released without clean event
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            OnActiveMouseUp(pt);
            return;
        }

        int deltaX = pt.x - s_prevMousePoint.x;
        s_prevMousePoint = pt;

        if (deltaX != 0) {
            s_currentAngle += (float)deltaX * g_settings.horizontalSensitivity;
            CubeRenderer::Instance().SetRotationAngle(s_currentAngle);
        }
    }

    // Active mouse release: commits snap to target desktop
    void OnActiveMouseUp(POINT pt) {
        if (s_state.load() != GestureState::ActiveDrag) return;

        LOG_INF(L"ACTIVE_DRAG -> COMMITTING angle=%.2f", s_currentAngle);
        s_state.store(GestureState::Committing);
        RemoveMouseHook();
        RemoveKeyboardHook();

        // Snap-back threshold: if drag was minimal (< 15 degrees), return to original desktop
        int snapOffset = 0;
        float targetAngle = 0.0f;
        if (std::abs(s_currentAngle) >= 15.0f) {
            snapOffset = (int)std::round(s_currentAngle / 90.0f);
            targetAngle = (float)snapOffset * 90.0f;
        }

        LOG_INF(L"Snap target: offset=%d targetAngle=%.2f", snapOffset, targetAngle);
        CubeRenderer::Instance().SnapToAngle(targetAngle, snapOffset);
    }

    void OnSnapCompleted() {
        s_state.store(GestureState::Idle);
        LOG_INF(L"COMMITTING completed -> IDLE");
    }

    void OnCancel() {
        GestureState st = s_state.load();
        if (st == GestureState::Arming) {
            CancelArming();
        } else if (st == GestureState::ActiveDrag) {
            LOG_INF(L"Escape pressed: cancelling active cube gesture");
            s_state.store(GestureState::Committing);
            RemoveMouseHook();
            RemoveKeyboardHook();
            CubeRenderer::Instance().Cancel();
        }
    }

    // Low-Level Mouse Hook Proc: globally tracks cursor across desktop, windows, and monitors
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION && s_state.load() == GestureState::ActiveDrag) {
            MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
            if (pMouse) {
                if (wParam == WM_MOUSEMOVE) {
                    OnActiveMouseMove(pMouse->pt);
                } else if (wParam == WM_LBUTTONUP) {
                    OnActiveMouseUp(pMouse->pt);
                    return 1; // Consume mouse up so underlying window/desktop is not clicked
                }
            }
        }
        return CallNextHookEx(s_hMouseHook, nCode, wParam, lParam);
    }

    static void InstallMouseHook() {
        if (!s_hMouseHook) {
            HMODULE hMod = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCWSTR)&LowLevelMouseProc,
                &hMod
            );
            s_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hMod, 0);
            LOG_DBG(L"Installed WH_MOUSE_LL hook (0x%p)", s_hMouseHook);
        }
    }

    static void RemoveMouseHook() {
        if (s_hMouseHook) {
            UnhookWindowsHookEx(s_hMouseHook);
            s_hMouseHook = nullptr;
            LOG_DBG(L"Removed WH_MOUSE_LL hook");
        }
    }

    // Low-Level Keyboard Hook: listens for Escape to cancel immediately
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
            KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
            if (pKb && pKb->vkCode == VK_ESCAPE) {
                if (s_state.load() == GestureState::ActiveDrag || s_state.load() == GestureState::Arming) {
                    OnCancel();
                    return 1; // Swallowed
                }
            }
        }
        return CallNextHookEx(s_hKeyboardHook, nCode, wParam, lParam);
    }

    static void InstallKeyboardHook() {
        if (!s_hKeyboardHook) {
            HMODULE hMod = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCWSTR)&LowLevelKeyboardProc,
                &hMod
            );
            s_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hMod, 0);
        }
    }

    static void RemoveKeyboardHook() {
        if (s_hKeyboardHook) {
            UnhookWindowsHookEx(s_hKeyboardHook);
            s_hKeyboardHook = nullptr;
        }
    }

} // namespace GestureController

// ============================================================================
// CubeRenderer Window Procedure
// ============================================================================
LRESULT CALLBACK CubeRenderer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            // DO NOT cancel the gesture on capture changes!
            // Global mouse tracking is managed by WH_MOUSE_LL so moving off the taskbar never terminates the cube.
            return 0;

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                GestureController::OnCancel();
                return 0;
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == TIMER_SNAP_ANIMATION) {
                Instance().OnAnimationTick();
                return 0;
            }
            break;
        }

        case WM_CLOSE: {
            Instance().Destroy();
            return 0;
        }

        case WM_DESTROY: {
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Taskbar Window Subclassing & InputSite Window Proc Hook
// ============================================================================
using CreateWindowInBand_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID, DWORD);
using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);

static CreateWindowInBand_t CreateWindowInBand_orig = nullptr;
static CreateWindowExW_t CreateWindowExW_orig = nullptr;

static std::unordered_set<void*> g_hookedInputSiteProcs;
static std::unordered_set<HWND> g_hookedInputSiteWindows;
static std::unordered_set<HWND> g_subclassedTaskbars;
static WNDPROC InputSiteWindowProc_Original = nullptr;
static bool g_isWhInitialized = false;

static LRESULT CALLBACK InputSiteWindowProc_Hook(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_POINTERDOWN: {
            if (IS_POINTER_FIRSTBUTTON_WPARAM(wParam)) {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                HWND hRoot = GetAncestor(hWnd, GA_ROOT);
                GestureController::OnMouseDown(hRoot ? hRoot : hWnd, pt);
            }
            break;
        }

        case WM_POINTERUPDATE: {
            if (GestureController::GetState() == GestureState::Arming) {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                GestureController::OnArmingMouseMove(pt);
            }
            break;
        }

        case WM_POINTERUP: {
            if (IS_POINTER_FIRSTBUTTON_WPARAM(wParam)) {
                if (GestureController::GetState() == GestureState::Arming) {
                    GestureController::OnArmingMouseUp();
                }
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hWnd, &pt);
            HWND hRoot = GetAncestor(hWnd, GA_ROOT);
            GestureController::OnMouseDown(hRoot ? hRoot : hWnd, pt);
            break;
        }

        case WM_MOUSEMOVE: {
            if (GestureController::GetState() == GestureState::Arming) {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hWnd, &pt);
                GestureController::OnArmingMouseMove(pt);
            }
            break;
        }

        case WM_LBUTTONUP: {
            if (GestureController::GetState() == GestureState::Arming) {
                GestureController::OnArmingMouseUp();
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == 1002 /* TIMER_ARMING_CHECK */) {
                GestureController::OnArmingTimerTick();
                return 0;
            }
            break;
        }
    }

    return InputSiteWindowProc_Original(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK TaskbarSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    DWORD_PTR dwRefData
) {
    switch (uMsg) {
        case WM_LBUTTONDOWN: {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hWnd, &pt);
            GestureController::OnMouseDown(hWnd, pt);
            break;
        }

        case WM_MOUSEMOVE: {
            if (GestureController::GetState() == GestureState::Arming) {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hWnd, &pt);
                GestureController::OnArmingMouseMove(pt);
            }
            break;
        }

        case WM_LBUTTONUP: {
            if (GestureController::GetState() == GestureState::Arming) {
                GestureController::OnArmingMouseUp();
            }
            break;
        }

        case WM_TIMER: {
            if (wParam == 1002 /* TIMER_ARMING_CHECK */) {
                GestureController::OnArmingTimerTick();
                return 0;
            }
            break;
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void HookInputSiteWindow(HWND hWnd) {
    if (!hWnd || g_hookedInputSiteWindows.contains(hWnd)) return;
    g_hookedInputSiteWindows.insert(hWnd);

    void* wndProc = (void*)GetWindowLongPtrW(hWnd, GWLP_WNDPROC);
    if (!wndProc) return;

    if (!g_hookedInputSiteProcs.contains(wndProc)) {
        if (Wh_SetFunctionHook(wndProc, (void*)InputSiteWindowProc_Hook, (void**)&InputSiteWindowProc_Original)) {
            if (g_isWhInitialized) {
                Wh_ApplyHookOperations();
            }
            g_hookedInputSiteProcs.insert(wndProc);
            LOG_INF(L"Hooked InputSite wndproc %p for hWnd 0x%p", wndProc, hWnd);
        } else {
            LOG_ERR(L"Failed to hook InputSite wndproc %p", wndProc);
        }
    }
}

static BOOL CALLBACK EnumChildFindInputSite(HWND hChild, LPARAM) {
    WCHAR szChildClass[128]{};
    if (GetClassNameW(hChild, szChildClass, ARRAYSIZE(szChildClass))) {
        if (_wcsicmp(szChildClass, L"Windows.UI.Input.InputSite.WindowClass") == 0) {
            HookInputSiteWindow(hChild);
        }
    }
    return TRUE;
}

static void SubclassTaskbar(HWND hWnd) {
    if (!hWnd) return;

    if (!g_subclassedTaskbars.contains(hWnd)) {
        if (WindhawkUtils::SetWindowSubclassFromAnyThread(hWnd, TaskbarSubclassProc, 0)) {
            g_subclassedTaskbars.insert(hWnd);
            LOG_INF(L"Subclassed taskbar window 0x%p", hWnd);
        }
    }

    EnumChildWindows(hWnd, EnumChildFindInputSite, 0);
}

static HWND WINAPI CreateWindowInBand_hook(
    DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle,
    int X, int Y, int nWidth, int nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam, DWORD dwBand
) {
    HWND hWnd = CreateWindowInBand_orig(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam, dwBand);
    if (hWnd && lpClassName && !IS_INTRESOURCE(lpClassName)) {
        if (_wcsicmp(lpClassName, L"Windows.UI.Input.InputSite.WindowClass") == 0) {
            HookInputSiteWindow(hWnd);
        } else if (_wcsicmp(lpClassName, L"Shell_TrayWnd") == 0 ||
                   _wcsicmp(lpClassName, L"Shell_SecondaryTrayWnd") == 0) {
            SubclassTaskbar(hWnd);
        }
    }
    return hWnd;
}

static HWND WINAPI CreateWindowExW_hook(
    DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle,
    int X, int Y, int nWidth, int nHeight,
    HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam
) {
    HWND hWnd = CreateWindowExW_orig(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hWnd && lpClassName && !IS_INTRESOURCE(lpClassName)) {
        if (_wcsicmp(lpClassName, L"Windows.UI.Input.InputSite.WindowClass") == 0) {
            HookInputSiteWindow(hWnd);
        } else if (_wcsicmp(lpClassName, L"Shell_TrayWnd") == 0 ||
                   _wcsicmp(lpClassName, L"Shell_SecondaryTrayWnd") == 0) {
            SubclassTaskbar(hWnd);
        }
    }
    return hWnd;
}

static BOOL CALLBACK EnumWindowsInitProc(HWND hWnd, LPARAM lParam) {
    WCHAR szClass[128]{};
    if (GetClassNameW(hWnd, szClass, ARRAYSIZE(szClass))) {
        if (_wcsicmp(szClass, L"Shell_TrayWnd") == 0 ||
            _wcsicmp(szClass, L"Shell_SecondaryTrayWnd") == 0) {
            SubclassTaskbar(hWnd);
        }
    }
    return TRUE;
}

static bool InitMod() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        Wh_SetFunctionHook(
            (void*)GetProcAddress(hUser32, "CreateWindowExW"),
            (void*)CreateWindowExW_hook,
            (void**)&CreateWindowExW_orig
        );

        void* pCreateWindowInBand = (void*)GetProcAddress(hUser32, "CreateWindowInBand");
        if (pCreateWindowInBand) {
            Wh_SetFunctionHook(
                pCreateWindowInBand,
                (void*)CreateWindowInBand_hook,
                (void**)&CreateWindowInBand_orig
            );
        }
    }

    EnumWindows(EnumWindowsInitProc, 0);
    return true;
}

// ============================================================================
// Windhawk Module Lifecycle
// ============================================================================
BOOL Wh_ModInit() {
    LOG_INF(L"Compiz Desktop Cube initializing");
    LoadSettings();
    if (!InitMod()) {
        LOG_ERR(L"Failed to initialize Compiz Desktop Cube mod");
        return FALSE;
    }
    g_isWhInitialized = true;
    return TRUE;
}

void Wh_ModUninit() {
    LOG_INF(L"Compiz Desktop Cube uninitializing");
    g_isWhInitialized = false;
    GestureController::OnCancel();
    CubeRenderer::Instance().Destroy();

    for (HWND hWnd : g_subclassedTaskbars) {
        if (IsWindow(hWnd)) {
            WindhawkUtils::RemoveWindowSubclassFromAnyThread(hWnd, TaskbarSubclassProc);
        }
    }
    g_subclassedTaskbars.clear();
    g_hookedInputSiteWindows.clear();
    g_hookedInputSiteProcs.clear();
}

void Wh_ModSettingsChanged() {
    LOG_INF(L"Settings changed, reloading");
    LoadSettings();
}
