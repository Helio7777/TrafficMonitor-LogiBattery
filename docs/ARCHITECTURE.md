# Architecture

## Overview

插件实现 TrafficMonitor Plugin API v8，并导出 `TMPluginGetInstance`。`MouseBatteryPlugin` 负责宿主生命周期、品牌选择、配置持久化、显示文本和工具提示；设备通信由独立后端完成。

## Data Flow

1. TrafficMonitor 加载 DLL 并获取单例插件对象。
2. 插件根据 `LogiBatteryPlugin.ini` 中的品牌设置启动一个后台服务。
3. 后台服务枚举 Windows HID collection，查询设备并生成 `BatterySnapshot`。
4. `DataRequired()` 复制最新快照，不在 TrafficMonitor UI 线程中执行阻塞式 HID I/O。
5. 显示项将快照格式化为百分比、充电标记和工具提示。

## Backends

`LogitechHidpp.cpp` 将同一物理设备的 HID++ short/long collection 分组，发现鼠标设备索引，并按优先级读取 HID++ 电量特性。成功读取后采用较长轮询周期，失败时缩短重试间隔。

`MchoseHid.cpp` 处理两类配置接口：已验证的 G3 A 状态查询，以及公开实现中使用的旧版 E2 状态协议。MCHOSE 型号间的 VID/PID 和报告格式可能不同，因此新设备必须经过实机验证，不能只按品牌 VID 推断兼容性。

## Threading and Lifetime

每个后端拥有一个工作线程、条件变量和互斥保护的快照。切换品牌时，插件先停止旧线程，再启动新后端。服务析构会等待线程退出，防止 DLL 卸载后继续访问代码或句柄。

## Compatibility Contracts

- `LogiMouseBatteryV1` 是持久化显示项 ID，不应随项目改名而改变。
- `LogiBatteryPlugin.ini` 和 `[MouseBattery] Brand` 是现有配置格式。
- DLL 位数必须与 TrafficMonitor 进程一致。
- ABI 声明仅覆盖本插件使用的 Plugin API v8 接口；变更前应与 TrafficMonitor 上游核对。
