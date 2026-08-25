# WebKit + Qt6 自研嵌入层 — 完善方案

版本 V3.1 ｜ 2026-08-24 ｜ 面向信创操作系统 · WPE WebKit 直渲接入

> V3.0：纳入现有 Vue3 SPA 生产级前端能力保障矩阵（覆盖 12 通道桥、流式渲染、Markdown 管线、Milkdown 编辑器、拖拽上传、157 SVG 图标、CSS 全特性等）；新增 QWebChannel 12 通道迁移设计；扩展功能/非功能需求与验收标准。

---

## 一、背景与目标

### 1.1 背景

| 维度 | 现状 |
|------|------|
| 宿主环境 | 信创操作系统发行版，Qt6 主壳，需承载 AI(vibe coding) 生成的 Vue 代码并即时预览 |
| 当前前端 | Vue3 SPA：164 个 tsx + 29 个 Pinia store + ~6 万行；12 个 QWebChannel 通道为唯一数据来源 |
| 当前内核 | Qt WebEngine（Chromium），版本受 Qt 发版节奏锁定 |
| 自研率问题 | Chromium ~3500 万行计入操作系统全量源码分母，严重拉低自研率 |
| 生态诉求 | 保留 Vue3/Vite/npm/DevTools 上游生态与现代 Web 兼容性 |

### 1.2 目标（可验证）

1. **自研嵌入层**：Qt6 宿主直渲 WPE WebKit，零 GTK / Qt WebEngine / CEF 依赖。
2. **现有前端零重写迁移**：Vue3 SPA（164 tsx + 29 store + 12 QWebChannel）原样运行，能力不缩水。
3. **自研率上浮**：分母剔除 Chromium（3500 万行）与 GTK+Cairo（150 万行），分子计入自研桥接层 3k~7k 行 C++。
4. **ARM64 + Mesa 可用**：信创 ARM64 图形栈跑通，出厂镜像可不打包浏览器引擎。
5. **解耦可维护**：桥接层与 WPE 引擎版本解耦，升级流水线独立。

### 1.3 非目标

- 不自研浏览器渲染引擎（布局/合成/光栅/JS 引擎均用上游 WebKit + JavaScriptCore）。
- 不追求脱离 WebKit 源码的"纯自研内核"。

---

## 二、现有前端能力保障矩阵

> 逐一映射现有 Vue3 SPA 的核心能力至 WPE WebKit 的覆盖情况，确保迁移后无功能缺失。

### 2.1 架构级能力（12 项）

| # | 现有前端能力 | 实现依赖 | WPE WebKit 覆盖 | 验证方式 |
|---|------------|----------|-----------------|----------|
| A1 | Vue SPA 运行时（组件化/响应式/v-if/v-for/生命周期） | JavaScriptCore 完整 ES2022+ | ✔ | 加载 Vue3 SPA，组件渲染/响应式更新/生命周期钩子全验证 |
| A2 | C++↔JS 桥 QWebChannel（12 通道：session/window/assistant/conversation/file/audio/task/skillsMgr/report 等） | `window.host` 桥 + 通道适配层（详见 4.7） | ✔ | 12 通道逐个往返验证 |
| A3 | 流式渲染 SSE/WebSocket 增量消息 + 打字机效果 | WebKit 原生 EventSource + WebSocket | ✔ | SSE 连接收消息增量渲染；WebSocket 双向通信 |
| A4 | Markdown 管线 marked→DOMPurify→highlight.js→MathJax | 全 JS 运行时 | ✔ | Markdown 文本→渲染→代码高亮→数学公式全链路 |
| A5 | Milkdown 富文本编辑器（ProseMirror 内核） | DOM API + Selection API | ✔ | 编辑器可输入、格式化、获取/设置内容 |
| A6 | 动态 JSON→组件引擎（interactiveComp + mcpUischema） | JS 动态组件 + render 函数 | ✔ | JSON schema→动态渲染组件→交互回调 |
| A7 | 交互面板（AgentTaskPanel/QueuePanel/SlashCommandList/ScenarioRecommendations/guessYouWant） | Vue 组件 + JS 交互 | ✔ | 各面板渲染、交互、状态切换 |
| A8 | 消息导航（MessageNavigator/MessageDotNav/ChatFindBar/outline） | DOM API + scrollIntoView + 搜索 | ✔ | 导航跳转、搜索高亮、目录展开 |
| A9 | 拖拽/上传/剪贴板（28 个文件涉及） | HTML5 Drag&Drop + Clipboard API + File API | ✔ | 文件拖入聊天窗、粘贴、上传回调 |
| A10 | 设置/定时任务/历史会话页（表单/搜索/分页/增删改） | Vue + Pinia 表单状态 | ✔ | 表单填写提交、搜索过滤、分页、增删改 |
| A11 | 弹层系统 Toast/Popover/DatePicker/Tooltip | JS 定位 + CSS 定位 + Web Animations API | ✔ | 弹层显隐、定位、关闭交互 |
| A12 | ElementPlus 组件库 | 完整 JS 组件库 | ✔ | ElTable/ElInput/ElTooltip 等渲染交互 |

### 2.2 CSS 渲染能力（13 项）

| # | CSS 特性 | 使用范围 | WPE WebKit (Skia) | 验证方式 |
|---|---------|----------|-------------------|----------|
| B1 | animation/transition/@keyframes | 81 文件 | ✔ CSS Animations Level 1 | LoadingDots 动画、面板滑入过渡 |
| B2 | box-shadow | 卡片投影/浮层 | ✔ CSS Backgrounds Level 3 | 卡片阴影渲染像素校验 |
| B3 | opacity | 半透明/禁用/loading | ✔ CSS Color Level 4 | 半透明叠加、淡入淡出 |
| B4 | CSS Grid | 11 文件 | ✔ CSS Grid Layout Level 1 | Grid 布局对齐校验 |
| B5 | backdrop-filter: blur | 毛玻璃 5+ 文件 | ✔ Filter Effects Level 2（WPE 2.50+） | 毛玻璃效果可见 |
| B6 | position: sticky | 吸顶操作条 | ✔ CSS Positioned Layout Level 3 | 滚动时吸顶条固定 |
| B7 | transform | 缩放/位移 | ✔ CSS Transforms Level 1 | 缩放/位移动画 |
| B8 | SVG 内联渲染 | 157 个图标 | ✔ 完整 SVG 1.1 | 157 图标全部渲染正确 |
| B9 | `<video>` | 数字人 webm | ✔ HTML5 Media + GStreamer 后端 | webm 播放、状态切换 |
| B10 | @font-face / 图标字体 | 字体 | ✔ CSS Fonts Level 4 | 自定义字体加载渲染 |
| B11 | calc()/vw/vh | 响应式尺寸 | ✔ CSS Values Level 4 | 响应式布局正确计算 |
| B12 | object-fit/word-break/overflow-wrap | 图片裁切/换行 | ✔ CSS Images Level 4 + Text Level 3 | 图片裁切、长 URL 换行 |
| B13 | 表单控件 input/select/textarea | 表单 | ✔ 原生表单控件 | 输入/选择/文本域交互 |

