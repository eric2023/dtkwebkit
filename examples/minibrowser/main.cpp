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
#include <DTitlebar>
#include <DApplication>
#include <DWidgetUtil>

#include <QSurfaceFormat>
#include <QWidget>
#include <QColor>
#include <QFileInfo>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QRect>
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

    // --translucent: make the content area semi-transparent (50% alpha),
    // letting the compositor show what's behind. The title bar stays
    // opaque. Without this flag, the background is fully opaque (#F4F4F4).
    bool translucent = (argc > 1 && QString::fromUtf8(argv[1]) == "--translucent");

    // Request OpenGL ES 3.0+ context for WPE EGL compatibility.
    // For --translucent mode, also request an alpha buffer (8-bit) so the
    // GL framebuffer can carry alpha to the compositor.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 0);
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    if (translucent)
        fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);
    // If a URL is given on the command line (not --translucent), load that instead.
    QString urlToLoad;
    {
        for (int i = 1; i < argc; ++i) {
            QString arg = QString::fromUtf8(argv[i]);
            if (arg != "--translucent") { urlToLoad = arg; break; }
        }
    }

    // Locate the Vue dist/ directory relative to the executable or source tree.
    QString distDir;
    {
        // Try: <exe-dir>/dist, <exe-dir>/../dist, source tree dist
        QString exeDir = QCoreApplication::applicationDirPath();
        QStringList candidates = {
            exeDir + "/dist",
            exeDir + "/../dist",
            exeDir + "/../../examples/minibrowser/dist",
            QStringLiteral(DTKWEBKIT_SOURCE_DIR "/examples/minibrowser/dist"),
        };
        for (const auto &c : candidates) {
            if (QFileInfo::exists(c + "/index.html")) {
                distDir = c;
                break;
            }
        }
    }

    DMainWindow window;
    window.resize(1024, 768);
    window.setWindowTitle("WPE MiniBrowser");
    QColor bgColor(0xF4, 0xF4, 0xF4, translucent ? 0x80 : 0xFF);

    // For --translucent: use setTranslucentBackground so the DMainWindow
    // surface is translucent. The title bar (DTitlebar) paints its own
    // opaque background, so only the content area is affected.
    if (translucent)
        window.setTranslucentBackground(true);

    auto *view = new DWPEView;
    view->setBackgroundColor(bgColor);

    if (translucent) {
        // Translucent mode: embed DWPEView as a top-level child QWindow
        // (parented to the DMainWindow's native handle) instead of using
        // createWindowContainer. QWidget::createWindowContainer creates
        // an opaque backing widget that discards the framebuffer alpha,
        // preventing compositor-level transparency. A parented QWindow
        // retains its alpha channel, so the compositor blends the content
        // with what's behind the window. The view is positioned manually
        // below the title bar and shown after the main window is visible.
        // The view is positioned and shown after the main window is visible
        // (see below, after window.show()).
    } else {
        // Opaque mode: createWindowContainer is fine.
        window.setCentralWidget(QWidget::createWindowContainer(view, &window));
    }

    // WPE is initialized inside DWPEView::initializeGL() (which runs once the
    // Qt GL context is current), and the wpeReady signal is emitted immediately
    // after. Connect to it so scheme registration, bridge setup, and content
    // loading all happen after both GL and WPE are fully initialized — no
    // QTimer guessing about GL readiness.
    QObject::connect(view, &DWPEView::wpeReady, [view, urlToLoad, distDir, bgColor]() {
        // Re-apply background color after WPE WebView is created.
        // The initial call before wpeReady sets m_bgColor, but the WebView
        // doesn't exist yet — this call injects the CSS into the live page.
        view->setBackgroundColor(bgColor);
        auto *bridge = view->bridge();
        if (bridge) {
            bridge->setMessageHandler([](const QVariantMap &msg) -> QVariant {
                // The JS bridge wraps messages as {id: N, data: {channel, method, data}}
                // so channel/method/data are nested inside msg["data"].
                QVariantMap data = msg.value("data").toMap();
                QString channel = data.value("channel").toString();
                QString method = data.value("method").toString();
                qDebug() << "Bridge received:" << channel << method << data;

                if (channel == "test" && method == "ping") {
                    QVariantMap reply;
                    reply["status"] = "ok";
                    reply["echo"] = data.value("data");
                    return reply;
                }

                // Tauri IPC command handlers
                if (channel == "tauri") {
                    QVariantMap tauriData = data.value("data").toMap();

                    if (method == "greet") {
                        QString name = tauriData.value("name").toString();
                        if (name.isEmpty())
                            name = "World";
                        return QVariant(QString("Hello, %1! 你好 from DTK WebKit").arg(name));
                    }

                    if (method == "get_system_info") {
                        QVariantMap info;
                        info["os"] = QSysInfo::productType();
                        info["arch"] = QSysInfo::currentCpuArchitecture();
                        info["hostname"] = QSysInfo::machineHostName();
                        info["cpu_count"] = QString::number(QThread::idealThreadCount());
                        {
                            QFile f("/proc/uptime");
                            if (f.open(QIODevice::ReadOnly)) {
                                double secs = f.readAll().split(' ')[0].toDouble();
                                int hours = int(secs) / 3600;
                                int mins = (int(secs) % 3600) / 60;
                                info["uptime"] = QString("%1h %2m").arg(hours).arg(mins);
                            } else {
                                info["uptime"] = QString("unknown");
                            }
                        }
                        return info;
                    }
                }

                return QVariant();
            });
        }

        if (urlToLoad.isEmpty()) {
            auto *scheme = view->schemeHandler();
            if (scheme) {
                // Load Vue dist/ if available, otherwise fall back to test HTML.
                if (!distDir.isEmpty()) {
                    scheme->loadFromDirectory(distDir);
                    qDebug() << "WPE minibrowser ready, loading Vue app from" << distDir;
                } else {
                    scheme->addResource("/", QByteArray(s_testHtml), "text/html");
                    scheme->addResource("/index.html", QByteArray(s_testHtml), "text/html");
                    qDebug() << "WPE minibrowser ready, loading test HTML (dist/ not found)";
                }
            }
            view->loadUrl("app:///index.html");
        } else {
            view->loadUrl(urlToLoad);
            qDebug() << "WPE minibrowser ready, loading" << urlToLoad;
        }
    });

    moveToCenter(&window);
    window.show();
    // In translucent mode, position and show the DWPEView after the window
    // is visible (windowHandle is needed for parenting).
    if (translucent) {
        view->setParent(window.windowHandle());
        int titleH = window.titlebar() ? window.titlebar()->height() : 30;
        view->setGeometry(window.frameGeometry().x(),
                          window.frameGeometry().y() + titleH,
                          window.width(), window.height() - titleH);
        view->show();
        // Keep the view positioned below the title bar on resize.
        // QWindow has no resize signal, so we poll via a timer.
        auto *resizeTimer = new QTimer;
        resizeTimer->setInterval(100);
        QObject::connect(resizeTimer, &QTimer::timeout, [view, &window, titleH]() {
            QRect target(window.frameGeometry().x(),
                          window.frameGeometry().y() + titleH,
                          window.width(), window.height() - titleH);
            if (view->geometry() != target)
                view->setGeometry(target);
        });
        resizeTimer->start();
    }

    return app->exec();
}
