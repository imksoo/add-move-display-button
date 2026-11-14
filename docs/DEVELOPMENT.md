# Building and testing

The product uses C++17, Win32, MSVC, the official Windows SDK, and a statically linked CRT (`/MT`). Windows distribution builds require Visual Studio 2022 or Build Tools with C++ desktop development. Linux builds exercise API simulations only; they do not produce a Windows EXE.

## Windows

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --verbose
./tools/Test-Startup.ps1 -ExecutablePath build/Release/MoveToMonitorButton.exe -OutputPath build/startup-result.json
```

Run desktop tests in a dedicated interactive test session. The scripts refuse to interfere with an already running instance of the product. `--show-on-single-monitor` enables a single-display test; normal use requires multiple displays. `--startup-check` tests loading, `--quit` requests shutdown, and `--help` prints usage.

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
- Inactive-window behavior using the product EXE and two explicitly labeled fixture windows.

The disposable Windows 11 desktop is prepared separately for first-run privacy and WSL prerequisites. Exact two-color screen-capture preflight runs at every scale. A preparation failure is not a product pass. See [the Windows 11 diagnosis](WINDOWS11-DESKTOP-DIAGNOSIS.md).

`tools/package_release.py` checks commit identity, the executable hash, all required cases, and readiness before packaging. On `main`, the release job uploads a draft, downloads its assets again, verifies hashes, then publishes. Existing assets are never overwritten.

The portable ZIP contains only the EXE, a short usage README, LICENSE, and the EXE checksum. GitHub-facing introduction pages and screenshots stay in the repository. Detailed logs, screenshots, and test metadata remain in Actions artifacts for 14–30 days.

These single-display CI tests do not prove physical monitor movement or transitions between monitors with different DPI settings. ChatGPT testing uses the actual app before sign-in, not every authenticated layout.

[Architecture (Japanese)](ARCHITECTURE.md) · [English introduction](../README.md) · [日本語の紹介](../README.ja.md)
