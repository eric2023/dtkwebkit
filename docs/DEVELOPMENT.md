# dtkwebkit 开发指南

> 版本 0.1.0 ｜ 2026-09-09 ｜ 面向 dtkwebkit 桥接层开发者

本文档是 dtkwebkit 项目的完整开发参考，涵盖架构、模块、API、构建、测试、打包及开发规范。

---

## 1. 项目概述

### 1.1 定位

dtkwebkit 是 **WPE WebKit Qt 自研嵌入层**：在 Qt5/Qt6 宿主窗口中直渲 WPE WebKit 合成帧，替代 Qt WebEngine（Chromium）。

- 面向信创操作系统（UOS），零 GTK / Qt WebEngine / CEF 依赖
- 保留 Vue3/Vite/npm/DevTools 上游前端生态
- 桥接层自研代码 ~3k~7k 行 C++，上游 WPE WebKit 引擎不计入自研率分母

### 1.2 核心设计决策

| 决策 | 理由 |
|------|------|
| WPEBackend-FDO EGL exportable（非 WPEPlatform） | 系统中 WPEPlatform 包不可用，FDO 1.12 成熟稳定 |
| `app://` scheme（禁止 `file://`） | 资源虚拟化 + SPA 路由 fallback + 安全边界 |
| `window.host` + JS Proxy 替代 QWebChannel | 前端 12 通道零修改，适配层在 user script 注入 |
| `script-message-with-reply-received` 信号 | WPE WebKit 原生 Promise 往返 API |
| `QT_NO_KEYWORDS` 全局定义 | GLib `signals` 成员名与 Qt `#define signals` 冲突 |
| GLES2 shader 管线（非固定管线） | GLES 2.0 不含 glBegin/glEnd，必须用可编程管线 |
| GLib 主循环 pump 定时器（16ms） | Qt 事件循环不调度 GLib default context，WPE 帧投递依赖 GLib GSource |
| Qt5/Qt6 双版本兼容 | CMake 自动检测 Qt 版本，`qt_compat.h` 屏蔽 API 差异，UOS V20(Qt5) / V25(Qt6) 通用 |

---

## 2. 架构

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
│  ├─ DWPEView              (WPEBackend-FDO EGL 纹理管线) │
│  ├─ DWPEEventTranslator   (Qt→WPE 事件转换)            │
│  ├─ DWPESchemeHandler     (app:// 资源虚拟化)           │
│  ├─ DWPEBridge            (window.host JS 双向桥)       │
│  ├─ DWPEChannelAdapter    (QWebChannel 12 通道路由)     │
│  └─ DWPEInputMethodContext (Qt IME→WebKit IME 桥)      │
├────────────────────────────────────────────────────┤
│  引擎层（上游）：WPE WebKit 2.46+ + libwpe 1.16+     │
│  └─ WPEBackend-FDO 1.12+ (EGL exportable)           │
├────────────────────────────────────────────────────┤
│  系统层：Mesa EGL/GLES · GLib · GStreamer · UOS     │
└────────────────────────────────────────────────────┘
```

### 2.1 渲染管线

```
WPE WebProcess 合成帧
    │
    ▼
WPEBackend-FDO EGL exportable 回调
    │  onExportFdoEglImage(data, wpe_fdo_egl_exported_image*)
    │  onExportShmBuffer(data, wpe_fdo_shm_exported_buffer*)  ← fallback
    ▼
DWPEView::handleExportedImage()
    │  ① 释放上一帧 (dispatch_release_exported_image)
    │  ② 存储新帧到 m_currentImage
    │  ③ dispatch_frame_complete (通知 WPE 帧已显示)
    │  ④ update() 触发 paintGL
    ▼
DWPEView::paintGL()
    │  ① glClearColor + glClear (背景色)
    │  ② wpe_fdo_egl_exported_image_get_egl_image()
    │  ③ glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, eglImage)
    │  ④ GLES2 shader 绘制全屏四边形 (GL_TRIANGLE_STRIP)
    │  ⑤ Y 翻转 (EGL image 原点在左下角)
    ▼
