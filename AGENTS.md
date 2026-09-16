# Repository Guidelines

## Project Structure & Module Organization

- `src/LogiBatteryPlugin.cpp` implements the TrafficMonitor Plugin API v8 adapter, display item, options dialog, and configuration handling.
- `src/LogitechHidpp.*` and `src/MchoseHid.*` contain the brand-specific Windows HID backends. Shared snapshot types live in `src/MouseBatteryTypes.h`.
- `src/TrafficMonitorPluginABI.h` declares the minimal host ABI; `src/LogiBatteryPlugin.def` preserves the required `TMPluginGetInstance` export.
- `docs/` contains architecture notes. `.github/` contains CI, release automation, and contribution templates.
- Generated `build-*` directories and binaries are local artifacts and must not be committed.

## Build, Test, and Development Commands

Use Visual Studio 2022 with CMake 3.20 or newer from PowerShell:

```powershell
.\build.ps1 -Arch x64 -Config Release
.\build.ps1 -Arch Win32 -Config Release
```

The script configures the matching `build-*` directory and builds `LogiBatteryPlugin.dll`. Release maintainers may add `-ProjectUrl "https://github.com/OWNER/REPO"` to embed the public project URL.

## Coding Style & Naming Conventions

Follow the existing C++17 style: four-space indentation, braces on separate lines, and `/W4 /permissive- /utf-8 /EHsc` MSVC settings. Use `PascalCase` for classes and public methods, `camelCase` for locals and parameters, trailing underscores for data members, and a `k` prefix for constants. Prefer RAII and standard-library containers. Keep protocol-specific behavior inside its backend.

## Testing Guidelines

No unit-test framework is included. Every change must pass Release builds for `x64` and `Win32`; CI also verifies `TMPluginGetInstance`. Hardware changes should cover startup, manual refresh, wake from sleep, charging states, disconnects, and `N/A` fallback. Document devices and untested scenarios in the PR.

## Commit & Pull Request Guidelines

History uses concise Conventional Commit subjects such as `feat: release v1.1.1`. Keep commits focused. PRs should describe user-visible behavior, list validation commands and architectures, link issues, and attach screenshots or sanitized logs when relevant.

## Security & Device Data

Do not commit credentials, firmware, full HID paths, serial numbers, raw captures, or proprietary protocol material. Preserve GPL-3.0-or-later attribution and update `THIRD_PARTY_NOTICES.md` when adding a new implementation reference.
