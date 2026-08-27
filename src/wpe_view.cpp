/*
 * DWPEView -- Core WPE WebKit view embedded in QOpenGLWindow.
 *
 * This is the M1 scaffold implementation. It creates a WPEBackend-FDO EGL
 * exportable view backend and connects the frame export callback.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "wpe_export.h"
#include "wpe_view.h"
#include "wpe_event_translator.h"

#include <wayland-server-core.h>
#include <QOpenGLContext>
#include <QDebug>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QInputMethodQueryEvent>
#include <jsc/jsc.h>
// WebKitHitTestResult.h is included transitively via <wpe/webkit.h>.
#include <glib.h>
#include <QStandardPaths>
#include <QDir>

DTKWPE_BEGIN_NAMESPACE

// GLES2 vertex shader: full-screen quad from a 4-vertex triangle strip.
// position is clip-space [-1,1]; texCoord samples the EGL image texture.
static const char *s_vertSrc = R"GLSL(
attribute vec2 a_pos;
attribute vec2 a_tex;
varying vec2 v_tex;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_tex = a_tex;
}
)GLSL";

// GLES2 fragment shader: sample the WPE EGL image or SHM texture.
// u_yFlip: 1.0 for EGL images (GL origin at bottom-left, needs flip),
//          0.0 for SHM buffers (screen origin at top-left, no flip).
static const char *s_fragSrc = R"GLSL(
precision mediump float;
varying vec2 v_tex;
uniform sampler2D u_tex;
uniform float u_yFlip;
void main() {
    gl_FragColor = texture2D(u_tex, vec2(v_tex.x, u_yFlip > 0.5 ? 1.0 - v_tex.y : v_tex.y));
}
)GLSL";

// Full-screen quad as a triangle strip: (pos.x, pos.y, tex.s, tex.t)
//  4 vertices, 4 floats each → interleaved VBO.
static const float s_quadVerts[] = {
    // pos        tex
    -1.0f,  1.0f,  0.0f, 0.0f,   // top-left
     1.0f,  1.0f,  1.0f, 0.0f,   // top-right
    -1.0f, -1.0f,  0.0f, 1.0f,   // bottom-left
     1.0f, -1.0f,  1.0f, 1.0f,   // bottom-right
};

// WPEBackend-FDO EGL exportable client callbacks.
// Free functions because the C client struct callbacks are referenced
// from a file-scope static initializer and need to call public methods.
static void onExportFdoEglImage(void *data, struct wpe_fdo_egl_exported_image *image)
{
    auto *view = static_cast<DWPEView *>(data);
    view->handleExportedImage(image);
}

static void onExportShmBuffer(void *data, struct wpe_fdo_shm_exported_buffer *buffer)
{
    auto *view = static_cast<DWPEView *>(data);
    view->handleShmBuffer(buffer);
}

static const struct wpe_view_backend_exportable_fdo_egl_client s_eglClient = {
    // export_egl_image -- legacy callback (unused when export_fdo_egl_image is set)
    [](void *data, EGLImageKHR image) -> void {
        (void)data;
        (void)image;
    },
    // export_fdo_egl_image -- main frame callback
    onExportFdoEglImage,
    // export_shm_buffer -- fallback for SHM buffers
    onExportShmBuffer,
    // reserved
    nullptr,
    nullptr,
};

DWPEView::DWPEView()
{
    m_eventTranslator = std::make_unique<DWPEEventTranslator>();
}

DWPEView::~DWPEView()
{
    // Stop the GLib poll timer first to prevent it from dispatching
    // into a half-destroyed backend.
    m_glibPollTimer.stop();

    m_bridge.reset();
    m_schemeHandler.reset();
    releaseCurrentImage();
    // Reset IME context before the WebView is destroyed — WebKit holds a
    // reference to it, so we must drop our back-pointer before unref'ing
    // the view to avoid use-after-free in virtual function callbacks.
    m_imContext.reset();

    // Unref the web view first. WebKit's teardown will release the
    // wpe_view_backend, triggering our destroy-notify callback (set
    // in initializeWPE) which frees m_exportable. This ordering avoids
    // the double-free crash that occurred when destroying both
    // independently.
    if (m_webView) {
        g_object_unref(m_webView);
        m_webView = nullptr;
    }

    // If the notify callback didn't fire (e.g. web view was never
    // fully created), clean up the exportable as a fallback.
    if (m_exportable) {
        wpe_view_backend_exportable_fdo_destroy(m_exportable);
        m_exportable = nullptr;
    }
}

void DWPEView::initializeWPE(EGLDisplay eglDisplay)
{
    m_eglDisplay = eglDisplay;

    // Set WPE backend library if not already set (needed for WPEBackend-FDO)
    if (!qEnvironmentVariableIsSet("WPE_BACKEND_LIBRARY"))
        qputenv("WPE_BACKEND_LIBRARY", "libWPEBackend-fdo-1.0.so");
    // Configure sandbox: add required paths to bwrap so the WebProcess
    // can access system libraries without full sandbox escape.
    // The sandbox is configured per-context after the WebView is created
    // (see webkit_web_context_add_path_to_sandbox below).
    // For UOS environments where /usr/share/zoneinfo is read-only,
    // we add the required paths explicitly instead of disabling the sandbox.
    if (!m_sandboxEnabled) {
        if (!qEnvironmentVariableIsSet("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS"))
            qputenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS", "1");
    }
    // Initialize WPEBackend-FDO for this EGL display
    wpe_fdo_initialize_for_egl_display(eglDisplay);

    // Create the exportable view backend (EGL image export mode)
    uint32_t w = width() > 0 ? static_cast<uint32_t>(width()) : 1024;
    uint32_t h = height() > 0 ? static_cast<uint32_t>(height()) : 768;
    m_exportable = wpe_view_backend_exportable_fdo_egl_create(&s_eglClient, this, w, h);

    if (!m_exportable) {
        qWarning() << "DWPEView: failed to create exportable EGL backend";
        return;
    }

    // Get the WPE view backend and create WebKitWebView
    struct wpe_view_backend *backend = wpe_view_backend_exportable_fdo_get_view_backend(m_exportable);

    // Fix #1: connect backend to EventTranslator so input/activity events dispatch.
    m_eventTranslator->setViewBackend(backend);

    // Initialize the xkb keymap so WPE can convert evdev keycodes to
    // the correct Unicode characters for the user's keyboard layout.
    m_eventTranslator->initializeXkbKeymap();

    // Fix #6: explicitly initialize the backend before creating the web view.
    // Configure sandbox paths on the default WebKitWebContext BEFORE
    // creating the WebView.  add_path_to_sandbox must be called before
    // any WebProcess is spawned (i.e. before webkit_web_view_new()).
    // Using the default context that webkit_web_view_new() will pick up.
    if (m_sandboxEnabled) {
        WebKitWebContext *defaultCtx = webkit_web_context_get_default();
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/usr/lib", true);
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/usr/share", true);
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/etc", true);
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/tmp", false);
        // TLS: CA certificates are needed for HTTPS connections.
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/etc/ssl", true);
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/etc/ca-certificates", true);
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/usr/lib/ssl", true);
        // Resolv.conf and hosts are needed for DNS resolution inside the sandbox.
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/etc/resolv.conf", true);
        webkit_web_context_add_path_to_sandbox(defaultCtx, "/etc/hosts", true);
    }


    // Pass a destroy notify so WebKit calls us when it releases the
    // wpe_view_backend. This avoids a double-free: if we destroy the
    // exportable ourselves AND WebKit also destroys the backend, the
    // second destroy crashes. By letting WebKit drive the teardown via
    // the notify callback, we get a single, ordered destruction.
    auto *webViewBackend = webkit_web_view_backend_new(backend,
        [](gpointer data) {
            auto *view = static_cast<DWPEView *>(data);
            if (view->m_exportable) {
                wpe_view_backend_exportable_fdo_destroy(view->m_exportable);
                view->m_exportable = nullptr;
            }
        }, this);
    m_webView = webkit_web_view_new(webViewBackend);
    // Set the WPE WebView background to opaque. The host application controls
    // the visible background color via setBackgroundColor(QColor). The WPE
    // WebView itself is opaque — the page content is composited on top of an
    // opaque clear color in paintGL, and window-level translucency (if desired)
    // is achieved via QWindow::setWindowOpacity at the compositor level.
    // Setting alpha=0 (transparent) triggers Mesa radeonsi SIGSEGV on AMD
    // Picasso/Raven GPUs when GL_BLEND is used.
    WebKitColor bgColor{0.0, 0.0, 0.0, 1.0};
    webkit_web_view_set_background_color(m_webView, &bgColor);

    // Get the WebKitWebContext and register app:// scheme
    m_context = webkit_web_view_get_context(m_webView);
    m_schemeHandler = std::make_unique<DWPESchemeHandler>();
    m_schemeHandler->registerScheme(m_context);

    // Configure network TLS policy: ignore TLS errors so HTTPS content
    // (video streams, etc.) can load even when the sandbox restricts
    // certificate verification or the system CA store is incomplete.
    WebKitNetworkSession *networkSession = webkit_web_view_get_network_session(m_webView);
    if (networkSession)
        webkit_network_session_set_tls_errors_policy(networkSession, WEBKIT_TLS_ERRORS_POLICY_IGNORE);

    // Enable media playback (video/audio) in the WebKit settings.
    WebKitSettings *settings = webkit_web_view_get_settings(m_webView);
    webkit_settings_set_enable_media(settings, TRUE);
    webkit_settings_set_enable_mediasource(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, TRUE);
    webkit_settings_set_enable_encrypted_media(settings, TRUE);
    webkit_settings_set_media_playback_requires_user_gesture(settings, FALSE);
    webkit_settings_set_media_playback_allows_inline(settings, TRUE);



    // Set WPE cache and data directories to XDG standard paths (§9.8).
    // Uses $XDG_CACHE_HOME/{org}/{app} and $XDG_DATA_HOME/{org}/{app}.
    {
        QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (cacheDir.isEmpty())
            cacheDir = QDir::homePath() + "/.cache/dtkwebkit";
        QDir().mkpath(cacheDir);
        // WebKit uses WEBKIT_CACHE_DIR env var for disk cache location.
        qputenv("WEBKIT_CACHE_DIR", cacheDir.toUtf8());
    }

    // Apply DevTools settings if enabled before first load.
    if (m_devToolsEnabled)
        setDevToolsEnabled(true);

    // Create and attach the IME (input method) context to the WebView.
    // This enables preedit (composition) and commit text forwarding from
    // Qt's input method system to WebKit's editable elements.
    m_imContext = std::make_unique<DWPEInputMethodContext>();
    webkit_web_view_set_input_method_context(m_webView, m_imContext->context());
    // Initialize the JS bridge
    m_bridge = std::make_unique<DWPEBridge>(m_webView);


    m_bridge->initialize();

    // Connect WebKitWebView signal handlers for load events and process crashes.
    g_signal_connect(m_webView, "load-changed", G_CALLBACK(onLoadChanged), this);
    g_signal_connect(m_webView, "web-process-terminated", G_CALLBACK(onWebProcessTerminated), this);
    g_signal_connect(m_webView, "user-message-received", G_CALLBACK(onUserMessageReceived), this);
    g_signal_connect(m_webView, "mouse-target-changed", G_CALLBACK(onMouseTargetChanged), this);
    g_signal_connect(m_webView, "decide-policy", G_CALLBACK(onDecidePolicy), this);
    g_signal_connect(m_webView, "create", G_CALLBACK(onCreate), this);
    g_signal_connect(m_webView, "ready-to-show", G_CALLBACK(onReadyToShow), this);
    g_signal_connect(m_webView, "close", G_CALLBACK(onClose), this);
    // Mark the view as visible, in-window, and focused so WPE starts
    // producing frames and accepting input. focusInEvent may not fire
    // when QOpenGLWindow is embedded in a QWidget container.
    wpe_view_backend_add_activity_state(backend,
        wpe_view_activity_state_visible | wpe_view_activity_state_in_window |
        wpe_view_activity_state_focused);

    // Dispatch the initial view size to WPE so it knows the drawing surface
    // dimensions and can start producing frames immediately.
    float dpr = static_cast<float>(devicePixelRatioF());
    uint32_t pw = static_cast<uint32_t>(width() * dpr);
    uint32_t ph = static_cast<uint32_t>(height() * dpr);
    wpe_view_backend_dispatch_set_size(backend, pw, ph);
    wpe_view_backend_dispatch_set_device_scale_factor(backend, dpr);

    // GLib context bridge: WPEBackend-FDO internally uses a wl_display with
    // GSources attached to g_main_context_default(). Qt's event loop never
    // dispatches that context, so WPE's Wayland events (including frame
    // export) never fire. Pump the GLib default context at 16ms intervals
    // (~60 FPS) so WPE frame delivery stays alive.
    QObject::connect(&m_glibPollTimer, &QTimer::timeout, []() {
        g_main_context_iteration(g_main_context_default(), FALSE);
    });
    m_glibPollTimer.start(16);

    m_wpeReady = true;
}

void DWPEView::initializeGL()
{
    initializeOpenGLFunctions();

    // Fix #4/#5: get the EGLDisplay from Qt's own GL context (not a separate
    // eglGetDisplay call) so WPE EGL images share the same display as the one
    // Qt uses for rendering. This is called from the GL thread, so the context
    // is guaranteed current.
    EGLDisplay eglDisplay = eglGetCurrentDisplay();
    if (eglDisplay == EGL_NO_DISPLAY) {
        // Fallback: try the default display (should not normally happen).
        eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }

    // Initialize WPE only once (initializeGL can be called again after
    // surface recreation, but we want the backend created only once).
    if (!m_wpeReady) {
        initializeWPE(eglDisplay);
    }

    m_glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    if (!m_glEGLImageTargetTexture2DOES)
        qWarning() << "DWPEView: glEGLImageTargetTexture2DOES not available";

    glGenTextures(1, &m_texture);

    // Fix #3: build the GLES2 shader pipeline that replaces the removed
    // fixed-function glBegin/glEnd calls (not available in GLES 2.0+).
    initShader();

    // Notify the application that WPE + GL are both ready for content loading.
    Q_EMIT wpeReady();
}

void DWPEView::resizeGL(int w, int h)
{
    releaseCurrentImage();
    m_eventTranslator->setViewSize(w, h, static_cast<float>(devicePixelRatioF()));
    // Keep the viewport in sync with the window size (device-pixel units).
    glViewport(0, 0, static_cast<GLsizei>(w * devicePixelRatioF()),
               static_cast<GLsizei>(h * devicePixelRatioF()));
}

void DWPEView::setBackgroundColor(const QColor &color)
{
    m_bgColor = color;
    // Sync the WPE WebView background so page transparent areas show this color.
    // WPE WebView background must be opaque (alpha=1.0) to avoid Mesa driver crash.
    if (m_webView) {
        WebKitColor wpeColor{color.redF(), color.greenF(), color.blueF(), 1.0};
        webkit_web_view_set_background_color(m_webView, &wpeColor);
    }
}

void DWPEView::paintGL()
{
    glViewport(
        0, 0, static_cast<GLsizei>(width() * devicePixelRatioF()), static_cast<GLsizei>(height() * devicePixelRatioF()));
    glClearColor(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF(), m_bgColor.alphaF());
    glClear(GL_COLOR_BUFFER_BIT);
    // NOTE: GL_BLEND is intentionally NOT enabled. The WPE texture covers the
 // full viewport, so blending with the clear color is unnecessary.
 // More importantly, enabling GL_BLEND with an alpha channel triggers a
 // Mesa radeonsi driver SIGSEGV on AMD Picasso/Raven GPUs.

    std::lock_guard<std::mutex> lock(m_imageMutex);
    if (!m_currentImage && !m_currentShmBuffer) {
        return;
    }
    if (!m_shaderProgram)
        return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    if (m_currentImage) {
        // EGL image path: import the WPE EGL image via the OES extension.
        EGLImageKHR eglImage = wpe_fdo_egl_exported_image_get_egl_image(m_currentImage);
        if (!eglImage || !m_glEGLImageTargetTexture2DOES) {
            glBindTexture(GL_TEXTURE_2D, 0);
            return;
        }
        m_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, eglImage);
    } else if (m_currentShmBuffer && m_shmData) {
        // SHM fallback path: upload pixel data from the Wayland SHM buffer.
        // WPE uses WL_SHM_FORMAT_ARGB8888 (0) or WL_SHM_FORMAT_XRGB8888 (1),
        // both 32bpp — map to GL_BGRA_EXT (GL_EXT_texture_format_BGRA8888).
        GLenum internalFormat = GL_RGBA;
        GLenum pixelFormat = GL_RGBA;
        if (m_shmFormat == 0 || m_shmFormat == 1) {
            // ARGB8888 / XRGB8888 → BGRA byte order on little-endian.
            pixelFormat = GL_BGRA_EXT;
            internalFormat = GL_BGRA_EXT;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
                     m_shmWidth, m_shmHeight, 0,
                     pixelFormat, GL_UNSIGNED_BYTE, m_shmData);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Draw the full-screen quad using the GLES2 shader pipeline.
    glUseProgram(m_shaderProgram);
    glUniform1i(m_texUniform, 0);
    // EGL images have GL's bottom-left origin (row 0 at t=0 = bottom of
    // screen), so they need a Y flip. SHM buffers from Wayland have a
    // top-left origin (row 0 = top of page); glTexImage2D puts row 0 at
    // t=0, and our quad maps screen-top to t=0, so no flip is needed.
    glUniform1f(m_yFlipUniform, m_currentImage ? 1.0f : 0.0f);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(m_posAttr);
    glVertexAttribPointer(m_posAttr, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(m_texCoordAttr);
    glVertexAttribPointer(m_texCoordAttr, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(2 * sizeof(float)));


    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);


    glDisableVertexAttribArray(m_texCoordAttr);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DWPEView::initShader()
{
    // Compile and link the GLES2 texture-blit shader program.
    m_vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(m_vertexShader, 1, &s_vertSrc, nullptr);
    glCompileShader(m_vertexShader);
    GLint compiled = 0;
    glGetShaderiv(m_vertexShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512] = {};
        glGetShaderInfoLog(m_vertexShader, sizeof(log), nullptr, log);
        qWarning() << "DWPEView: vertex shader compile failed:" << log;
        return;
    }

    m_fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(m_fragmentShader, 1, &s_fragSrc, nullptr);
    glCompileShader(m_fragmentShader);
    glGetShaderiv(m_fragmentShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512] = {};
        glGetShaderInfoLog(m_fragmentShader, sizeof(log), nullptr, log);
        qWarning() << "DWPEView: fragment shader compile failed:" << log;
        return;
    }

    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, m_vertexShader);
    glAttachShader(m_shaderProgram, m_fragmentShader);
    glLinkProgram(m_shaderProgram);
    GLint linked = 0;
    glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512] = {};
        glGetProgramInfoLog(m_shaderProgram, sizeof(log), nullptr, log);
        qWarning() << "DWPEView: shader link failed:" << log;
        return;
    }

    m_posAttr = glGetAttribLocation(m_shaderProgram, "a_pos");
    m_texCoordAttr = glGetAttribLocation(m_shaderProgram, "a_tex");
    m_texUniform = glGetUniformLocation(m_shaderProgram, "u_tex");
    m_yFlipUniform = glGetUniformLocation(m_shaderProgram, "u_yFlip");
    // Upload the full-screen quad into a VBO.
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(s_quadVerts), s_quadVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


void DWPEView::loadUrl(const QString &url)
{
    if (!m_webView)
        return;
    webkit_web_view_load_uri(m_webView, url.toUtf8().constData());
}

void DWPEView::loadHtml(const QString &html, const QString &baseUrl)
{
    if (!m_webView)
        return;
    webkit_web_view_load_html(m_webView, html.toUtf8().constData(), baseUrl.isEmpty() ? "app:///" : baseUrl.toUtf8().constData());
}

void DWPEView::runJavaScript(const QString &code, std::function<void(QVariant)> callback)
{
    if (!m_webView)
        return;

    auto *cb = new std::function<void(QVariant)>(std::move(callback));
    webkit_web_view_evaluate_javascript(
        m_webView,
        code.toUtf8().constData(),
        -1,
        nullptr,
        nullptr,
        nullptr,
        [](GObject *obj, GAsyncResult *res, gpointer data) {
            auto *cbPtr = static_cast<std::function<void(QVariant)> *>(data);
            GError *error = nullptr;
            JSCValue *result = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(obj), res, &error);
            if (error) {
                qWarning() << "JS eval error:" << error->message;
                g_error_free(error);
            } else if (cbPtr && *cbPtr) {
                (*cbPtr)(QVariant());
            }
            if (result)
                g_object_unref(result);
            delete cbPtr;
        },
        cb);
}

void DWPEView::setViewSize(int w, int h)
{
    m_eventTranslator->setViewSize(w, h, static_cast<float>(devicePixelRatioF()));
}

void DWPEView::handleExportedImage(struct wpe_fdo_egl_exported_image *image)
{
    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        if (m_currentImage && m_exportable) {
            wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(m_exportable, m_currentImage);
        }
        m_currentImage = image;
    }

    // Fix #2: notify WPEBackend-FDO that the frame has been "displayed".
    // Without this call, WPE considers the previous frame still in flight
    // and never produces the next one — the rendering pipeline freezes.
    if (m_exportable)
        wpe_view_backend_exportable_fdo_dispatch_frame_complete(m_exportable);

    requestUpdate();
}

void DWPEView::handleShmBuffer(struct wpe_fdo_shm_exported_buffer *buffer)
{
    // Release previous SHM buffer and store the new one atomically.
    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        releaseCurrentShmBuffer();
        m_currentShmBuffer = buffer;

        struct wl_shm_buffer *wlBuffer = wpe_fdo_shm_exported_buffer_get_shm_buffer(buffer);
        if (wlBuffer) {
            wl_shm_buffer_begin_access(wlBuffer);
            m_shmData = wl_shm_buffer_get_data(wlBuffer);
            m_shmStride = wl_shm_buffer_get_stride(wlBuffer);
            m_shmWidth = wl_shm_buffer_get_width(wlBuffer);
            m_shmHeight = wl_shm_buffer_get_height(wlBuffer);
            m_shmFormat = wl_shm_buffer_get_format(wlBuffer);
            wl_shm_buffer_end_access(wlBuffer);
        }
    }

    if (m_exportable)
        wpe_view_backend_exportable_fdo_dispatch_frame_complete(m_exportable);

    requestUpdate();
}

void DWPEView::releaseCurrentImage()
{
    std::lock_guard<std::mutex> lock(m_imageMutex);
    if (m_currentImage && m_exportable) {
        wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(m_exportable, m_currentImage);
        m_currentImage = nullptr;
    }
    releaseCurrentShmBuffer();
}

void DWPEView::releaseCurrentShmBuffer()
{
    if (m_currentShmBuffer && m_exportable) {
        wpe_view_backend_exportable_fdo_egl_dispatch_release_shm_exported_buffer(m_exportable, m_currentShmBuffer);
        m_currentShmBuffer = nullptr;
        m_shmData = nullptr;
        m_shmStride = 0;
        m_shmWidth = 0;
        m_shmHeight = 0;
        m_shmFormat = 0;
    }
}

void DWPEView::onLoadChanged(WebKitWebView *webView, WebKitLoadEvent loadEvent, gpointer data)
{
    auto *view = static_cast<DWPEView *>(data);
    switch (loadEvent) {
    case WEBKIT_LOAD_STARTED:
        Q_EMIT view->loadProgressChanged(0);
        view->m_lastLoadedUrl = QString::fromUtf8(webkit_web_view_get_uri(webView));
        Q_EMIT view->urlChanged(view->m_lastLoadedUrl);
        break;
    case WEBKIT_LOAD_COMMITTED:
        view->m_lastLoadedUrl = QString::fromUtf8(webkit_web_view_get_uri(webView));
        Q_EMIT view->urlChanged(view->m_lastLoadedUrl);
        Q_EMIT view->titleChanged(QString::fromUtf8(webkit_web_view_get_title(webView)));
        // Inject bridge code (window.host, __TAURI_IPC__, qt) into the page's
        // main JavaScript world. User scripts run in an isolated world, so
        // evaluate_javascript is used instead. LOAD_COMMITTED fires after
        // document creation but before page scripts execute.
        if (view->m_bridge)
            view->m_bridge->injectIntoMainWorld();
        break;
    case WEBKIT_LOAD_FINISHED:
        // Reset crash retry counter on successful page load.
        view->m_crashRetryCount = 0;
        Q_EMIT view->titleChanged(QString::fromUtf8(webkit_web_view_get_title(webView)));
        Q_EMIT view->loadProgressChanged(100);
        qDebug() << "DWPEView: load finished";
        break;
    case WEBKIT_LOAD_REDIRECTED:
        Q_EMIT view->urlChanged(QString::fromUtf8(webkit_web_view_get_uri(webView)));
        break;
    }
}
void DWPEView::onWebProcessTerminated(WebKitWebView *webView, WebKitWebProcessTerminationReason reason, gpointer data)
{
    auto *view = static_cast<DWPEView *>(data);
    qWarning() << "DWPEView: web process terminated, reason:" << reason;

    // Emit the onRenderCrashed signal for the host application.
    // The host can use this to show a "page crashed" message or take
    // other recovery actions beyond the automatic reload below.
    Q_EMIT view->onRenderCrashed();

    // Automatic crash recovery: reload the current page if enabled and
    // the retry count hasn't been exceeded. WPE WebKit automatically
    // relaunches the WebProcess on the next load/reload call.
    if (view->m_crashRecoveryEnabled && view->m_crashRetryCount < kMaxCrashRetries) {
        view->m_crashRetryCount++;
        qWarning() << "DWPEView: auto-recovering (attempt" << view->m_crashRetryCount
                   << "of" << kMaxCrashRetries << ")";
        // Reload bypassing cache to ensure fresh content after crash.
        webkit_web_view_reload_bypass_cache(webView);
    } else if (view->m_crashRetryCount >= kMaxCrashRetries) {
        qWarning() << "DWPEView: max crash retries exceeded, not reloading";
    }
}

gboolean DWPEView::onUserMessageReceived(WebKitWebView *webView, WebKitUserMessage *message, gpointer data)
{
    return FALSE;
}

void DWPEView::onMouseTargetChanged(WebKitWebView *webView, WebKitHitTestResult *hitTestResult, guint modifiers, gpointer data)
{
    // Change the Qt cursor shape based on the element under the mouse.
    // WebKitHitTestResult contexts: LINK, IMAGE, MEDIA, EDITABLE, SCROLLBAR, SELECTION
    auto *view = static_cast<DWPEView *>(data);
    if (webkit_hit_test_result_context_is_link(hitTestResult))
        view->setCursor(Qt::PointingHandCursor);
    else if (webkit_hit_test_result_context_is_editable(hitTestResult))
        view->setCursor(Qt::IBeamCursor);
    else
        view->setCursor(Qt::ArrowCursor);
}

void DWPEView::keyPressEvent(QKeyEvent *event)
{
    m_eventTranslator->translateKeyEvent(event);
    QOpenGLWindow::keyPressEvent(event);
}

void DWPEView::keyReleaseEvent(QKeyEvent *event)
{
    m_eventTranslator->translateKeyEvent(event);
    QOpenGLWindow::keyReleaseEvent(event);
}

void DWPEView::mousePressEvent(QMouseEvent *event)
{
    m_eventTranslator->translateMouseEvent(event);
    QOpenGLWindow::mousePressEvent(event);
}

void DWPEView::mouseReleaseEvent(QMouseEvent *event)
{
    m_eventTranslator->translateMouseEvent(event);
    QOpenGLWindow::mouseReleaseEvent(event);
}

void DWPEView::mouseMoveEvent(QMouseEvent *event)
{
    m_eventTranslator->translateMouseEvent(event);
    QOpenGLWindow::mouseMoveEvent(event);
}

void DWPEView::wheelEvent(QWheelEvent *event)
{
    m_eventTranslator->translateWheelEvent(event);
    QOpenGLWindow::wheelEvent(event);
}

void DWPEView::focusInEvent(QFocusEvent *event)
{
    m_eventTranslator->setFocused(true);
    QOpenGLWindow::focusInEvent(event);
}

void DWPEView::focusOutEvent(QFocusEvent *event)
{
    m_eventTranslator->setFocused(false);
    QOpenGLWindow::focusOutEvent(event);
}

bool DWPEView::event(QEvent *event)
{
    // Handle input method events via event() since QWindow doesn't provide
    // inputMethodEvent/inputMethodQuery virtual functions.
    if (event->type() == QEvent::InputMethod) {
        auto *imeEvent = static_cast<QInputMethodEvent *>(event);
        if (m_imContext) {
            if (!imeEvent->commitString().isEmpty()) {
                // IME committed final text — forward to WebKit which
                // inserts it into the focused editable element.
                m_imContext->commitText(imeEvent->commitString());
            } else if (!imeEvent->preeditString().isEmpty()) {
                // Preedit (composition) text — find cursor position
                // from attributes.
                int cursorPos = imeEvent->preeditString().length();
                for (const auto &attr : imeEvent->attributes()) {
                    if (attr.type == QInputMethodEvent::Cursor) {
                        cursorPos = attr.start;
                        break;
                    }
                }
                m_imContext->setPreeditText(imeEvent->preeditString(), cursorPos);
            } else {
                // Empty preedit with no commit — finish composition.
                m_imContext->setPreeditText(QString(), 0);
            }
        }
        return true;
    }
    if (event->type() == QEvent::InputMethodQuery) {
        auto *queryEvent = static_cast<QInputMethodQueryEvent *>(event);
        // Answer Qt's queries about the editable element's state so the
        // platform input method can position the candidate window.
        if (queryEvent->queries() & Qt::ImEnabled)
            queryEvent->setValue(Qt::ImEnabled, QVariant(true));
        if (queryEvent->queries() & Qt::ImCursorRectangle) {
            QRect rect(0, 0, 1, 16);
            if (m_imContext)
                rect = m_imContext->cursorRect();
            queryEvent->setValue(Qt::ImCursorRectangle, QVariant(rect));
        }
        if (queryEvent->queries() & Qt::ImHints)
            queryEvent->setValue(Qt::ImHints, QVariant(Qt::ImhNone));
        return true;
    }
    return QOpenGLWindow::event(event);
}

gboolean DWPEView::onDecidePolicy(WebKitWebView *webView, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer data)
{
    // For navigation actions (link clicks, form submits, redirects), allow them.
    // For new-window actions (target="_blank"), load the URL in the current
    // view instead of creating a new WebView without a WPE backend — that
    // would crash on the next interaction.
    auto *view = static_cast<DWPEView *>(data);

    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        // Extract the target URL and load it in the current view.
        auto *navDecision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        auto *action = webkit_navigation_policy_decision_get_navigation_action(navDecision);
        if (action) {
            auto *request = webkit_navigation_action_get_request(action);
            if (request) {
                const char *uri = webkit_uri_request_get_uri(request);
                if (uri)
                    webkit_web_view_load_uri(webView, uri);
            }
        }
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }

    // Default: allow the navigation.
    webkit_policy_decision_use(decision);
    return TRUE;
}

WebKitWebView *DWPEView::onCreate(WebKitWebView *webView, WebKitNavigationAction *navigationAction, gpointer data)
{
    // New-window requests are handled in onDecidePolicy (NEW_WINDOW_ACTION)
    // by loading the URL in the current view. This signal should not fire
    // for those cases. If it does (e.g. window.open() bypassing policy),
    // redirect the URL to the current view and return the same webView.
    // Returning nullptr can crash WebKit; returning the same view makes
    // WebKit reuse this view for the new "window".
    auto *request = webkit_navigation_action_get_request(navigationAction);
    if (request) {
        const char *uri = webkit_uri_request_get_uri(request);
        if (uri)
            webkit_web_view_load_uri(webView, uri);
    }
    return webView;
}

void DWPEView::onReadyToShow(WebKitWebView *webView, gpointer data)
{
    // A new WebView created by onCreate is ready to display.
    // Full multi-window embedding requires a new DWPEView + QOpenGLWindow
    // pair for each new WebView. This is a stub — the host application
    // should handle this signal to create and show a new window.
    Q_UNUSED(webView);
    Q_UNUSED(data);
}

void DWPEView::onClose(WebKitWebView *webView, gpointer data)
{
    // The WebView requested to close (e.g. window.close()).
    Q_UNUSED(data);
    webkit_web_view_try_close(webView);
}

void DWPEView::setDevToolsEnabled(bool enabled)
{
    m_devToolsEnabled = enabled;

    if (!m_webView)
        return;

    // Enable developer extras in WebKitSettings — this allows the
    // Web Inspector to be opened for this WebView.
    WebKitSettings *settings = webkit_web_view_get_settings(m_webView);
    webkit_settings_set_enable_developer_extras(settings, enabled);

    if (enabled) {
        // Start the remote inspector server on a standard path.
        // WPE WebKit 2.46 uses the WEBKIT_INSPECTOR_SERVER env var
        // to configure the remote debugging server.
        // Use XDG standard path for the inspector socket (§9.8).
        QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (runtimeDir.isEmpty())
            runtimeDir = QDir::homePath() + "/.runtime";
        QDir().mkpath(runtimeDir);

        // Default inspector server on a local port.
        // The host application can connect to this port via a browser.
        if (m_devToolsServerPath.isEmpty())
            m_devToolsServerPath = QStringLiteral("127.0.0.1:9222");

        qputenv("WEBKIT_INSPECTOR_SERVER", m_devToolsServerPath.toUtf8());
        qputenv("WEBKIT_INSPECTOR_HTTP_SERVER", m_devToolsServerPath.toUtf8());
        qDebug() << "DWPEView: DevTools inspector server enabled at" << m_devToolsServerPath;
    } else {
        // Clear the inspector server env vars to disable.
        qunsetenv("WEBKIT_INSPECTOR_SERVER");
        qunsetenv("WEBKIT_INSPECTOR_HTTP_SERVER");
        qDebug() << "DWPEView: DevTools inspector server disabled";
    }
}

void DWPEView::setSandboxEnabled(bool enabled)
{
    m_sandboxEnabled = enabled;

    if (!m_webView)
        return;

    if (!enabled) {
        // Disable sandbox entirely (dangerous: use only for debugging).
        if (!qEnvironmentVariableIsSet("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS"))
            qputenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS", "1");
    } else {
        // Re-enable sandbox by clearing the disable env var.
        qunsetenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS");

        // Add standard paths to the bwrap sandbox.
        if (m_context) {
            webkit_web_context_add_path_to_sandbox(m_context, "/usr/lib", true);
            webkit_web_context_add_path_to_sandbox(m_context, "/usr/share", true);
            webkit_web_context_add_path_to_sandbox(m_context, "/etc", true);
            webkit_web_context_add_path_to_sandbox(m_context, "/tmp", false);
            webkit_web_context_add_path_to_sandbox(m_context, "/etc/ssl", true);
            webkit_web_context_add_path_to_sandbox(m_context, "/etc/ca-certificates", true);
            webkit_web_context_add_path_to_sandbox(m_context, "/usr/lib/ssl", true);
            webkit_web_context_add_path_to_sandbox(m_context, "/etc/resolv.conf", true);
            webkit_web_context_add_path_to_sandbox(m_context, "/etc/hosts", true);
        }
    }
}

DTKWPE_END_NAMESPACE
