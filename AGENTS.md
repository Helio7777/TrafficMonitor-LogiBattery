# Repository Guidelines

## Project Structure & Module Organization

- `src/LogiBatteryPlugin.cpp` implements the TrafficMonitor Plugin API v8 adapter, display item, tooltip, and refresh command.
- `src/LogitechHidpp.cpp` and `src/LogitechHidpp.h` contain Logitech HID++ 2.0 discovery, transport, battery probing, and polling-thread logic.
- `src/TrafficMonitorPluginABI.h` is the minimal host ABI declaration; `src/LogiBatteryPlugin.def` preserves the required `TMPluginGetInstance` export.
- `CMakeLists.txt` defines the Windows DLL target and MSVC settings. `.github/workflows/build.yml` builds both architectures and checks the exported entry point.
- `README.md` and `THIRD_PARTY_NOTICES.md` document usage, compatibility, licensing, and implementation references.

## Build, Test, and Development Commands

Use Visual Studio 2022 with CMake 3.20 or newer from a PowerShell prompt:

```powershell
.\build.ps1 -Arch x64 -Config Release
.\build.ps1 -Arch Win32 -Config Release
```

The script configures `build-x64` or `build-Win32`, builds the DLL, and prints its path. For direct control, use `cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64` followed by `cmake --build build-x64 --config Release`.

## Coding Style & Naming Conventions

Follow the existing C++17 style: four-space indentation, braces on their own lines, and `/W4 /permissive- /utf-8 /EHsc`-clean MSVC builds. Use `PascalCase` for classes and public methods, `camelCase` for locals/parameters, and trailing underscores for data members (for example, `snapshotMutex_`). Keep platform/HID constants named with a `k` prefix. Prefer RAII, standard-library containers, and narrow, self-contained changes.

## Testing Guidelines

No unit-test framework is currently included. Every change should at minimum pass Release builds for both `x64` and `Win32`; CI also verifies that `TMPluginGetInstance` is exported. Hardware-facing changes should be manually checked with a supported Logitech mouse and TrafficMonitor, including startup, refresh command, charging states, and `N/A` behavior when no device is available.

## Commit & Pull Request Guidelines

This checkout does not include Git history, so no existing message convention can be verified. Use concise imperative subjects, preferably Conventional Commit-style (for example, `fix: retry HID++ reads after timeout`). Pull requests should explain behavior changes, list validation commands and target architectures, link related issues, and include screenshots or logs when UI or device-detection behavior changes. Keep generated build directories and binaries out of commits.

## Security & Configuration Tips

Do not commit device identifiers, captured HID reports, credentials, or proprietary protocol data. Preserve the GPL-3.0-or-later attribution when changing code derived from the documented third-party implementations.