Qt QOpenGLWindow 显示
```

### 2.2 JS 桥架构

```
JS 侧（注入到 main world）                    原生侧
┌─────────────────────────┐               ┌──────────────────────┐
│ window.host              │               │ DWPEBridge            │
│  .postMessage(msg)       │ ──JSON──→    │  onScriptMessageReceived│
│  .onMessage(data)        │ ←──JS eval── │  postMessage(msg)     │
│                          │               │                       │
│ window.qt (Proxy)        │               │ DWPEChannelAdapter    │
│  qt.channel.method(args) │ ──{channel,  │  handleMessage()       │
│  → host.postMessage()    │    method,   │  → 路由表分发           │
│                          │    data}──→  │  → channel handler     │
│ window.__TAURI_IPC__     │               │                       │
│  → host.postMessage()    │               │  emitSignal()         │
│                          │ ←─signal──── │  → host._emitSignal()  │
└─────────────────────────┘               └──────────────────────┘
```

### 2.3 架构偏差说明

方案 V3.1 要求实现 DWPEDisplay/DWPEToplevel（GObject 子类），但实际环境中 WPEPlatform 不可用。实际使用 **WPEBackend-FDO 1.12 EGL exportable** API：

| 方案要求 | 实际实现 | 原因 |
|---------|---------|------|
| DWPEDisplay (继承 WPEDisplay) | `wpe_fdo_initialize_for_egl_display()` | WPEPlatform 包不可用 |
| DWPEToplevel (继承 WPEToplevel) | QOpenGLWindow 直接作为 toplevel | 无 WPEToplevel 基类 |
| DWPEView (继承 WPEView) | QOpenGLWindow + FDO EGL exportable | 改用 `wpe_view_backend_exportable_fdo_egl_create()` |
| DMA-BUF 纹理导入 | EGLImageKHR 直接导入 | FDO 直接导出 EGLImage |
| buffer 释放 `g_clear_object` | `dispatch_release_exported_image()` | FDO 释放路径 |

---

## 3. 模块详解

### 3.1 WebSurface（公共抽象接口）

| 属性 | 值 |
|------|-----|
| 头文件 | `include/dtkwebkit/web_surface.h` |
| 命名空间 | `DTKWPE` |
| 导出宏 | `DWPE_EXPORT` |
| 继承 | `QObject` |

平台无关抽象接口，应用层唯一依赖。定义了 `loadUrl`、`setHtml`、`runJavaScript`、`postMessage`、`registerChannel`、`emitChannelSignal` 等纯虚方法，以及 `onLoadFinished`、`onRenderCrashed`、`onMessage` 等信号。跨平台后端（macOS WKWebView、Windows WebView2）可实现此接口。

> **当前状态**：`DWPEView` 尚未正式继承 `WebSurface`，而是直接暴露 API。应用层可通过 `WebSurface` 接口编程，也可直接使用 `DWPEView`。

### 3.2 DWPEView（核心视图）

| 属性 | 值 |
|------|-----|
| 头文件 | `src/wpe_view.h` |
| 实现文件 | `src/wpe_view.cpp`（~1184 行） |
| 继承 | `QOpenGLWindow` + `QOpenGLFunctions` |
| 命名空间 | `DTKWPE` |

核心 WPE WebKit 视图，嵌入 QOpenGLWindow。职责：

1. **EGL 纹理管线**：创建 WPEBackend-FDO EGL exportable backend，接收 `wpe_fdo_egl_exported_image` 帧，通过 `glEGLImageTargetTexture2DOES` 上传为 GL 纹理，GLES2 shader 绘制全屏四边形
2. **SHM fallback**：当 EGL image 导出不可用时，从 Wayland SHM buffer 读取像素数据，`glTexImage2D` 上传
3. **WebKitWebView 管理**：创建 WebView、注册信号处理器（load-changed、web-process-terminated、decide-policy、create、context-menu 等）
4. **输入事件转发**：将 Qt 键盘/鼠标/滚轮/触摸事件转发给 `DWPEEventTranslator`
5. **IME 桥接**：通过 `event()` 拦截 `QInputMethodEvent`/`QInputMethodQueryEvent`，转发给 `DWPEInputMethodContext`
6. **JS 桥初始化**：创建 `DWPEBridge`，在 LOAD_COMMITTED 时注入 bridge 代码到 main world
7. **GLib 主循环 pump**：16ms 定时器驱动 `g_main_context_iteration()`，保持 WPE 帧投递
8. **崩溃恢复**：WebProcess 崩溃后自动 reload（最多 3 次）
9. **沙箱/DevTools 控制**：可配置 bwrap 沙箱路径和 Web Inspector 端口

关键生命周期：
```
构造 → initializeGL() → initializeWPE(EGLDisplay)
                         ├─ wpe_fdo_initialize_for_egl_display()
                         ├─ wpe_view_backend_exportable_fdo_egl_create()
                         ├─ webkit_web_view_new()
                         ├─ registerScheme() + bridge init
                         ├─ g_signal_connect (8 个信号)
                         ├─ wpe_view_backend_add_activity_state()
                         └─ m_glibPollTimer.start(16)
     → wpeReady() 信号 (通知应用层可加载内容)
     → loadUrl("app:///index.html")