### 2.3 基础渲染能力（5 类）

| # | 特性 | WPE 覆盖 |
|---|------|----------|
| C1 | 基础排版（文本/标题/列表/表格/代码块） | ✔ |
| C2 | Flex 布局（114 样式文件核心） | ✔ CSS Flexbox Level 1 |
| C3 | 圆角/边框/线性/径向/锥形渐变/背景图 | ✔ |
| C4 | :hover/:active/:before/:after/计数器/媒体查询 | ✔ |
| C5 | var() CSS 自定义属性（主题色体系） | ✔ |

**矩阵结论**：WPE WebKit 覆盖 A 类 12/12 + B 类 13/13 + C 类 5/5 = **30/30**，现有前端零重写迁移。

---

## 三、需求规格

### 3.1 功能需求

| 编号 | 需求 | 说明 | 验收标准 |
|------|------|------|----------|
| FR-1 | Qt 窗口直渲 | QWindow/QOpenGLWindow 承载 WPE 合成帧，无 GTK 事件循环 | Vue 页面在 QWindow 内可见、可交互，60fps 无撕裂 |
| FR-2 | WebSurface 接口 | loadUrl / setHtml / runJavaScript / postMessage | 四个接口均可调用并返回预期结果 |
| FR-3 | 原生↔JS 双向桥 | 注入 window.host，Promise 回传，原生侧 onMessage | JS→原生 Promise resolve；原生→JS 回调触发；序列化往返正确 |
| FR-4 | Vue3 SPA 承载 | app:// scheme 承载 Vite 产物，hash/history 路由 | 164 tsx 工程 + 29 store 全量加载、路由切换、状态持久均正常 |
| FR-5 | 资源虚拟化 | app:// scheme 从内存 fs 响应 Vite 产物 | 产物全量资源经 scheme 加载，无 file:// 回退 |
| FR-6 | QWebChannel 通道迁移 | 12 通道（session/window/assistant/conversation/file/audio/task/skillsMgr/report 等）适配至 window.host | 12 通道逐个往返验证通过（详见 4.7） |
| FR-7 | 流式渲染 | SSE/WebSocket 增量消息 + 打字机效果 | SSE 连接收消息增量渲染；打字机逐字效果可见 |
| FR-8 | Markdown 管线 | marked→DOMPurify→highlight.js→MathJax 全链路 | Markdown 文本渲染为 HTML + 代码高亮 + 数学公式 |
| FR-9 | 拖拽/上传/剪贴板 | HTML5 Drag&Drop + Clipboard API + File API | 文件拖入聊天窗、粘贴上传、剪贴板回调 |
| FR-10 | 沙箱与崩溃恢复 | WPE 渲染进程沙箱（bwrap/seccomp），崩溃可重启 | 杀死 renderer 进程后自动恢复并重新加载当前页 |
| FR-11 | DevTools 开关 | 可开启远程/本地 Web Inspector | 开关切换可生效，Inspector 可连接并查看 DOM/Console/Network |

### 3.2 非功能需求

| 编号 | 需求 | 指标 |
|------|------|------|
| NFR-1 | Vue SPA 兼容性 | 保障矩阵 30/30 项逐条通过（详见第二节） |
| NFR-2 | 自研率 | 桥接层 3k~7k 行 C++；内核源码不计入我方仓库分子 |
| NFR-3 | 性能 | ARM64+Mesa 下 Vue 页 60fps 合成；首帧预热 ≤ 现有 WebEngine ×1.5 |
| NFR-4 | 安全 | 不可信 Web 内容走沙箱；CVE 跟随 WPE/WebKit 上游发版 |
| NFR-5 | 可维护 | 桥接层独立 git 子仓；WPE 升级不触动应用层接口 |
| NFR-6 | API 稳定 | WebSurface 接口 SemVer，引擎切换不改应用层代码 |
| NFR-7 | 零重写 | 现有 Vue3 SPA 源码不修改一行即可在 WPE 后端运行 |

---

## 四、技术方案

### 4.1 架构总览

```
┌────────────────────────────────────────────────────────┐
│  应用层：现有 Vue3 SPA（164 tsx + 29 Pinia store）       │
│  ├─ 12 个 QWebChannel 通道 → window.host 适配层         │
│  └─ 流式渲染 / Markdown / Milkdown / 拖拽上传 / SVG ... │
├────────────────────────────────────────────────────────┤
│  宿主层（自研）：Qt6 Widgets/QML 主壳                    │
│  └─ WebSurface 抽象接口（Facade）                       │
├────────────────────────────────────────────────────────┤
│  桥接层（自研）                                         │
│  ├─ WPEQtDisplay        (GObject, EGL display)         │
│  ├─ WPEQtToplevel       (GObject, QWindow 映射)        │
│  ├─ WPEQtView           (GObject, DMA-BUF→纹理)       │
│  ├─ WPEQtEventTranslator (Qt→WPE 事件)                │
│  ├─ WPEQtSchemeHandler   (app:// 虚拟化)               │
│  ├─ WPEQtBridge          (window.host 桥 + 通道适配)    │
│  └─ WPEQtChannelAdapter  (QWebChannel→window.host)     │
├────────────────────────────────────────────────────────┤
│  引擎层（上游）：WPE WebKit + libwpe                    │
│  ├─ WPEBackend-FDO / WPEPlatform                        │
│  └─ JavaScriptCore                                     │
├────────────────────────────────────────────────────────┤
│  系统层：Mesa EGL/GL ES · GLib · libsoup               │
│  · GStreamer(媒体/视频) · 信创 ARM64 图形栈             │
└────────────────────────────────────────────────────────┘
```

