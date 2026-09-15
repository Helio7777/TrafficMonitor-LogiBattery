# TrafficMonitor Logi Mouse Battery Plugin

一个用于 **TrafficMonitor** 的原生 C++ 插件，在主窗口/任务栏显示 Logitech（Logi）无线鼠标电量。

## 特点

- 直接通过 Windows HID + Logitech **HID++ 2.0** 读取电量。
- 不要求运行 LGSTrayBattery，也不依赖 .NET 或额外的 `hidapi.dll`。
- 参考 LGSTrayBattery 当前 Native HID 实现的设备发现与电量特性选择逻辑：
  - Logitech VID `0x046D`
  - vendor-defined HID UsagePage `0xFFxx`
  - Usage `0x0001` = HID++ short，`0x0002` = HID++ long
  - 鼠标 DeviceType = `3`
  - 电量特性优先级：`0x1000` → `0x1001` → `0x1004`
- 后台线程查询，不在 TrafficMonitor 的显示调用中执行阻塞 HID I/O。
- 默认成功读取后每 600 秒轮询一次；失败后每 10 秒重试，与 LGSTrayBattery Native 路径的默认 `PollPeriod=600` / `RetryTime=10` 对齐。
- 插件菜单提供“立即刷新 Logi 鼠标电量”。

## 显示

- `85%`：正常使用电池
- `85%+`：充电中
- `100%=`：已充满
- `N/A`：当前未读取到支持的鼠标电量

鼠标悬停 TrafficMonitor 时，插件 Tooltip 会显示鼠标名称、百分比、充电状态、电压（若设备使用 0x1001）以及命中的 HID++ 电量特性。

## 编译

推荐 Visual Studio 2022 + CMake。工程使用静态 MSVC CRT，并通过 `.def` 文件确保 x64/Win32 都以精确名称导出 `TMPluginGetInstance`。

### x64

```bat
cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build-x64 --config Release
```

DLL 位于：

```text
build-x64\Release\LogiBatteryPlugin.dll
```

### Win32

如果你使用 32 位 TrafficMonitor：

```bat
cmake -S . -B build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build-x86 --config Release
```

> 插件 DLL 的位数必须和 TrafficMonitor.exe 一致。

### 一键构建（PowerShell）

也可以在项目根目录运行：

```powershell
.\build.ps1 -Arch x64
# 或：.\build.ps1 -Arch Win32
```

项目还包含 `.github/workflows/build.yml`，可在 GitHub Actions 中自动构建 x64 / Win32 DLL 并检查入口导出。

## 安装

把 `LogiBatteryPlugin.dll` 放到：

```text
TrafficMonitor\plugins\LogiBatteryPlugin.dll
```

重新启动 TrafficMonitor，然后：

1. 右键 TrafficMonitor。
2. 打开插件管理，确认 `Logi Mouse Battery` 已加载。
3. 在任务栏窗口/主窗口“显示设置”里勾选 `Logi 鼠标电量`。

## 兼容性说明

当前版本聚焦 **HID++ 2.0** 鼠标，并按 LGSTrayBattery 的 Native HID 路径实现。理论上适用于使用 Unifying / Bolt / Lightspeed / 直接 HID++ 接口且提供 0x1000、0x1001 或 0x1004 电量特性的 Logitech 鼠标。

以下情况可能显示 `N/A`：

- 鼠标处于深度休眠，暂时不响应 HID++ ping；
- 设备只暴露 HID++ 1.0 电量接口；
- 某些新版设备/驱动隐藏了 vendor-defined HID++ collection；
- G HUB / Options+ 或第三方程序独占了 HID 接口；
- 某设备的 short/long collection 没有相同 Windows Container ID。

## 设计来源与许可

TrafficMonitor 插件 ABI：
- https://github.com/zhongyang219/TrafficMonitor

HID++ 获取方式参考：
- https://github.com/andyvorld/LGSTrayBattery

本项目重新以原生 Win32 HID 实现协议流程；其中 0x1001 的通用 Li-Po 电压查表沿用 LGSTrayBattery 的公开 GPL-3.0 实现思路和数据，因此本项目整体采用 **GPL-3.0-or-later**。

## 下一步建议

如果要做成更完整的发布版本，建议继续加入：

- 多鼠标选择/按设备名称固定显示；
- 配置界面（轮询周期、显示名称、充电符号）；
- G HUB WebSocket `ws://localhost:9010` fallback；
- HID++ 1.0 旧设备 fallback；
- DeviceChange 热插拔即时重扫；
- x64 / x86 GitHub Actions 自动构建发布 DLL。