```

关键约束：
- **buffer 释放**：`wpe_fdo_egl_exported_image` 必须通过 `dispatch_release_exported_image()` 释放，不可裸 `free()`
- **析构顺序**：先停 GLib timer → reset bridge/scheme → release image → reset IME → unref webView → fallback destroy exportable
- **webkit_web_view_set_background_color** 禁止调用：在 AMD Picasso/Raven GPU 上触发 Mesa radeonsi SIGSEGV
- **右键菜单**：WPE 内置菜单捕获指针导致后续鼠标事件失效 + pasteboard IPC 死锁，全部拦截

### 3.3 DWPEEventTranslator（事件转换器）

| 属性 | 值 |
|------|-----|
| 头文件 | `src/wpe_event_translator.h` |
| 实现文件 | `src/wpe_event_translator.cpp`（~340 行） |
| 命名空间 | `DTKWPE` |

将 Qt 输入事件转换为 WPE 输入事件。参考来源：WPEPlatform GLFW 示例（Igalia），是事件转换的唯一参考。

| Qt 事件 | WPE 事件 | 映射规则 |
|---------|----------|----------|
| QKeyEvent | wpe_input_keyboard_event | Qt::Key → Linux evdev keycode → xkb keycode (+8) → keysym |
| QMouseEvent(press/release) | wpe_input_pointer_event | 坐标/DPR；button: Left=1, Right=2, Middle=3 |
| QMouseEvent(move) | wpe_input_pointer_event | 16ms 节流(~60FPS)；drag 时携带 pressed button |
| QWheelEvent | wpe_input_axis_event | angleDelta/8；axis 0=垂直, axis 1=水平 |
| QTouchEvent | wpe_input_touch_event | **TODO: 未实现** |
| FocusIn/Out | wpe_view_activity_state | add/remove focused state |

关键细节：
- **xkb keymap**：从系统 RMLVO (`evdev`/`pc105`/`us`) 构建默认 keymap，使 WPE 能将 evdev scancode 转换为正确 Unicode 字符
- **右键拦截**：WPE 的上下文菜单代码路径触发 pasteboard IPC 死锁，在 translator 层直接拦截
- **鼠标拖拽选词**：motion 事件必须携带 pressed button（`m_mousePressedButton`），否则 WebKit 的 `handleMouseDraggedEvent` 不启动选词

### 3.4 DWPESchemeHandler（资源虚拟化）

| 属性 | 值 |
|------|-----|
| 头文件 | `src/wpe_scheme_handler.h` |
| 实现文件 | `src/wpe_scheme_handler.cpp`（~127 行） |
| 命名空间 | `DTKWPE` |

`app://` 自定义 scheme，将 Vite 构建产物加载到内存文件系统（`QHash<QString, ResourceEntry>`），供 WebKit 以 HTTP-like 方式请求。