### 4.2 关键技术决策

| 决策项 | 结论 | 理由 |
|--------|------|------|
| 渲染后端 | WPEBackend-FDO 起步 → WPEPlatform 2.54 稳定后切换 | 2.52 WPEPlatform 为预览 API，先用成熟 Backend-FDO 验证管线 |
| EGL 显示共享 | 从 QOpenGLContext 经 `QNativeInterface::QEGL` 取 EGLDisplay，传给 WPE | 零拷贝纹理共享要求 Qt 与 WPE 共享同一 EGLDisplay |
| 纹理导入 | `eglCreateImageKHR`(EGL_LINUX_DMA_BUF_EXT) → `glEGLImageTargetTexture2DOES` | Mesa 线性 modifier 走 `GL_TEXTURE_2D`；非线行走 `GL_TEXTURE_EXTERNAL_OES` |
| 输入事件 | Qt 事件 → WPEEvent 转换表，经 libwpe 喂回 WebKit | 保持与 WPEPlatform GLFW 示例一致 |
| 承载方式 | app:// 自定义 scheme，内存 fs，禁止 file:// | 安全 + 资源虚拟化 + 路由 fallback 可控 |
| 跨平台 | 统一 WebSurface Facade；Linux/信创→WPE，macOS→WKWebView，Windows→WebView2 | 应用层无感知，仅宿主桥分叉 |
| JS 桥 | window.host 统一桥 + QWebChannel 通道适配层 | 现有前端 12 通道零修改；适配层在 JS 侧注入 |
| 媒体后端 | GStreamer | `<video>` webm 播放需 GStreamer WebKit 媒体后端 |
| 构建 | CMake + pkg-config | `wpe-webkit-2.0` / `wpebackend-fdo-1.0` / `Qt6Gui` / `Qt6OpenGL` |

### 4.3 渲染管线（EGL / DMA-BUF）

```
WPE 渲染进程合成帧
    │
    ▼
WPEBuffer (导出 dmabuf: fd[] + stride[] + offset[] + modifier + 四元组格式)
    │
    ▼  [WPEQtView::frameRendered 回调]
eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, attribs)
    │  attribs: EGL_WIDTH/HEIGHT, EGL_LINUX_DRM_FORMAT_STRIDE,
    │           EGL_DMA_BUF_PLANE0_FD/OFFSET/PITCH, EGL_DMABUF_FORMAT_MODIFIER
    ▼
EGLImageKHR
    │
    ▼  [Qt GL 线程, paintGL]
glGenTextures → glBindTexture(GL_TEXTURE_2D)
glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, eglImage)
    │
    ▼
Qt QOpenGLWindow 绘制全屏四边形（shader 采样纹理）
    │
    ▼  [帧结束]
eglDestroyImageKHR + g_clear_object(WPEBuffer)   ← 释放路径，人工必审
```

**关键约束：**
- **线程秩序**：Qt GL context 与 WPE 共享同一 EGLDisplay，但 `eglMakeCurrent` 必须严格单线程串行——WPE 释放 buffer 后、Qt paintGL 前 makeCurrent，paint 后 release。
- **生命周期**：WPEBuffer 在纹理上传完成前不可释放；resize/focus 切换时先销毁 surface 再重建，避免悬挂引用。
- **modifier 处理**：若 dmabuf modifier 非 `DRM_FORMAT_MOD_LINEAR`，需用 `GL_TEXTURE_EXTERNAL_OES` 采样，shader 中 `#extension GL_OES_EGL_image_external`。

### 4.4 事件转换

| Qt 事件 | WPE 事件 | 关键映射 |
|---------|----------|----------|
| QKeyEvent | wpe_key_event | Qt::Key → Linux keycode（evdev 表）；text → Unicode |
| QMouseEvent(press/release) | wpe_pointer_event | 坐标除以 devicePixelRatio；button 映射 |
| QMouseEvent(move) | wpe_pointer_event | 同上 |
| QWheelEvent | wpe_axis_event | deltaX/deltaY → axis 值；angleDelta 换算 |
| QTouchEvent | wpe_touch_event | 多点触控 id 映射；phase 转换 |
| FocusIn/Out | wpe_view_focus_in/out | 切换 IME 状态 |
| Resize | wpe_view_resize | 宽高 × devicePixelRatio |
| QDragEvent | （WebKit 内部处理） | Drop→WebKit HTML5 DnD；文件路径→File API |

### 4.5 资源虚拟化（app:// scheme）

```
Vite 构建产物 (dist/)
    │  base: 'app:///'
    ▼
加载到内存 fs: QMap<QString, ResourceEntry{mime, bytes}>
    │
    ▼  [WPEQtSchemeHandler 注册 app://]
WebKit 请求 app:///assets/index-xxxx.js
    │
    ▼  [scheme 回调]
内存 fs 查找 → 返回 ResourceResponse(mime, bytes)
    │
    ▼  [未命中且路径无扩展名]
history 路由 fallback → 返回 index.html (SPA 入口)
```

**规则：**
- 有文件扩展名：精确匹配内存 fs。
- 无扩展名或 404：返回 `index.html`（history 路由 fallback）。
- `app:///` 根路径：返回 `index.html`。

### 4.6 原生↔JS 桥（window.host）

```
JS 侧（注入 user script）:
    window.host = {
        postMessage(msg) → Promise     // JS→原生，带 id 关联
        onMessage: null                  // 原生→JS 回调挂载点
        channels: {}                     // 通道适配对象（详见 4.7）
    }

JS→原生:
    window.host.postMessage({id, channel, method, data})
        → WebKit message handler 回调
        → 原生按 channel/method 路由处理
        → runJavaScript("window.host.onMessage({id, result|error})")
        → Promise resolve/reject

原生→JS:
    原生 postMessage(msg)
        → runJavaScript("window.host.onMessage(" + JSON.stringify(msg) + ")")
```

