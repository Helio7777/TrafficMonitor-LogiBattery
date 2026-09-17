# Changelog

本项目的版本变化记录在此。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### Changed

- 重写中英文项目说明，并优化插件名称、描述和选项文案。
- 增加贡献指南、安全策略、Issue/PR 模板和自动发布流程。

## [1.1.2] - 2026-09-17

### Fixed

- 修复 Logitech 与 MCHOSE OVERLAPPED HID I/O 在超时/停止路径下的取消生命周期，确保取消后等待 I/O 真正完成再释放资源。
- 为后台查询线程增加 stop event 中断路径，减少品牌切换和插件停止时的阻塞等待。
- 强化 HID++ 响应/错误帧关联：同时校验 device/feature/function/software，并支持 0x8F 与 0xFF 错误响应。
- Logitech 端点能力改为按 short/long 实际可用性工作，并在 short-only/long-only 场景下进行能力驱动发送。
- 修正 0x1001 电压阈值边界换算（`>=`）并增加 Debug 逻辑自检覆盖响应匹配、错误解析与电压边界。

## [1.1.1] - 2026-09-15

### Added

- 增加已实机验证的 MCHOSE G3 A (`A8A5:2255`) HID 电量读取。
- 保留旧版 MCHOSE E2 协议兼容路径。

## [1.1.0] - 2026-09-15

### Added

- 增加可切换的 MCHOSE 电量后端和持久化品牌选项。

## [1.0.0] - 2026-09-15

### Added

- 首次发布 Logitech HID++ 2.0 电量插件。

[Unreleased]: ../../compare/v1.1.2...HEAD
[1.1.2]: ../../releases/tag/v1.1.2
[1.1.1]: ../../releases/tag/v1.1.1
[1.1.0]: ../../releases/tag/v1.1.0
[1.0.0]: ../../releases/tag/v1.0.0