请求处理规则：
1. 精确匹配内存 fs → 返回 ResourceResponse(mime, bytes)
2. 未匹配 + 无文件扩展名 → 返回 index.html（SPA history 路由 fallback）
3. 未匹配 + 有文件扩展名 → 返回 404

**禁止 `file://` 回退**：所有资源必须在内存 fs 中。

### 3.5 DWPEBridge（JS 双向桥）

| 属性 | 值 |
|------|-----|
| 头文件 | `src/wpe_bridge.h` |
| 实现文件 | `src/wpe_bridge.cpp`（~278 行） |
| 命名空间 | `DTKWPE` |

通过 `window.host` 实现 Native ↔ JS 双向通信。

**JS→Native**：`window.host.postMessage(msg)` → `script-message-with-reply-received::host` 信号 → `onScriptMessageReceived()` → `m_messageHandler` → `webkit_script_message_reply_return_value()` → JS Promise resolve

**Native→JS**：`postMessage(msg)` → `evaluate_javascript("window.host.onMessage(json)")` → JS 侧回调

注入的 JS 代码（`s_mainWorldScript`）定义：
- `window.host`：postMessage（Promise 往返）、onMessage（原生→JS 回调）、_emitSignal（signal 推送）
- `window.qt`：Proxy 对象，将 `qt.channel.method(args)` 路由到 `host.postMessage({channel, method, data})`
- `window.__TAURI_IPC__`：Tauri IPC 兼容 stub

关键约束：
- bridge 代码注入到 **main world**（非 isolated world），因为 user script 在 isolated world 中对页面 JS 不可见
- 注入时机：`WEBKIT_LOAD_COMMITTED`（document 创建后、页面脚本执行前）
- 注入幂等：`window.__dtkwebkitBridgeInjected` 标志防止重复注入

### 3.6 DWPEChannelAdapter（12 通道路由）

| 属性 | 值 |
|------|-----|
| 头文件 | `src/wpe_channel_adapter.h` |
| 实现文件 | `src/wpe_channel_adapter.cpp`（~78 行） |
| 命名空间 | `DTKWPE` |

将现有 12 个 QWebChannel 通道适配到 `window.host`。JS 侧 `qt.channelName.method()` 经 Proxy 路由到 `host.postMessage({channel, method, data})`，原生侧按 channel 名查路由表分发。

12 个通道（来自现有 Vue3 SPA）：
`session`、`window`、`assistant`、`conversation`、`file`、`audio`、`task`、`skillsMgr`、`report`、...

信号推送（Native→JS）：`emitSignal(channel, signal, data)` → `bridge->postMessage({type:"signal", channel, signal, data})` → JS 侧 `_emitSignal` 回调

### 3.7 DWPEInputMethodContext（输入法桥）

| 属性 | 值 |
|------|-----|
| 头文件 | `src/wpe_input_method_context.h` |
| 实现文件 | `src/wpe_input_method_context.cpp`（~238 行） |
| 命名空间 | `DTKWPE` |

桥接 Qt 输入法（IME）事件到 WebKit 的 `WebKitInputMethodContext`。通过 GObject 子类 `DWpeImContext` 继承 `WebKitInputMethodContext`，重写虚函数转发到 C++ 对象。

| 功能 | 方法 |
|------|------|
| 预编辑文本 | `setPreeditText()` → `preedit-started`/`preedit-changed`/`preedit-finished` 信号 |
| 提交文本 | `commitText()` → `committed` 信号 |
| 焦点 | `notifyFocusIn()`/`notifyFocusOut()` |
| 光标区域 | `notifyCursorArea()` → 更新 `m_cursorRect` → Qt IME 定位候选窗 |

GObject 类型注册使用 `g_type_register_static()`，vtable 覆盖 7 个虚函数。

---

## 4. 仓库结构

