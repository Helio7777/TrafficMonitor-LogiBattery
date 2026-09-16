# Contributing

感谢你帮助改进 TrafficMonitor Mouse Battery。提交补丁前，请先搜索现有 Issue，避免重复工作。

## 开发环境

需要 Windows、Visual Studio 2022（含“使用 C++ 的桌面开发”）和 CMake 3.20 或更高版本。克隆仓库后执行：

```powershell
.\build.ps1 -Arch x64 -Config Release
.\build.ps1 -Arch Win32 -Config Release
```

两个架构都必须成功构建，并保留 `TMPluginGetInstance` 导出。生成的 `build-*` 目录和 DLL 不应提交到仓库。

## 修改原则

- 遵循现有 C++17 风格：四空格缩进、独立行大括号、数据成员使用尾随下划线。
- 保持 `/W4 /permissive- /utf-8 /EHsc` 构建；新增警告应在提交前处理。
- 将品牌协议实现限制在对应后端，通用显示状态放在 `MouseBatteryTypes.h`。
- 避免高频轮询。无线设备查询可能阻止休眠并增加耗电。
- 保留现有显示项 ID 和配置键，除非变更包含明确的迁移方案。

## 设备支持

新增或修复硬件支持时，请说明品牌、完整型号、VID/PID、连接方式以及复现结果。公开 Issue 和提交中必须隐藏设备路径中的实例标识、序列号和其他唯一信息。不要提交原始抓包、厂商固件或未获授权的协议资料。

硬件修改至少应手动验证：首次加载、立即刷新、休眠后恢复、充电状态、设备断开和 `N/A` 回退。无法覆盖的场景应在 PR 中明确列出。

## 提交与 PR

提交标题使用简洁的 Conventional Commit 风格，例如 `fix: retry MCHOSE reads after wake`。PR 应包含：

- 变更目的和用户可见行为；
- 已执行的构建与测试命令；
- 涉及的设备和连接方式；
- UI 变化截图或脱敏日志（如适用）；
- 关联的 Issue。

提交贡献即表示你有权按项目的 GPL-3.0-or-later 许可证提供相关内容。
