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
#include "wpe_export.h"
#include "wpe_scheme_handler.h"
#include "wpe_bridge.h"
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

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Event forwarding
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

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
    std::mutex m_imageMutex;

    // GL texture for rendering the WPE frame
    GLuint m_texture{0};

    // EGL extension functions

    // JS bridge and scheme handler
    std::unique_ptr<DWPEBridge> m_bridge;
    std::unique_ptr<DWPESchemeHandler> m_schemeHandler;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC m_glEGLImageTargetTexture2DOES{nullptr};

    // Event translator
    std::unique_ptr<DWPEEventTranslator> m_eventTranslator;

    // --- WebKitWebView signal handlers ---
    static void onLoadChanged(WebKitWebView *webView, WebKitLoadEvent loadEvent, gpointer data);
    static void onWebProcessTerminated(WebKitWebView *webView, WebKitWebProcessTerminationReason reason, gpointer data);
    static gboolean onUserMessageReceived(WebKitWebView *webView, WebKitUserMessage *message, gpointer data);

    // --- GL texture helpers ---
    void releaseCurrentImage();
};

}  // namespace DTKWPE
#endif  // DWPE_VIEW_H