```
dtkwebkit/
├── .clang-format              # 代码风格（同 dtkcore）
├── .gitignore
├── .reuse/dep5                # SPDX 版权声明
├── CMakeLists.txt             # 根构建文件
├── VERSION                    # 版本号（0.1.0）
├── LICENSE                    # LGPL-3.0-or-later 全文
├── LICENSES/
│   └── LGPL-3.0-or-later.txt  # SPDX 许可证文本
├── cmake/
│   └── dtkwebkit-config.cmake.in  # CMake Config 模板
├── debian/                    # Debian 打包
│   ├── changelog
│   ├── control
│   ├── copyright
│   ├── rules
│   ├── libdtkwebkit0.install  # 运行库安装清单
│   └── libdtkwebkit-dev.install # 开发包安装清单
├── docs/
│   ├── Specification.md       # 开发规范
│   ├── DEVELOPMENT.md         # 本文档（开发指南）
│   └── WebKit_Qt6_自研嵌入层_方案_v3.md  # 技术方案 V3.1
├── include/dtkwebkit/         # 公共头文件
│   ├── web_surface.h          # WebSurface 抽象接口
│   └── wpe_export.h           # 导出宏 + 命名空间宏
├── src/                       # 桥接层实现
│   ├── wpe_view.h/.cpp        # DWPEView 核心视图
│   ├── wpe_event_translator.h/.cpp
│   ├── wpe_scheme_handler.h/.cpp
│   ├── wpe_bridge.h/.cpp
│   ├── wpe_channel_adapter.h/.cpp
│   ├── qt_compat.h            # Qt5/Qt6 兼容层
├── tests/                     # Qt Test 单元测试
│   ├── CMakeLists.txt
│   ├── test_event_translator.cpp
│   └── test_scheme_handler.cpp
└── examples/minibrowser/      # 示例应用
    ├── CMakeLists.txt
    ├── main.cpp               # DMainWindow + DWPEView
    └── dist/                  # Vite 构建产物（SPA 测试页）
        ├── index.html
        ├── favicon.svg
        └── assets/
```

---

## 5. 依赖

### 5.1 编译依赖

| 依赖 | 最低版本 | 用途 |
|------|---------|------|
| CMake | 3.13 | 构建系统 |
| C++17 编译器 | GCC 8+ | 语言标准 |
| Qt5 (Core/Gui/OpenGL/Widgets/Test) 或 Qt6 (Core/Gui/OpenGL/OpenGLWidgets/Widgets/Test) | 5.11 / 6.5 | 宿主 GUI 框架（自动检测，`-DQT_VERSION=5` 或 `6` 指定） |
| DTK5 (DtkCore/DtkWidget) 或 DTK6 (Dtk6Core/Dtk6Widget) | 5.6 / 6.0 | DTK 重绘控件（对应 Qt 版本） |
| WPE WebKit | 2.46 | Web 渲染引擎（目标 2.50+） |
| libwpe | 1.16 | WPE 平台抽象 |
| WPEBackend-FDO | 1.12 | EGL exportable 后端 |
| Mesa EGL/GLES | 1.5/3.2 | 图形栈 |
| GLib | 2.0 | GObject 事件循环 |
| GStreamer | 1.20 | 媒体后端（`<video>` 播放） |
| xkbcommon | — | 键盘 keymap（evdev→Unicode） |
| Wayland | — | SHM buffer fallback 路径 |

### 5.2 运行时依赖

- GStreamer 插件：`gst-plugins-base`、`gst-plugins-good`（webm 播放）
- fontconfig（中文字体回退）
- bwrap（沙箱，可选）

---

## 6. 构建

### 6.1 基本构建

```bash
cd dtkwebkit
mkdir -p build && cd build

# 自动检测 Qt 版本（优先 Qt6，回退 Qt5）
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 或显式指定 Qt 版本
cmake .. -DCMAKE_BUILD_TYPE=Debug -DQT_VERSION=5  # 强制 Qt5
cmake .. -DCMAKE_BUILD_TYPE=Debug -DQT_VERSION=6  # 强制 Qt6

make -j$(nproc)
```

