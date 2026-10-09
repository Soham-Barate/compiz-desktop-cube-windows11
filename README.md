# Compiz Desktop Cube for Windows 11 (Windhawk Mod)

A Windows 11 Windhawk mod that recreates the classic **Compiz/GNOME 3D Desktop Cube** virtual-desktop switcher.

Built natively on **Windows Composition (`Windows.UI.Composition`)** for hardware-accelerated 144 Hz presentation, frosted glass Acrylic blur, and zero idle CPU usage.

---

## Features

- **Classic 4-Sided Desktop Cube**: Exactly 4 vertical desktop faces. No top or bottom faces. True 3D perspective projection and Y-axis rotation.
- **Native Windows Composition Engine**: Renders through the Windows DWM Compositor. Synchronized to your display refresh rate (60 Hz, 90 Hz, 120 Hz, 144 Hz) with zero game-engine loop or per-frame allocations.
- **Taskbar Empty-Space Gesture**:
  1. Click and hold the left mouse button on **empty space** of the Windows 11 taskbar.
  2. Drag **upward** (~20 px) to activate the 3D cube.
  3. Drag **left / right** to rotate continuously between virtual desktops.
  4. Release the mouse button to snap smoothly and switch to the target virtual desktop.
  5. Press **Escape** at any time to cancel.
- **Precision UI Automation Hit-Testing**: Will never trigger over Start, Search, Task View, application taskbar buttons, system tray icons, or widgets.
- **Real Windows 11 Virtual Desktop Switching**: Automatically queries and controls Windows virtual desktops via the Windows 11 24H2/25H2/26H2 (Build 26100+) immersive shell interfaces.
- **Frosted Glass Acrylic Backdrop**: Hardware-accelerated Acrylic blur behind the 3D cube.
- **Dynamic 3D Lighting & Back-Face Culling**: Front face is brightest; turning faces dim naturally; rear faces are culled for maximum performance.

---

## How to Install in Windhawk

1. Open the **Windhawk** application.
2. Go to the **Details** / **Advanced** tab or click the **"+"** button to **Create New Mod**.
3. Copy the entire contents of [`compiz-desktop-cube.wh.cpp`](./compiz-desktop-cube.wh.cpp) and paste it into the Windhawk Mod editor.
4. Click **Compile Mod**.
5. Once compiled successfully, click **Accept and Save**.
6. Move your mouse to empty space on your Windows 11 taskbar, hold left click, and drag up to activate the cube!

---

## Settings Configuration

The mod exposes configurable settings in the Windhawk UI:

| Setting | Default | Description |
|---|---|---|
| `ActivationThreshold` | `16 px` | Upward drag threshold on empty taskbar space to activate |
| `HorizontalSensitivity` | `45%` | Horizontal mouse drag rotation sensitivity |
| `SnapDuration` | `130 ms` | Duration of the ease-out snap animation when released |
| `EnableInertia` | `true` | Momentum-assisted rotation when flicking the mouse |
| `CubeScale` | `68%` | Size of the cube relative to screen height |
| `Perspective` | `1100 px` | Camera focal distance for 3D perspective depth |
| `BackgroundBlur` | `true` | Enable Windows 11 Acrylic frosted glass blur backdrop |
| `BackgroundDim` | `45%` | Darkness of the scrim behind the cube |
| `FaceBrightness` | `90%` | Lighting brightness of front-facing desktop faces |
| `ShowDesktopLabels` | `true` | Display pill badges (DESKTOP 1, DESKTOP 2, etc.) |
| `RequiredDesktopCount` | `4` | Number of virtual desktops mapped to the cube |
| `EnableDebugLogging` | `true` | Verbose debug output to the Windhawk log console |

---

## Troubleshooting & Verification

1. **How to Trigger the Cube**:
   - The cube does **not** appear just by clicking or holding still.
   - **Click & hold** the left mouse button on an empty area of your taskbar (e.g. between the system tray and app icons), and **drag upward** by ~16 pixels.
   - The 3D cube overlay will immediately materialize and track your mouse drag horizontally.
2. **Checking Logs**:
   - Open Windhawk, go to the **Advanced** tab of the mod, and click **Show log output** (or select **Mod logs**).
   - You will see live messages such as:
     - `[Cube] Gesture ARMING at (x, y)`
     - `[Cube] Activation threshold reached. Showing 3D Desktop Cube!`
     - `[Cube] Switched to Virtual Desktop #...`

