# dtkwebkit

WPE WebKit Qt6 自研嵌入层 — 在 Qt6 宿主中直渲 WPE WebKit，替代 Qt WebEngine。

面向信创操作系统（UOS），零 GTK / Qt WebEngine / CEF 依赖，保留 Vue3/Vite/npm/DevTools 上游生态。

## 背景

| 维度 | 说明 |
|------|------|
| 宿主环境 | 信创 UOS，Qt6 主壳 |
| 替换目标 | Qt WebEngine（Chromium ~3500 万行） |
| 自研率提升 | 分母剔除 Chromium + GTK+Cairo，分子计入自研桥接层 3k~7k 行 C++ |
| 前端兼容 | Vue3 SPA（164 tsx + 29 Pinia store + 12 QWebChannel 通道）零修改运行 |

## 架构

```
┌────────────────────────────────────────────────────┐
│  应用层：Vue3 SPA（现有前端，不改一行）              │
│  ├─ 12 个 QWebChannel 通道 → window.host 适配层      │
│  └─ 流式渲染 / Markdown / Milkdown / 拖拽上传 ...   │
├────────────────────────────────────────────────────┤
│  宿主层：Qt6 Widgets 主壳 + DTK 重绘控件              │
│  └─ WebSurface 抽象接口（Facade）                    │
├────────────────────────────────────────────────────┤
│  桥接层（自研，本仓库）                               │
│  ├─ DWPEView           (WPEBackend-FDO EGL 纹理管线) │
│  ├─ DWPEEventTranslator (Qt→WPE 事件转换)            │
│  ├─ DWPESchemeHandler   (app:// 资源虚拟化)           │
│  ├─ DWPEBridge          (window.host JS 双向桥)       │
│  └─ DWPEChannelAdapter  (QWebChannel 12 通道路由)     │
├────────────────────────────────────────────────────┤
│  引擎层（上游）：WPE WebKit 2.46+ + libwpe 1.16+     │
│  └─ WPEBackend-FDO 1.12+ (EGL exportable)           │
├────────────────────────────────────────────────────┤
│  系统层：Mesa EGL/GLES · GLib · GStreamer · UOS     │
└────────────────────────────────────────────────────┘
```

## 模块

| 类 | 文件 | 职责 |
|----|------|------|
| `WebSurface` | `include/dtkwebkit/web_surface.h` | 平台无关抽象接口，应用层唯一依赖 |
| `DWPEView` | `src/wpe_view.h/.cpp` | WPEBackend-FDO EGL exportable → Qt QOpenGLWindow 纹理渲染 |
| `DWPEEventTranslator` | `src/wpe_event_translator.h/.cpp` | Qt 键盘/鼠标/滚轮/触摸 → WPE 输入事件转换 |
| `DWPESchemeHandler` | `src/wpe_scheme_handler.h/.cpp` | `app://` 自定义 scheme，内存文件系统 + SPA history fallback |
| `DWPEBridge` | `src/wpe_bridge.h/.cpp` | `window.host.postMessage()` ↔ 原生双向桥（Promise 往返） |
| `DWPEChannelAdapter` | `src/wpe_channel_adapter.h/.cpp` | 12 个 QWebChannel 通道路由 + signal 推送 |

## 依赖

| 依赖 | 最低版本 | 说明 |
|------|---------|------|
| Qt6 (Core/Gui/OpenGL/OpenGLWidgets/Widgets/Test) | 6.5 | 宿主 GUI 框架 |
| DTK6 (Dtk6Core/Dtk6Widget) | 6.0 | DTK 重绘控件 |
| WPE WebKit | 2.46 | Web 渲染引擎（目标 2.50+） |
| libwpe | 1.16 | WPE 平台抽象 |
| WPEBackend-FDO | 1.12 | EGL exportable 后端 |
| GStreamer | 1.20 | 媒体后端（`<video>` 播放） |
| Mesa EGL/GLES | 1.5/3.2 | 图形栈 |
| GLib | 2.0 | GObject 事件循环 |

## 构建

```bash
cd dtkwebkit
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

## 测试

```bash
cd build
ctest --output-on-failure
```

## 运行示例

```bash
cd build
LD_LIBRARY_PATH=. ./examples/minibrowser/minibrowser
```

minibrowser 创建 DMainWindow + DWPEView，通过 `app://` scheme 加载测试页面，验证 JS bridge 往返。

## 安装

```bash
cd build
sudo make install
# 库:     /usr/lib/<arch>/libdtkwebkit.so
# 头文件:  /usr/include/dtkwebkit/
# CMake:   /usr/lib/<arch>/cmake/dtkwebkit/
```

## 在 CMake 项目中使用

```cmake
find_package(dtkwebkit REQUIRED)
target_link_libraries(your-app PRIVATE dtkwebkit::dtkwebkit)
```

```cpp
#include <dtkwebkit/web_surface.h>

// DWPEView 实现 WebSurface 接口
// 应用层仅依赖 WebSurface 抽象，不耦合 WPE 细节
```

## 关键设计决策

| 决策 | 理由 |
|------|------|
| WPEBackend-FDO EGL exportable（非 WPEPlatform） | 系统中 WPEPlatform 包不可用，FDO 1.12 成熟稳定 |
| `app://` scheme（禁止 `file://`） | 资源虚拟化 + SPA 路由 fallback + 安全边界 |
| `window.host` + JS Proxy 替代 QWebChannel | 前端 12 通道零修改，适配层在 user script 注入 |
| `script-message-with-reply-received` 信号 | WPE WebKit 原生 Promise 往返 API（非 WebKitUserMessage） |
| `QT_NO_KEYWORDS` 全局定义 | GLib `signals` 成员名与 Qt `#define signals` 冲突 |

## 许可证

LGPL-3.0-or-later，与 dtkcore/dtkwidget 一致。

- `LICENSE` — 许可证全文
- `LICENSES/LGPL-3.0-or-later.txt` — SPDX 许可证文本
- `.reuse/dep5` — 版权归属声明
- 每个源文件包含 `SPDX-License-Identifier: LGPL-3.0-or-later`

## 开发规范

详见 [docs/Specification.md](docs/Specification.md)。

- 命名：类名 PascalCase（`DWPEView`）、信号 snake_case 带 on 前缀（`onLoadFinished`）、文件名全小写下划线分词
- 代码风格：`.clang-format` 同 dtkcore（IndentWidth=4, ColumnLimit=130, PointerAlignment=Right）
- 提交：Conventional Commits，CI 闸门 commitlint + clang-format + REUSE license-check + build
- DTK 控件：DMainWindow / DApplication / DPalette 深浅色 / moveToCenter / tr() 国际化