### 6.2 CMake 配置说明

| 配置 | 默认值 | 说明 |
|------|--------|------|
| `CMAKE_INSTALL_PREFIX` | `/usr` | DTK 标准 |
| `CMAKE_BUILD_TYPE` | — | Debug/Release |
| `QT_VERSION` | 自动检测 | `5` 或 `6`，未指定时优先 Qt6 回退 Qt5 |
| `FETCHCONTENT_FULLY_DISCONNECTED` | ON | 禁止网络下载 |
CMake 通过 `find_package` 声明 Qt/DTK 依赖（Qt5→`Qt5`/`DtkCore`/`DtkWidget`，Qt6→`Qt6`/`Dtk6Core`/`Dtk6Widget`），通过 `pkg_check_modules` 声明 WPE/EGL/GLES/GStreamer/GLib 依赖。

### 6.3 编译目标

| 目标 | 类型 | 说明 |
|------|------|------|
| `dtkwebkit` | SHARED | 桥接层共享库（libdtkwebkit.so） |
| `minibrowser` | EXECUTABLE | 示例应用 |
| `test_event_translator` | EXECUTABLE | 事件转换器单元测试 |
| `test_scheme_handler` | EXECUTABLE | scheme handler 单元测试 |

### 6.4 编译标志

- `QT_NO_KEYWORDS`（全局）：防止 GLib `signals`/`slots`/`emit` 与 Qt 宏冲突
- `DTKWEBKIT_SOURCE_DIR`（minibrowser）：源码路径，用于定位 dist/

---

## 7. 测试

### 7.1 运行测试

```bash
cd build
ctest --output-on-failure
```

### 7.2 测试说明

| 测试 | 覆盖 | 注意 |
|------|------|------|
| `test_event_translator` | Qt::Key → Linux evdev keycode 映射 | 需显示环境（xvfb） |
| `test_scheme_handler` | 内存 fs + MIME 检测 + 扩展名判断 | 需显示环境（xvfb） |

> Debian 打包时 `override_dh_auto_test` 跳过需显示的测试，手动测试用 `xvfb-run ctest`。

### 7.3 minibrowser 手动验证

```bash
cd build
LD_LIBRARY_PATH=. ./examples/minibrowser/minibrowser
# 加载 Vue dist/:
LD_LIBRARY_PATH=. ./examples/minibrowser/minibrowser
# 加载外部 URL:
LD_LIBRARY_PATH=. ./examples/minibrowser/minibrowser https://example.com
# 半透明模式:
LD_LIBRARY_PATH=. ./examples/minibrowser/minibrowser --translucent
```

minibrowser 验证项：
- `app://` scheme 加载测试 HTML / Vue dist
- JS bridge 往返（`window.host.postMessage` → 原生 → Promise resolve）
- Tauri IPC 兼容（greet / get_system_info）
- 键盘/鼠标/滚轮输入
- Ctrl+C 复制（通过 JS `getSelection().toString()` 绕过 pasteboard 死锁）

---

## 8. 安装

### 8.1 从构建安装

```bash
cd build
sudo make install
# 库:     /usr/lib/<arch>/libdtkwebkit.so
# 头文件:  /usr/include/dtkwebkit/
# CMake:   /usr/lib/<arch>/cmake/dtkwebkit/
```

### 8.2 Debian 打包

```bash
dpkg-buildpackage -us -uc -b
# 产物：
#   libdtkwebkit0_*.deb    — 运行库
#   libdtkwebkit-dev_*.deb — 开发文件
```

### 8.3 在 CMake 项目中使用

```cmake
find_package(dtkwebkit REQUIRED)
target_link_libraries(your-app PRIVATE dtkwebkit::dtkwebkit)
```

```cpp
#include <dtkwebkit/web_surface.h>

// DWPEView 实现 WebSurface 接口
// 应用层仅依赖 WebSurface 抽象，不耦合 WPE 细节
```

