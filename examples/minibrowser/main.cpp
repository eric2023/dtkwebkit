/*
 * minibrowser -- minimal WPE WebKit Qt6 embedding test harness.
 *
 * Creates a DMainWindow with a DWPEView inside, loads a test HTML page
 * via app:// scheme, and validates the JS bridge round-trip.
 *
 * Copyright (c) 2026 UnionTech Software Technology Co., Ltd.
 * LGPL-3.0-or-later
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <DMainWindow>
#include <DApplication>
#include <DWidgetUtil>

#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QWidget>
#include <QTimer>
#include <QDebug>

#include <EGL/egl.h>

#include "web_surface.h"
#include "wpe_view.h"
#include "wpe_bridge.h"
#include "wpe_channel_adapter.h"

DTKWPE_USE_NAMESPACE

DWIDGET_USE_NAMESPACE

static const char *s_testHtml = R"HTML(
<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>WPE Bridge Test</title></head>
<body>
<h1>WPE WebKit Qt6 Bridge Test</h1>
<p>Testing app:// scheme + JS bridge round-trip</p>
<div id="result">Waiting...</div>
<div id="event-log"></div>
<script>
document.addEventListener('keydown', function(e) {
    var log = document.getElementById('event-log');
    log.textContent = 'Key: ' + e.key + ' (code=' + e.keyCode + ')';
});

document.addEventListener('click', function(e) {
    var log = document.getElementById('event-log');
    log.textContent = 'Click: ' + e.clientX + ',' + e.clientY;
});

// Test bridge round-trip
if (window.host) {
    document.getElementById('result').textContent = 'host available, sending message...';
    window.host.postMessage({channel: 'test', method: 'ping', data: ['hello']}).then(function(reply) {
        document.getElementById('result').textContent = 'Reply: ' + JSON.stringify(reply);
    }).catch(function(err) {
        document.getElementById('result').textContent = 'Error: ' + err;
    });
} else {
    document.getElementById('result').textContent = 'host NOT available';
}
</script>
</body>
</html>
)HTML";

int main(int argc, char *argv[])
{
    DApplication *app = DApplication::globalApplication(argc, argv);
    app->setOrganizationName("uniontech");
    app->setApplicationName("minibrowser");

    // Request OpenGL ES 3.0+ context for WPE EGL compatibility
    QSurfaceFormat fmt;
    fmt.setVersion(3, 0);
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    QSurfaceFormat::setDefaultFormat(fmt);

    DMainWindow window;
    window.resize(1024, 768);
    window.setWindowTitle("WPE MiniBrowser");

    auto *view = new DWPEView;
    window.setCentralWidget(QWidget::createWindowContainer(view, &window));

    // Initialize WPE after the window's GL context is available
    QTimer::singleShot(100, [view]() {
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        view->initializeWPE(display);

        // Register app:// resources
        auto *scheme = view->schemeHandler();
        if (scheme) {
            scheme->addResource("/", QByteArray(s_testHtml), "text/html");
            scheme->addResource("/index.html", QByteArray(s_testHtml), "text/html");
        }

        // Set up the JS bridge with a test channel
        auto *bridge = view->bridge();
        if (bridge) {
            bridge->setMessageHandler([](const QVariantMap &msg) -> QVariant {
                qDebug() << "Bridge received:" << msg;
                QString channel = msg.value("channel").toString();
                QString method = msg.value("method").toString();

                if (channel == "test" && method == "ping") {
                    QVariantMap reply;
                    reply["status"] = "ok";
                    reply["echo"] = msg.value("data");
                    return reply;
                }

                return QVariant();
            });
        }

        // Load via app:// scheme to test resource virtualization
        view->loadUrl("app:///index.html");

        qDebug() << "WPE minibrowser ready, loading app:///index.html";
    });

    moveToCenter(&window);
    window.show();

    return app->exec();
}