### 4.7 QWebChannel 通道迁移设计（FR-6 核心）

**问题**：现有前端通过 Qt QWebChannel 注册了 12 个通道（session/window/assistant/conversation/file/audio/task/skillsMgr/report 等），是前端唯一数据来源。迁移到 WPE 后 QWebChannel 不可用，需适配至 `window.host`。

**策略**：JS 侧注入通道适配层，将 `qt.channelName.method()` 调用代理到 `window.host.postMessage({channel, method, data})`，原生侧按 channel 路由。**前端源码零修改**——适配层在 user script 中重定义 `qt` 全局对象。

```
┌─────────────── JS 侧（注入 user script，前端不改一行） ───────────────┐
│                                                                     │
│  // 构造兼容 qt 全局对象                                             │
│  const qt = new Proxy({}, {                                         │
│      get(_, channel) {                                              │
│          return new Proxy({}, {                                     │
│              get(_, method) {                                       │
│                  return (...args) =>                                │
│                      window.host.postMessage({                     │
│                          channel, method, data: args                │
│                      });                                            │
│              }                                                      │
│          });                                                        │
│      }                                                              │
│  });                                                                │
│  window.qt = qt;   // 前端代码中 qt.session.getUser() 照常调用      │
│                                                                     │
│  // QWebChannel signal 兼容：原生→JS 事件                            │
│  window.host.onMessage = (msg) => {                                 │
│      if (msg.type === 'signal')                                     │
│          window._channelCallbacks[msg.channel]?.[msg.signal]?.(msg.data) │
│  };                                                                 │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────── 原生侧（WPEQtChannelAdapter） ───────────────────┐
│                                                                     │
│  路由表: channel → handler                                           │
│  ┌────────────┬──────────────────────────────────────┐              │
│  │ session    │ 会话管理：创建/切换/历史               │              │
│  │ window     │ 窗口控制：标题/尺寸/全屏/最小化         │              │
│  │ assistant  │ AI 助手：发送消息/取消/重试             │              │
│  │ conversation│ 对话：SSE 流式接收→onMessage 推送    │              │
│  │ file       │ 文件：读/写/选择/保存                  │              │
│  │ audio      │ 音频：播放/录制/状态                    │              │
│  │ task       │ 定时任务：增删改查                      │              │
│  │ skillsMgr  │ 技能管理：列表/启用/配置                │              │
│  │ report     │ 报告：生成/导出                         │              │
│  │ ...        │ 其余通道按需注册                        │              │
│  └────────────┴──────────────────────────────────────┘              │
│                                                                     │
│  收到 {channel, method, data} → 查路由表 → 调原生 → 回传 result     │
│  原生主动推送 → window.host.onMessage({channel, type:'signal', ...}) │
└─────────────────────────────────────────────────────────────────────┘
```

**12 通道迁移验证清单：**

| 通道 | 关键方法 | 验证场景 |
|------|----------|----------|
| session | create/switch/list/history | 创建会话、切换、查看历史 |
| window | setTitle/resize/fullscreen | 窗口标题更新、尺寸调整 |
| assistant | sendMessage/cancel/retry | 发送消息、取消生成、重试 |
| conversation | onStreamChunk (signal) | SSE 流式消息逐字推送、打字机效果 |
| file | read/write/select/save | 文件读取、保存对话框 |
| audio | play/record/status | 音频播放、录制状态 |
| task | add/remove/list/update | 定时任务增删改查 |
| skillsMgr | list/enable/config | 技能列表、启用切换 |
| report | generate/export | 报告生成、导出 |
| 其余通道 | 按现有接口 | 逐个往返验证 |

### 4.8 沙箱与崩溃恢复

- **沙箱**：WPE renderer 为独立进程，通过 bwrap + seccomp 限制系统调用。参数由人工锁定，AI 不得修改。
- **崩溃恢复**：宿主监控 renderer 进程 PID；退出码非 0 → 重新拉起 → 自动 loadUrl(当前页)。上层经 `onRenderCrashed` 信号通知。
- **安全边界**：不可信 Web 内容必须走沙箱；可信本地内容（app://）可放宽但默认沙箱开启。

### 4.9 DevTools

- 开关：WebSurface `devToolsEnabled(bool)` 属性。
- 实现：WebKit Web Inspector 远程协议；开启后监听本地端口，可用系统浏览器或 inspector 客户端连接。
- 默认关闭，仅排错时开启。

### 4.10 媒体后端（video/音频）

- WPE WebKit 媒体后端使用 GStreamer；`<video>` webm 播放需安装 GStreamer 插件（`gst-plugins-base`/`gst-plugins-good`）。
- `WPEQtChannelAdapter` audio 通道与 WebKit 原生 `<audio>` 独立——audio 通道是原生侧录音/播放控制，`<audio>` 是页面内媒体元素。

---

## 五、模块设计与任务切分

### 5.1 WebSurface 抽象接口（人工编写头文件）

```cpp
class WebSurface : public QObject {
    Q_OBJECT
    // 属性
    Q_PROPERTY(bool enableSandbox     READ isSandboxEnabled  WRITE setSandboxEnabled)
    Q_PROPERTY(bool devToolsEnabled   READ isDevToolsEnabled WRITE setDevToolsEnabled)
    Q_PROPERTY(QString rootUrl        READ rootUrl          WRITE setRootUrl)

public:
    // 接口
    virtual void loadUrl(const QString& url) = 0;
    virtual void setHtml(const QString& html, const QString& baseUrl = {}) = 0;
    virtual void runJavaScript(const QString& code,
                               std::function<void(QVariant)> callback = {}) = 0;
    virtual void postMessage(const QVariant& message) = 0;

    // QWebChannel 通道注册（迁移支持）
    virtual void registerChannel(const QString& name,
                                 std::function<QVariant(QString method, QVariantList args)> handler) = 0;
    virtual void emitChannelSignal(const QString& channel,
                                    const QString& signal, const QVariant& data) = 0;

signals:
    void onLoadFinished(bool ok);
    void onConsoleMessage(int level, const QString& message, int line, const QString& source);
    void onMessage(const QVariant& message);
    void onRenderCrashed();
};
```

### 5.2 WPEQt 桥接层

