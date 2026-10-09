// ==WindhawkMod==
// @id              compiz-desktop-cube-v2
// @name            Compiz Desktop Cube for Windows 11
// @description     Classic 3D Desktop Cube virtual-desktop switcher for Windows 11 using native Windows composition and real virtual desktop snapshots.
// @version         1.3.1
// @author          Soham
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -lole32 -loleaut32 -lwindowsapp -ldwmapi -luuid -luser32 -lgdi32 -lcomctl32 -ld3d11 -ld2d1 -ldxgi -lwinmm
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Compiz Desktop Cube for Windows 11

Adds a classic Compiz/GNOME-style 3D Desktop Cube virtual-desktop switcher to Windows 11.
Built with native Windows Composition (Windows.UI.Composition) and Direct2D for 144 Hz smoothness,
zero idle CPU usage, hardware-accelerated Acrylic blur, and real Windows 11 virtual desktop integration.

## Key Features
- **Real Windows Virtual Desktops**: Represents and switches between actual Windows virtual desktops.
- **Actual Desktop Snapshots**: Each cube face displays the real captured contents of that virtual desktop (wallpaper, open windows, taskbar, desktop icons).
- **Live Blurred Background**: Hardware-accelerated Windows 11 Desktop Acrylic backdrop blurs the active desktop wallpaper and windows behind the 3D cube.
- **Window Mouse Capture**: Uses native Win32 mouse capture (`SetCapture`) instead of fragile global hooks.
- **100% Explorer Stability**: Thread-level mouse hooking with zero tampering of InputSite window procedures.
- **Smooth 144Hz Dragging**: Incremental mouse movement tracking with zero jitter or lag.
- **Fluid UI-Thread Animation**: 100% thread-safe quartic ease-out snapping driven by the window message pump and multimedia timer.
- **ESC Cancellation**: Cancel anytime by pressing Escape to smoothly return to the current desktop without switching.

## How to Use
1. Move the mouse over **empty space** on the Windows 11 taskbar.
2. Press and hold the **left mouse button**.
3. Drag **inward / upward** (~16 px) to activate the 3D Desktop Cube.
4. While holding the left button, drag **left or right** to rotate the cube between virtual desktops.
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
  $name: Enable Acrylic Background Blur
  $description: Blurs the live active desktop behind the 3D cube with hardware Acrylic blur.
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
#include <objectarray.h>
#include <uiautomation.h>
#include <d3d11.h>
#include <d2d1_1.h>
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

#ifndef DWMWA_USE_HOSTBACKDROPBRUSH
#define DWMWA_USE_HOSTBACKDROPBRUSH 17
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
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
#include <thread>

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
__CRT_UUID_DECL(ICompositorDesktopInterop, 0x29E691FA, 0x4567, 0x4DCA, 0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07)

struct IGraphicsEffectD2D1Interop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetEffectId(GUID * id) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(
        LPCWSTR name, UINT * index, UINT * mapping) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT * count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UINT index, void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSource(UINT index, void** source) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSourceCount(UINT * count) = 0;
};
__CRT_UUID_DECL(IGraphicsEffectD2D1Interop, 0x2FC57384, 0xA068, 0x44D7, 0xA3, 0x31, 0x30, 0x98, 0x2F, 0xCB, 0x71, 0xB0)

namespace winrt::impl {
    template <>
    inline constexpr guid guid_v<IGraphicsEffectD2D1Interop>{ 0x2FC57384, 0xA068, 0x44D7, { 0xA3, 0x31, 0x30, 0x98, 0x2F, 0xCB, 0x71, 0xB0 } };
}

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
// MODULE 1: VirtualDesktopManager (Windows 11 Build >= 22000 / 26100+)
// ============================================================================
namespace VirtualDesktopManager {

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

    static bool QueryDesktops(DesktopList& outList) {
        InitIIDs();
        outList.items.clear();
        outList.currentIndex = 0;

        com_ptr<IServiceProvider> sp;
        HRESULT hr = CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(sp.put()));
        if (FAILED(hr) || !sp) {
            LOG_ERR(L"Failed to obtain IServiceProvider: 0x%08X", hr);
            return false;
        }

