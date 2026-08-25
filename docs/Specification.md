# dtkwebkit 开发规范说明

> 依据：DTK 开发指南 + linuxdeepin GitHub 仓库规范（dtkcore/dtkwidget 源码实践）

## 1. 命名规范

| 范畴 | 规则 | 示例 |
|------|------|------|
| 类名 | PascalCase | `DWPEView`、`WebSurface` |
| 成员函数 | camelCase | `loadUrl()`、`runJavaScript()` |
| 属性/信号 | snake_case，on 前缀 | `onLoadFinished`、`onRenderCrashed` |
| DTK D 前缀宏 | 大写 D 开头 | `DWIDGET_USE_NAMESPACE`、`DStyle` |
| 命名空间 | 小写，对应模块名 | `DTKWPE`（桥接层命名空间） |
| 文件名 | 全小写，下划线分词 | `wpe_view.h`、`web_surface.cpp` |

## 2. 代码风格

仓库根目录放置 `.clang-format`，与 dtkcore 一致。核心配置：

| 配置项 | 值 |
|--------|-----|
| `IndentWidth` | 4 |
| `TabWidth` | 4 |
| `UseTab` | Never |
| `ColumnLimit` | 130 |
| `BreakBeforeBraces` | Custom（class/struct/function 换行；control 不换行；namespace 不换行） |
| `AccessModifierOffset` | -4 |
| `PointerAlignment` | Right（`QObject *obj`） |
| `NamespaceIndentation` | Inner |
| `AllowShortFunctionsOnASingleLine` | Inline |
| `SpaceBeforeParens` | ControlStatements |
| `SortIncludes` | Never |

CI 在 PR 检查中运行 `clang-format --dry-run --Werror --style=file`。

## 3. 构建规范（CMake）

- `cmake_minimum_required(VERSION 3.13)`
- 从 `VERSION` 文件读取版本号：`file(READ VERSION FILE_VERSION)` → `project(VERSION ${FILE_VERSION})`
- 默认安装前缀 `/usr`（DTK 标准）
- 依赖通过 `find_package`（Qt6/DTK6）和 `pkg_check_modules`（WPE/EGL/GStreamer/GLib）声明
- 生成 CMake Config 文件供下游使用

## 4. 仓库结构

```
dtkwebkit/
├── .clang-format
├── .clog.toml
├── .commitlintrc.json
├── .github/workflows/ci.yml
├── .reuse/dep5
├── CMakeLists.txt
├── VERSION
├── LICENSE
├── LICENSES/
├── debian/
├── docs/
│   └── Specification.md
├── include/dtkwebkit/
├── src/
├── tests/
└── examples/minibrowser/
```

## 5. 提交规范（Conventional Commits）

```
<type>(<scope>): <subject>

<body>
```

| type | 用途 |
|------|------|
| `feat` | 新功能 |
| `fix` | 修复 |
| `refactor` | 重构 |
| `test` | 测试 |
| `docs` | 文档 |
| `chore` | 构建/杂务 |
| `ci` | CI 配置 |
| `style` | 代码风格 |
| `perf` | 性能优化 |
| `build` | 构建系统 |

CI 闸门：commitlint + CLA check + license-check (REUSE) + clang-format + build。

> 注意：commit message 中不得附加 AI co-author 行。

## 6. 许可证

LGPL-3.0-or-later，与 dtkcore/dtkwidget 一致。

- `LICENSE` — 许可证全文
- `LICENSES/LGPL-3.0-or-later.txt` — SPDX 许可证文本
- `.reuse/dep5` — 版权归属声明
- 每个源文件须包含 `SPDX-License-Identifier: LGPL-3.0-or-later`

## 7. DTK 控件使用

| 设计规范 | 要求 |
|---------|------|
| 深色/浅色 | 支持 DPalette 深浅色双模式，默认浅色 |
| 活动用色 | 主色 #0081FF，通过 DPalette::highlight() 获取 |
| 布局间距 | 控件间距 ≥10px，N×10 为单位 |
| 工具栏 | DTitlebar + logo 32px + 左边距 10px |
| 窗口层级 | DMainWindow 分层窗口，整体框架 + 叠加组件 |
| 控件选择 | 优先 DLineEdit/DComboBox/DBlurEffectBackground 等 DTK 控件 |
| 国际化 | 所有 UI 文本走 `tr()` |
| 高 DPI | `Qt::AA_UseHighDpiPixmaps` |
| 居中显示 | `Dtk::Widget::moveToCenter(&w)` |

## 8. 标准路径（Deepin Application Specification）

| 路径 | 位置 |
|------|------|
| 配置 | `$XDG_CONFIG_HOME/{org}/{app}` |
| 日志 | `$HOME/.log/{org}/{app}` |
| 缓存 | `$XDG_CACHE_HOME/{org}/{app}` |
| 数据 | `$XDG_DATA_HOME/{org}/{app}` |

Web Inspector 端口配置、WPE 缓存路径须走标准路径，不硬编码。

## 9. 架构偏差说明

方案 V3.1 §5.2 要求实现 `DWPEDisplay`（继承 WPEDisplay）和 `DWPEToplevel`（继承 WPEToplevel）两个 GObject 子类。实际环境中 WPEPlatform 不可用（系统未安装 wpeplatform 包），实际使用 **WPEBackend-FDO 1.12.0 EGL exportable** API.

### 偏差对照

| 方案要求 | 实际实现 | 原因 |
|---------|---------|------|
| DWPEDisplay (继承 WPEDisplay) | `wpe_fdo_initialize_for_egl_display(EGLDisplay)` | WPEPlatform 包不可用，改用 FDO 初始化 |
| DWPEToplevel (继承 WPEToplevel) | QOpenGLWindow 直接作为 toplevel | 无 WPEToplevel 基类可用 |
| DWPEView (继承 WPEView) | DWPEView : QOpenGLWindow + WPEBackend-FDO EGL exportable | 改用 `wpe_view_backend_exportable_fdo_egl_create()` + client 回调 |
| DMA-BUF 纹理导入 | EGLImageKHR 直接导入（FDO EGL exportable） | FDO 直接导出 EGLImage，无需 DMA-BUF fd 手动处理 |
| buffer 释放 `g_clear_object` | `wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image()` | FDO 释放路径而非 GObject 释放 |

当 WPEPlatform 2.54+ 稳定后，可切回 GObject 子类方案，桥接层通过 WebSurface 抽象接口隔离切换。