---

## 9. 开发规范

### 9.1 命名规范

| 范畴 | 规则 | 示例 |
|------|------|------|
| 类名 | PascalCase，D 前缀 | `DWPEView`、`WebSurface` |
| 成员函数 | camelCase | `loadUrl()`、`runJavaScript()` |
| 信号 | snake_case，on 前缀 | `onLoadFinished`、`onRenderCrashed` |
| 成员变量 | m_ 前缀 + camelCase | `m_webView`、`m_currentImage` |
| 常量 | k 前缀 + PascalCase | `kMaxCrashRetries`、`kMouseMoveThrottleMs` |
| 静态变量 | s_ 前缀 + camelCase | `s_keyMap`、`s_vertSrc` |
| DTK D 前缀宏 | 大写 D 开头 | `DWIDGET_USE_NAMESPACE`、`DStyle` |
| 命名空间 | 小写 | `DTKWPE` |
| 文件名 | 全小写，下划线分词 | `wpe_view.h`、`web_surface.h` |

### 9.2 代码风格

仓库根 `.clang-format` 与 dtkcore 一致。核心配置：

| 配置项 | 值 |
|--------|-----|
| `IndentWidth` | 4 |
| `UseTab` | Never |
| `ColumnLimit` | 130 |
| `BreakBeforeBraces` | Custom（class/struct/union/function 换行；control 不换行；namespace 不换行） |
| `AccessModifierOffset` | -4 |
| `PointerAlignment` | Right（`QObject *obj`） |
| `NamespaceIndentation` | Inner |
| `SortIncludes` | Never |
| `SpaceBeforeParens` | ControlStatements |
| `Standard` | c++17 |

提交前运行：
```bash
clang-format --dry-run --Werror --style=file src/*.h src/*.cpp
```

### 9.3 命名空间宏

```cpp
DTKWPE_BEGIN_NAMESPACE  // namespace DTKWPE {
DTKWPE_END_NAMESPACE    // }
DTKWPE_USE_NAMESPACE    // using namespace DTKWPE;
```

### 9.4 导出宏

```cpp
DWPE_EXPORT       // Q_DECL_EXPORT / Q_DECL_IMPORT（由 DWPE_LIBRARY 控制）
DWPE_NO_EXPORT    // Q_DECL_HIDDEN
```

### 9.5 头文件保护

使用 `#ifndef` 守卫（非 `#pragma once`），命名格式 `{CLASSNAME}_H`：
```cpp
#ifndef DWPE_VIEW_H
#define DWPE_VIEW_H
// ...
#endif  // DWPE_VIEW_H
```

### 9.6 许可证头

每个源文件必须包含 SPDX 标识：
```cpp
/*
 * This file is part of dtkwebkit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
```

### 9.7 提交规范（Conventional Commits）

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

> commit message 中**不得附加 AI co-author 行**（linuxdeepin commitlint CI 会拦截）。

### 9.8 DTK 控件使用规范

| 规范 | 要求 |
|------|------|
| 深色/浅色 | 支持 DPalette 深浅色双模式，默认浅色 |
| 活动用色 | 主色 #0081FF，通过 `DPalette::highlight()` 获取 |
| 布局间距 | 控件间距 ≥10px，N×10 为单位 |
| 工具栏 | DTitlebar + logo 32px + 左边距 10px |
| 窗口层级 | DMainWindow 分层窗口 |
| 控件选择 | 优先 DLineEdit/DComboBox/DBlurEffectBackground 等 |
| 国际化 | 所有 UI 文本走 `tr()` |
| 高 DPI | `Qt::AA_UseHighDpiPixmaps` |
| 居中显示 | `Dtk::Widget::moveToCenter(&w)` |

### 9.9 标准路径

| 路径 | 位置 |
|------|------|
| 配置 | `$XDG_CONFIG_HOME/{org}/{app}` |
| 日志 | `$HOME/.log/{org}/{app}` |
| 缓存 | `$XDG_CACHE_HOME/{org}/{app}` |
| 数据 | `$XDG_DATA_HOME/{org}/{app}` |