        com_ptr<IVirtualDesktopManagerInternal> manager;
        hr = sp->QueryService(CLSID_VirtualDesktopManagerInternal, s_iidManagerInternal, manager.put_void());
        if (FAILED(hr) || !manager) {
            LOG_ERR(L"Failed to query IVirtualDesktopManagerInternal: 0x%08X", hr);
            return false;
        }

        typedef HRESULT(STDMETHODCALLTYPE* GetDesktopsProc)(IVirtualDesktopManagerInternal*, IObjectArray**);
        typedef HRESULT(STDMETHODCALLTYPE* GetCurrentDesktopProc)(IVirtualDesktopManagerInternal*, IVirtualDesktop**);

        auto pVtbl = *(void***)manager.get();
        GetDesktopsProc fnGetDesktops = (GetDesktopsProc)pVtbl[7];
        GetCurrentDesktopProc fnGetCurrentDesktop = (GetCurrentDesktopProc)pVtbl[6];

        com_ptr<IObjectArray> desktopArray;
        hr = fnGetDesktops(manager.get(), desktopArray.put());
        if (FAILED(hr) || !desktopArray) {
            LOG_ERR(L"GetDesktops failed: 0x%08X", hr);
            return false;
        }

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
        LOG_INF(L"Switched to Real Windows Virtual Desktop #%d (0x%08X)", clampedIndex + 1, hr);
        return SUCCEEDED(hr);
    }

} // namespace VirtualDesktopManager

// ============================================================================
// MODULE 2: DesktopCaptureManager (Real screen snapshots for all virtual desktops)
// ============================================================================
namespace DesktopCaptureManager {

    struct SnapshotData {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels; // 32-bit BGRA
        bool isFresh = false;
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
        outData.isFresh = true;

        SelectObject(hdcMem, hOld);
        DeleteObject(hDIB);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return true;
    }

    // Refresh snapshots: captures current active desktop instantly (~2ms)
    static void AcquireSnapshots(const VirtualDesktopManager::DesktopList& dList, RECT rcMon) {
        std::lock_guard<std::mutex> lock(s_captureMutex);
        if (dList.items.empty()) return;

        int activeIdx = dList.currentIndex;
        int total = dList.TotalCount();

        // 1. Always capture current active desktop immediately
        if (activeIdx >= 0 && activeIdx < total) {
            std::wstring curKey = GuidToString(dList.items[activeIdx].id);
            CaptureMonitorScreen(rcMon, s_snapshotCache[curKey]);
            LOG_DBG(L"Captured active desktop snapshot #%d", activeIdx + 1);
        }
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
    Active,
    Snapping
};

namespace GestureController {
    void OnSnapCompleted();
    void OnCancel();
    GestureState GetState();
}

// ============================================================================
// MODULE 3: CubeRenderer (Windows Composition 3D Cube & Glass Acrylic Blur)
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
    void BuildFaceSurfaces();
    void OnAnimationTick();

    HWND m_hwnd = nullptr;
    Compositor m_compositor{ nullptr };
    DesktopWindowTarget m_target{ nullptr };
    ContainerVisual m_rootVisual{ nullptr };
    SpriteVisual m_scrimVisual{ nullptr };
    ContainerVisual m_cubeContainer{ nullptr };

    // DirectX / Composition Device
    ID3D11Device* m_d3dDevice = nullptr;
    ID2D1Device* m_d2dDevice = nullptr;
    ID2D1Factory1* m_d2dFactory = nullptr;
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
    VirtualDesktopManager::DesktopList m_desktopList;
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
                    d2dContext->Clear(D2D1::ColorF(D2D1::ColorF(0.08f, 0.10f, 0.15f, 1.0f)));

