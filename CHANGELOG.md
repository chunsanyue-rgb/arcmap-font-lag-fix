# Changelog

All notable changes to this project are documented here.
本项目的所有重要变更都记录在此。

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] — 2026-11-04

首个公开版本 / first public release.

### Added / 新增

- **`afFontShim.dll`** — 47 个 gdi32 导出（45 透传 + 2 钩子），实现三层优化：
  内存 DC 单遍枚举与缓存、按字体名预去重、回放期 `CB_GETCOUNT` 抑制。
  *47 gdi32 exports (45 pass-throughs + 2 hooks) implementing the three-layer fix:
  single-pass memory-DC enumeration with caching, font-name pre-deduplication, and
  `CB_GETCOUNT` suppression during replay.*
- **`afpatch.exe`** — 19 KB 原生补丁工具，命令：`discover` / `check` / `verify` /
  `patch` / `restore` / `list` / `version`。带安装前结构校验，不匹配即拒绝。
- **一键脚本包** — `01-安装_自动备份.bat` / `02-仅备份原文件.bat` /
  `03-回退还原.bat` / `04-查看状态.bat`，支持 `/auto` 静默部署。
- **`afFontShim.ini`** — 运行期开关：`enabled` / `dedupe_family` / `use_memory_dc` /
  `fast_combo` / `log` / `charset_whitelist`，用于定位与应急回退。
- 备份布局 `%ProgramData%\ArcMapFontFix\backup\<主机名>_<时间戳>_<版本>\`
  （含 `afUILib.dll.orig` 与 `info.txt`）。

### Measured / 实测

| 指标 | 修复前 | 修复后 |
|------|-------:|-------:|
| 首次打开「标注」页签 | 11.4 s | 0.36 s |
| 缓存命中后再次打开 | 11.4 s | 0.27 s |
| 字体条目数 | 779 | 779（一致） |
| 枚举回调次数 | 3087 | 779 |

### Verified / 已验证

- ArcGIS Desktop **10.2.2** (32-bit)，Windows，1111 种系统字体。
- 字体下拉框条目数与原生逻辑逐条一致；无业务逻辑改动。

### Known limitations / 已知限制

- 仅覆盖字体列表构建路径；其他卡顿原因无关。
- 10.7+ 无需安装；10.4.1–10.6.1 建议优先使用 Esri 官方补丁。
- 补丁为文件级、单机生效；ArcGIS 修复安装/升级后需重跑安装脚本。

### Development history / 开发过程中的关键节点

- 通过 32 位栈采样（`SuspendThread` + `Wow64GetThreadContext`）定位到
  `afUILib!AfFontsListCombo::Init` 的 O(n²) 判重循环。
- 由"改代码段"转为"改导入表"（新增只读节 `.shim` + 描述符重指向），
  换取可校验、可回退、失败可拒绝。
- 去重口径由"名字+类型"(783) 校正为"仅名字"(779)，与应用侧一致。
- 放弃两遍枚举，改单遍 + `HeapAlloc` 扩容，去掉翻倍成本。
- 工具输出由标准输出改为写 `afpatch.out`，绕开 TCC 控制台静默与代码页问题。