| 类/文件 | 职责 | 行数估算 | 人工必审 |
|---------|------|----------|----------|
| WPEQtDisplay | 继承 WPEDisplay，封装 Qt EGLDisplay，present 回调 | ~600 | ✔ buffer 释放 |
| WPEQtToplevel | 继承 WPEToplevel，映射 QWindow 尺寸/focus/close | ~500 | ✔ surface 销毁 |
| WPEQtView | 继承 WPEView，dmabuf→Qt GL 纹理并 paintGL | ~900 | ✔ 释放 + resize |
| WPEQtEventTranslator | Qt 事件→WPEEvent 转换表（键码/滚动/focus/DnD） | ~700 | |
| WPEQtSchemeHandler | 注册 app://，内存 fs 响应 + history fallback | ~500 | |
| WPEQtBridge | 注入 window.host，Promise 往返 | ~400 | |
| WPEQtChannelAdapter | QWebChannel 12 通道路由 + signal 推送 | ~800 | |
| **合计** | | **~4.4k 行（含通道适配，落 3k~7k 区间）** | |

### 5.3 人机分工

| 角色 | 职责 |
|------|------|
| 人 | 手写三个 GObject 子类头文件 + vfunc 签名（~300 行），锁死契约 |
| 人 | review 三处关键路径：①buffer 释放 ②resize/focus 时 surface 销毁 ③沙箱参数 |
| 人 | 确认 12 通道路由表 + signal 推送语义 |
| AI | 以 WPEPlatform GLFW 示例为 few-shot，生成 EGL/DMA-BUF 上传 + 事件转换 + scheme + JS 桥 + 通道适配 |
| AI 禁区 | 不改虚函数签名语义；不碰 bwrap/seccomp 参数；不假设 Chromium-only API |

---

## 六、Vue3 SPA 承载与运行约束

| 约束 | 说明 |
|------|------|
| 承载方式 | HTTP/虚拟 scheme；Vite `base: 'app:///'`；禁止 file:// 直开 |
| Router | hash 模式零配置；history 模式 scheme 回调 fallback 到 index.html |
| Pinia / DevTools | 正常可用 |
| Observer API | IntersectionObserver/ResizeObserver 需 WPE 2.50+ |
| 字体 | 显式 fontconfig 路径，避免信创环境中文回退方块 |
| SVG 图标 | 157 个内联 SVG 原生支持，无需改图片 |
| 媒体 | `<video>` 需 GStreamer 后端；webm 需 gst 插件 |
| Markdown | marked→DOMPurify→highlight.js→MathJax 全链路纯 JS 运行 |
| Milkdown | ProseMirror 依赖 DOM Selection API，WebKit 完整支持 |
| 拖拽上传 | HTML5 Drag&Drop + File API，WebKit 原生支持 |
| 兜底 | Skia 多线程合成异常时 `WEBKIT_SKIA_ENABLE_CPU_RENDERING=1` 或 `WEBKIT_SKIA_PAINTING_THREADS=1` |
| 业务规避 | 不可使用 Chromium-only API；功能检测降级 |

---

## 七、测试与验收策略

### 7.1 测试分层

| 层级 | 内容 | 方法 |
|------|------|------|
| 单元测试 | WPEQtEventTranslator 键码/坐标/DnD 映射 | Qt Test，输入 Qt 事件断言 WPEEvent 字段 |
| 单元测试 | WPEQtSchemeHandler 内存 fs + fallback | Qt Test，构造请求断言响应 |
| 单元测试 | WPEQtChannelAdapter 12 通道路由 | Qt Test，构造 {channel,method,data} 断言路由+回传 |
| 集成测试 | Vue SPA 全量渲染 | 加载现有 164 tsx 工程，截图/像素校验 |
| 集成测试 | JS 桥往返 | runJavaScript + postMessage 往返断言 |
| 集成测试 | 12 通道逐个往返 | 每通道关键方法调用→回传→signal 推送 |
| 集成测试 | 流式渲染 | SSE 连接→增量消息→打字机效果 |
| 集成测试 | Markdown 管线 | 文本→HTML→高亮→公式 |
| 集成测试 | Milkdown 编辑器 | 输入/格式化/内容读写 |
| 集成测试 | 拖拽上传 | 文件拖入→File API 回调 |
| 集成测试 | SVG 图标 | 157 图标全渲染校验 |
| 集成测试 | video 播放 | webm 加载播放 |
| 回归测试 | WPE 升级后全量集成测试重跑 | CI 触发 |
| 兼容性测试 | 保障矩阵 30/30 项 | 逐项最小检测页 |
| 崩溃恢复 | 杀 renderer → 自动恢复 | 端到端验证 |

### 7.2 前端能力保障矩阵检测清单（NFR-1）

**A 类（架构级，12 项）：**

| # | 特性 | 检测方式 |
|---|------|----------|
| A1 | Vue 运行时 | Vue3 SPA 加载，组件渲染 + 响应式更新 + v-if/v-for |
| A2 | 12 通道桥 | 每通道关键方法调用往返 |
| A3 | 流式渲染 | `new EventSource(url)` 连接 + onmessage 增量 |
| A4 | Markdown | marked→DOMPurify→highlight.js→MathJax 渲染 |
| A5 | Milkdown | 编辑器创建 + 输入 + 内容获取 |
| A6 | 动态组件 | JSON schema→render→交互回调 |
| A7 | 交互面板 | AgentTaskPanel/QueuePanel 等渲染交互 |
| A8 | 消息导航 | scrollIntoView + 搜索高亮 |
| A9 | 拖拽上传 | dragover/drop + File API |
| A10 | 设置页 | 表单填写 + 提交 + 分页 |
| A11 | 弹层 | Toast/Popover/DatePicker 显隐定位 |
| A12 | ElementPlus | ElTable/ElInput 渲染交互 |

**B 类（CSS，13 项）：**