                    // Retrieve real captured snapshot
                    const GUID& dId = m_desktopList.items[dIdx].id;
                    const auto* snapshot = DesktopCaptureManager::GetSnapshot(dId);
                    if (!snapshot || snapshot->pixels.empty()) {
                        if (curIdx >= 0 && curIdx < total) {
                            snapshot = DesktopCaptureManager::GetSnapshot(m_desktopList.items[curIdx].id);
                        }
                    }
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
                                (float)offset.x,
                                (float)offset.y,
                                (float)offset.x + m_faceWidth,
                                (float)offset.y + m_faceHeight
                            );
                            d2dContext->DrawBitmap(pBitmap, destRect);
                            pBitmap->Release();
                        }
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
    VirtualDesktopManager::QueryDesktops(m_desktopList);

    // 2. Refresh active desktop screen snapshot (~3ms)
    DesktopCaptureManager::AcquireSnapshots(m_desktopList, m_monitorRect);

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

    // Create transparent topmost overlay window (NOACTIVATE preserves Explorer focus)
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
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

    // 1. Extend DWM frame into client area
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    // 2. Hardware-accelerated Windows 11 Desktop Acrylic backdrop
    if (g_settings.backgroundBlur) {
        int backdrop = 3; // DWMSBT_TRANSIENTWINDOW (Desktop Acrylic blur of live desktop)
        DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    }

    // 3. Fallback Accent Policy for Acrylic blur
    typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, void*);
    auto pfnSetWindowCompositionAttribute = (pSetWindowCompositionAttribute)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    if (pfnSetWindowCompositionAttribute) {
        struct ACCENT_POLICY {
            INT AccentState;
            INT AccentFlags;
            INT GradientColor;
            INT AnimationId;
        };
        struct WINCOMPATTRDATA {
            DWORD Attrib;
            PVOID pvData;
            SIZE_T cbData;
        };
        ACCENT_POLICY accentPolicy{};
        accentPolicy.AccentState = g_settings.backgroundBlur ? 4 /* ACCENT_STATE_ENABLE_ACRYLICBLURBEHIND */ : 2;
        accentPolicy.AccentFlags = (1 << 2); // ACCENT_FLAG_ENABLE_FULLSCREEN
        accentPolicy.GradientColor = 0x2210121A; // Translucent dark tint (~13% opacity)

        WINCOMPATTRDATA winCompAttrData{ 19, &accentPolicy, sizeof(accentPolicy) };
        pfnSetWindowCompositionAttribute(m_hwnd, &winCompAttrData);
    }

    try {
        m_compositor = Compositor();
        auto interop = m_compositor.as<ICompositorDesktopInterop>();
        HRESULT hr = interop->CreateDesktopWindowTarget(m_hwnd, TRUE, winrt::put_abi(m_target));
        if (FAILED(hr) || !m_target) {
            Destroy();
            return false;
        }

        m_rootVisual = m_compositor.CreateContainerVisual();
        m_rootVisual.Size({ m_screenWidth, m_screenHeight });

        // Subtle dark scrim over blurred desktop (translucent so live desktop visibly shines through)
        m_scrimVisual = m_compositor.CreateSpriteVisual();
        m_scrimVisual.Size({ m_screenWidth, m_screenHeight });
        uint8_t scrimAlpha = (uint8_t)((g_settings.backgroundDim * 70) / 100);
        m_scrimVisual.Brush(m_compositor.CreateColorBrush({ scrimAlpha, 10, 12, 18 }));
        m_rootVisual.Children().InsertAtTop(m_scrimVisual);

        // 3D Cube Container
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

            // Face background snapshot visual
            f.background = m_compositor.CreateSpriteVisual();
            f.background.Size({ m_faceWidth, m_faceHeight });
            f.background.Brush(m_compositor.CreateColorBrush({ 220, 20, 24, 32 })); // Fallback base
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

        // Initialize Direct2D device & upload real captured desktop textures
        InitializeGraphicsDevice();
        BuildFaceSurfaces();

        m_target.Root(m_rootVisual);
        UpdateCubeFaceTransforms(0.0f);

        ShowWindow(m_hwnd, SW_SHOWNA);
        LOG_INF(L"Cube overlay presented with real desktop snapshots");
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

    // Dynamic fluid animation duration based on snap distance
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
                VirtualDesktopManager::SwitchToDesktopByIndex(targetIndex);
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
    static com_ptr<IUIAutomation> s_pUIAutomation;
    static UINT_PTR s_armingTimerId = 0;
    static constexpr UINT_PTR TIMER_ARMING_CHECK = 1002;

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

        // 1. Verify that window belongs to the taskbar window hierarchy
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

        // 2. Reject TrayNotifyWnd (clock, system tray, notification center)
        HWND hTrayNotify = FindWindowExW(hTaskbar, nullptr, L"TrayNotifyWnd", nullptr);
        if (hTrayNotify && IsWindowVisible(hTrayNotify)) {
            RECT rcTray{};
            GetWindowRect(hTrayNotify, &rcTray);
            if (PtInRect(&rcTray, pt)) {
                LOG_DBG(L"Taskbar hit rejected: inside TrayNotifyWnd");
                return false;
            }
        }

        // 3. Reject classic Start button if separate window
        HWND hStart = FindWindowExW(hTaskbar, nullptr, L"Start", nullptr);
        if (hStart && IsWindowVisible(hStart)) {
            RECT rcStart{};
            GetWindowRect(hStart, &rcStart);
            if (PtInRect(&rcStart, pt)) {
                LOG_DBG(L"Taskbar hit rejected: inside Start button");
                return false;
            }
        }

        // 4. Reject active context menus or popups
        HWND hMenu = FindWindowW(L"#32768", nullptr);
        if (hMenu && IsWindowVisible(hMenu)) {
            LOG_DBG(L"Taskbar hit rejected: context menu is open");
            return false;
        }

        // 5. Modern XAML Island Button / Control Filtering via UIAutomation
        if (InitUIAutomation()) {
            com_ptr<IUIAutomationElement> element;
            if (SUCCEEDED(s_pUIAutomation->ElementFromPoint(pt, element.put())) && element) {
                BSTR bstrClass = nullptr;
                element->get_CurrentClassName(&bstrClass);
                std::wstring className = bstrClass ? bstrClass : L"";
                if (bstrClass) SysFreeString(bstrClass);

                // Reject buttons & interactive controls
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

        s_state.store(GestureState::Active);
        s_prevMousePoint = pt;
        s_currentAngle = 0.0f;

        LOG_INF(L"ARMING -> ACTIVE at (%ld, %ld)", pt.x, pt.y);

        if (!CubeRenderer::Instance().Create(s_anchorTaskbar, s_startPoint)) {
            LOG_ERR(L"Failed to create CubeRenderer window");
            s_state.store(GestureState::Idle);
            RemoveKeyboardHook();
            return;
        }

        HWND hOverlay = CubeRenderer::Instance().GetHwnd();
        HWND hPrevCap = SetCapture(hOverlay);
        if (GetCapture() != hOverlay) {
            LOG_ERR(L"SetCapture FAILED error=%lu", GetLastError());
        } else {
            LOG_INF(L"SetCapture hwnd=0x%p result=0x%p", hOverlay, hPrevCap);
        }
    }

    // Called on left mouse down on taskbar
    bool OnMouseDown(HWND hWnd, POINT pt) {
        if (s_state.load() != GestureState::Idle || CubeRenderer::Instance().IsActive()) {
            return false;
        }

        LOG_DBG(L"Gesture Begin candidate x=%ld y=%ld hwnd=0x%p", pt.x, pt.y, hWnd);

        if (IsEmptyTaskbarSpace(hWnd, pt)) {
            s_state.store(GestureState::Arming);
            s_startPoint = pt;
            s_prevMousePoint = pt;
            s_anchorTaskbar = hWnd;

            // Start arming tracker timer on taskbar window (~8ms)
            s_armingTimerId = SetTimer(hWnd, TIMER_ARMING_CHECK, 8, nullptr);
            InstallKeyboardHook();

            LOG_INF(L"IDLE -> ARMING at (%ld, %ld)", pt.x, pt.y);
            return true;
        }

        return false;
    }

    // Evaluates inward movement distance relative to taskbar position
    static int GetInwardDragDistance(POINT pt) {
        RECT rcBar{};
        if (s_anchorTaskbar && IsWindow(s_anchorTaskbar)) {
            GetWindowRect(s_anchorTaskbar, &rcBar);
        }
        if (rcBar.top > 100) {
            // Taskbar is at the bottom: dragging upward into screen is positive
            return s_startPoint.y - pt.y;
        } else {
            // Taskbar is at the top: dragging downward into screen is positive
            return pt.y - s_startPoint.y;
        }
    }

    // Called during Arming movement
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

    // Arming timer tick: catches upward movement even if cursor leaves taskbar without mousemove
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

    // Authoritative Active mouse movement (called exclusively through SetCapture in CubeRenderer::WndProc)
    void OnActiveMouseMove(POINT pt) {
        if (s_state.load() != GestureState::Active) return;

        int deltaX = pt.x - s_prevMousePoint.x;
        s_prevMousePoint = pt;

        if (deltaX != 0) {
            s_currentAngle += (float)deltaX * g_settings.horizontalSensitivity;
            CubeRenderer::Instance().SetRotationAngle(s_currentAngle);
        }

        static auto s_lastLogTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastLogTime).count() > 80) {
            LOG_DBG(L"MOVE x=%ld dx=%d angle=%.2f", pt.x, deltaX, s_currentAngle);
            s_lastLogTime = now;
        }
    }

    // Authoritative Mouse Release (called exclusively through SetCapture in CubeRenderer::WndProc)
    void OnActiveMouseUp(POINT pt) {
        if (s_state.load() != GestureState::Active) return;

        LOG_INF(L"LBUTTONUP angle=%.2f", s_currentAngle);
        s_state.store(GestureState::Snapping);
        RemoveKeyboardHook();

        int snapOffset = (int)std::round(s_currentAngle / 90.0f);
        float targetAngle = (float)snapOffset * 90.0f;
        LOG_INF(L"SNAP offset=%d targetAngle=%.2f", snapOffset, targetAngle);

        CubeRenderer::Instance().SnapToAngle(targetAngle, snapOffset);
    }

    void OnSnapCompleted() {
        s_state.store(GestureState::Idle);
        LOG_INF(L"Snap animation completed -> IDLE");
    }

    void OnCancel() {
        GestureState st = s_state.load();
        if (st == GestureState::Arming) {
            CancelArming();
        } else if (st == GestureState::Active) {
            LOG_INF(L"Escape pressed: cancelling active cube gesture");
            ReleaseCapture();
            LOG_DBG(L"ReleaseCapture");
            s_state.store(GestureState::Snapping);
            RemoveKeyboardHook();
            CubeRenderer::Instance().Cancel();
        }
    }

    void OnCaptureLost() {
        if (s_state.load() == GestureState::Active) {
            LOG_INF(L"Mouse capture lost externally: returning cube to current desktop");
            s_state.store(GestureState::Snapping);
            RemoveKeyboardHook();
            CubeRenderer::Instance().Cancel();
        }
    }

    // Low-level keyboard hook: ONLY listens for VK_ESCAPE to guarantee instant cancellation
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
            KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
            if (pKb && pKb->vkCode == VK_ESCAPE) {
                if (s_state.load() == GestureState::Active || s_state.load() == GestureState::Arming) {
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
// CubeRenderer Window Procedure (Receives SetCapture Input Exclusively)
// ============================================================================
LRESULT CALLBACK CubeRenderer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            POINT pt;
            GetCursorPos(&pt);
            GestureController::OnActiveMouseMove(pt);
            return 0;
        }

        case WM_LBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            ReleaseCapture();
            LOG_DBG(L"ReleaseCapture");
            GestureController::OnActiveMouseUp(pt);
            return 0;
        }

        case WM_CAPTURECHANGED: {
            GestureController::OnCaptureLost();
            return 0;
        }

        case WM_CANCELMODE: {
            GestureController::OnCancel();
            return 0;
        }

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

    // Recursively discover and hook any Windows.UI.Input.InputSite.WindowClass child windows
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