/*
 * This file is part of dtkwebkit.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 *
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef DWPE_VIEW_H
#define DWPE_VIEW_H

#include <QOpenGLWindow>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QSurfaceFormat>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include "wpe_event_translator.h"
#include "wpe_scheme_handler.h"
#include "wpe_bridge.h"
#include "wpe_input_method_context.h"
#include <QInputMethodEvent>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <wpe/webkit.h>
#include <wpe/fdo.h>
#include <wpe/fdo-egl.h>
#include <memory>
#include <mutex>
namespace DTKWPE {

class DWPEEventTranslator;

/**
 * @brief Core WPE WebKit view embedded in a QOpenGLWindow.
 *
 * Creates a WPEBackend-FDO EGL exportable view backend, receives EGLImageKHR
 * frames via the export_fdo_egl_image callback, and uploads them as GL
 * textures for display in the Qt window.
 *
 * Lifecycle:
 *   1. construct → createWPEViewBackend() (needs EGLDisplay ready)
 *   2. WPE renders → exportFdoEglImage callback → store image
 *   3. paintGL → eglImage → GL texture → draw quad
 *   4. ~destroy → release image + backend
 *
 * Buffer release (HUMAN REVIEW REQUIRED):
 *   - wpe_fdo_egl_exported_image must be released via
 *     wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image()
 *   - Never bare free(); always g_clear_object or the FDO release dispatch
 *   - On resize/focus change: release current image before creating new surface
 */
class DWPEView : public QOpenGLWindow, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit DWPEView();
    ~DWPEView() override;

    // Initialize WPE backend (call after EGLDisplay is available)
    void initializeWPE(EGLDisplay eglDisplay);

    // Emitted from initializeGL() after WPE backend is ready for use.
    // Connect to this instead of using a QTimer to ensure GL context + WPE
    // initialization are both complete before loading content.
    Q_SIGNAL void wpeReady();

    // True after initializeWPE() has run (backend, webView, bridge all created).
    bool isWpeReady() const { return m_wpeReady; }
    // Input method context for IME (composition/preedit) support.
    DWPEInputMethodContext *inputMethodContext() const { return m_imContext.get(); }


    // WebKitWebView access
    WebKitWebView *webView() const { return m_webView; }

    // Load content
    void loadUrl(const QString &url);
    void loadHtml(const QString &html, const QString &baseUrl);

    // JS bridge (initialize after WPE is ready)
    DWPEBridge *bridge() const { return m_bridge.get(); }

    // Scheme handler (register before loading app:// URLs)
    DWPESchemeHandler *schemeHandler() const { return m_schemeHandler.get(); }

    // Load content
    void runJavaScript(const QString &code, std::function<void(QVariant)> callback);

    // Resize notification (WPE backend needs updated size)
    void setViewSize(int width, int height);

    // Set the background color used by paintGL's glClearColor. This is the
    // color visible behind the web page content wherever the page has no
    // opaque background of its own. Defaults to transparent (0,0,0,0).
    // The WPE WebView itself is set to transparent (alpha=0) so the page
    // content takes priority — this color only fills the "no content" areas.
    void setBackgroundColor(const QColor &color) { m_bgColor = color; }
    QColor backgroundColor() const { return m_bgColor; }

    // --- DevTools / Inspector ---
    bool isDevToolsEnabled() const { return m_devToolsEnabled; }
    void setDevToolsEnabled(bool enabled);

    // --- Sandbox ---
    bool isSandboxEnabled() const { return m_sandboxEnabled; }
    void setSandboxEnabled(bool enabled);

    // --- Crash recovery ---
    // When enabled, a crashed WebProcess is automatically restarted and
    // the current page is reloaded. Defaults to true.
    void setCrashRecoveryEnabled(bool enabled) { m_crashRecoveryEnabled = enabled; }
    bool isCrashRecoveryEnabled() const { return m_crashRecoveryEnabled; }


protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void initShader();

protected:
    // Event forwarding
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    // Input method events are handled via event() since QWindow doesn't
    // provide inputMethodEvent/inputMethodQuery virtual functions.
    bool event(QEvent *event) override;

public:
    // Called from WPEBackend-FDO EGL callbacks (free functions in .cpp)
    void handleExportedImage(struct wpe_fdo_egl_exported_image *image);
    void handleShmBuffer(struct wpe_fdo_shm_exported_buffer *buffer);

