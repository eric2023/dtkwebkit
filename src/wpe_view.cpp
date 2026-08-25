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

#include <QOpenGLContext>

#include <QDebug>
#include <jsc/jsc.h>

#include <glib.h>

DTKWPE_BEGIN_NAMESPACE

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
    m_bridge.reset();
    m_schemeHandler.reset();
    releaseCurrentImage();

    if (m_webView)
        g_object_unref(m_webView);
    if (m_exportable)
        wpe_view_backend_exportable_fdo_destroy(m_exportable);
}

void DWPEView::initializeWPE(EGLDisplay eglDisplay)
{
    m_eglDisplay = eglDisplay;

    // Set WPE backend library if not already set (needed for WPEBackend-FDO)
    if (!qEnvironmentVariableIsSet("WPE_BACKEND_LIBRARY"))
        qputenv("WPE_BACKEND_LIBRARY", "libWPEBackend-fdo-1.0.so");
    // Disable bwrap sandbox on UOS (read-only filesystem issue with /usr/share/zoneinfo)
    // TODO (M9): properly configure sandbox with webkit_web_context_add_path_to_sandbox
    if (!qEnvironmentVariableIsSet("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS"))
        qputenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS", "1");
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
    auto *webViewBackend = webkit_web_view_backend_new(backend, nullptr, nullptr);
    m_webView = webkit_web_view_new(webViewBackend);
    // webkit_web_view_new takes ownership of webViewBackend; do not unref.

    // Get the WebKitWebContext and register app:// scheme
    m_context = webkit_web_view_get_context(m_webView);
    m_schemeHandler = std::make_unique<DWPESchemeHandler>();
    m_schemeHandler->registerScheme(m_context);

    // Initialize the JS bridge
    m_bridge = std::make_unique<DWPEBridge>(m_webView);
    m_bridge->initialize();
}

void DWPEView::initializeGL()
{
    initializeOpenGLFunctions();

    m_glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    if (!m_glEGLImageTargetTexture2DOES)
        qWarning() << "DWPEView: glEGLImageTargetTexture2DOES not available";

    glGenTextures(1, &m_texture);
}

void DWPEView::resizeGL(int w, int h)
{
    releaseCurrentImage();
    m_eventTranslator->setViewSize(w, h, static_cast<float>(devicePixelRatioF()));
}

void DWPEView::paintGL()
{
    std::lock_guard<std::mutex> lock(m_imageMutex);

    if (!m_currentImage)
        return;

    EGLImageKHR eglImage = wpe_fdo_egl_exported_image_get_egl_image(m_currentImage);
    if (eglImage && m_glEGLImageTargetTexture2DOES && m_texture) {
        glBindTexture(GL_TEXTURE_2D, m_texture);
        m_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, eglImage);

        glViewport(
            0, 0, static_cast<GLsizei>(width() * devicePixelRatioF()), static_cast<GLsizei>(height() * devicePixelRatioF()));
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(-1.0f, 1.0f);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2f(1.0f, 1.0f);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2f(1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2f(-1.0f, -1.0f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
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
    requestUpdate();
}

void DWPEView::handleShmBuffer(struct wpe_fdo_shm_exported_buffer *buffer)
{
    if (m_exportable)
        wpe_view_backend_exportable_fdo_egl_dispatch_release_shm_exported_buffer(m_exportable, buffer);
}

void DWPEView::releaseCurrentImage()
{
    std::lock_guard<std::mutex> lock(m_imageMutex);
    if (m_currentImage && m_exportable) {
        wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(m_exportable, m_currentImage);
        m_currentImage = nullptr;
    }
}

void DWPEView::onLoadChanged(WebKitWebView *webView, WebKitLoadEvent loadEvent, gpointer data)
{
    if (loadEvent == WEBKIT_LOAD_FINISHED)
        qDebug() << "DWPEView: load finished";
}

void DWPEView::onWebProcessTerminated(WebKitWebView *webView, WebKitWebProcessTerminationReason reason, gpointer data)
{
    qWarning() << "DWPEView: web process terminated, reason:" << reason;
}

gboolean DWPEView::onUserMessageReceived(WebKitWebView *webView, WebKitUserMessage *message, gpointer data)
{
    return FALSE;
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

DTKWPE_END_NAMESPACE
