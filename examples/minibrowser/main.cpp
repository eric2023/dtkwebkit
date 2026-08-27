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
#include <QWidget>
#include <QColor>
#include <QFileInfo>
#include <QSysInfo>
#include <QThread>
#include <QFile>
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
    // opaque. Without this flag, the content is fully opaque.
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

    // Background color for the WPE page body. The Vue dist app uses white
    // text and semi-transparent panels, so a dark background is required for
    // visibility. The QColor alpha controls compositor-level transparency:
    //   alpha=0xFF → opaque content (default)
    //   alpha<0xFF → semi-transparent content (needs --translucent flag)
    QColor bgColor(0x1A, 0x1A, 0x2E, translucent ? 0x80 : 0xFF);

    // For --translucent: three things must all be set to get a 32-bit
    // X11 visual with alpha:
    //   1. DMainWindow::setTranslucentBackground(true) — DTK platform hook
    //   2. WA_TranslucentBackground on the DMainWindow — Qt attribute
    //   3. A stylesheet making the DMainWindow background transparent
    // Without all three, the window stays 24-bit (no alpha channel) and
    // the compositor ignores the framebuffer alpha. The DTitlebar paints
    // its own opaque background, so the title bar is unaffected.
    if (translucent) {
        window.setTranslucentBackground(true);
        window.setAttribute(Qt::WA_TranslucentBackground, true);
        window.setStyleSheet("DMainWindow { background: transparent; }");
    }

    auto *view = new DWPEView;
    view->setBackgroundColor(bgColor);
    auto *container = QWidget::createWindowContainer(view, &window);
    if (translucent)
        container->setAttribute(Qt::WA_TranslucentBackground, true);
    window.setCentralWidget(container);

    // WPE is initialized inside DWPEView::initializeGL() (which runs once the
    // Qt GL context is current), and the wpeReady signal is emitted immediately
    // after. Connect to it so scheme registration, bridge setup, and content
    // loading all happen after both GL and WPE are fully initialized — no
    // QTimer guessing about GL readiness.
    QObject::connect(view, &DWPEView::wpeReady, [view, urlToLoad, distDir, bgColor]() {
        // setBackgroundColor sets the GL clear color (fallback behind
        // transparent WPE pixels). For the embedded Vue app, also call
        // setPageBackgroundColor to inject a dark body background — the
        // Vue app uses white text on transparent panels. For external
        // URLs (e.g. Baidu), skip setPageBackgroundColor so the page's
        // own background is respected.
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
        auto *scheme = view->schemeHandler();
        if (urlToLoad.isEmpty()) {
            if (scheme) {
                // Load Vue dist/ if available, otherwise fall back to test HTML.
                if (!distDir.isEmpty()) {
                    scheme->loadFromDirectory(distDir);
                    // Vue app expects a dark body background for white text visibility.
                    view->setPageBackgroundColor(bgColor);
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

    return app->exec();
}