Web Inspector 端口配置、WPE 缓存路径须走标准路径，不硬编码。

---

## 10. 关键技术约束（开发红线）

以下约束来自方案 V3.1 附录 A，违反会导致渲染管线崩溃或安全边界破坏：

1. **buffer 释放**：`wpe_fdo_egl_exported_image` 必须通过 `dispatch_release_exported_image()` 释放，不得裸 `free()` 或 `g_clear_object()`
2. **析构顺序**：先停 GLib timer → reset 子对象 → release image → unref webView → destroy exportable
3. **webkit_web_view_set_background_color** 禁止调用：AMD Picasso/Raven GPU 上触发 Mesa radeonsi SIGSEGV
4. **右键/pasteboard**：WPE pasteboard 未接线，右键菜单和 Ctrl+C 的 pasteboard IPC 会死锁 GUI；Ctrl+C 通过 JS `getSelection().toString()` 绕过
5. **GLib 主循环 pump**：Qt 事件循环不调度 GLib default context，必须用 16ms 定时器 pump，否则 WPE 帧投递停止
6. **makeCurrent 秩序**：Qt GL context 与 WPE 共享同一 EGLDisplay，但 `eglMakeCurrent` 必须严格单线程串行
7. **app:// scheme**：统一走 `app://`，禁止 `file://` 直开
8. **bridge 注入**：注入到 main world（非 isolated world），在 LOAD_COMMITTED 时注入
9. **事件转换参考**：以 WPEPlatform GLFW 示例（Igalia）为唯一参考，不得自行发明映射
10. **沙箱参数**：bwrap/seccomp 参数由人工锁定，AI 不得修改
11. **Qt5/Qt6 兼容**：源码必须同时兼容 Qt5 和 Qt6，版本差异通过 `src/qt_compat.h` 屏蔽；禁止直接调用 `QMouseEvent::position()`、`QVariant::typeId()` 等 Qt6 专属 API，必须使用 `DTKWPE::eventPos()` / `DTKWPE::variantTypeId()` 兼容包装

---

## 11. 已知限制与待办

| 项目 | 状态 | 说明 |
|------|------|------|
| 触摸事件转换 | TODO | `translateTouchEvent()` 未实现，标记为 M3 |
| WebSurface 继承 | 待完成 | `DWPEView` 尚未正式继承 `WebSurface` |
| 多窗口支持 | stub | `onCreate`/`onReadyToShow` 为占位实现 |
| 单元测试覆盖 | 不足 | 现有测试多为占位 `QVERIFY(true)`，需扩充 |
| CI 配置 | 缺失 | `.github/workflows/ci.yml`、`.clog.toml`、`.commitlintrc.json` 尚未创建 |
| pasteboard 剪贴板 | 未接线 | WPEBackend-FDO pasteboard 未实现，复制/粘贴通过 JS 绕过 |
| backdrop-filter | 需 WPE 2.50+ | 当前 2.46.3 不支持 |
| IntersectionObserver/ResizeObserver | 需 WPE 2.50+ | 当前 2.46.3 不支持 |

---

## 12. 参考文档

| 文档 | 位置 |
|------|------|
| 技术方案 V3.1 | `docs/WebKit_Qt6_自研嵌入层_方案_v3.md` |
| 开发规范 | `docs/Specification.md` |
| 本开发指南 | `docs/DEVELOPMENT.md` |
| WPEPlatform GLFW 示例（Igalia） | 事件转换唯一参考 |
| DTK 开发指南 | docs.deepin.org |
| WPE WebKit API 文档 | wpe.webkit.org |

---

## 13. 版本与许可证

- 版本：0.1.0（`VERSION` 文件）
- 许可证：LGPL-3.0-or-later（与 dtkcore/dtkwidget 一致）
- 版权：© 2026 UnionTech Software Technology Co., Ltd.
- 维护者：dev@uniontech.com
