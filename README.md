# TrafficMonitor Mouse Battery

[English](README.en.md)

一个面向 Windows 的原生 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 鼠标电量插件。在任务栏或主窗口中显示 Logitech 与 MCHOSE 鼠标的电量、充电状态和连接信息。

插件直接访问 Windows HID，不依赖 G HUB、Options+ 或厂商常驻程序，也不会上传设备信息。

## 功能

- 支持 Logitech HID++ 2.0 电量特性 `0x1000`、`0x1001` 和 `0x1004`。
- 支持已实机验证的 MCHOSE G3 A，以及部分采用旧版 E2 协议的 MCHOSE 设备。
- 可在插件选项中切换品牌，配置会随 TrafficMonitor 保存。
- 显示电量、充电或充满状态，并在工具提示中提供设备和连接信息。
- 提供 x64 与 Win32 构建，使用静态 MSVC 运行库。

## 兼容性

| 品牌 / 设备 | 状态 | 说明 |
| --- | --- | --- |
| Logitech HID++ 2.0 鼠标 | 支持 | 实际可用性取决于设备是否公开受支持的电量特性。 |
| MCHOSE G3 A (`A8A5:2255`) | 已验证 | 支持原生 2.4G HID 电量读取。 |
| MCHOSE `VID 3837` 设备 | 实验性 | 支持公开的 E2 协议；不同型号可能采用其他协议。 |

欢迎通过 [设备支持请求](../../issues/new?template=device-support.yml) 提交未覆盖的型号。请勿公开上传完整设备路径或未经脱敏的 HID 抓包。

## 安装

1. 从 [Releases](../../releases) 下载与 `TrafficMonitor.exe` 位数一致的压缩包。
2. 解压并将 `LogiBatteryPlugin.dll` 放入 TrafficMonitor 的 `plugins` 目录。
3. 重启 TrafficMonitor，在插件管理中启用 **Mouse Battery** 显示项。
4. 使用 MCHOSE 时，在插件“选项”中将读取品牌切换为 **MCHOSE**。

显示值含义：`85%` 表示使用电池，`85%+` 表示充电中，`100%=` 表示已充满，`N/A` 表示暂时未读取到电量。

## 构建

需要 Windows、Visual Studio 2022（含“使用 C++ 的桌面开发”）和 CMake 3.20 或更高版本。

```powershell
.\build.ps1 -Arch x64 -Config Release
.\build.ps1 -Arch Win32 -Config Release
```

产物位于 `build-x64\Release` 或 `build-Win32\Release`。也可以直接使用 CMake：

```powershell
cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build-x64 --config Release
```

发布构建可通过 `-ProjectUrl` 写入插件主页，例如：

```powershell
.\build.ps1 -Arch x64 -ProjectUrl "https://github.com/OWNER/REPOSITORY"
```

## 排查

- 确认 DLL 与 TrafficMonitor 的架构一致。
- 唤醒鼠标后执行插件命令“立即刷新鼠标电量”。
- 退出可能独占配置接口的厂商软件后重试。
- 查看工具提示中的检测结果；未知型号可能需要新增协议适配。

## 参与项目

架构与 HID 后端说明见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。提交代码前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 和 [SECURITY.md](SECURITY.md)。版本变化记录在 [CHANGELOG.md](CHANGELOG.md)。

项目采用 [GPL-3.0-or-later](LICENSE) 许可。第三方实现参考与许可信息见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