| # | 特性 | 检测方式 |
|---|------|----------|
| B1 | animation/transition | `@keyframes` + `transition` 动画可见 |
| B2 | box-shadow | `box-shadow: 0 2px 8px` 渲染校验 |
| B3 | opacity | `opacity: 0.5` 半透明可见 |
| B4 | CSS Grid | `display: grid` 布局对齐 |
| B5 | backdrop-filter | `backdrop-filter: blur(10px)` 毛玻璃 |
| B6 | position: sticky | 滚动时吸顶 |
| B7 | transform | `transform: scale(1.5)` 缩放 |
| B8 | SVG | 157 图标全渲染 |
| B9 | video | `<video src=".webm">` 播放 |
| B10 | @font-face | 自定义字体加载 |
| B11 | calc()/vw/vh | `width: calc(100vw - 200px)` |
| B12 | object-fit/word-break | 图片裁切 + 长 URL 换行 |
| B13 | 表单控件 | input/select/textarea 交互 |

### 7.3 性能基准

- 基准页：现有 Vue3 SPA（164 tsx 全量加载）+ 复杂列表（1000 项虚拟滚动）。
- 指标：合成 fps（`requestAnimationFrame` 计数 / 秒）；首帧时间（loadUrl → onLoadFinished）。
- 对比：与现有 Qt WebEngine 同页同环境对比，NFR-3 要求 ≤1.5×。
---

## 八、开发流程

### 8.1 流程选型：Spec-Driven + 人机协作分阶段

采用 **Spec-Driven Dev（规格驱动开发）** 流程，结合方案中已有的人机分工模型（人定骨架、AI 填实现、人审关键路径）。每个模块按"规格锁定 → AI 生成 → 编译验证 → 人工 review → 集成回归"五步闭环推进，逐步累积、逐阶段验收。

选择理由：本方案已有明确的人机分工契约（§5.3）、可验证的里程碑（§七）、和必须锁定的 API 骨架（§5.1），Spec-Driven 的"先规格后实现、先验证后合入"天然适配。替代项如纯 Scrum 迭代缺乏对 AI 生成代码的结构化 review 闸门；纯瀑布则无法适应 WPE API 2.52→2.54 的中途切换。

### 8.2 分阶段实施

将里程碑分为四个阶段，每阶段内含并行轨道，阶段间以可观测验收为闸门。

#### 阶段一：基础管线（M0–M2）

| 属性 | 说明 |
|------|------|
| 目标 | 在信创 ARM64+Mesa 环境跑通 WPE→Qt 渲染管线，证明技术可行性 |
| 流程 | M0 环境验证（纯人工）→ M1 契约锁定（手写头文件，Spec-Driven 规格文件）→ M2 渲染管线（AI 生成 + 人工审 buffer 释放） |
| 闸门 | Qt QWindow 内渲染 Vue 页达 60fps |
| 关键约束 | buffer 释放路径人工必审；EGL makeCurrent 秩序锁定 |

#### 阶段二：交互与承载（M3–M5）

| 属性 | 说明 |
|------|------|
| 目标 | 打通输入事件、资源虚拟化、JS 双向桥，形成可交互的 WebSurface |
| 流程 | M3 事件全通（AI 生成转换表 + 人工审 focus/resize 销毁）→ M4‖M5 资源虚拟化与 JS 桥并行 |
| 闸门 | 键鼠/触摸/拖拽全通；app:// 加载 Vite 产物；JS↔原生往返验证 |
| 关键约束 | app:// history fallback 规则锁定；window.host 协议格式锁定 |

#### 阶段三：生产级能力（M6–M9）

| 属性 | 说明 |
|------|------|
| 目标 | 迁移 12 通道桥、流式渲染、Markdown、Milkdown、拖拽、媒体、沙箱——覆盖前端全部生产场景 |
| 流程 | M6 通道迁移（串行，依赖 M5）→ M7‖M8‖M9 三轨并行（流式+Markdown / 拖拽+媒体 / 沙箱+DevTools） |
| 闸门 | 12 通道逐个往返；SSE 打字机效果；webm 播放；杀 renderer 自动恢复 |
| 关键约束 | 12 通道路由表逐一对照现有 QWebChannel 注册代码；沙箱参数人工锁定 |

#### 阶段四：集成与交付（M10–M13）

| 属性 | 说明 |
|------|------|
| 目标 | 现有 SPA 零修改全量验证、AI 闭环、独立子仓 CI、自研率对齐 |
| 流程 | M10 SPA 全量验证（30/30 矩阵）→ M11 AI 闭环 → M12 独立子仓+CI → M13 自研率对齐 |
| 闸门 | 保障矩阵 30/30 通过；审计方确认口径 |
| 关键约束 | 前端源码不修改一行（NFR-7） |

### 8.3 单模块开发闭环（Spec-Driven 五步）

```
① 规格锁定        ② AI 生成         ③ 编译验证
   人工编写          AI 按 spec         cmake build
   头文件+spec       + GLFW few-shot    + ARM64-Mesa
   锁定 vfunc/接口   生成实现            编译通过
       │                  │                  │
       ▼                  ▼                  ▼
④ 人工 review ──────────────────────── ⑤ 集成回归
   三处关键路径审                          minibrowser
   (buffer释放/surface销毁/沙箱)           Vue 页渲染回归
```

每个模块独立走完五步方可合入。阶段间以阶段闸门验收为硬性条件，不满足则回退重做。

---

## 九、DTK 开发规范遵从

> 依据：DTK 开发指南（docs.deepin.org）+ linuxdeepin GitHub 仓库规范（dtkcore/dtkwidget 源码实践）

### 9.1 命名规范

| 范畴 | 规则 | 示例 |
|------|------|------|
| 类名 | PascalCase | `WPEQtDisplay`、`WebSurface` |
| 成员函数 | camelCase | `loadUrl()`、`runJavaScript()` |
| 属性/信号 | snake_case | `onLoadFinished`、`onRenderCrashed` |
 | DTK D 前缀宏 | 大写 D 开头 | `DWIDGET_USE_NAMESPACE`、`DStyle` |
| 命名空间 | 小写，对应模块名 | `WPEQt`（桥接层命名空间） |
| 文件名 | 全小写，下划线分词 | `wpe_qt_display.h`、`web_surface.cpp` |

### 9.2 代码风格（遵从 dtkcore `.clang-format`）

桥接层仓库须放置与 dtkcore 一致的 `.clang-format`，核心配置：