private:
    // WPEBackend-FDO EGL exportable backend
    struct wpe_view_backend_exportable_fdo *m_exportable{nullptr};
    WebKitWebView *m_webView{nullptr};
    WebKitWebContext *m_context{nullptr};

    // EGL display (shared from Qt's QOpenGLContext)
    EGLDisplay m_eglDisplay{EGL_NO_DISPLAY};

    // Current exported EGL image (pending texture upload)
    struct wpe_fdo_egl_exported_image *m_currentImage{nullptr};

    // Current SHM buffer (fallback when EGL image export is unavailable)
    struct wpe_fdo_shm_exported_buffer *m_currentShmBuffer{nullptr};
    void *m_shmData{nullptr};
    int32_t m_shmStride{0};
    int32_t m_shmWidth{0};
    int32_t m_shmHeight{0};
    uint32_t m_shmFormat{0};

    std::mutex m_imageMutex;

    // GL texture for rendering the WPE frame
    GLuint m_texture{0};

    // GLES2 shader pipeline for EGLImage texture blit
    GLuint m_shaderProgram{0};
    GLuint m_vertexShader{0};
    GLuint m_fragmentShader{0};
    GLuint m_vbo{0};
    GLint m_posAttr{0};
    GLint m_texCoordAttr{0};
    GLint m_texUniform{0};
    GLint m_yFlipUniform{0};
    // EGL extension functions

    // JS bridge and scheme handler
    std::unique_ptr<DWPEBridge> m_bridge;
    std::unique_ptr<DWPESchemeHandler> m_schemeHandler;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC m_glEGLImageTargetTexture2DOES{nullptr};

    // Event translator
    std::unique_ptr<DWPEEventTranslator> m_eventTranslator;
    // Whether initializeWPE() has completed
    bool m_wpeReady{false};

    // Background clear color for paintGL (set by host via setBackgroundColor).
    QColor m_bgColor{0, 0, 0, 0};  // transparent by default

    // GLib main context bridge: WPEBackend-FDO registers GSources on the GLib
    // default context. Qt's event loop doesn't dispatch GLib, so we pump it
    // via a high-frequency timer to keep WPE frame delivery alive.
    QTimer m_glibPollTimer;
    // --- WebKitWebView signal handlers ---
    static void onLoadChanged(WebKitWebView *webView, WebKitLoadEvent loadEvent, gpointer data);
    static void onWebProcessTerminated(WebKitWebView *webView, WebKitWebProcessTerminationReason reason, gpointer data);
    // DevTools state
    bool m_devToolsEnabled{false};
    QString m_devToolsServerPath;  // inspector server address (host:port)

    // Sandbox state
    bool m_sandboxEnabled{true};

    // Crash recovery state
    bool m_crashRecoveryEnabled{true};
    QString m_lastLoadedUrl;  // for crash-reload
    int m_crashRetryCount{0};
    static constexpr int kMaxCrashRetries = 3;

    static gboolean onUserMessageReceived(WebKitWebView *webView, WebKitUserMessage *message, gpointer data);
    static void onMouseTargetChanged(WebKitWebView *webView, WebKitHitTestResult *hitTestResult, guint modifiers, gpointer data);
    // --- decide-policy and create signal handlers ---
    static gboolean onDecidePolicy(WebKitWebView *webView, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer data);
    static WebKitWebView *onCreate(WebKitWebView *webView, WebKitNavigationAction *navigationAction, gpointer data);
    static void onReadyToShow(WebKitWebView *webView, gpointer data);
    static void onClose(WebKitWebView *webView, gpointer data);

    // IME context bridge
public:
    // --- Host-facing signals (public) ---
    Q_SIGNAL void urlChanged(const QString &url);
    Q_SIGNAL void titleChanged(const QString &title);
    Q_SIGNAL void loadProgressChanged(int progress);
    Q_SIGNAL void onRenderCrashed();

private:
    // IME context bridge
    std::unique_ptr<DWPEInputMethodContext> m_imContext;
    // --- GL texture helpers ---
    void releaseCurrentImage();
    void releaseCurrentShmBuffer();
};

}  // namespace DTKWPE
#endif  // DWPE_VIEW_H
