# TrafficMonitor Mouse Battery Plugin (Logi / MCHOSE)

一个用于 **TrafficMonitor** 的原生 C++ 鼠标电量插件。可在插件选项中选择：

- **Logitech / Logi**：Windows HID + HID++ 2.0
- **迈从 / MCHOSE**：按 `Fransice/dsh-mchose-battery` 的 WebHID 协议行为移植为原生 Windows HID

当前版本：**v1.1.1**。

## v1.1.x：迈从 MCHOSE 支持

在 TrafficMonitor 中打开：

```text
右键 TrafficMonitor → 其他功能 → 插件管理 → Mouse Battery (Logi / MCHOSE) → 选项
```

可选择：

```text
○ Logitech / Logi（HID++ 2.0）
○ 迈从 / MCHOSE（dsh-mchose-battery HID 方法）
```

点击“确定”后立即切换读取线程，不需要重启 TrafficMonitor。选择会保存到 TrafficMonitor 提供的插件配置目录：

```text
LogiBatteryPlugin.ini
```

## MCHOSE 读取方式

### MCHOSE G3 A（实测）

- VID/PID：`0xA8A5:0x2255`
- 配置接口：`UsagePage 0xFF01`、`Usage 0x0010`（MI_02）
- 查询报告：报告 ID `0`，发送 `55 30 A5 0B 2E 01 01 01`，其余补零
- 返回报告：`AA 30 ...`，第 9 个数据字节为电量百分比，第 10 个数据字节为充电标志
- 实测返回示例：`00 AA 30 A5 0B 0A 01 01 01 61 00 ...`，表示 `97%`、未充电

### 旧 MCHOSE 协议

按 `dsh-mchose-battery` 当前实现移植：

- VID：`0x3837`
- 配置 HID UsagePage：`0xFF01`
- 查询 Report ID：`0x11`
- 输入 Report ID：`0x13`
- 查询 payload：64 字节，默认 `0xFF`，前两个字节分别为 `0x0B ^ 0xFF`、`0xAA ^ 0xFF`
- 对输入 `0x13` 的 payload 每个字节执行 `XOR 0xFF`
- 解码后首字节必须为 `0xE2`
- `byte[4]`：电量百分比
- `byte[3] != 0`：充电中
- `byte[9...]`：设备名（ASCII，遇 `0x00` 结束）
- 有线 USB PID：`0x4018`
- 2.4G PID：`0x100A`
- 优先有线，再选 2.4G；其他 `VID 0x3837 + UsagePage 0xFF01` 接口也会作为 fallback 尝试
- 与原项目一致，每 **5 秒**刷新一次

原项目通过 WebHID 先调用 `sendFeatureReport()`，失败后回退 `sendReport()`；本插件对应使用 Windows `HidD_SetFeature()`，失败时回退到 HID output report。

## Logitech 读取方式

保持 v1.0.0 的 Native HID++ 实现：

- Logitech VID `0x046D`
- vendor-defined HID UsagePage `0xFFxx`
- Usage `0x0001` = HID++ short，`0x0002` = HID++ long
- 鼠标 DeviceType = `3`
- 电量特性优先级：`0x1000` → `0x1001` → `0x1004`
- 默认成功读取后 600 秒轮询一次，失败后 10 秒重试

Logitech 分支参考 LGSTrayBattery 的 Native HID 行为，不要求安装或运行 LGSTrayBattery、G HUB、Options+。

## 显示

- `85%`：正常使用电池
- `85%+`：充电中
- `100%=`：已充满
- `N/A`：当前未获取到电量

显示项 ID 继续使用 v1.0.0 的 `LogiMouseBatteryV1`，因此覆盖升级 DLL 时不会主动重置 TrafficMonitor 中已有的显示项/颜色配置。

Tooltip 会显示：

- 当前选择的品牌
- 鼠标名称
- 电量和充电状态
- MCHOSE 的 USB / 2.4G 模式
- Logitech 的电压（若使用 HID++ `0x1001`）
- 实际读取方式

## 编译

推荐 Visual Studio 2022 + CMake。

### x64

```bat
cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build-x64 --config Release
```

生成：

```text
build-x64\Release\LogiBatteryPlugin.dll
```

### Win32

```bat
cmake -S . -B build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build-x86 --config Release
```

> DLL 位数必须与 TrafficMonitor.exe 一致。

### PowerShell

```powershell
.\build.ps1 -Arch x64
# 或
.\build.ps1 -Arch Win32
```

工程仍通过 `.def` 强制导出精确的 `TMPluginGetInstance`。

## 安装 / 从 v1.0.0 升级

将新版本：

```text
LogiBatteryPlugin.dll
```

覆盖到：

```text
TrafficMonitor\plugins\LogiBatteryPlugin.dll
```

然后重启 TrafficMonitor。

首次仍默认选择 **Logitech / Logi**，如果使用迈从鼠标，请进入插件管理的“选项”切换到 **迈从 / MCHOSE**。

## MCHOSE 兼容性

当前实现包含旧 E2 协议，以及已实测的 G3 A 专用协议。已明确针对：

- `VID 0x3837`
- `PID 0x4018`：USB wired
- `PID 0x100A`：2.4G receiver
- `VID/PID 0xA8A5:0x2255`、`UsagePage 0xFF01`、`Usage 0x0010`：MCHOSE G3 A

旧协议会尝试同 VID、同 `0xFF01` 配置 UsagePage 的其他 PID，但其他迈从型号是否采用同一个 E2 协议需要实机验证。

若显示 `N/A`，常见原因：

- 鼠标休眠，查询时没有返回电量 report；
- 型号使用不同 PID / 不同 HID 协议；
- Windows 上对应 HID collection 无法以读写方式打开；
- 其他配置软件正在独占接口。

## 设计来源与许可

TrafficMonitor Plugin API：
- https://github.com/zhongyang219/TrafficMonitor

Logitech HID++ 参考：
- https://github.com/andyvorld/LGSTrayBattery

MCHOSE HID 参考：
- https://github.com/Fransice/dsh-mchose-battery

`dsh-mchose-battery` 为 MIT License；LGSTrayBattery 为 GPL-3.0。由于 Logitech 分支包含基于其 GPL 实现思路/数据的部分，本工程整体继续采用 **GPL-3.0-or-later**。