| 配置项 | 值 | 说明 |
--------|-----|------|
| `IndentWidth` | 4 | 缩进 4 空格 |
| `TabWidth` | 4 | Tab 宽度 4 |
| `UseTab` | Never | 不用 Tab，全空格 |
| `ColumnLimit` | 130 | 行宽上限 130 |
| `BreakBeforeBraces` | Custom | class/struct/union/function 后换行；control 语句不换行；namespace 不换行 |
| `AccessModifierOffset` | -4 | public/private 缩进 -4 |
| `PointerAlignment` | Right | 指针 `*` 靠右：`QObject *obj` |
| `NamespaceIndentation` | Inner | 命名空间内容缩进 |
| `AllowShortFunctionsOnASingleLine` | Inline | 仅类内定义短函数可单行 |
| `SpaceBeforeParens` | ControlStatements | 控制语句括号前加空格 |
| `SortIncludes` | Never | 不自动排序 include |
| `AlignAfterOpenBracket` | Align | 括号后对齐 |

CI 须在 PR 检查中加入 `clang-format --dry-run` 步骤，与 DTK 仓库一致。

### 9.3 构建（CMake 规范）

遵从 DTK 仓库 CMake 模式：

```cmake
cmake_minimum_required(VERSION 3.13)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FILE_VERSION)
string(STRIP "${FILE_VERSION}" FILE_VERSION)

project(WPEWebKitQt
    VERSION ${FILE_VERSION}
    DESCRIPTION "WPE WebKit Qt6 embedding layer"
    HOMEPAGE_URL "https://github.com/linuxdeepin/<repo>"
    LANGUAGES CXX C
)

# DTK 标准：默认安装 /usr
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 依赖：DTK + Qt6 + WPE
find_package(Dtk6Core REQUIRED)
find_package(Dtk6Widget REQUIRED)
find_package(Qt6 COMPONENTS Gui OpenGL Widgets REQUIRED)
pkg_check_modules(WPE REQUIRED IMPORTED_TARGET wpe-webkit-2.0>=2.50)
pkg_check_modules(WPE_FDO REQUIRED IMPORTED_TARGET wpebackend-fdo-1.0>=1.0)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.20)

target_link_libraries(wpewebkit-qt
    Dtk6::Core
    Dtk6::Widget
    Qt6::Gui Qt6::OpenGL Qt6::Widgets
    PkgConfig::WPE PkgConfig::WPE_FDO PkgConfig::GST
)
```
### 9.4 仓库结构（对齐 DTK 仓库布局）

```
wpewebkit-qt/
├── .clang-format          # 同 dtkcore 风格
├── .clog.toml             # changelog 生成配置
├── .github/workflows/      # CI: commitlint + clacheck + license-check + build
├── .reuse/dep5            # SPDX 版权声明
├── CMakeLists.txt
├── VERSION                # 版本文件（单行版本号）
├── LICENSE                 # LGPL-3.0-or-later
├── LICENSES/              # SPDX 许可证文本
├── debian/                # 打包规则
│   ├── changelog
│   ├── control
│   ├── copyright
│   └── rules
├── docs/
│   └── Specification.md   # 开发规范说明
├── include/               # 公共头文件
│   └── wpewebkit-qt/
│       ├── websurface.h
│       └── wpe_qt_export.h
├── src/                   # 桥接层实现
│   ├── wpe_qt_display.cpp
│   ├── wpe_qt_toplevel.cpp
│   ├── wpe_qt_view.cpp
│   ├── wpe_qt_event_translator.cpp
│   ├── wpe_qt_scheme_handler.cpp
│   ├── wpe_qt_bridge.cpp
│   └── wpe_qt_channel_adapter.cpp
├── tests/                 # Qt Test 单元/集成测试
└── examples/              # minibrowser 示例
    └── minibrowser/
```

### 9.5 提交规范（Conventional Commits）

遵从 linuxdeepin 仓库的 commitlint + CLA 检查 CI。提交信息格式：

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

示例：`feat(bridge): add WPEQtChannelAdapter 12-channel routing`

**CI 闸门**（与 DTK 仓库一致）：
- commitlint：Conventional Commits 格式检查
- CLA check：贡献者协议签署检查
- license-check：SPDX 许可证合规检查
- cppcheck：静态分析
- build：Arch Linux + deepin 双构建矩阵

> 注意：linuxdeepin commitlint CI 会拦截 commit message 中的 AI co-author 行（如 `Co-authored-by:.*claude`），AI 生成代码合入时不得附加此类署名。

### 9.6 许可证

LGPL-3.0-or-later，与 dtkcore/dtkwidget 一致。SPDX 文件放 `LICENSES/` 目录，`.reuse/dep5` 声明版权归属。

### 9.7 宿主层 DTK 控件使用

WebSurface 嵌入的宿主主壳应优先使用 DTK 重绘控件，遵从 DTK 设计规范：

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

### 9.8 标准路径

遵从 Deepin Application Specification（dtkcore/docs/Specification.md）：

| 路径 | 位置 |
|------|------|
| 配置 | `$XDG_CONFIG_HOME/{org}/{app}` |
| 日志 | `$HOME/.log/{org}/{app}` |
| 缓存 | `$XDG_CACHE_HOME/{org}/{app}` |
| 数据 | `$XDG_DATA_HOME/{org}/{app}` |

桥接层的 Web Inspector 端口配置、WPE 缓存路径须走标准路径，不硬编码。

---

## 十、落地里程碑

