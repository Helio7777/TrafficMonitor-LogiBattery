# TrafficMonitor Mouse Battery

[简体中文](README.md)

A native Windows plugin that shows Logitech and MCHOSE mouse battery information in [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).

The plugin talks directly to Windows HID devices. It does not require G HUB, Options+, or a vendor background service, and it does not transmit device information.

## Features

- Logitech HID++ 2.0 battery features `0x1000`, `0x1001`, and `0x1004`.
- Verified support for MCHOSE G3 A, plus experimental support for devices using the legacy MCHOSE E2 protocol.
- Brand selection stored through TrafficMonitor's plugin configuration directory.
- Battery, charging, connection, voltage, and backend details in the display item and tooltip.
- Reproducible x64 and Win32 builds with the static MSVC runtime.

## Compatibility

| Device | Status | Notes |
| --- | --- | --- |
| Logitech HID++ 2.0 mice | Supported | Availability depends on the battery features exposed by the device. |
| MCHOSE G3 A (`A8A5:2255`) | Verified | Native 2.4 GHz HID battery query. |
| MCHOSE devices with VID `3837` | Experimental | Uses the publicly documented E2 protocol. |

## Install

1. Download the archive matching the architecture of `TrafficMonitor.exe` from [Releases](../../releases).
2. Copy `LogiBatteryPlugin.dll` into TrafficMonitor's `plugins` directory.
3. Restart TrafficMonitor and enable the **Mouse Battery** plugin item.
4. For MCHOSE devices, open the plugin options and select **MCHOSE**.

Values use `+` for charging, `=` for fully charged, and `N/A` when no reading is available.

## Build

Use Windows with Visual Studio 2022 Desktop development with C++ and CMake 3.20 or newer:

```powershell
.\build.ps1 -Arch x64 -Config Release
.\build.ps1 -Arch Win32 -Config Release
```

Release maintainers can embed the public repository address with `-ProjectUrl "https://github.com/OWNER/REPOSITORY"`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md), and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before submitting a change. Device support requests must not contain full device paths or unredacted HID captures.

This project is licensed under [GPL-3.0-or-later](LICENSE). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for acknowledgements.
