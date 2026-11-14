# Building and testing

The product uses C++17, Win32, MSVC, the official Windows SDK, and a statically linked CRT (`/MT`). Windows distribution builds require Visual Studio 2022 or Build Tools with C++ desktop development. Linux builds exercise API simulations only; they do not produce a Windows EXE.

## Windows

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --verbose
./tools/Test-Startup.ps1 -ExecutablePath build/Release/MoveToMonitorButton.exe -OutputPath build/startup-result.json
```

Run desktop tests in a dedicated interactive test session. The scripts refuse to interfere with an already running instance of the product. `--show-on-single-monitor` enables a single-display test; adding `--preview-two-buttons` previews the pair without inventing extra monitors. These flags do not simulate successful movement; normal use requires multiple displays. `--startup-check` tests loading, `--quit` requests shutdown, and `--help` prints usage.

## Linux simulations

```sh
cmake -S . -B build-host -DCMAKE_CXX_COMPILER=clang++ -DMTMB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure --verbose
python tests/package_release_test.py
```

API doubles and sanitizers verify logic. They do not stand in for Windows runtime, composition, or input tests.

## Release gates

`Real application caption compatibility` builds the x64 EXE once and distributes that exact binary to the desktop jobs. Required checks include:

- MSVC Release and Debug, official SDK tests, actual EXE startup, and startup-menu registration / removal.
- Real-app geometry, pixels, hover, hit regions, and stability on Server 2022 / 2025: 36 cases.
- Windows 11 Arm: 27 real-app cases, plus Settings at actual 150% and 200% scaling.
- Official ChatGPT and ChatGPT Classic Store apps at 100%, 150%, and 200%: 18 cases. New ChatGPT caption centers are checked independently with UI Automation, with a maximum 0.5 physical-pixel difference and no overlap.
- Paired Settings, ChatGPT and ChatGPT Classic at actual 100%, 150%, and 200% scaling: 27 additional cases, ten clickable probes per case, independent glyph and hover evidence for each button.
- Inactive-window behavior using the product EXE and two explicitly labeled fixture windows.

The disposable Windows 11 desktop is prepared separately for first-run privacy and WSL prerequisites. Exact two-color screen-capture preflight runs at every scale. A preparation failure is not a product pass. See [the Windows 11 diagnosis](WINDOWS11-DESKTOP-DIAGNOSIS.md).

`tools/package_release.py` checks commit identity, the executable hash, all required cases, and readiness before packaging. On `main`, the release job uploads a draft, downloads its assets again, verifies hashes, then publishes. Existing assets are never overwritten.

The portable ZIP contains only the EXE, a short usage README, LICENSE, and the EXE checksum. GitHub-facing introduction pages and screenshots stay in the repository. Detailed logs, screenshots, and test metadata remain in Actions artifacts for 14–30 days.

These single-display CI tests do not prove physical monitor movement or transitions between monitors with different DPI settings. ChatGPT testing uses the actual app before sign-in, not every authenticated layout.

[Architecture (Japanese)](ARCHITECTURE.md) · [English introduction](../README.md) · [日本語の紹介](../README.ja.md)

## Display numbering

`monitor_order.cpp` joins active logical monitors to the CCD inventory using source GDI names and target device paths. It ranks known displays, retaining connected disabled targets, rather than numbering by screen coordinates or the order of `EnumDisplayMonitors`. A clone group is one move destination. `WM_DISPLAYCHANGE` rebuilds the active list; no in-flight move stores a transient array index.

There is no public API contract promising that this ranking equals Settings Identify on every driver. The user can correct numbers through the menu; unique corrections persist under `HKCU\Software\MoveToMonitorButton\DisplayNumbers`, keyed by monitor target path where available. Reset removes only this utility's corrections. No Windows display configuration is modified by the product. Source-name fallback applies when CCD is unavailable, for example in some remote sessions.

Research: [QueryDisplayConfig documents path *priority* order](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig), whereas [PowerToys PowerDisplay currently uses path indices](https://github.com/microsoft/PowerToys/blob/main/src/modules/powerdisplay/PowerDisplay.Lib/Drivers/DisplayConfigInventory.cs), and [DisplayHotKeys ranks known displays](https://github.com/jon-mil-92/DisplayHotKeys/blob/main/jni/DisplayConfig.cpp). These are implementation precedents, not a Windows numbering guarantee. We keep explicit correction instead of claiming universally exact automatic mapping.