| 阶段 | 里程碑 | 交付物 | 验收 |
|------|--------|--------|------|
| M0 | 环境验证 | WPEPlatform GLFW 示例在信创 ARM64+Mesa 跑通 Vue 页 | GLFW 窗口内 Vue 页可见可交互 |
| M1 | 契约锁定 | WPEQtDisplay/Toplevel/View 头文件 + WebSurface 接口（~400 行） | 编译通过，vfunc 签名 review 签字 |
| M2 | 渲染管线打通 | EGL 共享 display + DMA-BUF→纹理 + paintGL | Qt QWindow 内渲染 Vue 页，60fps |
| M3 | 事件全通 | WPEQtEventTranslator + minibrowser main | 键盘/鼠标/滚轮/触摸/拖拽全可用 |
| M4 | 资源虚拟化 | app:// scheme + 内存 fs + history fallback | Vite 产物全量经 app:// 加载 |
| M5 | JS 桥闭环 | window.host 注入 + Promise 往返 | JS↔原生双向通信验证通过 |
| M6 | 12 通道迁移 | WPEQtChannelAdapter + JS 适配层 | 12 通道逐个往返 + signal 推送验证 |
| M7 | 流式+Markdown | SSE/WebSocket + Markdown 管线 + Milkdown | 流式渲染 + Markdown + 编辑器验证 |
| M8 | 拖拽+媒体+SVG | DnD + Clipboard + video + 157 SVG | 拖拽上传 + webm 播放 + 图标渲染 |
| M9 | 沙箱+DevTools | bwrap/seccomp + watchdog + Inspector | 杀 renderer 自动恢复 + Inspector 可连接 |
| M10 | SPA 全量验证 | 现有 164 tsx 工程零修改加载运行 | 保障矩阵 30/30 通过 |
| M11 | AI 闭环验证 | AI 生成 Vue 组件 → 预览 → 渲染 | 端到端闭环跑通 |
| M12 | 独立子仓+CI | 桥接层独立 git 仓 + WPE 升级流水线 | 升级流水线自动重跑回归 |
| M13 | 自研率对齐 | 口径说明文档 | 审计方确认封装层/内核/npm 计入口径 |

**关键路径**：M0 → M1 → M2 → M3 →（M4 ‖ M5）→ M6 →（M7 ‖ M8 ‖ M9）→ M10 → M11 → M12 → M13

---

## 十一、风险与应对

| 风险 | 影响 | 应对 | 触发条件 |
|------|------|------|----------|
| WPEPlatform 2.52 预览 API 变动 | 接口微调 | 先 Backend-FDO 跑通，2.54(2026-09) 稳定后切换；抽象层隔离切换点 | 2.54 发布后 |
| EGL 上下文竞争 | Qt/WPE makeCurrent 冲突 | 严格单线程 makeCurrent 秩序；QOpenGLContext 生命周期管控 | M2 渲染管线 |
| AI 生成 GObject/EGL 翻车 | buffer 泄漏/事件漏转换 | 人工 review 三处关键路径 + 回归 diff MiniBrowser 参考 | 每次合入 |
| 信创 GPU 合成异常 | Skia 多线程合成崩 | 环境变量兜底切 CPU 渲染/单线程 painting | M2 ARM64 环境 |
| QWebChannel 通道语义差异 | 通道方法/ signal 行为不一致 | 逐通道对照现有 QWebChannel 注册代码，语义对齐验证 | M6 通道迁移 |
| 流式渲染性能 | SSE 高频增量导致渲染卡顿 | 虚拟滚动 + requestAnimationFrame 节流 | M7 流式验证 |
| GStreamer 缺插件 | video 无法播放 | 镜像打包 gst-plugins-base/good；启动检测插件 | M8 媒体验证 |
| Vue 新 CSS 特性滞后 | 极新特性不可用 | 业务规避 Chromium-only API；功能检测降级 | Vue 升级时 |
| WPE 上游 CVE | 安全风险 | 跟随上游发版；升级流水线自动重跑回归 | 安全公告 |

---

## 十二、自研率评估口径

$$\text{自研率} = \frac{\text{自研源码行数}}{\text{自研} + \text{第三方开源} + \text{上游内核} + \text{运行时库}}$$

分母为操作系统全量源码。

| 方案 | 分母变化 | 自研率影响 |
|------|----------|-----------|
| Qt WebEngine（现状） | +Chromium 3500 万行 | 基准 |
| CEF 替换 | Chromium 仍在分母 | 几乎无边际贡献 |
| WebKitGTK 嵌 Qt | +WebKit ~300 万 + GTK ~150 万 + Cairo | 分母仍较大 |
| **本方案 WPE+Qt 直渲** | +WebKit 公共部 ~300 万，不含 GTK 系 | 分母少 GTK/Cairo 150 万+ 行，自研率上浮 |

> 系统已装 libwpewebkit 不打包进出厂镜像时，WebKit 源码可不计入镜像分母。分子为本方案自研桥接层 3k~7k 行（AI 生成经 review 后计入团队自研）。

---

## 十三、附录

### A. coding 智能体约束（CLAUDE.md 片段）

1. 禁止修改 WPEDisplay/WPEToplevel/WPEView 的虚函数签名与所有权语义。
2. 禁止自行调整 bwrap/seccomp 沙箱参数。
3. WPEBuffer/dmabuf 释放必须走 `g_clear_object`，不得裸 free。
4. 事件转换以 WPEPlatform GLFW 示例为唯一 few-shot 参考。
5. Vue 承载统一走 app:// scheme，禁止 file:// 直开。
6. 所有生成的 C++ 须通过 Qt6 + WPE 2.52 + ARM64-Mesa 编译与 minibrowser Vue 页渲染回归。
7. QWebChannel 通道适配层仅注入 user script，不得修改前端 Vue 源码。
8. 12 通道路由表需与现有 QWebChannel 注册代码逐一对照，不得遗漏通道。

### B. 参考基线

- WPEPlatform GLFW 示例（Igalia）— display/toplevel/view 三 GObject 子类实现范式
- WebKit 仓库 `Source/WebKit/UIProcess/API/wpe/qt6` — Qt6 WPE 嵌入 API 原型（参考，不黑盒依赖）
- WebKitGTK 2.52+ / WPE WebKit 2.50+ 发版公告与安全公告

### C. 构建/依赖规格

```
# pkg-config 模块
wpe-webkit-2.0 >= 2.50
wpebackend-fdo-1.0 >= 1.0   # 阶段一
wpeplatform-0.0              # 阶段二（2.54 稳定后）
Qt6Gui >= 6.5
Qt6OpenGL >= 6.5

# GStreamer（媒体后端，<video> 播放）
gstreamer-1.0 >= 1.20
gst-plugins-base-1.0 >= 1.20
gst-plugins-good-1.0 >= 1.20

# 编译标志
-DQT_OPENGL_DYNAMIC_CAST   # QNativeInterface EGL 取值
-DWPE_BACKEND_FDO           # 阶段一后端选择
```
